/*
 * Goodix 53x5 driver for libfprint — USB transport and command execution
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "driver-private.h"
#include "device/transport.h"
#include "device/commands.h"
#include "device/calibration.h"

#include <string.h>

/* USB endpoints — interface 1, CDC Data class */
#define GOODIX_EP_OUT (0x03 | FPI_USB_ENDPOINT_OUT)
#define GOODIX_EP_IN  (0x01 | FPI_USB_ENDPOINT_IN)

/* USB chunk size */
#define GOODIX_USB_CHUNK_SIZE 64

#define GOODIX_PROTO_CATEGORY_ACK     0x0B
#define GOODIX_PROTO_CMD_ACK          0x00
#define GOODIX_PROTO_ACK_FLAG_VALID   0x01
#define GOODIX_PROTO_CATEGORY_FDT     0x03
#define GOODIX_PROTO_CMD_FDT_DOWN     0x01
#define GOODIX_PROTO_CMD_FDT_UP       0x02
#define GOODIX_FDT_EVENT_PAYLOAD_LEN  (4 + GOODIX_FDT_BASE_LEN)
#define GOODIX_PROTO_CMD_BYTE(category, command) \
  (((category) << 4) | ((command) << 1))

typedef enum {
  GOODIX_TRANSPORT_IDLE,
  GOODIX_TRANSPORT_JOIN_IDLE,
  GOODIX_TRANSPORT_SEND,
  GOODIX_TRANSPORT_ACK,
  GOODIX_TRANSPORT_RESPONSE,
  GOODIX_TRANSPORT_EVENT,
  GOODIX_TRANSPORT_REPLY,
  GOODIX_TRANSPORT_STOPPING,
} GoodixTransportPhase;

/* One owner survives every submitted callback. JOIN_IDLE reserves a command,
 * but the winning old callback is still processed under idle policy. */
struct _GoodixTransport
{
  FpDevice                 *dev;
  GoodixTransportPhase      phase;
  GoodixTransportDone       done;
  gpointer                  data;
  GoodixTransportJoined     joined;
  gpointer                  joined_data;
  GCancellable             *cancellable;
  guint                     timeout_ms;
  gint64                    deadline_us;
  GoodixProfile9FdtWaitMode event_mode;
  GoodixCmd                 cmd;
  gboolean                  expect_data;
  gboolean                  idle_after_ack;
  GoodixProfile9FdtWaitMode cancelled_fdt_mode;
  gboolean                  retry_mode;
  guint8                    response_bit;
  guint8                    ack_status;
  guint                     attempt;
  guint                     ack_timeout_ms;
  guint                     response_timeout_ms;
};

static void goodix_transport_send (GoodixTransport *operation);
static void goodix_transport_receive (GoodixTransport *operation, guint timeout);
static void goodix_transport_complete (GoodixTransport *operation, GError *error);

static gboolean
goodix_transport_is_idle (GoodixTransport *operation)
{
  return operation->phase == GOODIX_TRANSPORT_IDLE ||
         operation->phase == GOODIX_TRANSPORT_JOIN_IDLE ||
         operation->phase == GOODIX_TRANSPORT_STOPPING;
}

/* Native budgets count Sleep(1) ACK polls and 50-ms response waits. GUsb uses
 * elapsed milliseconds instead: retain the caller's nominal budget across
 * complete packet reception, beginning after write/ACK completion respectively.
 * Other commands retain their unaudited existing timing policy. */
static void
goodix_cmd_set_budgets (GoodixTransport *operation)
{
  guint8 command = GOODIX_PROTO_CMD_BYTE (operation->cmd.category,
                                          operation->cmd.command);

  operation->ack_timeout_ms = GOODIX_ACK_TIMEOUT;
  operation->response_timeout_ms = GOODIX_DATA_TIMEOUT;
  switch (command)
    {
    case 0xa8: /* Firmware version */
      operation->response_timeout_ms = 2000;
      G_GNUC_FALLTHROUGH;

    case 0x00: /* Startup mode */
      operation->ack_timeout_ms = 500;
      break;

    case 0x36: /* Manual FDT */
    case 0x90: /* Configuration */
      operation->response_timeout_ms = 500;
      G_GNUC_FALLTHROUGH;

    case 0x32: /* FDT down */
    case 0x34: /* FDT up */
      operation->ack_timeout_ms = 500;
      break;

    case 0x60: /* Sleep */
    case 0xae: /* EC */
      operation->ack_timeout_ms = 200;
      break;

    default:
      break;
    }
}

