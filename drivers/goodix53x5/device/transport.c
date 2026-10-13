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
#include "device/image.h"
#include "device/session.h"

#include <string.h>

/* USB chunk size */
#define GOODIX_USB_READ_SIZE 0x8000
#define GOODIX_MCU_RX_SIZE (0x40000)

#define GOODIX_FDT_EVENT_PAYLOAD_LEN  (4 + GOODIX_FDT_BASE_LEN)
#define GOODIX_PROTO_CMD_BYTE(category, command) \
  (((category) << 4) | ((command) << 1))


typedef enum {
  GOODIX_TRANSPORT_IDLE,
  GOODIX_TRANSPORT_SEND,
  GOODIX_TRANSPORT_ACK,
  GOODIX_TRANSPORT_RESPONSE,
  GOODIX_TRANSPORT_EVENT,
  GOODIX_TRANSPORT_REPLY,
} GoodixTransportPhase;

/* A command owns its OUT callback and logical wait, never the physical IN. */
struct _GoodixTransport
{
  FpDevice                 *dev;
  GoodixTransportPhase      phase;
  GoodixTransportDone       done;
  gpointer                  data;
  GCancellable             *cancellable;
  GSource                  *timer;
  GSource                  *cancel_source;
  guint                     timeout_ms;
  gint64                    deadline_us;
  GoodixProfile9FdtWaitMode event_mode;
  GoodixCmd                 cmd;
  /* A raw TLS record to write instead of cmd; it is never ACKed. */
  GBytes                   *record;
  gboolean                  expect_data;
  gboolean                  retry;
  GoodixResponseSlot        response_slot;
  guint8                    ack_status;
  guint                     attempt;
  gboolean                  write_submitted;
  guint                     ack_timeout_ms;
  guint                     response_timeout_ms;
  gsize                     mcu_length;
  /* A native read completion dispatches every cell before its owner yields. */
  gboolean                  processing_cells;
  gboolean                  completion_pending;
  GError                   *completion_error;
};

/* usbinterface 020058/021200: one continuous read, stopped only by a joined
 * hardware boundary. Sender expiry/capture cancellation leaves it untouched. */
struct _GoodixReader
{
  FpDevice             *dev;
  GCancellable         *cancel;
  gboolean              pending;
  GError               *error;
  GoodixTransportJoined joined;
  gpointer              joined_data;
};

static void goodix_transport_send (GoodixTransport *operation);
static void goodix_transport_receive (GoodixTransport *operation, guint timeout);
static void goodix_transport_complete (GoodixTransport *operation, GError *error);
static void goodix_reader_start (FpDevice *dev);
static void goodix_transport_ack (GoodixTransport *operation);

static gboolean
goodix_transport_is_idle (GoodixTransport *operation)
{
  return operation->phase == GOODIX_TRANSPORT_IDLE;
}

/* Native 018dd8 and its command wrappers. Budgets are nominal milliseconds;
 * GLib measures elapsed time, rather than Sleep(1)/Wait(50) poll counts.
 * Register 82 here is the sized/chip adapter (200), not the factory two-byte
 * adapter (500). Unknown commands retain their existing fallback policy. */
static const GoodixCommandPolicy command_policies[] = {
  { 0x32, 500, 0, GOODIX_RESPONSE_NONE, TRUE },
  { 0x34, 500, 0, GOODIX_RESPONSE_NONE, TRUE },
  { 0x60, 200, 0, GOODIX_RESPONSE_NONE, TRUE },
  { 0x36, 500, 500, GOODIX_RESPONSE_MANUAL, TRUE },
  { 0x90, 500, 500, GOODIX_RESPONSE_CONFIG, TRUE },
  { 0x00, 500, 0, GOODIX_RESPONSE_NONE, TRUE },
  { 0xa8, 500, 2000, GOODIX_RESPONSE_SYSTEM, TRUE },
  { 0x20, 500, 500, GOODIX_RESPONSE_IMAGE, FALSE },
  { 0xa2, 500, 1000, GOODIX_RESPONSE_SYSTEM, FALSE },
  { 0x82, 500, 200, GOODIX_RESPONSE_REGISTER, TRUE },
  { 0xa6, 500, 200, GOODIX_RESPONSE_OTP, TRUE },
  { 0xe4, 500, 1000, GOODIX_RESPONSE_PRODUCTION, TRUE },
  { 0xe2, 500, 1000, GOODIX_RESPONSE_PRODUCTION, TRUE },
  { 0xae, 200, 0, GOODIX_RESPONSE_NONE, FALSE },
  { 0xd2, 500, 0, GOODIX_RESPONSE_NONE, FALSE },
};

G_STATIC_ASSERT (GOODIX_RESPONSE_COUNT <= 8);

const GoodixCommandPolicy *
goodix_cmd_policy (guint8 command)
{
  for (guint i = 0; i < G_N_ELEMENTS (command_policies); i++)
    if (command_policies[i].command == command)
      return &command_policies[i];
  return NULL;
}

/* Checks whether replies for @slot land in the shared response cache, which
 * the awaited command reads as its own reply. */
static gboolean
goodix_response_slot_is_shared (GoodixResponseSlot slot)
{
  return slot == GOODIX_RESPONSE_SYSTEM || slot == GOODIX_RESPONSE_REGISTER ||
         slot == GOODIX_RESPONSE_OTP || slot == GOODIX_RESPONSE_PRODUCTION;
}

static guint8
goodix_response_bit (GoodixResponseSlot slot)
{
  return slot == GOODIX_RESPONSE_NONE ? 0 : 1u << slot;
}

