/*
 * Goodix 53x5 driver for libfprint — the GM168SEC family (27c6:533c)
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/* 533c is the GF5288 of the 53x5 parts behind a GM168SEC secure element: the
 * same commands and image pipeline, carried in the wrapped framing of the 51x7
 * family and secured with standard TLS 1.2-PSK, the sensor being the client. */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "driver-private.h"
#include "device/transport.h"
#include "family/gm168sec/gm168sec.h"
#include "family/gm168sec/tls.h"

#include <string.h>

/* Wrapped framing: [flags(1)][length(2 LE)][sum of the first three(1)]
 * then length bytes. The high nibble of the flags is the family: 0xa packs
 * carry one protocol message, 0xb packs TLS records, behind a 9-byte prefix
 * when flag 0x02 is set (0xb2, image data). */
#define GOODIX_PACK_HEADER         4
#define GOODIX_PACK_MESSAGE        0xa0
#define GOODIX_PACK_TLS            0xb0
#define GOODIX_PACK_FAMILY_MESSAGE 0xa
#define GOODIX_PACK_FAMILY_TLS     0xb
#define GOODIX_PACK_TLS_PREFIXED   0x02
/* The largest TLS record RFC 5246 allows (2^14 + 2048 bytes and its 5-byte
 * header) behind the prefix; a longer pack is a misread header. */
#define GOODIX_PACK_MAX_PAYLOAD    (GOODIX_PACK_TLS_DATA_HDR + GOODIX_TLS_RECORD_HEADER + \
                                    16384 + 2048)

/* Returns the checksum a pack header carries in its fourth byte. */
static guint8
goodix_pack_checksum (const guint8 *header)
{
  return header[0] + header[1] + header[2];
}

static void
goodix_pack_write_header (guint8 *header, guint8 flags, gsize len)
{
  header[0] = flags;
  header[1] = len & 0xff;
  header[2] = (len >> 8) & 0xff;
  header[3] = goodix_pack_checksum (header);
}

/* Returns the payload length a pack header announces, or FALSE when it is
 * not a header: an unknown family, a wrong checksum or an impossible length. */
static gboolean
goodix_pack_read_header (const guint8 *header, gsize *len)
{
  guint8 family = header[0] >> 4;

  if ((family != GOODIX_PACK_FAMILY_MESSAGE && family != GOODIX_PACK_FAMILY_TLS) ||
      goodix_pack_checksum (header) != header[3])
    return FALSE;
  *len = header[1] | (header[2] << 8);
  return *len <= GOODIX_PACK_MAX_PAYLOAD;
}

/* Returns @data as one pack of kind @flags, zero-padded to whole 64-byte
 * writes, or NULL when @data does not fit the header's 16-bit length. */
static guint8 *
goodix_frame_pack (guint8 flags, const guint8 *data, gsize data_len, gsize *out_len)
{
  gsize len = (GOODIX_PACK_HEADER + data_len + GOODIX_USB_CHUNK_SIZE - 1) /
              GOODIX_USB_CHUNK_SIZE * GOODIX_USB_CHUNK_SIZE;
  guint8 *framed;

  if (data_len > G_MAXUINT16)
    return NULL;
  framed = g_malloc0 (len);
  goodix_pack_write_header (framed, flags, data_len);
  memcpy (framed + GOODIX_PACK_HEADER, data, data_len);
  *out_len = len;
  return framed;
}

static guint8 *
goodix_frame_wrapped (const guint8 *msg, gsize msg_len, gsize *out_len)
{
  return goodix_frame_pack (GOODIX_PACK_MESSAGE, msg, msg_len, out_len);
}

static guint8 *
goodix_frame_tls_record (GBytes *record, gsize *out_len)
{
  gsize len;
  const guint8 *data = g_bytes_get_data (record, &len);

  return goodix_frame_pack (GOODIX_PACK_TLS, data, len, out_len);
}

/* Passes one protocol message to the receiver as the 64-byte direct-framing
 * cells it would have arrived in, so both framings share one parser. */