static guint8
goodix_mode_ack_bit (guint8 cmd_byte)
{
  switch (cmd_byte)
    {
    case 0x32:
      return 1;

    case 0x34:
      return 2;

    case 0x60:
      return 4;

    case 0x36:
      return 8;

    case 0x90:
      return 16;

    case 0x00:
      return 32;

    case 0xa8:
      return 64;

    default:
      return 0;
    }
}

static const char *
goodix_cmd_phase_name (GoodixTransportPhase phase)
{
  if (phase == GOODIX_TRANSPORT_SEND)
    return "send";
  if (phase == GOODIX_TRANSPORT_ACK)
    return "ACK";
  g_assert (phase == GOODIX_TRANSPORT_RESPONSE);
  return "data";
}

/* Only a transport timeout or send I/O failure in the first transaction is
 * recoverable here. Protocol, cancellation, disconnect and ownership errors
 * keep their existing failure policy. The parent and command owner never
 * change between attempts. */
static gboolean
goodix_cmd_native_zero (GoodixTransportPhase phase, const GError *error)
{
  if (g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT))
    return TRUE;
  if (phase != GOODIX_TRANSPORT_SEND || !error || error->domain != G_USB_DEVICE_ERROR)
    return FALSE;
  /* Native maps negative writes to zero. These GUsb codes include transfer
   * error, endpoint stall and ordinary backend failure. Host preconditions,
   * cancellation and device loss do not authorize another write. */
  return error->code == G_USB_DEVICE_ERROR_IO ||
         error->code == G_USB_DEVICE_ERROR_FAILED ||
         error->code == G_USB_DEVICE_ERROR_NOT_SUPPORTED ||
         error->code == G_USB_DEVICE_ERROR_INTERNAL;
}

static gboolean
goodix_cmd_retry (GoodixTransport *operation, GError *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (operation->dev);
  GoodixTransportPhase phase = operation->phase;

  if (!operation->retry_mode || operation->attempt != 1 ||
      (phase != GOODIX_TRANSPORT_SEND && phase != GOODIX_TRANSPORT_ACK &&
       !(operation->response_bit && phase == GOODIX_TRANSPORT_RESPONSE)) ||
      !goodix_cmd_native_zero (phase, error))
    return FALSE;

  fp_dbg ("Retrying command cat=0x%02x cmd=0x%02x phase=%s attempt=1: %s",
          operation->cmd.category, operation->cmd.command,
           goodix_cmd_phase_name (phase), error->message);
  self->retried_mode_acks |= goodix_mode_ack_bit (
    GOODIX_PROTO_CMD_BYTE (operation->cmd.category, operation->cmd.command));
  g_error_free (error);
  goodix_transport_send (operation);
  return TRUE;
}

/* Zero means infinite to GUsb, so never round an expired deadline to zero. */
static guint
goodix_deadline_remaining (gint64 deadline_us)
{
  gint64 remaining = deadline_us - g_get_monotonic_time ();

  return remaining > 0 ? (guint) ((remaining + 999) / 1000) : 0;
}

