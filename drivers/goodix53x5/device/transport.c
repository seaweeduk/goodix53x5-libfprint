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
#include "device/calibration.h"
#include "device/scan.h"

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

/* Command sub-SSM */
typedef enum {
  GOODIX_CMD_SEND = 0,
  GOODIX_CMD_RECV_ACK,
  GOODIX_CMD_VALIDATE_ACK,
  GOODIX_CMD_RECV_DATA,
  GOODIX_CMD_NUM_STATES,
} GoodixCmdState;

typedef struct
{
  guint64                     token;
  guint                       timeout_ms;
  gint64                      deadline_us;
  gboolean                    fixed_deadline;
  GCancellable               *cancellable;
  GoodixRecvCancelledCallback cancelled_cb;
  gpointer                    cancelled_data;
} GoodixRecvOperation;

typedef struct
{
  GoodixCmd                 cmd;
  gboolean                  idle_after_ack;
  FpiSsm                   *parent_ssm;
  GoodixProfile9FdtWaitMode cancelled_fdt_mode;
  gboolean                  retry_mode;
  guint8                    response_bit;
  guint8                    ack_status;
  GoodixCmdResultCallback   result_cb;
  guint                     attempt;
  GoodixCmdState            phase;
  gint64                    ack_deadline_us;
  guint                     ack_timeout_ms;
  guint                     response_timeout_ms;
} GoodixCmdOperation;

typedef struct
{
  FpDevice                *dev;
  GCancellable            *cancel;
  GoodixIdleJoinedCallback joined;
  gpointer                 joined_data;
} GoodixIdleRecv;

static void
goodix_idle_recv_free (GoodixIdleRecv *idle)
{
  g_object_unref (idle->cancel);
  g_object_unref (idle->dev);
  g_free (idle);
}

static void
goodix_idle_recv_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixIdleRecv *idle = fpi_ssm_get_data (ssm);

  self->idle_rx_ssm = NULL;
  self->rx_idle_partial = self->rx.len && !goodix_proto_rx_complete (&self->rx);
  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      fp_dbg ("Idle receive stopped: %s", error->message);
      self->needs_reinit = TRUE;
    }
  g_clear_error (&error);
  fp_dbg ("Idle receive joined; partial=%d", self->rx_idle_partial);
  if (idle->joined)
    idle->joined (dev, idle->joined_data);
}

static void
goodix_idle_recv_handler (FpiSsm *ssm, FpDevice *dev)
{
  GoodixIdleRecv *idle = fpi_ssm_get_data (ssm);

  goodix_recv_start (ssm, dev, 0, idle->cancel);
}

static void
goodix_idle_recv_start (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixIdleRecv *idle = g_new0 (GoodixIdleRecv, 1);

  g_assert (!self->rx_active && !self->idle_rx_ssm);
  idle->dev = g_object_ref (dev);
  idle->cancel = g_cancellable_new ();
  self->idle_rx_ssm = fpi_ssm_new (dev, goodix_idle_recv_handler, 1);
  fpi_ssm_set_data (self->idle_rx_ssm, idle, (GDestroyNotify) goodix_idle_recv_free);
  fp_dbg ("Idle receive started after EC-off ACK");
  fpi_ssm_start (self->idle_rx_ssm, goodix_idle_recv_done);
}

void
goodix_idle_recv_stop (FpDevice *dev, GoodixIdleJoinedCallback joined,
                       gpointer data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (self->idle_rx_ssm)
    {
      GoodixIdleRecv *idle = fpi_ssm_get_data (self->idle_rx_ssm);
      g_assert (!idle->joined);
      idle->joined = joined;
      idle->joined_data = data;
      g_cancellable_cancel (idle->cancel);
    }
  else
    {
      joined (dev, data);
    }
}

/* Native budgets count Sleep(1) ACK polls and 50-ms response waits. GUsb uses
 * elapsed milliseconds instead: retain the caller's nominal budget across
 * complete packet reception, beginning after write/ACK completion respectively.
 * Other commands retain their unaudited existing timing policy. */
