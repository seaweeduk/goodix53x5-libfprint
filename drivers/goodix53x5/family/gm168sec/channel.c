/*
 * Goodix 53x5 driver for libfprint — the GM168SEC secure channel (TLS 1.2-PSK)
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
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
#include "device/commands.h"
#include "device/session.h"
#include "device/crypto.h"
#include "family/gm168sec/gm168sec.h"
#include "family/gm168sec/tls.h"

/* FpParseImage takes exactly the raw12 pixels and their CRC. */
#define GOODIX_WRAPPED_IMAGE_LEN (GOODIX_SENSOR_RAW12_BYTES + 4)

/* Checks whether @image is a whole raw12 image whose trailing CRC, in the
 * sensor's byte order, matches its pixels. */
static gboolean
goodix_wrapped_image_valid (const guint8 *image, gsize len)
{
  return len == GOODIX_WRAPPED_IMAGE_LEN &&
         goodix_crypto_crc32_mpeg2 (image, GOODIX_SENSOR_RAW12_BYTES) ==
         goodix_crypto_decode_u32 (image + GOODIX_SENSOR_RAW12_BYTES);
}

GoodixFrameStatus
goodix_decrypt_frame_tls (FpiDeviceGoodix53x5 *self, const guint8 *payload,
                          gsize len, guint8 **out, gsize *out_len)
{
  gboolean established = self->tls && goodix_tls_established (self->tls);

  *out = established ? goodix_tls_decrypt (self->tls, payload, len, out_len) : NULL;
  if (*out && !goodix_wrapped_image_valid (*out, *out_len))
    {
      /* The vendor restarts TLS only when the read itself fails. */
      fp_warn ("Rejecting a %zu-byte image without a valid CRC", *out_len);
      g_clear_pointer (out, g_free);
      return GOODIX_FRAME_INVALID;
    }
  if (*out)
    return GOODIX_FRAME_DECRYPTED;
  return established ? GOODIX_FRAME_REJECTED_IN_SESSION : GOODIX_FRAME_REJECTED;
}

/* OpenSSL returns a flight; the sensor receives one pack per TLS record. */
static void
goodix_tls_send_handler (FpiSsm *ssm, FpDevice *dev)
{
  GBytes *out = fpi_ssm_get_data (ssm);
  gsize len;
  const guint8 *data = g_bytes_get_data (out, &len);
  g_autoptr(GError) error = NULL;

  if (g_cancellable_set_error_if_cancelled (goodix_session_io_cancellable (dev), &error))
    {
      fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
      return;
    }

  if (fpi_ssm_get_cur_state (ssm) == 1)
    /* TOD's write and send callback sleep 1 + 2 ms, even on the last record. */
    fpi_ssm_jump_to_state_delayed (ssm, 0, 3);
  else if (!len)
    fpi_ssm_mark_completed (ssm);
  else
    {
      gsize record_len = GOODIX_TLS_RECORD_HEADER + (data[3] << 8 | data[4]);
      g_autoptr(GBytes) record = g_bytes_new_from_bytes (out, 0, record_len);
      GBytes *remaining = g_bytes_new_from_bytes (out, record_len, len - record_len);

      fpi_ssm_set_data (ssm, remaining, (GDestroyNotify) g_bytes_unref);
      goodix_cmd_send_tls_record (ssm, dev, record);
    }
}

typedef enum {
  GOODIX_TLS_PSK_REFRESH,
  GOODIX_TLS_PSK_CHECK,
  GOODIX_TLS_REQUEST,
  GOODIX_TLS_RECV,
  GOODIX_TLS_STEP,
  GOODIX_TLS_SENT,
  GOODIX_TLS_NUM_STATES,
} GoodixTlsState;

static void
goodix_tls_received (FpDevice *dev, const GoodixTransportResult *result,
                      GError *error, gpointer data)
{
  FpiSsm *ssm = data;

  if (g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT))
    g_clear_error (&error);
  if (error)
    fpi_ssm_mark_failed (ssm, error);
  else
    fpi_ssm_next_state (ssm);
}