static gboolean
goodix_validate_ack_for_cmd (FpDevice       *dev,
                             GoodixTransport *operation,
                             guint8          *status,
                             GError         **error)
{
  guint8 category, command;
  const guint8 *payload;
  gsize payload_len;
  guint8 expected_cmd_byte = GOODIX_PROTO_CMD_BYTE (operation->cmd.category,
                                                   operation->cmd.command);

  if (!goodix_parse_reply (dev, &category, &command,
                           &payload, &payload_len, NULL))
    {
      g_set_error_literal (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                           "Failed to parse ACK");
      return FALSE;
    }

  if (category == GOODIX_PROTO_CATEGORY_FDT &&
      operation->cancelled_fdt_mode != GOODIX_PROFILE9_FDT_WAIT_NONE)
    {
      GoodixFdtEventType type;
      GoodixProfile9FdtEvent event;
      guint8 expected_command = operation->cancelled_fdt_mode == GOODIX_PROFILE9_FDT_WAIT_DOWN
                                  ? GOODIX_PROTO_CMD_FDT_DOWN : GOODIX_PROTO_CMD_FDT_UP;

      if (command != expected_command || payload_len != GOODIX_FDT_EVENT_PAYLOAD_LEN)
        {
          g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                       "Unexpected FDT packet during cleanup: armed=0x%02x cat=0x%02x cmd=0x%02x len=%zu",
                       expected_command, category, command, payload_len);
          return FALSE;
        }
      if (!goodix_cmd_parse_fdt_event (dev, operation->cancelled_fdt_mode,
                                      &type, &event, error))
        return FALSE;
      /* Stopping invalidates notification, not parser-side base publication.
       * A zero status continues ACK reception with the original deadline. */
      goodix_recv_apply_fdt_event (dev, type, &event);
      operation->cancelled_fdt_mode = GOODIX_PROFILE9_FDT_WAIT_NONE;
      fp_dbg ("Drained cancelled FDT event before sleep ACK: mode=0x%02x irq=0x%04x",
              expected_command, event.irq);
      *status = 0;
      return TRUE;
    }

  if (category != GOODIX_PROTO_CATEGORY_ACK ||
      command != GOODIX_PROTO_CMD_ACK ||
      payload_len < 2)
    {
      g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                   "Unexpected ACK: expected cmd_byte=0x%02x, got cat=0x%02x cmd=0x%02x len=%zu",
                   expected_cmd_byte, category, command, payload_len);
      return FALSE;
    }

  if (payload[0] != expected_cmd_byte)
    {
      g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                   "Unexpected ACK: expected cmd_byte=0x%02x, got ack_cmd=0x%02x flags=0x%02x",
                   expected_cmd_byte, payload[0], payload[1]);
      return FALSE;
    }

  *status = payload[1];
  return TRUE;
}

/* ========================================================================
 * USB I/O helpers
 * ======================================================================== */

static void
goodix_tx_cb (FpiUsbTransfer *transfer,
              FpDevice       *dev,
              gpointer        user_data,
              GError         *error)
{
  GoodixTransport *operation = user_data;

  if (error)
    {
      goodix_transport_complete (operation, error);
      return;
    }

  operation->phase = GOODIX_TRANSPORT_ACK;
  operation->deadline_us = g_get_monotonic_time () + operation->ack_timeout_ms * 1000LL;
  if ((operation->cmd.category == 0 && operation->cmd.command == 0) ||
      (operation->cmd.category == 0x0a && operation->cmd.command == 4))
    operation->cancellable = g_object_ref (fpi_device_get_cancellable (dev));
  goodix_transport_receive (operation, operation->ack_timeout_ms);
}

/* Submit OUT only after the previous IN callback has joined. The ACK clock
 * starts in goodix_tx_cb, never while the write is pending. */
static void
goodix_transport_send (GoodixTransport *operation)
{
  FpDevice *dev = operation->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixCmd *cmd = &operation->cmd;
  gsize msg_len;
  guint8 *msg;
  FpiUsbTransfer *transfer;
  guint8 cmd_byte;

  operation->phase = GOODIX_TRANSPORT_SEND;
  operation->attempt++;
  g_clear_object (&operation->cancellable);
  self->command_response_ready &= ~operation->response_bit;
  if (operation->response_bit)
    self->retried_mode_acks |= goodix_mode_ack_bit (
      GOODIX_PROTO_CMD_BYTE (cmd->category, cmd->command));
  msg = goodix_proto_build_message (cmd->category, cmd->command,
                                    cmd->payload, cmd->payload_len,
                                    cmd->use_checksum, &msg_len);
  cmd_byte = msg[0];

  /* The transport uses 64-byte USB writes. Continuation chunks prepend
   * cmd_byte | 1 and carry up to 63 more bytes of message data. */

  gsize first_len = MIN (msg_len, GOODIX_USB_CHUNK_SIZE);
  gsize total_chunks = 1 + (msg_len - first_len + 62) / 63;
  gsize padded_len = total_chunks * GOODIX_USB_CHUNK_SIZE;
  guint8 *chunked = g_malloc0 (padded_len);

  memcpy (chunked, msg, first_len);
  gsize src_offset = first_len;
  for (gsize dst_offset = GOODIX_USB_CHUNK_SIZE;
       dst_offset < padded_len; dst_offset += GOODIX_USB_CHUNK_SIZE)
    {
      gsize data_in_chunk = MIN (63, msg_len - src_offset);

      chunked[dst_offset] = cmd_byte | 1;
      memcpy (chunked + dst_offset + 1, msg + src_offset, data_in_chunk);
      src_offset += data_in_chunk;
    }

  g_free (msg);

  transfer = fpi_usb_transfer_new (dev);
  fpi_usb_transfer_fill_bulk_full (transfer, GOODIX_EP_OUT,
                                   chunked, padded_len, g_free);
  /* Native writes have no timeout. The active libfprint action owns device
   * lifetime and cancellation until this callback joins; Linux USB removal
   * independently completes in-flight transfers with NO_DEVICE. */
  fpi_usb_transfer_submit (transfer, 0, fpi_device_get_cancellable (dev),
                           goodix_tx_cb, operation);
}

