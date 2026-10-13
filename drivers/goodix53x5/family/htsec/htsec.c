/*
 * Goodix 53x5 driver for libfprint — the HTSEC family (5335/5385/5395)
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "driver-private.h"
#include "device/transport.h"
#include "family/htsec/htsec.h"

#include <string.h>

/* USB endpoints — interface 1, CDC Data class */
#define GOODIX_EP_OUT (0x03 | FPI_USB_ENDPOINT_OUT)
#define GOODIX_EP_IN  (0x01 | FPI_USB_ENDPOINT_IN)

/* The 53x5 native budgets: the command table, with fixed fallbacks for
 * commands it does not list. */
static gboolean
goodix_htsec_command_budget (guint8 command, gboolean expect_data,
                             guint *ack_ms, guint *response_ms)
{
  const GoodixCommandPolicy *policy = goodix_cmd_policy (command);

  *ack_ms = policy ? policy->ack_ms : GOODIX_ACK_TIMEOUT;
  *response_ms = policy && policy->response_ms ? policy->response_ms : GOODIX_DATA_TIMEOUT;
  return policy && policy->retry &&
         (expect_data == (policy->response_slot != GOODIX_RESPONSE_NONE));
}

static gboolean
goodix_htsec_retry_write (const GError *error)
{
  return error && error->domain == G_USB_DEVICE_ERROR &&
         (error->code == G_USB_DEVICE_ERROR_TIMED_OUT ||
          error->code == G_USB_DEVICE_ERROR_IO ||
          error->code == G_USB_DEVICE_ERROR_FAILED ||
          error->code == G_USB_DEVICE_ERROR_NOT_SUPPORTED ||
          error->code == G_USB_DEVICE_ERROR_INTERNAL);
}

/* Returns @msg split into 64-byte writes. Continuation chunks prepend the
 * command byte | 1 and carry up to 63 more bytes of message data. */
static guint8 *
goodix_frame_direct (const guint8 *msg, gsize msg_len, gsize *out_len)
{
  gsize first_len = MIN (msg_len, GOODIX_USB_CHUNK_SIZE);
  gsize total_chunks = 1 + (msg_len - first_len + 62) / 63;
  gsize padded_len = total_chunks * GOODIX_USB_CHUNK_SIZE;
  guint8 *chunked = g_malloc0 (padded_len);
  gsize src_offset = first_len;

  memcpy (chunked, msg, first_len);
  for (gsize dst_offset = GOODIX_USB_CHUNK_SIZE;
       dst_offset < padded_len; dst_offset += GOODIX_USB_CHUNK_SIZE)
    {
      gsize data_in_chunk = MIN (63, msg_len - src_offset);

      chunked[dst_offset] = msg[0] | 1;
      memcpy (chunked + dst_offset + 1, msg + src_offset, data_in_chunk);
      src_offset += data_in_chunk;
    }

  *out_len = padded_len;
  return chunked;
}

/* Native posts one 0x8000-byte read and traverses that capacity in 64-byte
 * cells, gated by nonzero received length and first byte. Only transferred
 * bytes are available here; never parse the unfilled allocation suffix. */
static void
goodix_receive_direct (FpiDeviceGoodix53x5 *self, GoodixTransport *operation,
                       const guint8 *data, gsize len)
{
  if (len && data[0])
    for (gsize offset = 0; offset < len; offset += GOODIX_USB_CHUNK_SIZE)
      goodix_rx_cell (operation, data + offset, MIN (GOODIX_USB_CHUNK_SIZE, len - offset));
}

static gboolean
goodix_htsec_repair_arm (guint8 status)
{
  return (status & 3) == 3;
}

static gboolean
goodix_htsec_wake_arm_up (gboolean waiting_up)
{
  return waiting_up;
}

static gboolean
goodix_htsec_notice_irq (FpiDeviceGoodix53x5 *self, const guint8 *payload, gsize len)
{
  memcpy (self->shared_response_storage, payload,
          MIN (len, sizeof (self->shared_response_storage)));
  memcpy (self->notice_irq, self->shared_response_storage, sizeof (self->notice_irq));
  return TRUE;
}

