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
  FpiSsm                   *parent_ssm;
  GoodixProfile9FdtWaitMode cancelled_fdt_mode;
  gboolean                  retry_mode;
  guint                     attempt;
  GoodixCmdState            phase;
  gint64                    ack_deadline_us;
  guint                     ack_timeout_ms;
  guint                     response_timeout_ms;
} GoodixCmdOperation;

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
       operation->phase != GOODIX_CMD_RECV_ACK) ||
      !(g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT) ||
        (operation->phase == GOODIX_CMD_SEND &&
         g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_IO))))
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
  fpi_usb_transfer_submit (transfer, GOODIX_CMD_TIMEOUT, NULL,
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
      if (!operation->fixed_deadline)
        operation->deadline_us = g_get_monotonic_time () + GOODIX_DATA_TIMEOUT * 1000LL;
      goto receive_more;
    }
  return;

receive_more:
  {
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
        FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
        guint8 category, command;
        const guint8 *payload;
        gsize payload_len;
        guint8 status;

        /* Native applies EVERY event's base updates before replacing its one
         * worker notification. Keep these updates even across ACK failure;
         * the outstanding command still retries its original copied payload. */
        if (operation->retry_mode && cmd->category == GOODIX_PROTO_CATEGORY_FDT &&
            goodix_proto_rx_parse (&self->rx, &category, &command, &payload, &payload_len) &&
            category == GOODIX_PROTO_CATEGORY_FDT && command == cmd->command &&
            payload_len == GOODIX_FDT_EVENT_PAYLOAD_LEN)
          {
            GoodixFdtEventType type;
            GoodixProfile9FdtEvent event;
            GoodixProfile9FdtWaitMode mode = cmd->command == GOODIX_PROTO_CMD_FDT_DOWN ?
                                             GOODIX_PROFILE9_FDT_WAIT_DOWN : GOODIX_PROFILE9_FDT_WAIT_UP;

            if (!goodix_cmd_parse_fdt_event (dev, mode, &type, &event, &error))
              {
                fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
                return;
              }
            goodix_recv_apply_fdt_event (dev, type, &event);
            self->pending_fdt_packet_len = self->rx.expected;
            memcpy (self->pending_fdt_packet, self->rx.buf, self->rx.expected);
            fpi_ssm_jump_to_state (ssm, GOODIX_CMD_RECV_ACK);
            return;
          }

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
          fpi_ssm_jump_to_state (ssm, GOODIX_CMD_RECV_ACK);
        else
          fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_CMD_RECV_DATA:
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
      goodix_mark_coordinator_io_failure (self, error);
      fpi_ssm_mark_failed (operation->parent_ssm, error);
    }
  else
    {
      fpi_ssm_next_state (operation->parent_ssm);
    }
}

static void
goodix_run_cmd_full (FpiSsm                    *parent_ssm,
                     FpDevice                  *dev,
                     guint8                     category,
                     guint8                     command,
                     const guint8              *payload,
                     gsize                      payload_len,
                     gboolean                   expect_data,
                     GoodixProfile9FdtWaitMode  cancelled_mode)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiSsm *cmd_ssm;
  GoodixCmdOperation *operation;
  GoodixCmd *cmd;

  if (self->cmd_owner || self->rx_active)
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
  operation->cancelled_fdt_mode = cancelled_mode;
  operation->retry_mode = !expect_data &&
                          ((category == GOODIX_PROTO_CATEGORY_FDT &&
                            (command == GOODIX_PROTO_CMD_FDT_DOWN || command == GOODIX_PROTO_CMD_FDT_UP)) ||
                           (category == 0x06 && command == 0));
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
  fpi_ssm_start (cmd_ssm, goodix_cmd_ssm_done);
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
                       GOODIX_PROFILE9_FDT_WAIT_NONE);
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
                       payload_len, FALSE, cancelled_mode);
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