static void
goodix_rx_message (GoodixTransport *operation, const guint8 *msg, gsize len)
{
  guint8 cell[GOODIX_USB_CHUNK_SIZE];
  gsize offset = MIN (len, GOODIX_USB_CHUNK_SIZE);

  goodix_rx_cell (operation, msg, offset);
  while (offset < len)
    {
      gsize n = MIN (len - offset, GOODIX_USB_CHUNK_SIZE - 1);

      cell[0] = msg[0] | 1;
      memcpy (cell + 1, msg + offset, n);
      goodix_rx_cell (operation, cell, n + 1);
      offset += n;
    }
}

static void
goodix_rx_pack (FpiDeviceGoodix53x5 *self, GoodixTransport *operation,
                guint8 flags, const guint8 *payload, gsize len)
{
  if (flags >> 4 == GOODIX_PACK_FAMILY_MESSAGE)
    {
      if (len)
        goodix_rx_message (operation, payload, len);
    }
  else
    {
      if (flags & GOODIX_PACK_TLS_PREFIXED)
        {
          if (len < GOODIX_PACK_TLS_DATA_HDR)
            return;
          payload += GOODIX_PACK_TLS_DATA_HDR;
          len -= GOODIX_PACK_TLS_DATA_HDR;
        }
      if (self->tls && goodix_tls_established (self->tls))
        goodix_rx_image (operation, payload, len);
      else
        /* Handshake records share the category-D stream and its waiter. */
        goodix_mcu_publish (self, payload, len);
    }
}

/* Drops the partial pack kept for the next transfer, if any. */
void
goodix_drop_pack_stream (FpiDeviceGoodix53x5 *self)
{
  g_clear_pointer (&self->pack_rx, g_byte_array_unref);
}

/* Dispatches every complete pack once @data is appended to the stream,
 * keeping a partial one for the next transfer; drops the stream at a bad
 * header. A pack never starts with zero, so its write padding is skipped. */
static void
goodix_receive_wrapped (FpiDeviceGoodix53x5 *self, GoodixTransport *operation,
                        const guint8 *data, gsize len)
{
  GByteArray *stream;

  if (!self->pack_rx)
    self->pack_rx = g_byte_array_new ();
  stream = self->pack_rx;
  g_byte_array_append (stream, data, len);

  for (;;)
    {
      gsize padding = 0, length;

      while (padding < stream->len && !stream->data[padding])
        padding++;
      g_byte_array_remove_range (stream, 0, padding);
      if (stream->len < GOODIX_PACK_HEADER)
        return;
      if (!goodix_pack_read_header (stream->data, &length))
        {
          fp_warn ("Dropping %u wrapped bytes with a bad pack header", stream->len);
          g_byte_array_set_size (stream, 0);
          return;
        }
      if (stream->len < GOODIX_PACK_HEADER + length)
        return;
      goodix_rx_pack (self, operation, stream->data[0],
                      stream->data + GOODIX_PACK_HEADER, length);
      g_byte_array_remove_range (stream, 0, GOODIX_PACK_HEADER + length);
    }
}

/* Checks whether @firmware parses as chip_platform_mode_version, such as
 * GF5288_GM168SEC_APP_13016, and names the GM168 or GM168SEC platform, as
 * the vendor's SelfCheck requires. */
static gboolean
goodix_wrapped_self_check (const char *firmware)
{
  g_auto(GStrv) fields = g_strsplit (firmware, "_", 4);
  const char *platform;

  if (g_strv_length (fields) != 4)
    return FALSE;
  platform = fields[1];
  return g_ascii_strcasecmp (platform, "GM168") == 0 ||
         g_ascii_strcasecmp (platform, "GM168SEC") == 0;
}

GError *
goodix_gm168sec_check_firmware (const char *firmware)
{
  if (!firmware)
    return fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "Firmware version unreadable");
  if (!goodix_wrapped_self_check (firmware))
    return fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
                                     "Firmware %s is not for this sensor's MCU", firmware);
  return NULL;
}

/* The vendor's MCU context waits 1500 ms for every ACK and every reply;
 * EC control alone gives its ACK 500 ms. Dell's TOD driver never enables
 * the IoHub retry for this MCU, so every command goes out once; the Windows
 * driver would resend once after a timeout. */