/* Runs one TLS handshake attempt, the sensor being the client, reading the
 * PSK hash first when the sensor reported a power loss. Fails when the hash is
 * not this driver's, the sensor sends a record OpenSSL rejects, or the
 * handshake runs out of steps. */
static void
goodix_tls_ssm_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  g_autoptr(GError) error = NULL;

  if (g_cancellable_set_error_if_cancelled (goodix_session_io_cancellable (dev), &error))
    {
      fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
      return;
    }

  switch ((GoodixTlsState) fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_TLS_PSK_REFRESH:
      if (!self->psk_refresh_pending)
        fpi_ssm_jump_to_state (ssm, GOODIX_TLS_REQUEST);
      else
        goodix_cmd_preset_read (ssm, dev, GOODIX_WRAPPED_PSK_HASH_TYPE, GOODIX_PSK_HASH_LEN);
      break;

    case GOODIX_TLS_PSK_CHECK:
      if (!goodix_require_current_psk (dev, GOODIX_WRAPPED_PSK_HASH_TYPE, &error))
        {
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
          return;
        }
      self->psk_refresh_pending = FALSE;
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_TLS_REQUEST:
      goodix_transport_reset_mcu (dev);
      self->mcu_ready = FALSE;
      g_clear_pointer (&self->tls, goodix_tls_free);
      self->tls = goodix_tls_new (self->psk, GOODIX_PSK_LEN, &error);
      if (!self->tls)
        {
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
          return;
        }
      goodix_cmd_tls_request (ssm, dev);
      break;

    case GOODIX_TLS_RECV:
      goodix_transport_wait_mcu (dev, 50, 0, goodix_tls_received, ssm);
      break;

    case GOODIX_TLS_STEP:
      {
        g_autoptr(GBytes) out = NULL;
        const guint8 *in = NULL;
        gsize in_len = 0;

        if (self->mcu_reply)
          in = g_bytes_get_data (self->mcu_reply, &in_len);
        if (!goodix_tls_handshake (self->tls, in, in_len, &out, &error))
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
        else if (out)
          {
            FpiSsm *send = fpi_ssm_new (dev, goodix_tls_send_handler, 2);

            fpi_ssm_set_data (send, g_steal_pointer (&out), (GDestroyNotify) g_bytes_unref);
            fpi_ssm_start_subsm (ssm, send);
          }
        else
          fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_TLS_SENT:
      /* The Geneva handshake sends nothing after the last record. */
      if (goodix_tls_established (self->tls))
        fpi_ssm_mark_completed (ssm);
      else if (!goodix_tls_handshake_pending (self->tls))
        fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                              FP_DEVICE_ERROR_PROTO, "TLS handshake did not finish in ten empty waits"));
      else
        fpi_ssm_jump_to_state (ssm, GOODIX_TLS_RECV);
      break;

    case GOODIX_TLS_NUM_STATES:
      g_assert_not_reached ();
    }
}

static void
goodix_tls_read_restart_handler (FpiSsm *ssm, FpDevice *dev)
{
  if (fpi_ssm_get_cur_state (ssm) == GOODIX_TLS_PSK_REFRESH)
    fpi_ssm_jump_to_state (ssm, GOODIX_TLS_REQUEST);
  else
    goodix_tls_ssm_handler (ssm, dev);
}

FpiSsm *
goodix_tls_attempt_new (FpDevice *dev, GoodixChannelReason reason)
{
  return fpi_ssm_new (dev, reason == GOODIX_CHANNEL_FAILED_READ ?
                      goodix_tls_read_restart_handler : goodix_tls_ssm_handler,
                      GOODIX_TLS_NUM_STATES);
}

/* An OpenSSL session is dead after a failed record, so every undecodable
 * frame restarts it. */
gboolean
goodix_tls_image_error (FpiDeviceGoodix53x5 *self)
{
  (void) self;
  return TRUE;
}

void
goodix_tls_discard (FpiDeviceGoodix53x5 *self)
{
  g_clear_pointer (&self->tls, goodix_tls_free);
}