static void
goodix_cmd_set_policy (GoodixTransport *operation)
{
  guint8 command = GOODIX_PROTO_CMD_BYTE (operation->cmd.category, operation->cmd.command);
  const GoodixCommandPolicy *policy = goodix_cmd_policy (command);

  operation->retry = goodix_family (FPI_DEVICE_GOODIX53X5 (operation->dev))->protocol.command_budget (
    command, operation->expect_data, &operation->ack_timeout_ms, &operation->response_timeout_ms);
  operation->response_slot = policy && operation->expect_data ?
                             policy->response_slot : GOODIX_RESPONSE_NONE;
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

/* Returns what @operation sends, for its logs and errors. */
static gchar *
goodix_transport_describe (GoodixTransport *operation)
{
  if (operation->record)
    return g_strdup ("TLS record");
  return g_strdup_printf ("command cat=0x%02x cmd=0x%02x",
                          operation->cmd.category, operation->cmd.command);
}

static gboolean
goodix_cmd_retry (GoodixTransport *operation, GError *error)
{
  GoodixTransportPhase phase = operation->phase;

  if (!operation->retry || operation->attempt != 1 ||
      (phase != GOODIX_TRANSPORT_SEND && phase != GOODIX_TRANSPORT_ACK &&
       !(operation->response_slot != GOODIX_RESPONSE_NONE && phase == GOODIX_TRANSPORT_RESPONSE)) ||
      !(phase == GOODIX_TRANSPORT_SEND ?
        (operation->record ? goodix_cmd_native_zero (phase, error) :
         goodix_family (FPI_DEVICE_GOODIX53X5 (operation->dev))->protocol.retry_write (error)) :
        g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT)))
    return FALSE;

  g_autofree gchar *what = goodix_transport_describe (operation);

  fp_dbg ("Retrying %s phase=%s attempt=1: %s",
          what, goodix_cmd_phase_name (phase), error->message);
  g_error_free (error);
  goodix_transport_send (operation);
  return TRUE;
}

/* Round a positive remaining command budget up to milliseconds. */
static guint
goodix_deadline_remaining (gint64 deadline_us)
{
  gint64 remaining = deadline_us - g_get_monotonic_time ();

  return remaining > 0 ? (guint) ((remaining + 999) / 1000) : 0;
}

/* Checks whether @cmd downloads a sensor configuration. */
static gboolean
goodix_cmd_is_config_download (const GoodixCmd *cmd)
{
  return cmd->category == GOODIX_PROTO_CATEGORY_CONFIG &&
         cmd->command == GOODIX_PROTO_CMD_CONFIG_DOWNLOAD;
}

/* Checks whether the family's protocol refuses @cmd, whichever caller asks. */
static gboolean
goodix_cmd_refused (FpiDeviceGoodix53x5 *self, const GoodixCmd *cmd)
{
  return !goodix_family (self)->protocol.may_send (cmd);
}

/* Appends @payload to the category-D stream and signals its waiter. */
void
goodix_mcu_publish (FpiDeviceGoodix53x5 *self, const guint8 *payload, gsize len)
{
  if (!self->mcu_rx)
    self->mcu_rx = g_byte_array_new ();
  g_byte_array_append (self->mcu_rx, payload,
                       MIN (len, GOODIX_MCU_RX_SIZE - self->mcu_rx->len));
  self->mcu_ready = TRUE;
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
goodix_record_write_failed (FpDevice *dev, gpointer data)
{
  GoodixTransport *operation = FPI_DEVICE_GOODIX53X5 (dev)->transport;

  operation->timer = NULL;
  goodix_transport_complete (operation, g_error_copy (data));
}

static void
goodix_tx_cb (FpiUsbTransfer *transfer,
              FpDevice       *dev,
              gpointer        user_data,
              GError         *error)
{
  GoodixTransport *operation = user_data;
  GoodixReader *reader = FPI_DEVICE_GOODIX53X5 (dev)->reader;

  /* TOD's WriteTls sleeps 1 ms after every record write, failed or not. */
  if (error && operation->record)
    {
      operation->timer = fpi_device_add_timeout (dev, 1, goodix_record_write_failed,
                                                 error, (GDestroyNotify) g_error_free);
      return;
    }
  if (error)
    {
      goodix_transport_complete (operation, error);
      return;
    }

  operation->phase = GOODIX_TRANSPORT_ACK;
  if (reader && reader->error)
    {
      goodix_transport_complete (operation, g_error_copy (reader->error));
      return;
    }
  /* A TLS record is not a protocol message; the sensor never ACKs it. */
  if (operation->record)
    {
      goodix_transport_complete (operation, NULL);
      return;
    }
  operation->deadline_us = g_get_monotonic_time () + operation->ack_timeout_ms * 1000LL;
  if ((operation->cmd.category == GOODIX_PROTO_CATEGORY_NOP && operation->cmd.command == 0) ||
      (operation->cmd.category == GOODIX_PROTO_CATEGORY_SYSTEM && operation->cmd.command == 4))
    operation->cancellable = g_object_ref (goodix_session_io_cancellable (dev));
  if (operation->ack_status & GOODIX_PROTO_ACK_FLAG_VALID)
    goodix_transport_ack (operation);
  else
    goodix_transport_receive (operation, operation->ack_timeout_ms);
}

/* The ACK clock starts after OUT completion. IN stays posted during writes. */
static void
goodix_transport_send (GoodixTransport *operation)
{
  FpDevice *dev = operation->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixCmd *cmd = &operation->cmd;
  gsize msg_len, padded_len;
  guint8 *msg, *chunked;
  FpiUsbTransfer *transfer;
  GCancellable *cancellable;

  operation->phase = GOODIX_TRANSPORT_SEND;
  operation->attempt++;
  operation->ack_status = 0;
  g_clear_pointer (&operation->timer, g_source_destroy);
  g_clear_pointer (&operation->cancel_source, g_source_destroy);
  g_clear_object (&operation->cancellable);
  self->command_response_ready &= ~goodix_response_bit (operation->response_slot);

  if (operation->record)
    {
      chunked = goodix_family (self)->link.frame_record (operation->record, &padded_len);
    }
  else
    {
      msg = goodix_proto_build_message (cmd->category, cmd->command,
                                        cmd->payload, cmd->payload_len,
                                        cmd->use_checksum, &msg_len);
      chunked = goodix_family (self)->link.frame (msg, msg_len, &padded_len);
      g_free (msg);
    }
  if (!chunked)
    {
      goodix_transport_complete (operation, fpi_device_error_new_msg (
                                   FP_DEVICE_ERROR_PROTO, "Message too long to frame"));
      return;
    }

  transfer = fpi_usb_transfer_new (dev);
  fpi_usb_transfer_fill_bulk_full (transfer, goodix_family (self)->link.ep_out,
                                   chunked, padded_len, g_free);
  /* Native writes have no timeout. The selected action or hardware-service
   * owner supplies cancellation until this callback joins; Linux USB removal
   * independently completes in-flight transfers with NO_DEVICE.
   * libfprint completes an already-cancelled submission without starting
   * it, so only a started write can leave the sensor partially commanded. */
  cancellable = goodix_session_io_cancellable (dev);
  operation->write_submitted = !g_cancellable_is_cancelled (cancellable);
  /* Native D0 entry starts the continuous reader before launching the GTLS
   * worker. Post IN before the first OUT after quiesce, too, so queued wake
   * notifications can arrive without waiting for a command submission. */
  goodix_reader_start (dev);
  fpi_usb_transfer_submit (transfer, 0, cancellable, goodix_tx_cb, operation);
}

/* Forward declarations */
static void goodix_rx_cb (FpiUsbTransfer *transfer,
                          FpDevice       *dev,
                          gpointer        user_data,
                          GError         *error);

static void
goodix_transport_free (GoodixTransport *operation)
{
  g_clear_pointer (&operation->timer, g_source_destroy);
  g_clear_pointer (&operation->cancel_source, g_source_destroy);
  g_clear_object (&operation->cancellable);
  g_clear_error (&operation->completion_error);
  g_free (operation->cmd.payload);
  g_clear_pointer (&operation->record, g_bytes_unref);
  g_object_unref (operation->dev);
  g_free (operation);
}

static void
goodix_transport_expired (FpDevice *dev, gpointer data)
{
  GoodixTransport *operation = data;

  operation->timer = NULL;
  if (operation->phase == GOODIX_TRANSPORT_ACK && operation->expect_data &&
      goodix_family (FPI_DEVICE_GOODIX53X5 (dev))->protocol.ack_timeout_waits_for_data)
    {
      g_autofree gchar *what = goodix_transport_describe (operation);

      fp_dbg ("No ACK for %s; waiting for its data", what);
      goodix_transport_ack (operation);
      return;
    }
  goodix_transport_complete (operation,
                            g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                                 "Receive deadline expired"));
}