static void
goodix_cmd_set_budgets (GoodixCmdOperation *operation)
{
  guint8 command = GOODIX_PROTO_CMD_BYTE (operation->cmd.category,
                                          operation->cmd.command);

  operation->ack_timeout_ms = GOODIX_ACK_TIMEOUT;
  operation->response_timeout_ms = GOODIX_DATA_TIMEOUT;
  switch (command)
    {
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

    default:
      return 0;
    }
}

static const char *
goodix_cmd_phase_name (GoodixCmdState phase)
{
  switch (phase)
    {
    case GOODIX_CMD_SEND:
      return "send";

    case GOODIX_CMD_RECV_ACK:
    case GOODIX_CMD_VALIDATE_ACK:
      return "ACK";

    case GOODIX_CMD_RECV_DATA:
      return "data";

    case GOODIX_CMD_NUM_STATES:
      return "unknown";
    }
  g_assert_not_reached ();
}

/* Only a transport timeout or send I/O failure in the first transaction is
 * recoverable here. Protocol, cancellation, disconnect and ownership errors
 * keep their existing failure policy. The parent and command owner never
 * change between attempts. */
static gboolean
goodix_cmd_native_zero (GoodixCmdOperation *operation, const GError *error)
{
  if (g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT))
    return TRUE;
  if (operation->phase != GOODIX_CMD_SEND || !error || error->domain != G_USB_DEVICE_ERROR)
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
goodix_cmd_retry (FpDevice *dev,
                  FpiSsm   *ssm,
                  GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixCmdOperation *operation;

  if (self->cmd_ssm != ssm || self->rx_active)
    return FALSE;
  operation = fpi_ssm_get_data (ssm);
  if (self->cmd_owner != operation->parent_ssm ||
      !operation->retry_mode || operation->attempt != 1 ||
      (operation->phase != GOODIX_CMD_SEND &&
       operation->phase != GOODIX_CMD_RECV_ACK &&
       !(operation->response_bit && operation->phase == GOODIX_CMD_RECV_DATA)) ||
      !goodix_cmd_native_zero (operation, error))
    return FALSE;

  fp_dbg ("Retrying command cat=0x%02x cmd=0x%02x phase=%s attempt=1 parent-state=%d: %s",
          operation->cmd.category, operation->cmd.command,
          goodix_cmd_phase_name (operation->phase),
          fpi_ssm_get_cur_state (operation->parent_ssm), error->message);
  self->retried_mode_acks |= goodix_mode_ack_bit (
    GOODIX_PROTO_CMD_BYTE (operation->cmd.category, operation->cmd.command));
  operation->ack_deadline_us = 0;
  g_error_free (error);
  fpi_ssm_jump_to_state (ssm, GOODIX_CMD_SEND);
  return TRUE;
}

/* Zero means infinite to GUsb, so never round an expired deadline to zero. */
static guint
goodix_deadline_remaining (gint64 deadline_us)
{
  gint64 remaining = deadline_us - g_get_monotonic_time ();

  return remaining > 0 ? (guint) ((remaining + 999) / 1000) : 0;
}

