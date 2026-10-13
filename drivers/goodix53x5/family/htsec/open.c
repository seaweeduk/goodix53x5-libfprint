/*
 * Goodix 53x5 driver for libfprint — HTSEC bring-up
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
#include "device/commands.h"
#include "device/session.h"
#include "family/htsec/htsec.h"

/* The production slot holding the SHA-256 of the sensor's PSK. */
#define GOODIX_HTSEC_PSK_HASH_TYPE 0xB003

/* PSK white box for writing the default all-zero PSK. */
#define GOODIX_PSK_WHITE_BOX_LEN 96

/* PSK white box for writing the default all-zero PSK. */
static const guint8 goodix_psk_white_box[GOODIX_PSK_WHITE_BOX_LEN] = {
  0xec, 0x35, 0xae, 0x3a, 0xbb, 0x45, 0xed, 0x3f,
  0x12, 0xc4, 0x75, 0x1f, 0x1e, 0x5c, 0x2c, 0xc0,
  0x5b, 0x3c, 0x54, 0x52, 0xe9, 0x10, 0x4d, 0x9f,
  0x2a, 0x31, 0x18, 0x64, 0x4f, 0x37, 0xa0, 0x4b,
  0x6f, 0xd6, 0x6b, 0x1d, 0x97, 0xcf, 0x80, 0xf1,
  0x34, 0x5f, 0x76, 0xc8, 0x4f, 0x03, 0xff, 0x30,
  0xbb, 0x51, 0xbf, 0x30, 0x8f, 0x2a, 0x98, 0x75,
  0xc4, 0x1e, 0x65, 0x92, 0xcd, 0x2a, 0x2f, 0x9e,
  0x60, 0x80, 0x9b, 0x17, 0xb5, 0x31, 0x60, 0x37,
  0xb6, 0x9b, 0xb2, 0xfa, 0x5d, 0x4c, 0x8a, 0xc3,
  0x1e, 0xdb, 0x33, 0x94, 0x04, 0x6e, 0xc0, 0x6b,
  0xbd, 0xac, 0xc5, 0x7d, 0xa6, 0xa7, 0x56, 0xc5,
};

typedef enum {
  GOODIX_HTSEC_OPEN_PROBE,
  GOODIX_HTSEC_OPEN_RESET,
  GOODIX_HTSEC_OPEN_READ_OTP,
  GOODIX_HTSEC_OPEN_PARSE_OTP,
  GOODIX_HTSEC_OPEN_READ_PSK_HASH,
  GOODIX_HTSEC_OPEN_WRITE_PSK,
  GOODIX_HTSEC_OPEN_VERIFY_PSK_WRITE,
  GOODIX_HTSEC_OPEN_CHANNEL,
  GOODIX_HTSEC_OPEN_NUM_STATES,
} GoodixHtsecOpenState;

#ifdef GOODIX53X5_DEBUG
static const gchar *
goodix_htsec_open_state_name (GoodixHtsecOpenState state)
{
  switch (state)
    {
    case GOODIX_HTSEC_OPEN_PROBE:
      return "ping";
    case GOODIX_HTSEC_OPEN_RESET:
      return "reset";
    case GOODIX_HTSEC_OPEN_READ_OTP:
      return "read_otp";
    case GOODIX_HTSEC_OPEN_PARSE_OTP:
      return "parse_otp";
    case GOODIX_HTSEC_OPEN_READ_PSK_HASH:
      return "read_psk_hash";
    case GOODIX_HTSEC_OPEN_WRITE_PSK:
      return "write_psk";
    case GOODIX_HTSEC_OPEN_VERIFY_PSK_WRITE:
      return "verify_psk_write";
    case GOODIX_HTSEC_OPEN_CHANNEL:
      return "gtls_client_hello";
    case GOODIX_HTSEC_OPEN_NUM_STATES:
    default:
      return "unknown";
    }
}
#endif

/* Brings a 53x5 up to an established GTLS session: wake it and read its
 * firmware version, identify the chip, read the OTP, make sure the sensor
 * holds this driver's PSK, writing the default one where it holds another
 * and no key was imported, then run the handshake. */