static gboolean
goodix_transport_cancelled (GCancellable *cancel, gpointer data)
{
  goodix_transport_complete (data, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                       "Receive wait cancelled"));
  return G_SOURCE_REMOVE;
}

static void
goodix_transport_arm_wait (GoodixTransport *operation)
{
  g_clear_pointer (&operation->timer, g_source_destroy);
  g_clear_pointer (&operation->cancel_source, g_source_destroy);
  if (operation->deadline_us)
    operation->timer = fpi_device_add_timeout (operation->dev,
                                               goodix_deadline_remaining (operation->deadline_us),
                                               goodix_transport_expired, operation, NULL);
  if (operation->cancellable)
    {
      operation->cancel_source = g_cancellable_source_new (operation->cancellable);
      g_source_set_callback (operation->cancel_source, G_SOURCE_FUNC (goodix_transport_cancelled),
                             operation, NULL);
      g_source_attach (operation->cancel_source, g_main_context_get_thread_default ());
      g_source_unref (operation->cancel_source);
    }
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
  operation->response_slot = GOODIX_RESPONSE_NONE;
  operation->done = done;
  operation->data = data;
  self->transport = operation;
  return operation;
}

/* Consume one coalesced MCU signal, retaining any unread stream suffix. */
static gboolean
goodix_transport_take_mcu (GoodixTransport *operation)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (operation->dev);
  gsize count;

  if (operation->processing_cells ||
      operation->phase != GOODIX_TRANSPORT_REPLY || !self->mcu_ready)
    return FALSE;

  count = self->mcu_rx ? self->mcu_rx->len : 0;
  if (operation->mcu_length)
    count = MIN (count, operation->mcu_length);
  g_clear_pointer (&self->mcu_reply, g_bytes_unref);
  self->mcu_reply = g_bytes_new (count ? self->mcu_rx->data : NULL, count);
  if (count)
    g_byte_array_remove_range (self->mcu_rx, 0, count);
  self->mcu_ready = FALSE;
  self->reply_valid = TRUE;
  self->reply_category = 0x0d;
  self->reply_command = 1;
  self->reply_payload = g_bytes_get_data (self->mcu_reply, &self->reply_payload_len);
  goodix_transport_complete (operation, NULL);
  return TRUE;
}

/* Command deadlines do not own the continuous receiver's partial packet. */
static void
goodix_transport_receive (GoodixTransport *operation, guint timeout)
{
  FpDevice *dev = operation->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (goodix_transport_take_mcu (operation))
    return;
  g_clear_pointer (&self->mcu_reply, g_bytes_unref);
  operation->timeout_ms = timeout;
  if (!self->rx.len || goodix_proto_rx_complete (&self->rx))
    goodix_proto_rx_reset (&self->rx);
  self->reply_valid = FALSE;

  if (operation->processing_cells)
    return;
  goodix_transport_arm_wait (operation);
  goodix_reader_start (dev);
}

/* A generic response is a view of the shared cache at consumption time, not
 * the last RX packet (which may be the ACK). Event families can alias command
 * selectors; expose the awaited selector to existing named reply parsers.
 * Keep short-packet lengths for Linux validation, and the native production
 * length prefix, while bounding every view by the owned cache. */
static GError *
goodix_transport_select_response (GoodixTransport *operation)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (operation->dev);
  GoodixResponseSlot slot = operation->response_slot;
  gsize offset = 0, length;

  if (!goodix_response_slot_is_shared (slot))
    return NULL;
  if (!(self->command_response_ready & goodix_response_bit (slot)))
    return fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                     "Command response event is unavailable");

  length = self->shared_response_lengths[slot];
  if (slot == GOODIX_RESPONSE_PRODUCTION)
    {
      guint32 wire_length;
      memcpy (&wire_length, self->shared_response_storage, sizeof (wire_length));
      length = GUINT32_FROM_LE (wire_length);
      offset = sizeof (wire_length);
    }
  if (length > sizeof (self->shared_response_storage) - offset)
    return fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                     "Shared response length exceeds cache capacity");

  self->reply_valid = TRUE;
  self->reply_category = operation->cmd.category;
  self->reply_command = operation->cmd.command;
  self->reply_payload = self->shared_response_storage + offset;
  self->reply_payload_len = length;
  return NULL;
}

