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
  guint64                       token;
  guint                         timeout_ms;
  GCancellable                 *cancellable;
  GoodixRecvCancelledCallback   cancelled_cb;
  gpointer                      cancelled_data;
} GoodixRecvOperation;

typedef struct
{
  GoodixCmd                   cmd;
  FpiSsm                     *parent_ssm;
  GoodixProfile9FdtWaitMode   cancelled_fdt_mode;
} GoodixCmdOperation;

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

  if (payload[0] != expected_cmd_byte ||
      (payload[1] & GOODIX_PROTO_ACK_FLAG_VALID) == 0)
    {
      g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                   "Unexpected ACK: expected cmd_byte=0x%02x, got ack_cmd=0x%02x flags=0x%02x",
                   expected_cmd_byte, payload[0], payload[1]);
      return FALSE;
    }

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

  irq = payload[0] | ((guint16) payload[1] << 8);
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

      goodix_mark_coordinator_io_failure (self, error);
      goodix_recv_operation_free (operation);
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }

  /* Skip zero-length reads — resubmit with same timeout/cancellable */
  if (transfer->actual_length == 0)
    {
      next = fpi_usb_transfer_new (dev);
      next->ssm = transfer->ssm;
      fpi_usb_transfer_fill_bulk (next, GOODIX_EP_IN, GOODIX_USB_CHUNK_SIZE);
      fpi_usb_transfer_submit (next, operation->timeout_ms,
                               operation->cancellable,
                               goodix_rx_cb, operation);
      return;
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
      /* Message complete — advance SSM */
      goodix_recv_operation_finish (dev, transfer->ssm, operation);
      goodix_recv_operation_free (operation);
      fpi_ssm_next_state (transfer->ssm);
    }
  else
    {
      /* Need more chunks — use stored timeout/cancellable for continuations.
       * For event waits (timeout=0/infinite), once we start getting data
       * the remaining chunks should arrive quickly, so use DATA_TIMEOUT. */
      next = fpi_usb_transfer_new (dev);
      next->ssm = transfer->ssm;
      fpi_usb_transfer_fill_bulk (next, GOODIX_EP_IN, GOODIX_USB_CHUNK_SIZE);
      fpi_usb_transfer_submit (next, GOODIX_DATA_TIMEOUT,
                               operation->cancellable,
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

/* ========================================================================
 * Command sub-SSM: send → recv ACK → recv data
 * ======================================================================== */

static void
goodix_cmd_ssm_handler (FpiSsm   *ssm,
                        FpDevice *dev)
{
  GoodixCmdOperation *operation = fpi_ssm_get_data (ssm);
  GoodixCmd *cmd = &operation->cmd;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_CMD_SEND:
      goodix_send_message (ssm, dev, cmd->category, cmd->command,
                           cmd->payload, cmd->payload_len, cmd->use_checksum);
      break;

    case GOODIX_CMD_RECV_ACK:
      goodix_recv_start (ssm, dev, GOODIX_ACK_TIMEOUT, NULL);
      break;

    case GOODIX_CMD_VALIDATE_ACK:
      {
        g_autoptr(GError) error = NULL;

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

        if (!goodix_validate_ack_for_cmd (dev, cmd, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_CMD_RECV_DATA:
      goodix_recv_start (ssm, dev, GOODIX_DATA_TIMEOUT, NULL);
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
      goodix_mark_coordinator_io_failure (self, error);
      fpi_ssm_mark_failed (operation->parent_ssm, error);
    }
  else
    fpi_ssm_next_state (operation->parent_ssm);
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
  cmd = &operation->cmd;
  cmd->category = category;
  cmd->command = command;
  cmd->use_checksum = TRUE;

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