/* Forward declarations */
static void goodix_rx_cb (FpiUsbTransfer *transfer,
                          FpDevice       *dev,
                          gpointer        user_data,
                          GError         *error);

static void
goodix_transport_free (GoodixTransport *operation)
{
  g_clear_object (&operation->cancellable);
  g_free (operation->cmd.payload);
  g_object_unref (operation->dev);
  g_free (operation);
}

static GoodixTransport *
goodix_transport_new (FpDevice             *dev,
                      GoodixTransportPhase  phase,
                      GoodixTransportDone   done,
                      gpointer              data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport *operation = g_new0 (GoodixTransport, 1);

  g_assert (!self->transport);
  operation->dev = g_object_ref (dev);
  operation->phase = phase;
  operation->done = done;
  operation->data = data;
  self->transport = operation;
  return operation;
}

/* Start a new packet wait, retaining only a partial inherited from idle. */
static void
goodix_transport_receive (GoodixTransport *operation, guint timeout)
{
  FpDevice *dev = operation->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiUsbTransfer *transfer;

  operation->timeout_ms = timeout;
  if (!self->rx_idle_partial)
    goodix_proto_rx_reset (&self->rx);
  self->reply_valid = FALSE;

  transfer = fpi_usb_transfer_new (dev);
  fpi_usb_transfer_fill_bulk (transfer, GOODIX_EP_IN, GOODIX_USB_CHUNK_SIZE);
  fpi_usb_transfer_submit (transfer, timeout, operation->cancellable,
                           goodix_rx_cb, operation);
}

static void
goodix_transport_complete (GoodixTransport *operation, GError *error)
{
  g_autoptr(FpDevice) dev = g_object_ref (operation->dev);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransportDone done = operation->done;
  gpointer data = operation->data;
  GoodixTransportJoined joined = operation->joined;
  gpointer joined_data = operation->joined_data;
  gboolean idle_after_ack = !error && operation->idle_after_ack;
  GoodixTransportResult result = {
    .ack_status = operation->ack_status,
    .ordinary_exhaustion = error && goodix_cmd_native_zero (operation->phase, error),
    .write_cancelled = operation->phase == GOODIX_TRANSPORT_SEND &&
      (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
       g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_CANCELLED)),
  };

  if (goodix_transport_is_idle (operation))
    {
      self->rx_idle_partial = self->rx.len && !goodix_proto_rx_complete (&self->rx);
      if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          fp_dbg ("Idle receive stopped: %s", error->message);
          self->needs_reinit = TRUE;
        }
      g_clear_error (&error);
      if (operation->phase == GOODIX_TRANSPORT_JOIN_IDLE)
        {
          goodix_transport_send (operation);
          return;
        }
    }
  else if (error && goodix_cmd_retry (operation, error))
    return;
  else if (error && operation->attempt)
    g_prefix_error (&error, "Command cat=0x%02x cmd=0x%02x phase=%s attempt=%u: ",
                    operation->cmd.category, operation->cmd.command,
                    goodix_cmd_phase_name (operation->phase), operation->attempt);

  self->transport = NULL;
  goodix_transport_free (operation);
  if (idle_after_ack)
    {
      GoodixTransport *idle = goodix_transport_new (dev, GOODIX_TRANSPORT_IDLE, NULL, NULL);
      idle->cancellable = g_cancellable_new ();
      goodix_transport_receive (idle, 0);
    }
  if (done)
    done (dev, &result, error, data);
  if (joined)
    joined (dev, joined_data);
}