static void
goodix_transport_complete (GoodixTransport *operation, GError *error)
{
  /* Completion can free this owner and submit the next command. Drain the
   * completed USB buffer first, including packets after its ACK or response. */
  if (operation->processing_cells)
    {
      operation->completion_pending = TRUE;
      if (!operation->completion_error)
        operation->completion_error = error;
      else
        g_clear_error (&error);
      return;
    }
  g_autoptr(FpDevice) dev = g_object_ref (operation->dev);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransportDone done = operation->done;
  gpointer data = operation->data;
  gboolean image_failed = !error && operation->response_slot == GOODIX_RESPONSE_IMAGE &&
                          self->image_response_failed &&
                          (operation->phase == GOODIX_TRANSPORT_ACK ||
                           operation->phase == GOODIX_TRANSPORT_RESPONSE);
  gboolean config_failed = !error && operation->expect_data &&
                           goodix_cmd_is_config_download (&operation->cmd) &&
                           self->config_download_failed;

  if (image_failed)
    error = g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_IO,
                                 "Image receiver rejected the frame");
  if (config_failed)
    error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                     "Configuration download failed: status %u",
                                     self->config_download_status);
  if (!error && operation->expect_data &&
      (operation->phase == GOODIX_TRANSPORT_ACK || operation->phase == GOODIX_TRANSPORT_RESPONSE))
    error = goodix_transport_select_response (operation);
  GoodixTransportResult result = {
    .ack_status = operation->ack_status,
    .restart_gtls = operation->phase == GOODIX_TRANSPORT_EVENT && self->gtls_restart_pending,
    .ordinary_exhaustion = error && !(self->reader && self->reader->error) &&
      (image_failed || config_failed || goodix_cmd_native_zero (operation->phase, error)),
    .write_cancelled = operation->phase == GOODIX_TRANSPORT_SEND &&
      operation->write_submitted &&
      (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
       g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_CANCELLED)),
  };

  if (error && !(self->reader && self->reader->error) && goodix_cmd_retry (operation, error))
    return;
  else if (error && operation->attempt)
    {
      g_autofree gchar *what = goodix_transport_describe (operation);

      what[0] = g_ascii_toupper (what[0]);
      g_prefix_error (&error, "%s phase=%s attempt=%u: ", what,
                      goodix_cmd_phase_name (operation->phase), operation->attempt);
    }

  if (!error && operation->expect_data && goodix_cmd_is_config_download (&operation->cmd) &&
      !self->config_restore_pending)
    self->config_lost = FALSE;
  self->transport = NULL;
  goodix_transport_free (operation);
  if (done)
    done (dev, &result, error, data);
}

static void
goodix_rx_continue (GoodixTransport *operation)
{
  FpDevice *dev = operation->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (self->rx.len == 0)
    {
      self->rx_idle_partial = FALSE;
      self->reply_valid = FALSE;
    }
  if (operation->processing_cells)
    return;
  goodix_transport_arm_wait (operation);
}

static void
goodix_transport_ack (GoodixTransport *operation)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (operation->dev);

  if (!operation->expect_data ||
      (goodix_response_bit (operation->response_slot) & self->command_response_ready))
    goodix_transport_complete (operation, NULL);
  else
    {
      operation->phase = GOODIX_TRANSPORT_RESPONSE;
      operation->deadline_us = g_get_monotonic_time () + operation->response_timeout_ms * 1000LL;
      g_clear_object (&operation->cancellable);
      if (operation->cmd.category == GOODIX_PROTO_CATEGORY_SYSTEM && operation->cmd.command == 4)
        operation->cancellable = g_object_ref (goodix_session_io_cancellable (operation->dev));
      goodix_transport_receive (operation, operation->response_timeout_ms);
    }
}

/* Checks whether a reply for @slot answers another command than the one in
 * flight, as the family's protocol decides. A stale firmware reply must not
 * complete a reset that shares its slot. */
static gboolean
goodix_reply_is_foreign (FpiDeviceGoodix53x5 *self, GoodixResponseSlot slot,
                         guint8 category, guint8 command)
{
  GoodixTransport *operation = self->transport;

  return operation && !operation->record &&
         (operation->phase == GOODIX_TRANSPORT_SEND || operation->phase == GOODIX_TRANSPORT_ACK ||
          operation->phase == GOODIX_TRANSPORT_RESPONSE) &&
         (goodix_response_slot_is_shared (slot) ||
          (slot == GOODIX_RESPONSE_CONFIG && goodix_cmd_is_config_download (&operation->cmd))) &&
         !goodix_family (self)->protocol.reply_answers (category, command, &operation->cmd);
}

/* Each sensor request owns a new session followed by a finger-down arm. */
static void
goodix_request_reconnect (FpiDeviceGoodix53x5 *self)
{
  fp_info ("Sensor requested a new %s session", goodix_family (self)->channel.name);
  self->reconnect_pending++;
  self->gtls_restart_pending = TRUE;
}