static void
goodix_htsec_open_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixHtsecOpenState state = fpi_ssm_get_cur_state (ssm);

  goodix_debug_timing_open_state (self, dev, goodix_htsec_open_state_name (state),
                                  g_get_monotonic_time ());

  switch (state)
    {
    case GOODIX_HTSEC_OPEN_PROBE:
      goodix_open_probe (ssm, dev, 5, goodix_htsec_check_firmware);
      break;

    case GOODIX_HTSEC_OPEN_RESET:
      goodix_open_identify_chip (ssm, dev, FALSE);
      break;

    case GOODIX_HTSEC_OPEN_READ_OTP:
      goodix_cmd_read_otp (ssm, dev);
      break;

    case GOODIX_HTSEC_OPEN_PARSE_OTP:
      {
        g_autoptr(GError) error = NULL;

        if (!goodix_open_parse_otp (dev, &error) || !goodix_load_psk (self, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_HTSEC_OPEN_READ_PSK_HASH:
      goodix_cmd_production_read (ssm, dev, GOODIX_HTSEC_PSK_HASH_TYPE);
      break;

    case GOODIX_HTSEC_OPEN_WRITE_PSK:
      {
        gboolean matches;
        GError *error = NULL;

        if (!goodix_psk_hash_reply_matches (dev, GOODIX_HTSEC_PSK_HASH_TYPE, &matches, &error))
          {
            fpi_ssm_mark_failed (ssm, error);
            return;
          }
        if (matches)
          {
            fp_dbg ("PSK hash matches, no need to write");
            self->psk_write_verify_pending = FALSE;
            fpi_ssm_jump_to_state (ssm, GOODIX_HTSEC_OPEN_CHANNEL);
            return;
          }

        if (self->psk_imported)
          {
            fpi_ssm_mark_failed (
              ssm,
              fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_INVALID,
                                        "Sensor PSK does not match imported Windows PSK; refusing to overwrite it"));
            return;
          }

        fp_info ("Writing default PSK white box");
        self->psk_write_verify_pending = TRUE;
        goodix_cmd_production_write (ssm, dev, 0xB002, goodix_psk_white_box,
                                     GOODIX_PSK_WHITE_BOX_LEN);
      }
      break;

    case GOODIX_HTSEC_OPEN_VERIFY_PSK_WRITE:
      {
        const guint8 *pl;
        gsize pl_len;

        if (!goodix_cmd_parse_production_write_reply (dev, &pl, &pl_len) ||
            pl_len < 1)
          {
            fpi_ssm_mark_failed (ssm,
                                 fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                           "Failed to parse PSK write reply"));
            return;
          }

        if (pl[0] != 0)
          {
            fpi_ssm_mark_failed (ssm,
                                 fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                           "PSK write failed: %u",
                                                           pl[0]));
            return;
          }

        /* Re-read the PSK hash to verify the write before GTLS */
        goodix_cmd_production_read (ssm, dev, GOODIX_HTSEC_PSK_HASH_TYPE);
      }
      break;

    case GOODIX_HTSEC_OPEN_CHANNEL:
      {
        if (self->psk_write_verify_pending)
          {
            /* The previous state re-read the hash of the PSK just written. */
            gboolean matches;
            GError *error = NULL;

            if (!goodix_psk_hash_reply_matches (dev, GOODIX_HTSEC_PSK_HASH_TYPE, &matches, &error))
              {
                g_clear_error (&error);
                fpi_ssm_mark_failed (ssm,
                                     fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                               "Failed to re-read PSK hash"));
                return;
              }

            if (!matches)
              {
                fpi_ssm_mark_failed (ssm,
                                     fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                               "PSK hash mismatch after write"));
                return;
              }

            self->psk_write_verify_pending = FALSE;
          }

        goodix_open_channel (ssm, dev);
      }
      break;

    case GOODIX_HTSEC_OPEN_NUM_STATES:
      g_assert_not_reached ();
      break;
    }
}

FpiSsm *
goodix_htsec_open_new (FpDevice *dev)
{
  return fpi_ssm_new (dev, goodix_htsec_open_handler, GOODIX_HTSEC_OPEN_NUM_STATES);
}