static void
goodix_mark_coordinator_io_failure (FpiDeviceGoodix53x5 *self,
                                    const GError         *error)
{
  if (self->cmd_ssm && ((GoodixCmdOperation *) fpi_ssm_get_data (self->cmd_ssm))->result_cb)
    return; /* The composite command owns its intermediate result. */
  if (self->profile9_fdt.owner &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    self->needs_reinit = TRUE;
}

static void
goodix_cmd_operation_free (GoodixCmdOperation *operation)
{
  if (!operation)
    return;

  g_free (operation->cmd.payload);
  g_free (operation);
}

static gboolean
goodix_validate_ack_for_cmd (FpDevice        *dev,
                             const GoodixCmd *cmd,
                             guint8          *status,
                             GError         **error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  guint8 category, command;
  const guint8 *payload;
  gsize payload_len;
  guint8 expected_cmd_byte = GOODIX_PROTO_CMD_BYTE (cmd->category, cmd->command);

  if (!goodix_proto_rx_parse (&self->rx, &category, &command,
                              &payload, &payload_len))
    {
      g_set_error_literal (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                           "Failed to parse ACK");
      return FALSE;
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

static gboolean
goodix_try_drain_cancelled_fdt (FpDevice           *dev,
                                GoodixCmdOperation *operation,
                                GError            **error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  guint8 category, command;
  guint8 expected_command;
  const guint8 *payload;
  gsize payload_len;
  guint16 irq;
  GoodixFdtEventType type;
  GoodixProfile9FdtEvent event;

  if (!goodix_proto_rx_parse (&self->rx, &category, &command,
                              &payload, &payload_len))
    return FALSE;
  if (category != GOODIX_PROTO_CATEGORY_FDT)
    return FALSE;

  expected_command =
    operation->cancelled_fdt_mode == GOODIX_PROFILE9_FDT_WAIT_DOWN
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
  /* Stopping the worker invalidates notification, not parser-side base
   * publication. Keep the event local so cleanup cannot restart dispatch. */
  goodix_recv_apply_fdt_event (dev, type, &event);
  irq = event.irq;
  operation->cancelled_fdt_mode = GOODIX_PROFILE9_FDT_WAIT_NONE;
  fp_dbg ("Drained cancelled FDT event before sleep ACK: mode=0x%02x irq=0x%04x",
          expected_command, irq);
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
  if (error)
    {
      if (goodix_cmd_retry (dev, transfer->ssm, error))
        return;
      /* Cancellation can interrupt a partially sent command. Cleanup using
       * that same action token may also be prevented from establishing sleep. */
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
          g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_CANCELLED))
        FPI_DEVICE_GOODIX53X5 (dev)->needs_reinit = TRUE;
      goodix_mark_coordinator_io_failure (FPI_DEVICE_GOODIX53X5 (dev), error);
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }

  fpi_ssm_next_state (transfer->ssm);
}

/**
 * Send a complete protocol message, splitting into USB chunks.
 * Advances the SSM on completion.
 */
static void
goodix_send_message (FpiSsm   *ssm,
                     FpDevice *dev,
                     guint8    category,
                     guint8    command,
                     const guint8 *payload,
                     gsize     payload_len,
                     gboolean  use_checksum)
{
  gsize msg_len;
  guint8 *msg;
  FpiUsbTransfer *transfer;
  guint8 cmd_byte;

  msg = goodix_proto_build_message (category, command, payload, payload_len,
                                    use_checksum, &msg_len);
  cmd_byte = msg[0];

  /* The transport uses 64-byte USB writes. Continuation chunks prepend
   * cmd_byte | 1 and carry up to 63 more bytes of message data. */

  gsize total_chunks = 0;
  gsize padded_len = 0;

  /* Calculate how many chunks we need */
  if (msg_len <= GOODIX_USB_CHUNK_SIZE)
    {
      total_chunks = 1;
      padded_len = GOODIX_USB_CHUNK_SIZE;
    }
  else
    {
      /* First chunk: 64 bytes of message data */
      gsize remaining = msg_len - GOODIX_USB_CHUNK_SIZE;
      /* Each continuation chunk carries 63 bytes of data (1 byte for marker) */
      gsize cont_chunks = (remaining + 62) / 63;
      total_chunks = 1 + cont_chunks;
      padded_len = total_chunks * GOODIX_USB_CHUNK_SIZE;
    }

  guint8 *chunked = g_malloc0 (padded_len);

  if (total_chunks == 1)
    {
      memcpy (chunked, msg, msg_len);
    }
  else
    {
      /* First chunk */
      memcpy (chunked, msg, GOODIX_USB_CHUNK_SIZE);

      gsize src_offset = GOODIX_USB_CHUNK_SIZE;
      gsize dst_offset = GOODIX_USB_CHUNK_SIZE;

      for (gsize chunk = 1; chunk < total_chunks; chunk++)
        {
          chunked[dst_offset] = cmd_byte | 1;
          gsize data_in_chunk = MIN (63, msg_len - src_offset);
          if (data_in_chunk > 0)
            memcpy (chunked + dst_offset + 1, msg + src_offset, data_in_chunk);
          src_offset += data_in_chunk;
          dst_offset += GOODIX_USB_CHUNK_SIZE;
        }
    }

  g_free (msg);

  transfer = fpi_usb_transfer_new (dev);
  transfer->ssm = ssm;
  fpi_usb_transfer_fill_bulk_full (transfer, GOODIX_EP_OUT,
                                   chunked, padded_len, g_free);
  /* Native writes have no timeout. The active libfprint action owns device
   * lifetime and cancellation until this callback joins; Linux USB removal
   * independently completes in-flight transfers with NO_DEVICE. */
  fpi_usb_transfer_submit (transfer, 0, fpi_device_get_cancellable (dev),
                           goodix_tx_cb, NULL);
}

/* Forward declarations */
static void goodix_rx_cb (FpiUsbTransfer *transfer,
                          FpDevice       *dev,
                          gpointer        user_data,
                          GError         *error);

static void
goodix_recv_operation_free (GoodixRecvOperation *operation)
{
  g_clear_object (&operation->cancellable);
  g_free (operation);
}

static gboolean
goodix_recv_operation_finish (FpDevice           *dev,
                               FpiSsm             *ssm,
                               GoodixRecvOperation *operation)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (!self->rx_active || self->rx_owner != ssm ||
      self->rx_token != operation->token)
    return FALSE;

  self->rx_active = FALSE;
  self->rx_owner = NULL;
  return TRUE;
}