void
goodix_rx_cell (GoodixTransport *operation,
                const guint8    *buffer,
                gsize            cell_length)
{
  FpDevice *dev = operation->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GError *error = NULL;
  gboolean fixed_deadline = operation->phase == GOODIX_TRANSPORT_ACK ||
    (operation->phase == GOODIX_TRANSPORT_RESPONSE && operation->response_slot != GOODIX_RESPONSE_NONE) ||
    (operation->phase == GOODIX_TRANSPORT_REPLY && operation->mcu_length);

  /* Bounded command polling retains its deadline. Other receives keep the existing
   * zero-length-read timeout policy. */
  if (cell_length == 0)
    {
      if (!fixed_deadline)
        operation->deadline_us = operation->timeout_ms ?
                                 g_get_monotonic_time () + operation->timeout_ms * 1000LL : 0;
      goto receive_more;
    }

  /* Native reassembly restarts on a first cell or a different selector;
   * orphan continuations are ignored. This also permits an ACK to interrupt
   * a partial reply retained across a command timeout. */
  if (!(buffer[0] & 1) ||
      (buffer[0] & 0xfe) != self->rx.cmd_byte)
    {
      goodix_proto_rx_reset (&self->rx);
      self->rx_idle_partial = FALSE;
    }
  if (!self->rx.len && (buffer[0] & 1))
    goto receive_more;

  if (!goodix_proto_rx_feed_chunk (&self->rx, buffer, cell_length))
    {
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                        "Protocol reassembly error");
      goto protocol_error;
    }

  if (goodix_proto_rx_complete (&self->rx))
    {
      gboolean idle_packet = goodix_transport_is_idle (operation) ||
                             operation->completion_pending;
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

      /* ACK slots are written even before the OUT callback returns. Only the
       * addressed sender clears/observes its slot; no command-generation tag. */
      if (ack_packet && self->transport &&
          payload[0] == GOODIX_PROTO_CMD_BYTE (self->transport->cmd.category,
                                              self->transport->cmd.command))
        self->transport->ack_status = payload[1];
      /* Only 533c reports events; goodix_htsec_event always returns NONE. */
      if (self->reply_valid)
        switch (goodix_family (self)->protocol.event (category, command, payload, payload_len))
          {
          case GOODIX_SENSOR_EVENT_CONFIG_LOST:
            if (!self->config_lost)
              fp_info ("ACK for 0x%02x reports lost configuration", payload[0]);
            if (goodix_family (self)->channel.config_lost)
              goodix_family (self)->channel.config_lost (self);
            self->config_lost = TRUE;
            if (!self->config_restore_active)
              self->config_restore_pending = TRUE;
            /* A scan waiting for a finger restores the configuration first. */
            if (operation->phase == GOODIX_TRANSPORT_EVENT)
              {
                goodix_proto_rx_reset (&self->rx);
                goodix_transport_complete (operation, NULL);
                return;
              }
            break;
          case GOODIX_SENSOR_EVENT_RECONNECT:
            goodix_request_reconnect (self);
            goodix_proto_rx_reset (&self->rx);
            if (operation->phase == GOODIX_TRANSPORT_EVENT)
              goodix_transport_complete (operation, NULL);
            else
              goto receive_more;
            return;
          case GOODIX_SENSOR_EVENT_NONE:
            break;
          }

      if (self->reply_valid)
        {
          if (category == GOODIX_PROTO_CATEGORY_NOTICE)
            {
              /* Notice publication shares the worker's coalescing slot, but
               * never supplies a raw FDT vector or mutates FDT bases. */
              gboolean published = command == 1 ||
                (command == 0 && goodix_family (self)->protocol.notice_irq (self, payload, payload_len));

              if (published)
                {
                  self->pending_fdt.type = command == 0 ?
                                           GOODIX_FDT_EVENT_ESD : GOODIX_FDT_EVENT_WAKE;
                  self->pending_fdt.event.pending = TRUE;
                }
              goodix_proto_rx_reset (&self->rx);
              if (published && operation->phase == GOODIX_TRANSPORT_EVENT)
                goodix_transport_complete (operation, NULL);
              else
                goto receive_more;
              return;
            }
          if (category == GOODIX_PROTO_CATEGORY_IMAGE)
            {
              gboolean completed = goodix_rx_image (operation, payload, payload_len);

              goodix_proto_rx_reset (&self->rx);
              if (!completed)
                goto receive_more;
              return;
            }
          if (category == GOODIX_PROTO_CATEGORY_MCU)
            {
              /* Parser publication is independent of command, ACK, and GTLS
               * state. Preserve the native bounded byte stream and event. */
              goodix_mcu_publish (self, payload, payload_len);
              goodix_proto_rx_reset (&self->rx);
              if (goodix_transport_take_mcu (operation))
                return;
              goto receive_more;
            }
          GoodixResponseSlot slot =
            category == GOODIX_PROTO_CATEGORY_FDT && command == GOODIX_PROTO_CMD_FDT_MANUAL ?
              GOODIX_RESPONSE_MANUAL :
            category == GOODIX_PROTO_CATEGORY_CONFIG ? GOODIX_RESPONSE_CONFIG :
            category == GOODIX_PROTO_CATEGORY_SYSTEM && (command == 0 || command == 1 || command == 4) ?
              GOODIX_RESPONSE_SYSTEM :
            category == GOODIX_PROTO_CATEGORY_REGISTER ? GOODIX_RESPONSE_REGISTER :
            category == GOODIX_PROTO_CATEGORY_SYSTEM && command == 3 ? GOODIX_RESPONSE_OTP :
            category == GOODIX_PROTO_CATEGORY_FIRMWARE ? GOODIX_RESPONSE_FIRMWARE :
            category == GOODIX_PROTO_CATEGORY_PRODUCTION ? GOODIX_RESPONSE_PRODUCTION :
            GOODIX_RESPONSE_NONE;
          gboolean shared = goodix_response_slot_is_shared (slot) ||
                            category == GOODIX_PROTO_CATEGORY_FIRMWARE;

          if (goodix_reply_is_foreign (self, slot, category, command))
            {
              fp_dbg ("Ignoring a cat=0x%02x cmd=0x%02x reply while cat=0x%02x cmd=0x%02x waits",
                      category, command, self->transport->cmd.category,
                      self->transport->cmd.command);
              goodix_proto_rx_reset (&self->rx);
              goto receive_more;
            }
          if ((category == GOODIX_PROTO_CATEGORY_FIRMWARE ||
               (category == GOODIX_PROTO_CATEGORY_CONFIG && command == 2)) && !payload_len)
            {
              error = fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                "Status reply is too short");
              goto protocol_error;
            }
          if (category == GOODIX_PROTO_CATEGORY_CONFIG && command == 2)
            self->config_response_status = payload[0];
          if (category == GOODIX_PROTO_CATEGORY_CONFIG && command == GOODIX_PROTO_CMD_CONFIG_DOWNLOAD)
            {
              self->config_download_status = payload_len ? payload[0] : 0;
              self->config_download_failed =
                !goodix_family (self)->protocol.download_succeeded (payload, payload_len);
            }

          /* DataFromDevice publishes these shared stores without a matching
           * command. Only A/0, A/1 and A/4 signal the version getter's event.
           * Retain every unwritten suffix, including beyond the firmware view. */
          if (shared)
            {
              gsize offset = category == GOODIX_PROTO_CATEGORY_PRODUCTION ? 4 : 0;
              gsize count = category == GOODIX_PROTO_CATEGORY_FIRMWARE ? MIN (payload_len, 1) :
                            MIN (payload_len, sizeof (self->shared_response_storage) - offset);

              if (category == GOODIX_PROTO_CATEGORY_PRODUCTION)
                {
                  guint32 length = GUINT32_TO_LE ((guint32) payload_len);
                  memcpy (self->shared_response_storage, &length, sizeof (length));
                }
              memcpy (self->shared_response_storage + offset, payload, count);
              if (slot != GOODIX_RESPONSE_NONE)
                self->shared_response_lengths[slot] = payload_len;
            }

          if (slot != GOODIX_RESPONSE_NONE)
            {
              if (slot == GOODIX_RESPONSE_MANUAL)
                {
                  if (payload_len < sizeof (self->manual_response))
                    {
                      error = fpi_device_error_new_msg (
                        FP_DEVICE_ERROR_PROTO, "Manual FDT reply is too short");
                      goto protocol_error;
                    }
                  memcpy (self->manual_response, payload, sizeof (self->manual_response));
                }
              /* Native 01a7ec publishes response events independently of the
               * waiter. Every category-9 command signals configuration; its
               * command-2 status does not decide download success. Category F
               * signals the separate firmware event after its shared-byte copy. */
              self->command_response_ready |= goodix_response_bit (slot);
              if (!current || operation->phase != GOODIX_TRANSPORT_RESPONSE ||
                  current->response_slot != slot)
                {
                  goodix_proto_rx_reset (&self->rx);
                  goto receive_more;
                }
              goodix_transport_complete (operation, NULL);
              return;
            }
          else if (category == GOODIX_PROTO_CATEGORY_FDT &&
                   (command == GOODIX_PROTO_CMD_FDT_DOWN || command == GOODIX_PROTO_CMD_FDT_UP))
            {
              GoodixFdtEventType type;
              GoodixProfile9FdtEvent event;
              g_autoptr(GError) event_error = NULL;
              GoodixProfile9FdtWaitMode mode = command == GOODIX_PROTO_CMD_FDT_DOWN ?
                GOODIX_PROFILE9_FDT_WAIT_DOWN : GOODIX_PROFILE9_FDT_WAIT_UP;

              if (!goodix_cmd_parse_fdt_event (dev, mode, &type, &event, &event_error))
                {
                  error = g_steal_pointer (&event_error);
                  goto protocol_error;
                }
              /* Every native parser mutation precedes replacement of the
               * latest worker notification, including during config/manual. */
              if (!event.pending)
                {
                  goodix_proto_rx_reset (&self->rx);
                  goto receive_more;
                }
              goodix_recv_apply_fdt_event (dev, type, &event);
              if (type != GOODIX_FDT_EVENT_CONFIG)
                self->pending_fdt.event = event;
              self->pending_fdt.event.pending = TRUE;
              self->pending_fdt.type = type;
              goodix_proto_rx_reset (&self->rx);
              if (operation->phase == GOODIX_TRANSPORT_EVENT)
                {
                  goodix_transport_complete (operation, NULL);
                  return;
                }
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
      else
        {
          /* Native checksum rejection publishes nothing and continues reading. */
          goodix_proto_rx_reset (&self->rx);
          goto receive_more;
        }

      /* Native updates independent ACK slots. Only the command being polled
       * consumes its status; unrelated ACKs never fail or satisfy that wait. */
      if (ack_packet && !expected_ack)
        {
          goodix_proto_rx_reset (&self->rx);
          goto receive_more;
        }
      if (ack_wait && category == GOODIX_PROTO_CATEGORY_ACK &&
          command == GOODIX_PROTO_CMD_ACK)
        {
          guint8 status;

          if (!goodix_validate_ack_for_cmd (dev, current, &status, &error))
            goodix_transport_complete (operation, error);
          /* Even status preserves this attempt's
           * original ACK deadline. Only exhaustion permits a retry. */
          else if (!(status & GOODIX_PROTO_ACK_FLAG_VALID))
            {
              goodix_proto_rx_reset (&self->rx);
              goto receive_more;
            }
          else
            {
              current->ack_status = status;
              goodix_transport_ack (operation);
            }
          return;
        }
      /* A complete packet is not evidence that this waiter completed. Native
       * unclaimed families/subcommands publish nothing; passive responses above
       * publish only their own event. Keep ACK/data/MCU/FDT waits and their clocks
       * intact, including for notifications without a consumer in this layer. */
      goodix_proto_rx_reset (&self->rx);
      goto receive_more;
    }
  else
    {
      /* Preserve unrelated per-continuation data budgets. Scoped command waits
       * keep one deadline across interleaved packets and continuations. */
      if (!fixed_deadline && !goodix_transport_is_idle (operation) &&
          !(operation->phase == GOODIX_TRANSPORT_EVENT && self->service_active))
        operation->deadline_us = g_get_monotonic_time () + GOODIX_DATA_TIMEOUT * 1000LL;
      goto receive_more;
    }
  return;

protocol_error:
  goodix_transport_complete (operation, error);
  return;

receive_more:
  goodix_rx_continue (operation);
}