/* 53x5 native gives no message a meaning beyond its reply. */
static GoodixSensorEvent
goodix_htsec_event (guint8 category, guint8 command, const guint8 *payload, gsize len)
{
  (void) category;
  (void) command;
  (void) payload;
  (void) len;
  return GOODIX_SENSOR_EVENT_NONE;
}

/* 53x5 native lets selectors alias: any reply in the slot answers. */
static gboolean
goodix_htsec_reply_answers (guint8 category, guint8 command, const GoodixCmd *awaited)
{
  (void) category;
  (void) command;
  (void) awaited;
  return TRUE;
}

/* The download reply's payload is scratch, not a status. */
static gboolean
goodix_htsec_download_succeeded (const guint8 *payload, gsize len)
{
  (void) payload;
  (void) len;
  return TRUE;
}

/* The driver provisions 53x5 sensors, so it may send any command. */
static gboolean
goodix_htsec_may_send (const GoodixCmd *cmd)
{
  (void) cmd;
  return TRUE;
}

/* 53x5 native checks no MCU platform and continues without a version. */
GError *
goodix_htsec_check_firmware (const char *firmware)
{
  (void) firmware;
  return NULL;
}

static gboolean
goodix_htsec_rearm_after_capture (FpiDeviceGoodix53x5 *self)
{
  (void) self;
  return TRUE;
}

static GoodixFdtEventType
goodix_htsec_fdt_event (FpiDeviceGoodix53x5 *self, const guint8 *payload,
                        gsize len, GError **error)
{
  (void) self;
  if (len != 4 + GOODIX_FDT_BASE_LEN)
    {
      g_set_error_literal (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                           "Unexpected FDT event length");
      return GOODIX_FDT_EVENT_NONE;
    }
  switch (payload[0] | ((guint16) payload[1] << 8))
    {
    case 0:
    case 1:
    case 4:
    case 8:
    case 0x10:
    case 0x20:
    case 0x40:
      return GOODIX_FDT_EVENT_NONE;
    case GOODIX_FDT_IRQ_DOWN:
      return GOODIX_FDT_EVENT_DOWN;
    case GOODIX_FDT_IRQ_UP:
      return GOODIX_FDT_EVENT_UP;
    case GOODIX_FDT_IRQ_REVERSE:
    case GOODIX_FDT_IRQ_REVERSE_ALT:
      return GOODIX_FDT_EVENT_REVERSE;
    default:
      return GOODIX_FDT_EVENT_CONFIG;
    }
}

const GoodixFamily goodix_family_htsec = {
  .link = {
    .usb_interface = 1,
    .ep_in = GOODIX_EP_IN,
    .ep_out = GOODIX_EP_OUT,
    .frame = goodix_frame_direct,
    .frame_record = NULL,
    .receive = goodix_receive_direct,
    .discard = NULL,
  },
  .protocol = {
    .wake_payload_len = 2,
    .ec_on_payload = { 0x01, 0x01, 0x00 },
    .command_budget = goodix_htsec_command_budget,
    .retry_write = goodix_htsec_retry_write,
    .repair_arm = goodix_htsec_repair_arm,
    .wake_arm_up = goodix_htsec_wake_arm_up,
    .notice_irq = goodix_htsec_notice_irq,
    .fdt_event = goodix_htsec_fdt_event,
    .ack_timeout_waits_for_data = FALSE,
    .event = goodix_htsec_event,
    .reply_answers = goodix_htsec_reply_answers,
    .download_succeeded = goodix_htsec_download_succeeded,
    .may_send = goodix_htsec_may_send,
  },
  .channel = {
    .name = "GTLS",
    /* deviceInit runs the three-attempt wrapper twice; the event restart once. */
    .attempts = { .open = 6, .after_failed_read = 3, .reconnect = 3 },
    .retry_delay_ms = 10,
    .attempt_new = goodix_gtls_attempt_new,
    .decrypt_frame = goodix_decrypt_frame_gtls,
    .image_error = goodix_gtls_image_error,
    .rearm_after_capture = goodix_htsec_rearm_after_capture,
    .restart_disarms = FALSE,
    .config_lost = NULL,
    .discard = goodix_gtls_discard,
  },
  .open_new = goodix_htsec_open_new,
};