static gboolean
goodix_recv_start_full (FpiSsm                     *ssm,
                        FpDevice                   *dev,
                        guint                       timeout_ms,
                        GCancellable               *cancellable,
                        GoodixRecvCancelledCallback cancelled_cb,
                        gpointer                    cancelled_data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixRecvOperation *operation;
  FpiUsbTransfer *transfer;

  if (self->rx_active ||
      (self->cmd_owner != NULL && self->cmd_ssm != ssm))
    return FALSE;

  operation = g_new0 (GoodixRecvOperation, 1);
  operation->token = ++self->rx_token;
  if (operation->token == 0)
    operation->token = ++self->rx_token;
  operation->timeout_ms = timeout_ms;
  operation->deadline_us = timeout_ms ?
                           g_get_monotonic_time () + timeout_ms * 1000LL : 0;
  if (self->cmd_ssm == ssm)
    {
      GoodixCmdOperation *cmd_operation = fpi_ssm_get_data (ssm);

      if (cmd_operation->phase == GOODIX_CMD_RECV_ACK)
        {
          operation->fixed_deadline = TRUE;
          operation->deadline_us = cmd_operation->ack_deadline_us;
        }
      else if (cmd_operation->phase == GOODIX_CMD_RECV_DATA &&
               cmd_operation->response_timeout_ms == 500)
        {
          operation->fixed_deadline = TRUE;
        }
    }
  operation->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
  operation->cancelled_cb = cancelled_cb;
  operation->cancelled_data = cancelled_data;

  if (!self->rx_idle_partial)
    goodix_proto_rx_reset (&self->rx);
  self->rx_active = TRUE;
  self->rx_owner = ssm;

  transfer = fpi_usb_transfer_new (dev);
  transfer->ssm = ssm;
  fpi_usb_transfer_fill_bulk (transfer, GOODIX_EP_IN, GOODIX_USB_CHUNK_SIZE);
  fpi_usb_transfer_submit (transfer, timeout_ms, cancellable,
                           goodix_rx_cb, operation);
  return TRUE;
}

void
goodix_recv_start (FpiSsm       *ssm,
                   FpDevice     *dev,
                   guint         timeout_ms,
                   GCancellable *cancellable)
{
  if (!goodix_recv_start_full (ssm, dev, timeout_ms, cancellable, NULL, NULL))
    fpi_ssm_mark_failed (
      ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
                                     "A device receive is already active"));
}