/* Requests a secure-channel restart when the family's channel needs one for
 * an undecodable frame. An error during a restart still counts. */
static void
goodix_count_image_error (FpiDeviceGoodix53x5 *self)
{
  if (!goodix_family (self)->channel.image_error (self))
    return;
  if (!self->gtls_restart_active)
    self->gtls_restart_pending = TRUE;
}

/* Checks whether @operation is a command waiting for its image response. */
static gboolean
goodix_transport_awaits_image (GoodixTransport *operation)
{
  return !goodix_transport_is_idle (operation) && !operation->completion_pending &&
         operation->phase == GOODIX_TRANSPORT_RESPONSE &&
         operation->response_slot == GOODIX_RESPONSE_IMAGE;
}

/* Publishes an encrypted frame as the image response and returns TRUE if
 * that completed @operation. */
gboolean
goodix_rx_image (GoodixTransport *operation, const guint8 *payload, gsize len)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (operation->dev);
  g_autofree guint8 *decoded = NULL;
  gsize decoded_len = 0;
  GoodixFrameStatus status = goodix_family (self)->channel.decrypt_frame (
    self, payload, len, &decoded, &decoded_len);
  guint16 *frame = status == GOODIX_FRAME_DECRYPTED ?
                   goodix_device_decode_image (decoded, decoded_len) : NULL;

  /* The native raw callback authenticates, converts and signals
   * before the sender consumes ACK. Failed authentication leaves
   * the counter/cache intact; an authenticated bad frame retains
   * the advanced counter but does not signal readiness. */
  if (frame)
    {
      g_free (self->image_response);
      self->image_response = frame;
      self->image_response_failed = FALSE;
      self->command_response_ready |= goodix_response_bit (GOODIX_RESPONSE_IMAGE);
    }
  else if (status != GOODIX_FRAME_REJECTED)
    {
      /* Incomplete-session rejection consumes the counter but preserves raw
       * status; an established frame failing to decrypt or decode sets it. */
      self->image_response_failed = TRUE;
    }
  if (!frame && status != GOODIX_FRAME_INVALID)
    goodix_count_image_error (self);

  if (operation->phase == GOODIX_TRANSPORT_EVENT && self->gtls_restart_pending)
    {
      goodix_transport_complete (operation, NULL);
      return TRUE;
    }
  if (goodix_transport_awaits_image (operation) &&
      (self->command_response_ready & goodix_response_bit (GOODIX_RESPONSE_IMAGE)))
    {
      goodix_transport_complete (operation, NULL);
      return TRUE;
    }
  return FALSE;
}