static void
goodix_rx_cb (FpiUsbTransfer *transfer,
              FpDevice       *dev,
              gpointer        user_data,
              GError         *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport *operation = user_data;
  FpiUsbTransfer *next;
  gboolean fixed_deadline = operation->phase == GOODIX_TRANSPORT_ACK ||
    (operation->phase == GOODIX_TRANSPORT_RESPONSE && operation->response_bit);

  g_assert (self->transport == operation);

  if (error)
    {
      goodix_transport_complete (operation, error);
      return;
    }

  /* Bounded command polling retains its deadline. Other receives keep the existing
   * zero-length-read timeout policy. */
  if (transfer->actual_length == 0)
    {
      if (!fixed_deadline)
        operation->deadline_us = operation->timeout_ms ?
                                 g_get_monotonic_time () + operation->timeout_ms * 1000LL : 0;
      goto receive_more;
    }

  if (!goodix_proto_rx_feed_chunk (&self->rx, transfer->buffer,
                                   transfer->actual_length))
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                        "Protocol reassembly error");
      goto protocol_error;
    }

  if (goodix_proto_rx_complete (&self->rx))
    {
      gboolean idle_packet = goodix_transport_is_idle (operation) || self->rx_idle_partial;
      GoodixTransport *current = !idle_packet &&
        (operation->phase == GOODIX_TRANSPORT_ACK || operation->phase == GOODIX_TRANSPORT_RESPONSE)
        ? operation : NULL;
      gboolean ack_wait = current && operation->phase == GOODIX_TRANSPORT_ACK;

      self->reply_valid = goodix_proto_rx_parse (
        &self->rx, &self->reply_category, &self->reply_command,
        &self->reply_payload, &self->reply_payload_len);
      guint8 category = self->reply_category, command = self->reply_command;
      const guint8 *payload = self->reply_payload;
      gsize payload_len = self->reply_payload_len;
      gboolean ack_packet = self->reply_valid && category == GOODIX_PROTO_CATEGORY_ACK &&
                            command == GOODIX_PROTO_CMD_ACK && payload_len >= 2;
      gboolean expected_ack = ack_packet && ack_wait && payload[0] ==
                              GOODIX_PROTO_CMD_BYTE (current->cmd.category, current->cmd.command);

      if (self->reply_valid)
        {
          guint8 bit = category == 3 && command == 3 ? 1 :
                       category == 9 && command == 0 ? 2 :
                       category == 0x0a && (command == 0 || command == 1 || command == 4) ? 4 : 0;
          gboolean shared = bit == 4 || (category == 0x0a && command == 3) ||
                            category == 8 || category == 0x0e || category == 0x0f;
          gboolean current_data = current && operation->phase == GOODIX_TRANSPORT_RESPONSE &&
                                  current->cmd.category == category && current->cmd.command == command;

          /* DataFromDevice publishes these shared stores without a matching
           * command. Only A/0, A/1 and A/4 signal the version getter's event.
           * Project the first 64 bytes, retaining every unwritten suffix. */
          if (shared)
            {
              gsize offset = category == 0x0e ? 4 : 0;
              gsize count = category == 0x0f ? MIN (payload_len, 1) :
                            MIN (payload_len, sizeof (self->shared_response) - offset);

              if (category == 0x0e)
                {
                  guint32 length = GUINT32_TO_LE ((guint32) payload_len);
                  memcpy (self->shared_response, &length, sizeof (length));
                }
              memcpy (self->shared_response + offset, payload, count);
              /* Preserve ordinary current response consumption (e.g. register
               * and PSK reads); unrelated stores do not finish that waiter. */
              if (!bit && current && !current_data)
                {
                  goodix_proto_rx_reset (&self->rx);
                  goto receive_more;
                }
            }

          if (bit)
            {
              if (bit == 1)
                {
                  if (payload_len < sizeof (self->manual_response))
                    {
                      error = fpi_device_error_new_msg (
                        FP_DEVICE_ERROR_PROTO, "Manual FDT reply is too short");
                      goto protocol_error;
                    }
                  memcpy (self->manual_response, payload, sizeof (self->manual_response));
                }
              /* Configuration publishes only an event, never a success byte.
               * Response slots are independent of ACK reception. */
              self->command_response_ready |= bit;
              if (!current || operation->phase != GOODIX_TRANSPORT_RESPONSE ||
                  (current->response_bit != bit && !current_data))
                {
                  goodix_proto_rx_reset (&self->rx);
                  goto receive_more;
                }
            }
          else if (category == 3 && (command == 1 || command == 2) &&
                   (idle_packet || (current && (current->response_bit ||
                                                (current->cmd.category == 3 && current->cmd.command <= 2)))))
            {
              GoodixFdtEventType type;
              GoodixProfile9FdtEvent event;
              GoodixProfile9FdtWaitMode mode = command == 1 ?
                GOODIX_PROFILE9_FDT_WAIT_DOWN : GOODIX_PROFILE9_FDT_WAIT_UP;
              g_autoptr(GError) event_error = NULL;

              if (payload_len != GOODIX_FDT_EVENT_PAYLOAD_LEN)
                event_error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                        "Unexpected FDT event length");
              if (event_error || !goodix_cmd_parse_fdt_event (dev, mode, &type, &event, &event_error))
                {
                  error = g_steal_pointer (&event_error);
                  goto protocol_error;
                }
              /* Every native parser mutation precedes replacement of the
               * latest worker notification, including during config/manual. */
              goodix_recv_apply_fdt_event (dev, type, &event);
              if (!idle_packet)
                {
                  self->pending_fdt.event = event;
                  self->pending_fdt.type = type;
                  memcpy (self->pending_fdt.prior_down, self->fdt_prior_down,
                          sizeof (self->pending_fdt.prior_down));
                }
              goodix_proto_rx_reset (&self->rx);
              goto receive_more;
            }
          if (idle_packet)
            {
              /* No active waiter: ordinary unclaimed packets have no result
               * owner. EC data likewise has no native cache/event side effect. */
              fp_dbg ("Idle receive consumed cat=0x%02x cmd=0x%02x", category, command);
              goodix_proto_rx_reset (&self->rx);
              goto receive_more;
            }
        }
      else if (idle_packet)
        {
          goodix_rx_cb (transfer, dev, operation, fpi_device_error_new_msg (
                          FP_DEVICE_ERROR_PROTO, "Invalid idle protocol packet"));
          return;
        }

      /* Native EC has no response event. Optional category-A/command-7 data
       * is ignored independently of the command or event currently awaited. */
      if (self->reply_valid &&
          category == 0x0a && command == 7)
        {
          goodix_proto_rx_reset (&self->rx);
          goto receive_more;
        }

      /* Native updates the acknowledged command's independent slot. A late
       * ACK from a repeated mode command cannot satisfy a different command
       * or an event/data wait. Validate the envelope before routing it. */
      if (ack_packet && !expected_ack &&
          (self->retried_mode_acks & goodix_mode_ack_bit (payload[0])))
        {
          goodix_proto_rx_reset (&self->rx);
          goto receive_more;
        }
      if (ack_wait)
        {
          guint8 status;

          if (!goodix_validate_ack_for_cmd (dev, current, &status, &error))
            goodix_transport_complete (operation, error);
          /* Even status (or drained cleanup FDT) preserves this attempt's
           * original ACK deadline. Only exhaustion permits a retry. */
          else if (!(status & GOODIX_PROTO_ACK_FLAG_VALID))
            {
              goodix_proto_rx_reset (&self->rx);
              goto receive_more;
            }
          else
            {
              current->ack_status = status;
              if (!current->expect_data ||
                  (current->response_bit & self->command_response_ready))
                goodix_transport_complete (operation, NULL);
              else
                {
                  operation->phase = GOODIX_TRANSPORT_RESPONSE;
                  operation->deadline_us = g_get_monotonic_time () +
                                           operation->response_timeout_ms * 1000LL;
                  g_clear_object (&operation->cancellable);
                  if (operation->response_bit == 4)
                    operation->cancellable = g_object_ref (fpi_device_get_cancellable (dev));
                  goodix_transport_receive (operation, operation->response_timeout_ms);
                }
            }
          return;
        }
      if (operation->phase == GOODIX_TRANSPORT_EVENT)
        {
          GoodixFdtNotification *pending = &self->pending_fdt;
          if (!goodix_cmd_parse_fdt_event (dev, operation->event_mode,
                                          &pending->type, &pending->event, &error))
            goto protocol_error;
          goodix_recv_apply_fdt_event (dev, pending->type, &pending->event);
          memcpy (pending->prior_down, self->fdt_prior_down, sizeof (pending->prior_down));
        }
      goodix_transport_complete (operation, NULL);
    }
  else
    {
      /* Preserve unrelated per-continuation data budgets. Scoped command waits
       * keep one deadline across interleaved packets and continuations. */
      if (!fixed_deadline && !goodix_transport_is_idle (operation))
        operation->deadline_us = g_get_monotonic_time () + GOODIX_DATA_TIMEOUT * 1000LL;
      goto receive_more;
    }
  return;