static void
goodix_rx_cb (FpiUsbTransfer *transfer,
              FpDevice       *dev,
              gpointer        user_data,
              GError         *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixRecvOperation *operation = user_data;
  FpiUsbTransfer *next;

  if (!self->rx_active || self->rx_owner != transfer->ssm ||
      self->rx_token != operation->token)
    {
      fp_err ("Discarding callback for an inactive device receive");
      g_clear_error (&error);
      goodix_recv_operation_free (operation);
      return;
    }

  if (error)
    {
      goodix_recv_operation_finish (dev, transfer->ssm, operation);

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
          operation->cancelled_cb)
        {
          GoodixRecvCancelledCallback cancelled_cb = operation->cancelled_cb;
          gpointer cancelled_data = operation->cancelled_data;

          goodix_recv_operation_free (operation);
          cancelled_cb (transfer->ssm, dev, error, cancelled_data);
          return;
        }

      goodix_recv_operation_free (operation);
      if (!goodix_cmd_retry (dev, transfer->ssm, error))
        {
          goodix_mark_coordinator_io_failure (self, error);
          fpi_ssm_mark_failed (transfer->ssm, error);
        }
      return;
    }

  /* Bounded command polling retains its deadline. Other receives keep the existing
   * zero-length-read timeout policy. */
  if (transfer->actual_length == 0)
    {
      if (!operation->fixed_deadline)
        operation->deadline_us = operation->timeout_ms ?
                                 g_get_monotonic_time () + operation->timeout_ms * 1000LL : 0;
      goto receive_more;
    }

  if (!goodix_proto_rx_feed_chunk (&self->rx, transfer->buffer,
                                   transfer->actual_length))
    {
      goodix_recv_operation_finish (dev, transfer->ssm, operation);
      goodix_recv_operation_free (operation);
      fpi_ssm_mark_failed (transfer->ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Protocol reassembly error"));
      return;
    }

  if (goodix_proto_rx_complete (&self->rx))
    {
      guint8 category, command;
      const guint8 *payload;
      gsize payload_len;
      gboolean expected_ack = FALSE;
      gboolean idle_packet = self->idle_rx_ssm == transfer->ssm || self->rx_idle_partial;

      if (goodix_proto_rx_parse (&self->rx, &category, &command,
                                 &payload, &payload_len))
        {
          guint8 bit = category == 3 && command == 3 ? 1 :
                       category == 9 && command == 0 ? 2 : 0;
          GoodixCmdOperation *current = !idle_packet && self->cmd_ssm == transfer->ssm ?
                                        fpi_ssm_get_data (transfer->ssm) : NULL;

          if (bit)
            {
              if (bit == 1)
                {
                  if (payload_len < sizeof (self->manual_response))
                    {
                      goodix_recv_operation_finish (dev, transfer->ssm, operation);
                      goodix_recv_operation_free (operation);
                      fpi_ssm_mark_failed (transfer->ssm, fpi_device_error_new_msg (
                        FP_DEVICE_ERROR_PROTO, "Manual FDT reply is too short"));
                      return;
                    }
                  memcpy (self->manual_response, payload, sizeof (self->manual_response));
                }
              /* Configuration publishes only an event, never a success byte.
               * Both response slots are independent of ACK reception. */
              self->command_response_ready |= bit;
              if (!current || current->response_bit != bit ||
                  current->phase != GOODIX_CMD_RECV_DATA)
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
                  goodix_recv_operation_finish (dev, transfer->ssm, operation);
                  goodix_recv_operation_free (operation);
                  fpi_ssm_mark_failed (transfer->ssm, g_steal_pointer (&event_error));
                  return;
                }
              /* Every native parser mutation precedes replacement of the
               * latest worker notification, including during config/manual. */
              goodix_recv_apply_fdt_event (dev, type, &event);
              if (!idle_packet)
                {
                  self->pending_fdt_packet_len = self->rx.expected;
                  self->pending_fdt_mode = mode;
                  memcpy (self->pending_fdt_packet, self->rx.buf, self->rx.expected);
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
      if (goodix_proto_rx_parse (&self->rx, &category, &command,
                                 &payload, &payload_len) &&
          category == 0x0a && command == 7)
        {
          goodix_proto_rx_reset (&self->rx);
          goto receive_more;
        }

      if (self->cmd_ssm == transfer->ssm)
        {
          GoodixCmdOperation *cmd_operation = fpi_ssm_get_data (transfer->ssm);

          if (cmd_operation->phase == GOODIX_CMD_RECV_ACK &&
              goodix_proto_rx_parse (&self->rx, &category, &command,
                                     &payload, &payload_len) &&
              category == GOODIX_PROTO_CATEGORY_ACK && command == GOODIX_PROTO_CMD_ACK &&
              payload_len >= 2)
            expected_ack = payload[0] == GOODIX_PROTO_CMD_BYTE (
              cmd_operation->cmd.category, cmd_operation->cmd.command);
        }
      /* Native updates the acknowledged command's independent slot. A late
       * ACK from a repeated mode command cannot satisfy a different command
       * or an event/data wait. Validate the envelope before routing it. */
      if (!expected_ack &&
          goodix_proto_rx_parse (&self->rx, &category, &command,
                                 &payload, &payload_len) &&
          category == GOODIX_PROTO_CATEGORY_ACK && command == GOODIX_PROTO_CMD_ACK &&
          payload_len >= 2 &&
          (self->retried_mode_acks & goodix_mode_ack_bit (payload[0])))
        {
          goodix_proto_rx_reset (&self->rx);
          goto receive_more;
        }
      /* Message complete — advance SSM */
      goodix_recv_operation_finish (dev, transfer->ssm, operation);
      goodix_recv_operation_free (operation);
      fpi_ssm_next_state (transfer->ssm);
    }
  else
    {
      /* Preserve unrelated per-continuation data budgets. Scoped command waits
       * keep one deadline across interleaved packets and continuations. */
      if (!operation->fixed_deadline && self->idle_rx_ssm != transfer->ssm)
        operation->deadline_us = g_get_monotonic_time () + GOODIX_DATA_TIMEOUT * 1000LL;
      goto receive_more;
    }
  return;

receive_more:
  {
    if (self->rx.len == 0)
      self->rx_idle_partial = FALSE;
    if (self->idle_rx_ssm == transfer->ssm &&
        g_cancellable_is_cancelled (operation->cancellable))
      {
        goodix_recv_operation_finish (dev, transfer->ssm, operation);
        goodix_recv_operation_free (operation);
        fpi_ssm_mark_completed (transfer->ssm);
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
    next->ssm = transfer->ssm;
    fpi_usb_transfer_fill_bulk (next, GOODIX_EP_IN, GOODIX_USB_CHUNK_SIZE);
    fpi_usb_transfer_submit (next, timeout, operation->cancellable,
                             goodix_rx_cb, operation);
  }
}

gboolean
goodix_recv_start_cancellable_full (
  FpiSsm                     *ssm,
  FpDevice                   *dev,
  GCancellable               *cancellable,
  GoodixRecvCancelledCallback cancelled_cb,
  gpointer                    user_data)
{
  return goodix_recv_start_full (ssm, dev, 0, cancellable,
                                 cancelled_cb, user_data);
}

gboolean
goodix_recv_take_pending_fdt (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  gsize len = self->pending_fdt_packet_len;

  if (!len || self->rx_active || self->cmd_owner)
    return FALSE;
  self->pending_fdt_packet_len = 0;
  goodix_proto_rx_reset (&self->rx);
  return goodix_proto_rx_feed_chunk (&self->rx, self->pending_fdt_packet, len);
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

/* ========================================================================
 * Command sub-SSM: send → recv ACK → recv data
 * ======================================================================== */

static void
goodix_cmd_ssm_handler (FpiSsm   *ssm,
                        FpDevice *dev)
{
  GoodixCmdOperation *operation = fpi_ssm_get_data (ssm);
  GoodixCmd *cmd = &operation->cmd;

  operation->phase = fpi_ssm_get_cur_state (ssm);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_CMD_SEND:
      operation->attempt++;
      FPI_DEVICE_GOODIX53X5 (dev)->command_response_ready &= ~operation->response_bit;
      if (operation->response_bit)
        FPI_DEVICE_GOODIX53X5 (dev)->retried_mode_acks |= goodix_mode_ack_bit (
          GOODIX_PROTO_CMD_BYTE (cmd->category, cmd->command));
      goodix_send_message (ssm, dev, cmd->category, cmd->command,
                           cmd->payload, cmd->payload_len, cmd->use_checksum);
      break;

    case GOODIX_CMD_RECV_ACK:
      {
        guint timeout;

        if (!operation->ack_deadline_us)
          operation->ack_deadline_us = g_get_monotonic_time () + operation->ack_timeout_ms * 1000LL;
        timeout = goodix_deadline_remaining (operation->ack_deadline_us);
        if (!timeout)
          {
            GError *error = g_error_new_literal (
              G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT, "ACK deadline expired");

            if (!goodix_cmd_retry (dev, ssm, error))
              fpi_ssm_mark_failed (ssm, error);
            return;
          }
        goodix_recv_start (ssm, dev, timeout, NULL);
      }
      break;

    case GOODIX_CMD_VALIDATE_ACK:
      {
        g_autoptr(GError) error = NULL;
        guint8 status;

        if (operation->cancelled_fdt_mode != GOODIX_PROFILE9_FDT_WAIT_NONE &&
            goodix_try_drain_cancelled_fdt (dev, operation, &error))
          {
            fpi_ssm_jump_to_state (ssm, GOODIX_CMD_RECV_ACK);
            return;
          }
        if (error)
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        if (!goodix_validate_ack_for_cmd (dev, cmd, &status, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        /* A valid even status leaves the ACK unsatisfied. Keep this attempt's
         * original deadline; only exhaustion invokes its existing retry policy. */
        if ((status & GOODIX_PROTO_ACK_FLAG_VALID) == 0)
          {
            fpi_ssm_jump_to_state (ssm, GOODIX_CMD_RECV_ACK);
          }
        else
          {
            operation->ack_status = status;
            fpi_ssm_next_state (ssm);
          }
      }
      break;

    case GOODIX_CMD_RECV_DATA:
      if (operation->response_bit & FPI_DEVICE_GOODIX53X5 (dev)->command_response_ready)
        fpi_ssm_next_state (ssm);
      else
        goodix_recv_start (ssm, dev, operation->response_timeout_ms, NULL);
      break;
    }
}

static void
goodix_cmd_ssm_done (FpiSsm   *ssm,
                     FpDevice *dev,
                     GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixCmdOperation *operation = fpi_ssm_get_data (ssm);

  g_assert (self->cmd_ssm == ssm);
  self->cmd_owner = NULL;
  self->cmd_ssm = NULL;

  if (error)
    {
      g_prefix_error (&error,
                      "Command cat=0x%02x cmd=0x%02x phase=%s attempt=%u parent-state=%d: ",
                      operation->cmd.category, operation->cmd.command,
                      goodix_cmd_phase_name (operation->phase), operation->attempt,
                      fpi_ssm_get_cur_state (operation->parent_ssm));
    }
  if (operation->result_cb)
    {
      operation->result_cb (operation->parent_ssm, dev, operation->ack_status,
                            error && goodix_cmd_native_zero (operation, error), error);
      return;
    }

  if (error)
    {
      goodix_scan_note_command_error (operation->parent_ssm, dev, error);
      goodix_mark_coordinator_io_failure (self, error);
      fpi_ssm_mark_failed (operation->parent_ssm, error);
    }
  else
    {
      if (operation->idle_after_ack)
        goodix_idle_recv_start (dev);
      fpi_ssm_next_state (operation->parent_ssm);
    }
}

static void
goodix_cmd_begin_after_idle (FpDevice *dev, gpointer data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  fpi_ssm_start (self->cmd_ssm, goodix_cmd_ssm_done);
}

static void
goodix_run_cmd_full (FpiSsm                    *parent_ssm,
                     FpDevice                  *dev,
                     guint8                     category,
                     guint8                     command,
                     const guint8              *payload,
                     gsize                      payload_len,
                     gboolean                   expect_data,
                     GoodixProfile9FdtWaitMode  cancelled_mode,
                     GoodixCmdResultCallback   callback,
                     gboolean                  idle_after_ack)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiSsm *cmd_ssm;
  GoodixCmdOperation *operation;
  GoodixCmd *cmd;

  if (self->cmd_owner || (self->rx_active && self->rx_owner != self->idle_rx_ssm))
    {
      fpi_ssm_mark_failed (
        parent_ssm,
        fpi_device_error_new_msg (
          FP_DEVICE_ERROR_BUSY,
          self->cmd_owner ? "A device command is already active"
                          : "Cannot start a command while a receive is active"));
      return;
    }

  operation = g_new0 (GoodixCmdOperation, 1);
  operation->parent_ssm = parent_ssm;
  operation->result_cb = callback;
  operation->idle_after_ack = idle_after_ack;
  operation->cancelled_fdt_mode = cancelled_mode;
  operation->response_bit = expect_data ?
                            (category == 3 && command == 3 ? 1 :
                             category == 9 && command == 0 ? 2 : 0) : 0;
  operation->retry_mode = operation->response_bit || (!expect_data &&
                         ((category == GOODIX_PROTO_CATEGORY_FDT &&
                           (command == GOODIX_PROTO_CMD_FDT_DOWN || command == GOODIX_PROTO_CMD_FDT_UP)) ||
                          (category == 0x06 && command == 0)));
  cmd = &operation->cmd;
  cmd->category = category;
  cmd->command = command;
  cmd->use_checksum = TRUE;
  goodix_cmd_set_budgets (operation);

  if (payload_len > 0 && payload != NULL)
    {
      cmd->payload = g_memdup2 (payload, payload_len);
      cmd->payload_len = payload_len;
    }
  else
    {
      cmd->payload = NULL;
      cmd->payload_len = 0;
    }

  self->cmd_owner = parent_ssm;

  cmd_ssm = fpi_ssm_new_full (dev, goodix_cmd_ssm_handler,
                               expect_data ? GOODIX_CMD_NUM_STATES : GOODIX_CMD_RECV_DATA,
                                expect_data ? GOODIX_CMD_NUM_STATES : GOODIX_CMD_RECV_DATA,
                                "goodix-cmd");
  fpi_ssm_set_data (cmd_ssm, operation,
                    (GDestroyNotify) goodix_cmd_operation_free);
  self->cmd_ssm = cmd_ssm;
  goodix_idle_recv_stop (dev, goodix_cmd_begin_after_idle, NULL);
}

void
goodix_run_cmd (FpiSsm       *parent_ssm,
                FpDevice     *dev,
                guint8        category,
                guint8        command,
                const guint8 *payload,
                gsize         payload_len,
                gboolean      expect_data)
{
  goodix_run_cmd_full (parent_ssm, dev, category, command, payload,
                       payload_len, expect_data,
                       GOODIX_PROFILE9_FDT_WAIT_NONE, NULL, FALSE);
}

void
goodix_run_cmd_ec_off (FpiSsm *ssm, FpDevice *dev,
                       const guint8 *payload, gsize payload_len)
{
  goodix_run_cmd_full (ssm, dev, 0x0a, 7, payload, payload_len, FALSE,
                       GOODIX_PROFILE9_FDT_WAIT_NONE, NULL, TRUE);
}

void
goodix_run_cmd_result (FpiSsm *ssm, FpDevice *dev,
                       guint8 category, guint8 command,
                       const guint8 *payload, gsize payload_len,
                       gboolean expect_data, GoodixCmdResultCallback callback)
{
  goodix_run_cmd_full (ssm, dev, category, command, payload, payload_len,
                       expect_data, GOODIX_PROFILE9_FDT_WAIT_NONE, callback, FALSE);
}

void
goodix_run_cmd_drain_fdt_once (
  FpiSsm                    *parent_ssm,
  FpDevice                  *dev,
  guint8                     category,
  guint8                     command,
  const guint8              *payload,
  gsize                      payload_len,
  GoodixProfile9FdtWaitMode  cancelled_mode)
{
  g_return_if_fail (cancelled_mode != GOODIX_PROFILE9_FDT_WAIT_NONE);
  goodix_run_cmd_full (parent_ssm, dev, category, command, payload,
                       payload_len, FALSE, cancelled_mode, NULL, FALSE);
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

  if (goodix_proto_rx_parse (&self->rx, out_category, out_command,
                             out_payload, out_payload_len))
    return TRUE;

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