/* Drops the partial frame the family's link keeps for the next transfer. */
static void
goodix_link_discard (FpiDeviceGoodix53x5 *self)
{
  const GoodixLink *link = &goodix_family (self)->link;

  if (link->discard)
    link->discard (self);
}

static void
goodix_reader_joined (GoodixReader *reader)
{
  g_autoptr(FpDevice) dev = reader->dev;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransportJoined joined = reader->joined;
  gpointer data = reader->joined_data;

  g_assert (!reader->pending);
  self->reader = NULL;
  /* The next reader starts a new stream: a partial pack cannot continue. */
  goodix_link_discard (self);
  g_clear_object (&reader->cancel);
  g_clear_error (&reader->error);
  g_free (reader);
  if (joined)
    joined (dev, data);
}

static void
goodix_reader_start (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixReader *reader = self->reader;
  FpiUsbTransfer *transfer;

  if (!reader)
    {
      reader = self->reader = g_new0 (GoodixReader, 1);
      reader->dev = g_object_ref (dev);
      reader->cancel = g_cancellable_new ();
    }
  if (reader->pending || reader->joined || reader->error)
    return;
  reader->pending = TRUE;
  transfer = fpi_usb_transfer_new (dev);
  fpi_usb_transfer_fill_bulk (transfer, goodix_family (self)->link.ep_in,
                              GOODIX_USB_READ_SIZE);
  fpi_usb_transfer_submit (transfer, 0, reader->cancel, goodix_rx_cb, reader);
}

static void
goodix_rx_cb (FpiUsbTransfer *transfer,
              FpDevice       *dev,
              gpointer        user_data,
              GError         *error)
{
  GoodixReader *reader = user_data;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport idle = { .dev = dev, .phase = GOODIX_TRANSPORT_IDLE,
                          .response_slot = GOODIX_RESPONSE_NONE };
  GoodixTransport *operation = self->transport;

  reader->pending = FALSE;
  if (!operation || operation->phase == GOODIX_TRANSPORT_SEND)
    operation = &idle;

  if (error)
    {
      if (reader->joined)
        {
          g_clear_error (&error);
          goodix_reader_joined (reader);
          return;
        }
      /* WDF requests pipe reset/restart here. GUsb has no corresponding pipe
       * reset API; retain the failure until joined hardware reconstruction.
       * Never disguise a physical failure as sender timeout/retry. */
      reader->error = error;
      self->needs_reinit = TRUE;
      /* The lost transfer leaves no way to find the next pack header. */
      goodix_link_discard (self);
      if (operation != &idle)
        goodix_transport_complete (operation, g_error_copy (error));
      return;
    }

  operation->processing_cells = TRUE;
  goodix_family (self)->link.receive (self, operation, transfer->buffer,
                                     transfer->actual_length);
  operation->processing_cells = FALSE;
  self->rx_idle_partial = self->rx.len && !goodix_proto_rx_complete (&self->rx);

  if (reader->joined)
    {
      g_clear_error (&idle.completion_error);
      goodix_reader_joined (reader);
      return;
    }
  goodix_reader_start (dev);
  if (operation == &idle)
    {
      g_clear_error (&idle.completion_error);
      return;
    }

  /* Category-D packets in this completion share one stream/event. Select the
   * caller's bounded view once, after every publication, not once per cell. */
  if (!operation->completion_error && goodix_transport_take_mcu (operation))
    return;
  if (operation->completion_pending)
    {
      operation->completion_pending = FALSE;
      goodix_transport_complete (operation, g_steal_pointer (&operation->completion_error));
      return;
    }
  /* Empty completions retain the existing timeout policy; draining a nonempty
   * batch must not look like a fresh zero-byte completion or renew its budget. */
  if (transfer->actual_length == 0)
    goodix_rx_cell (operation, NULL, 0);
  else
    goodix_rx_continue (operation);
}