protocol_error:
  goodix_transport_complete (operation, error);
  return;

receive_more:
  {
    if (self->rx.len == 0)
      {
        self->rx_idle_partial = FALSE;
        self->reply_valid = FALSE;
      }
    if (goodix_transport_is_idle (operation) &&
        g_cancellable_is_cancelled (operation->cancellable))
      {
        goodix_transport_complete (operation, NULL);
        return;
      }
    guint timeout = operation->deadline_us ?
                    goodix_deadline_remaining (operation->deadline_us) : 0;

    if (operation->deadline_us && !timeout)
      {
        goodix_rx_cb (transfer, dev, operation,
                      g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                           "Receive deadline expired"));
        return;
      }
    next = fpi_usb_transfer_new (dev);
    fpi_usb_transfer_fill_bulk (next, GOODIX_EP_IN, GOODIX_USB_CHUNK_SIZE);
    fpi_usb_transfer_submit (next, timeout, operation->cancellable,
                             goodix_rx_cb, operation);
  }
}

void
goodix_transport_invalidate (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  g_assert (!self->transport);
  if (self->rx.buf)
    goodix_proto_rx_reset (&self->rx);
  self->reply_valid = FALSE;
  self->rx_idle_partial = FALSE;
  self->pending_fdt.event.pending = FALSE;
}