static gboolean
goodix_wrapped_command_budget (guint8 command, gboolean expect_data,
                              guint *ack_ms, guint *response_ms)
{
  (void) expect_data;
  *ack_ms = command == 0xae ? 500 : 1500;
  *response_ms = 1500;
  return FALSE;
}

static gboolean
goodix_gm168sec_retry_write (const GError *error)
{
  (void) error;
  return FALSE;
}

/* ACK flag 0x02, on any command, means the sensor lost its configuration
 * and needs it downloaded again. */
#define GOODIX_PROTO_ACK_FLAG_CONFIG_LOST 0x02

static gboolean
goodix_gm168sec_repair_arm (guint8 status)
{
  (void) status;
  return FALSE;
}

/* An ACK flagging lost configuration reports that loss; a D0 message asks
 * for a new secure-channel session rather than carrying channel bytes. */
static GoodixSensorEvent
goodix_gm168sec_event (guint8 category, guint8 command, const guint8 *payload, gsize len)
{
  if (category == GOODIX_PROTO_CATEGORY_ACK && command == GOODIX_PROTO_CMD_ACK &&
      len >= 2 && (payload[1] & GOODIX_PROTO_ACK_FLAG_CONFIG_LOST))
    return GOODIX_SENSOR_EVENT_CONFIG_LOST;
  if (category == GOODIX_PROTO_CATEGORY_MCU && command == 0)
    return GOODIX_SENSOR_EVENT_RECONNECT;
  return GOODIX_SENSOR_EVENT_NONE;
}

static gboolean
goodix_gm168sec_wake_arm_up (gboolean waiting_up)
{
  (void) waiting_up;
  return FALSE;
}

/* A reply answers a command only if its category and command are that
 * command's, as the vendor's IoHubNotifyDataIn checks. */
static gboolean
goodix_gm168sec_reply_answers (guint8 category, guint8 command, const GoodixCmd *awaited)
{
  return category == awaited->category && command == awaited->command;
}

/* The download reply's first byte is 1 on success. */
static gboolean
goodix_gm168sec_download_succeeded (const guint8 *payload, gsize len)
{
  return len && payload[0] == 1;
}

/* Checks whether @cmd is a preset-slot read: e4 with a length, a zero
 * offset, a slot and a zero word. A PSK write may also use e4, but its
 * payload is a fragment header and data, never this. */
static gboolean
goodix_cmd_is_preset_read (const GoodixCmd *cmd)
{
  static const guint8 zero[4] = { 0 };

  return cmd->category == GOODIX_PROTO_CATEGORY_PRODUCTION && cmd->command == 0x2 &&
         cmd->payload_len == 16 && memcmp (cmd->payload + 4, zero, 4) == 0 &&
         memcmp (cmd->payload + 12, zero, 4) == 0;
}

/* Checks whether @cmd could change what the sensor stores: any production
 * command but a preset read (e0 and e2 writes, e4 used as a write), a
 * firmware write (f0) or an APP erase (a4). */
static gboolean
goodix_cmd_writes_sensor (const GoodixCmd *cmd)
{
  if (cmd->category == GOODIX_PROTO_CATEGORY_PRODUCTION)
    return !goodix_cmd_is_preset_read (cmd);
  return (cmd->category == GOODIX_PROTO_CATEGORY_FIRMWARE && cmd->command == 0x0) ||
         (cmd->category == GOODIX_PROTO_CATEGORY_SYSTEM && cmd->command == 0x2);
}

/* 533c returns no firmware in APP mode, so nothing could restore it: the
 * driver never writes to the sensor. */
static gboolean
goodix_gm168sec_may_send (const GoodixCmd *cmd)
{
  return !goodix_cmd_writes_sensor (cmd);
}

static gboolean
goodix_gm168sec_rearm_after_capture (FpiDeviceGoodix53x5 *self)
{
  return !self->psk_refresh_pending;
}