void
goodix_transport_reset_mcu (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  g_assert (!self->transport);
  if (self->mcu_rx)
    g_byte_array_set_size (self->mcu_rx, 0);
  g_clear_pointer (&self->mcu_reply, g_bytes_unref);
  self->reply_valid = FALSE;
  /* A pack cut short by the previous channel never completes. */
  goodix_link_discard (self);
  /* The native attempt resets ring indices, not the auto-reset event. */
}

void
goodix_transport_invalidate (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  g_assert (!self->transport);
  g_assert (!self->reader);
  g_clear_pointer (&self->mcu_rx, g_byte_array_unref);
  g_clear_pointer (&self->mcu_reply, g_bytes_unref);
  self->mcu_ready = FALSE;
  g_clear_pointer (&self->image_response, g_free);
  self->image_response_failed = FALSE;
  self->image_error_history = FALSE;
  self->gtls_restart_pending = FALSE;
  self->reconnect_requested = FALSE;
  self->reconnect_pending = 0;
  /* psk_refresh_pending survives: the sensor stays unkeyed across a suspend
   * until the refresh runs, as the vendor's Stop keeps its flag for Start. */
  self->command_response_ready &= ~goodix_response_bit (GOODIX_RESPONSE_IMAGE);
  if (self->rx.buf)
    goodix_proto_rx_reset (&self->rx);
  goodix_link_discard (self);
  self->reply_valid = FALSE;
  self->rx_idle_partial = FALSE;
  self->pending_fdt.event.pending = FALSE;
  /* Joined cold teardown retires the HAL mode owner, not an ordinary reader
   * stop, command completion, FDT rearm or coordinator handoff. */
  self->requested_mode = GOODIX_REQUESTED_MODE_CAPTURE;
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
      *type = pending->type;
      if (*type == GOODIX_FDT_EVENT_DOWN || *type == GOODIX_FDT_EVENT_UP ||
          *type == GOODIX_FDT_EVENT_REVERSE)
        *event = pending->event;
      event->pending = TRUE;
      if (*type == GOODIX_FDT_EVENT_REVERSE)
        {
          /* 014480 calls 0053b0 at handler execution. Reference publication
           * can replace this shared manual/prior store after packet reception;
           * only the selected handler's local operands become immutable. */
          for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
            prior_down[i] = self->profile9_fdt.base_manual[i * 2 + 1];
        }
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

  if (type != GOODIX_FDT_EVENT_DOWN && type != GOODIX_FDT_EVENT_UP &&
      type != GOODIX_FDT_EVENT_REVERSE)
    return;
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

/* Completes @done with a busy error, returning TRUE, if another transport
 * operation or a reader join owns the device. */
static gboolean
goodix_transport_reject_busy (FpDevice *dev, GoodixTransportDone done, gpointer data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransportResult result = {0};

  if (!self->transport && !(self->reader && self->reader->joined))
    return FALSE;
  done (dev, &result, fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
         "A device transport operation is already active"), data);
  return TRUE;
}

void
goodix_transport_command (FpDevice                     *dev,
                           const GoodixTransportRequest *request,
                           GoodixTransportDone          done,
                           gpointer                     data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport *operation;

  if (goodix_transport_reject_busy (dev, done, data))
    return;

  if (goodix_cmd_refused (self, &request->cmd))
    {
      GoodixTransportResult result = {0};
      done (dev, &result, fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
             "Refusing to write to this sensor: it is never provisioned"), data);
      return;
    }

  operation = goodix_transport_new (dev, GOODIX_TRANSPORT_SEND, done, data);
  operation->done = done;
  operation->data = data;
  operation->cmd = request->cmd;
  operation->cmd.payload = request->cmd.payload_len ?
    g_memdup2 (request->cmd.payload, request->cmd.payload_len) : NULL;
  operation->expect_data = request->expect_data;
  goodix_cmd_set_policy (operation);

  goodix_transport_send (operation);
}

void
goodix_transport_send_record (FpDevice           *dev,
                              GBytes             *record,
                              GoodixTransportDone done,
                              gpointer            data)
{
  GoodixTransport *operation;

  if (goodix_transport_reject_busy (dev, done, data))
    return;
  if (!goodix_family (FPI_DEVICE_GOODIX53X5 (dev))->link.frame_record)
    {
      GoodixTransportResult result = {0};
      done (dev, &result, fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
             "This sensor has no TLS channel"), data);
      return;
    }

  operation = goodix_transport_new (dev, GOODIX_TRANSPORT_SEND, done, data);
  operation->record = g_bytes_ref (record);
  goodix_transport_send (operation);
}

static void
goodix_transport_wait (FpDevice                  *dev,
                        GoodixTransportPhase       phase,
                        guint                      timeout,
                        gsize                      mcu_length,
                        GoodixProfile9FdtWaitMode  mode,
                        GoodixTransportDone        done,
                        gpointer                   data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixTransport *operation;

  if (goodix_transport_reject_busy (dev, done, data))
    return;
  operation = goodix_transport_new (dev, phase, done, data);
  if (self->reader && self->reader->error)
    {
      goodix_transport_complete (operation, g_error_copy (self->reader->error));
      return;
    }
  operation->mcu_length = mcu_length;
  operation->event_mode = mode;
  operation->deadline_us = timeout ? g_get_monotonic_time () + timeout * 1000LL : 0;
  if (phase == GOODIX_TRANSPORT_EVENT)
    {
      operation->cancellable = g_cancellable_new ();
      if (self->pending_fdt.event.pending || self->gtls_restart_pending)
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
  goodix_transport_wait (dev, GOODIX_TRANSPORT_EVENT, 0, 0, mode, done, data);
}

void
goodix_transport_wait_mcu (FpDevice           *dev,
                           guint               timeout,
                           gsize               length,
                           GoodixTransportDone done,
                           gpointer            data)
{
  goodix_transport_wait (dev, GOODIX_TRANSPORT_REPLY, timeout, length,
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
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixReader *reader = self->reader;

  g_assert (!self->transport);
  if (!reader)
    {
      joined (dev, data);
      return;
    }
  g_assert (!reader->joined);
  reader->joined = joined;
  reader->joined_data = data;
  if (reader->pending)
    g_cancellable_cancel (reader->cancel);
  else
    goodix_reader_joined (reader);
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
