/*
 * Goodix 53x5 driver for libfprint — the HTSEC secure channel (GTLS)
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
#include "device/commands.h"
#include "device/session.h"
#include "family/htsec/htsec.h"

#include <string.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

typedef enum {
  GOODIX_GTLS_HELLO,
  GOODIX_GTLS_IDENTITY,
  GOODIX_GTLS_VERIFY,
  GOODIX_GTLS_COMPLETION,
  GOODIX_GTLS_READY,
  GOODIX_GTLS_NUM_STATES,
} GoodixGtlsState;

static void
goodix_gtls_ssm_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  const guint8 *mcu_data;
  gsize mcu_len;
  g_autoptr(GError) error = NULL;

  if (g_cancellable_set_error_if_cancelled (goodix_session_io_cancellable (dev), &error))
    {
      fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
      return;
    }

  switch ((GoodixGtlsState) fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_GTLS_HELLO:
      goodix_transport_reset_mcu (dev);
      goodix_crypto_gtls_init (&self->gtls, self->psk);
      if (RAND_bytes (self->gtls.client_random, 32) != 1)
        {
          fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                                 FP_DEVICE_ERROR_PROTO, "GTLS client random generation failed"));
          return;
        }
      goodix_cmd_mcu_send (ssm, dev, 0xFF01, self->gtls.client_random, 32);
      break;

    case GOODIX_GTLS_IDENTITY:
      self->gtls.state = 2;
      goodix_recv_mcu (ssm, dev, GOODIX_CHANNEL_REPLY_TIMEOUT_MS, 72);
      break;

    case GOODIX_GTLS_VERIFY:
      if (!goodix_cmd_parse_mcu_reply (dev, 0xFF02, &mcu_data, &mcu_len) || mcu_len != 64)
        {
          fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                                FP_DEVICE_ERROR_PROTO, "Invalid GTLS server identity"));
          return;
        }
      memcpy (self->gtls.server_random, mcu_data, 32);
      memcpy (self->gtls.server_identity, mcu_data + 32, 32);
      goodix_crypto_gtls_derive_keys (&self->gtls);
      if (!goodix_crypto_gtls_verify_identity (&self->gtls))
        {
          fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                                FP_DEVICE_ERROR_PROTO, "GTLS identity verification failed"));
          return;
        }
      {
        guint8 verify_data[36];

        memcpy (verify_data, self->gtls.client_identity, 32);
        memset (verify_data + 32, 0xEE, 4);
        goodix_cmd_mcu_send (ssm, dev, 0xFF03, verify_data, 36);
      }
      break;

    case GOODIX_GTLS_COMPLETION:
      self->gtls.state = 4;
      goodix_recv_mcu (ssm, dev, GOODIX_CHANNEL_REPLY_TIMEOUT_MS, 12);
      break;

    case GOODIX_GTLS_READY:
      if (!goodix_cmd_parse_mcu_reply (dev, 0xFF04, &mcu_data, &mcu_len))
        {
          fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                                FP_DEVICE_ERROR_PROTO, "Failed to parse GTLS done"));
          return;
        }
      /* The native completion is exactly twelve bytes including the MCU
       * header. Its four trailing bytes are not a result code. */
      if (mcu_len != 4)
        {
          fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                                FP_DEVICE_ERROR_PROTO, "Wrong GTLS done payload size: %zu", mcu_len));
          return;
        }
      self->gtls.hmac_client_counter = self->gtls.hmac_client_counter_init;
      self->gtls.hmac_server_counter = self->gtls.hmac_server_counter_init;
      self->gtls.state = 5;
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_GTLS_NUM_STATES:
      g_assert_not_reached ();
    }
}

FpiSsm *
goodix_gtls_attempt_new (FpDevice *dev, GoodixChannelReason reason)
{
  (void) reason;
  return fpi_ssm_new (dev, goodix_gtls_ssm_handler, GOODIX_GTLS_NUM_STATES);
}

/* A GTLS frame counts against an established session only once it consumed
 * the HMAC counter. */
GoodixFrameStatus
goodix_decrypt_frame_gtls (FpiDeviceGoodix53x5 *self, const guint8 *payload,
                           gsize len, guint8 **out, gsize *out_len)
{
  guint32 counter = self->gtls.hmac_server_counter;

  *out = goodix_crypto_gtls_decrypt_sensor_data (&self->gtls, payload, len, out_len);
  if (*out)
    return GOODIX_FRAME_DECRYPTED;
  return self->gtls.hmac_server_counter != counter && self->gtls.state == 5 ?
         GOODIX_FRAME_REJECTED_IN_SESSION : GOODIX_FRAME_REJECTED;
}

/* Restarts the session at every second undecodable frame. Successful frames
 * do not clear the history. */
gboolean
goodix_gtls_image_error (FpiDeviceGoodix53x5 *self)
{
  if (!self->image_error_history)
    {
      self->image_error_history = TRUE;
      return FALSE;
    }
  self->image_error_history = FALSE;
  return TRUE;
}

void
goodix_gtls_discard (FpiDeviceGoodix53x5 *self)
{
  OPENSSL_cleanse (self->gtls.psk, sizeof (self->gtls.psk));
}