gboolean
goodix_recv_select_fdt (FpDevice *dev, GoodixFdtEventType *type,
                        GoodixProfile9FdtEvent *event,
                        guint16 *prior_down, GError **error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixFdtNotification *pending = &self->pending_fdt;

  g_assert (!self->transport);
  if (pending->event.pending)
    {
      *event = pending->event;
      *type = pending->type;
      if (*type == GOODIX_FDT_EVENT_REVERSE)
        memcpy (prior_down, pending->prior_down, sizeof (pending->prior_down));
      pending->event.pending = FALSE;
      return TRUE;
    }
  g_set_error_literal (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                       "No completed FDT event");
  return FALSE;
}

void
goodix_recv_apply_fdt_event (FpDevice                     *dev,
                             GoodixFdtEventType            type,
                             const GoodixProfile9FdtEvent *event)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixProfile9FdtState *fdt = &self->profile9_fdt;

  if (type == GOODIX_FDT_EVENT_DOWN)
    {
      goodix_device_generate_fdt_up_base (event->raw, event->touch_flag,
                                          &self->calib, fdt->base_up);
    }
  else
    {
      if (type == GOODIX_FDT_EVENT_REVERSE)
        {
          for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
            self->fdt_prior_down[i] = fdt->base_down[i * 2 + 1];
          memcpy (fdt->base_manual, fdt->base_down, sizeof (fdt->base_manual));
        }
      goodix_device_generate_fdt_base (event->raw, GOODIX_FDT_BASE_LEN,
                                       fdt->base_down);
    }
}

void
goodix_transport_command (FpDevice                     *dev,
                           const GoodixTransportRequest *request,
                           GoodixTransportDone          done,
                           gpointer                     data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport *operation = self->transport;
  guint8 category = request->cmd.category, command = request->cmd.command;

  if (operation && operation->phase != GOODIX_TRANSPORT_IDLE)
    {
      GoodixTransportResult result = {0};
      done (dev, &result, fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
             "A device transport operation is already active"), data);
      return;
    }

  if (!operation)
    operation = goodix_transport_new (dev, GOODIX_TRANSPORT_SEND, done, data);
  operation->done = done;
  operation->data = data;
  operation->cmd = request->cmd;
  operation->cmd.payload = request->cmd.payload_len ?
    g_memdup2 (request->cmd.payload, request->cmd.payload_len) : NULL;
  operation->expect_data = request->expect_data;
  operation->idle_after_ack = request->idle_after_ack;
  operation->cancelled_fdt_mode = request->cancelled_mode;
  operation->response_bit = request->expect_data ?
                            (category == 3 && command == 3 ? 1 :
                             category == 9 && command == 0 ? 2 :
                             category == 0x0a && command == 4 ? 4 : 0) : 0;
  operation->retry_mode = operation->response_bit || (!request->expect_data &&
                         ((category == GOODIX_PROTO_CATEGORY_FDT &&
                           (command == GOODIX_PROTO_CMD_FDT_DOWN || command == GOODIX_PROTO_CMD_FDT_UP)) ||
                          ((category == 0x06 || category == 0) && command == 0)));
  goodix_cmd_set_budgets (operation);

  if (operation->phase == GOODIX_TRANSPORT_IDLE)
    {
      operation->phase = GOODIX_TRANSPORT_JOIN_IDLE;
      g_cancellable_cancel (operation->cancellable);
    }
  else
    goodix_transport_send (operation);
}