/* Every ACK reports the loss until a restore; only the first is a new power
 * loss, after which the next handshake checks the PSK. Dell's drivers set
 * the flag on every such ACK. Keeping the first-loss rule stops later ACKs
 * setting it again after the check, which would leave a failed capture
 * unarmed. */
static void
goodix_gm168sec_config_lost (FpiDeviceGoodix53x5 *self)
{
  if (!self->config_lost)
    self->psk_refresh_pending = TRUE;
}

static gboolean
goodix_gm168sec_notice_irq (FpiDeviceGoodix53x5 *self, const guint8 *payload, gsize len)
{
  if (len < sizeof (self->notice_irq))
    return FALSE;
  memcpy (self->notice_irq, payload, sizeof (self->notice_irq));
  return TRUE;
}

static GoodixFdtEventType
goodix_gm168sec_fdt_event (FpiDeviceGoodix53x5 *self, const guint8 *payload,
                          gsize len, GError **error)
{
  GoodixFdtEventType type;
  guint16 irq;

  if (len < 2)
    goto short_event;
  irq = payload[0] | ((guint16) payload[1] << 8);
  switch (irq)
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
      type = GOODIX_FDT_EVENT_DOWN;
      break;
    case GOODIX_FDT_IRQ_UP:
      type = GOODIX_FDT_EVENT_UP;
      break;
    case GOODIX_FDT_IRQ_REVERSE:
    case GOODIX_FDT_IRQ_REVERSE_ALT:
      type = GOODIX_FDT_EVENT_REVERSE;
      break;
    case 0x100:
      type = GOODIX_FDT_EVENT_NONE;
      break;
    default:
      return GOODIX_FDT_EVENT_CONFIG;
    }
  if (len < 4 + GOODIX_FDT_BASE_LEN)
    goto short_event;
  if (irq == 0x100)
    self->manual_touch = payload[2] | ((guint16) payload[3] << 8);
  return type;

short_event:
  g_set_error_literal (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                       "Unexpected FDT event length");
  return GOODIX_FDT_EVENT_NONE;
}

const GoodixFamily goodix_family_gm168sec = {
  .link = {
    .usb_interface = 0,
    .ep_in = 0x03 | FPI_USB_ENDPOINT_IN,
    .ep_out = 0x01 | FPI_USB_ENDPOINT_OUT,
    .frame = goodix_frame_wrapped,
    .frame_record = goodix_frame_tls_record,
    .receive = goodix_receive_wrapped,
    .discard = goodix_drop_pack_stream,
  },
  .protocol = {
    .wake_payload_len = 4,
    /* McuSetEcCtrl builds [enable, mode, enable]; Dell's 533c driver turns
     * isolation on with mode 0 when a session starts. */
    .ec_on_payload = { 0x01, 0x00, 0x01 },
    .command_budget = goodix_wrapped_command_budget,
    .retry_write = goodix_gm168sec_retry_write,
    .repair_arm = goodix_gm168sec_repair_arm,
    .wake_arm_up = goodix_gm168sec_wake_arm_up,
    .notice_irq = goodix_gm168sec_notice_irq,
    .fdt_event = goodix_gm168sec_fdt_event,
    .ack_timeout_waits_for_data = TRUE,
    .event = goodix_gm168sec_event,
    .reply_answers = goodix_gm168sec_reply_answers,
    .download_succeeded = goodix_gm168sec_download_succeeded,
    .may_send = goodix_gm168sec_may_send,
  },
  .channel = {
    .name = "TLS",
    /* Start makes one TLS call and one retry; StartTlsThread, after a failed
     * read, makes one; LogicEvtTlsConn makes at most two. */
    .attempts = { .open = 2, .after_failed_read = 1, .reconnect = 2 },
    .retry_delay_ms = 0,
    .attempt_new = goodix_tls_attempt_new,
    .decrypt_frame = goodix_decrypt_frame_tls,
    .image_error = goodix_tls_image_error,
    .rearm_after_capture = goodix_gm168sec_rearm_after_capture,
    .restart_disarms = TRUE,
    .config_lost = goodix_gm168sec_config_lost,
    .discard = goodix_tls_discard,
  },
  .open_new = goodix_gm168sec_open_new,
};