static void
goodix_transport_wait (FpDevice                  *dev,
                        GoodixTransportPhase       phase,
                        guint                      timeout,
                        GoodixProfile9FdtWaitMode  mode,
                        GoodixTransportDone        done,
                        gpointer                   data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport *operation;

  if (self->transport)
    {
      GoodixTransportResult result = {0};
      done (dev, &result, fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
             "A device transport operation is already active"), data);
      return;
    }
  operation = goodix_transport_new (dev, phase, done, data);
  operation->event_mode = mode;
  operation->deadline_us = timeout ? g_get_monotonic_time () + timeout * 1000LL : 0;
  if (phase == GOODIX_TRANSPORT_EVENT)
    {
      operation->cancellable = g_cancellable_new ();
      if (self->pending_fdt.event.pending)
        {
          goodix_transport_complete (operation, NULL);
          return;
        }
    }
  goodix_transport_receive (operation, timeout);
}

void
goodix_transport_wait_event (FpDevice                  *dev,
                              GoodixProfile9FdtWaitMode mode,
                              GoodixTransportDone       done,
                              gpointer                  data)
{
  goodix_transport_wait (dev, GOODIX_TRANSPORT_EVENT, 0, mode, done, data);
}

void
goodix_transport_wait_reply (FpDevice           *dev,
                              guint               timeout,
                              GoodixTransportDone done,
                              gpointer            data)
{
  goodix_transport_wait (dev, GOODIX_TRANSPORT_REPLY, timeout,
                         GOODIX_PROFILE9_FDT_WAIT_NONE, done, data);
}

void
goodix_transport_cancel_event (FpDevice *dev)
{
  GoodixTransport *operation = FPI_DEVICE_GOODIX53X5 (dev)->transport;

  if (operation && operation->phase == GOODIX_TRANSPORT_EVENT)
    g_cancellable_cancel (operation->cancellable);
}

void
goodix_transport_quiesce (FpDevice             *dev,
                           GoodixTransportJoined joined,
                           gpointer              data)
{
  GoodixTransport *operation = FPI_DEVICE_GOODIX53X5 (dev)->transport;

  if (!operation)
    {
      joined (dev, data);
      return;
    }
  g_assert (operation->phase == GOODIX_TRANSPORT_IDLE);
  operation->phase = GOODIX_TRANSPORT_STOPPING;
  operation->joined = joined;
  operation->joined_data = data;
  g_cancellable_cancel (operation->cancellable);
}

gboolean
goodix_parse_reply (FpDevice      *dev,
                    guint8        *out_category,
                    guint8        *out_command,
                    const guint8 **out_payload,
                    gsize         *out_payload_len,
                    GError       **error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (self->reply_valid)
    {
      *out_category = self->reply_category;
      *out_command = self->reply_command;
      *out_payload = self->reply_payload;
      *out_payload_len = self->reply_payload_len;
      return TRUE;
    }

  g_set_error_literal (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                       "Failed to parse device reply");
  return FALSE;
}

gboolean
goodix_parse_reply_exact (FpDevice      *dev,
                          guint8         expected_category,
                          guint8         expected_command,
                          const guint8 **out_payload,
                          gsize         *out_payload_len,
                          GError       **error)
{
  guint8 category, command;

  if (!goodix_parse_reply (dev, &category, &command, out_payload,
                           out_payload_len, error))
    return FALSE;

  if (category != expected_category || command != expected_command)
    {
      g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                   "Unexpected reply: cat=0x%02x cmd=0x%02x",
                   category, command);
      return FALSE;
    }

  return TRUE;
}
