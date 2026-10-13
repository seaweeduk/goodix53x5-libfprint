/*
 * Goodix 53x5 driver for libfprint — GM168SEC bring-up
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
#include "device/commands.h"
#include "device/session.h"
#include "family/gm168sec/gm168sec.h"

typedef enum {
  GOODIX_GM168SEC_OPEN_PROBE,
  GOODIX_GM168SEC_OPEN_FIRMWARE,
  GOODIX_GM168SEC_OPEN_FIRMWARE_RETRY,
  GOODIX_GM168SEC_OPEN_PSK_READ,
  GOODIX_GM168SEC_OPEN_PSK_CHECK,
  GOODIX_GM168SEC_OPEN_RESET,
  GOODIX_GM168SEC_OPEN_CHIP,
  GOODIX_GM168SEC_OPEN_READ_OTP,
  GOODIX_GM168SEC_OPEN_PARSE_OTP,
  GOODIX_GM168SEC_OPEN_CHANNEL,
  GOODIX_GM168SEC_OPEN_NUM_STATES,
} GoodixGm168secOpenState;

#ifdef GOODIX53X5_DEBUG
static const gchar *
goodix_gm168sec_open_state_name (GoodixGm168secOpenState state)
{
  switch (state)
    {
    case GOODIX_GM168SEC_OPEN_PROBE:
      return "ping";
    case GOODIX_GM168SEC_OPEN_FIRMWARE:
    case GOODIX_GM168SEC_OPEN_FIRMWARE_RETRY:
      return "firmware";
    case GOODIX_GM168SEC_OPEN_PSK_READ:
      return "early_psk_read";
    case GOODIX_GM168SEC_OPEN_PSK_CHECK:
      return "early_psk_check";
    case GOODIX_GM168SEC_OPEN_RESET:
      return "reset";
    case GOODIX_GM168SEC_OPEN_CHIP:
      return "chip";
    case GOODIX_GM168SEC_OPEN_READ_OTP:
      return "read_otp";
    case GOODIX_GM168SEC_OPEN_PARSE_OTP:
      return "parse_otp";
    case GOODIX_GM168SEC_OPEN_CHANNEL:
      return "gtls_client_hello";
    case GOODIX_GM168SEC_OPEN_NUM_STATES:
    default:
      return "unknown";
    }
}
#endif

static void
goodix_gm168sec_probe_result (FpiSsm *ssm, FpDevice *dev, guint8 status,
                             gboolean native_zero, GError *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixGm168secOpenState state = fpi_ssm_get_cur_state (ssm);

  if (error && !native_zero)
    {
      fpi_ssm_mark_failed (ssm, error);
      return;
    }
  if (state == GOODIX_GM168SEC_OPEN_PROBE)
    {
      g_clear_error (&error);
      fpi_ssm_next_state (ssm);
      return;
    }
  if (error)
    {
      g_clear_error (&error);
      if (state == GOODIX_GM168SEC_OPEN_FIRMWARE)
        fpi_ssm_next_state (ssm);
      else
        fpi_ssm_mark_failed (ssm, goodix_gm168sec_check_firmware (NULL));
      return;
    }
  g_free (self->fw_version);
  self->fw_version = g_strndup ((const gchar *) self->shared_response,
                                sizeof (self->shared_response));
  error = goodix_gm168sec_check_firmware (self->fw_version);
  if (error)
    fpi_ssm_mark_failed (ssm, error);
  else
    fpi_ssm_jump_to_state (ssm, GOODIX_GM168SEC_OPEN_PSK_READ);
}

static void
goodix_gm168sec_chip_result (FpiSsm *ssm, FpDevice *dev, guint8 status,
                            gboolean native_zero, GError *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  const guint8 *payload;
  gsize len;

  if (fpi_ssm_get_cur_state (ssm) == GOODIX_GM168SEC_OPEN_RESET && (!error || native_zero))
    {
      g_clear_error (&error);
      fpi_ssm_next_state_delayed (ssm, 10);
      return;
    }
  if (error || !goodix_cmd_parse_chip_id_reply (dev, &payload, &len, &error))
    {
      fpi_ssm_mark_failed (ssm, error);
      return;
    }
  if (len != 4)
    {
      fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                            FP_DEVICE_ERROR_PROTO, "Unexpected chip ID reply length: %zu", len));
      return;
    }
  self->chip_id = goodix_crypto_decode_u32 (payload);
  if (!goodix_milan_runtime_subtype_for_chip (self->chip_id, &self->milan_sensor_subtype))
    fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (
                          FP_DEVICE_ERROR_NOT_SUPPORTED, "Unsupported chip 0x%08x", self->chip_id));
  else
    fpi_ssm_next_state (ssm);
}

/* Brings a 533c up to an established TLS session in the vendor's order:
 * SelfCheck's firmware reads, the PSK check the vendor makes before it
 * resets the sensor, the chip ID read after a reset asking for the IRQ
 * reply, the OTP, then the handshake. The driver never writes a PSK here,
 * so one that does not match fails open. */
static void
goodix_gm168sec_open_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixGm168secOpenState state = fpi_ssm_get_cur_state (ssm);

  goodix_debug_timing_open_state (self, dev, goodix_gm168sec_open_state_name (state),
                                  g_get_monotonic_time ());

  switch (state)
    {
    case GOODIX_GM168SEC_OPEN_PROBE:
    case GOODIX_GM168SEC_OPEN_FIRMWARE:
    case GOODIX_GM168SEC_OPEN_FIRMWARE_RETRY:
      goodix_cmd_probe (ssm, dev, state != GOODIX_GM168SEC_OPEN_PROBE,
                         goodix_gm168sec_probe_result);
      break;

    case GOODIX_GM168SEC_OPEN_PSK_READ:
      {
        g_autoptr(GError) error = NULL;

        if (!goodix_load_psk (self, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        goodix_cmd_preset_read (ssm, dev, GOODIX_WRAPPED_PSK_HASH_TYPE, GOODIX_PSK_HASH_LEN);
      }
      break;

    case GOODIX_GM168SEC_OPEN_PSK_CHECK:
      {
        GError *error = NULL;

        if (!goodix_require_current_psk (dev, GOODIX_WRAPPED_PSK_HASH_TYPE, &error))
          fpi_ssm_mark_failed (ssm, error);
        else
          fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_GM168SEC_OPEN_RESET:
      self->profile9_fdt.drift_anchor_empty = TRUE;
      goodix_health_reset (&self->health);
      self->capture_callback_pending = FALSE;
      goodix_cmd_reset_sensor (ssm, dev, TRUE, goodix_gm168sec_chip_result);
      break;

    case GOODIX_GM168SEC_OPEN_CHIP:
      goodix_cmd_read_chip_id (ssm, dev, goodix_gm168sec_chip_result);
      break;

    case GOODIX_GM168SEC_OPEN_READ_OTP:
      goodix_cmd_read_otp (ssm, dev);
      break;

    case GOODIX_GM168SEC_OPEN_PARSE_OTP:
      {
        g_autoptr(GError) error = NULL;

        if (!goodix_open_parse_otp (dev, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_GM168SEC_OPEN_CHANNEL:
      goodix_open_channel (ssm, dev);
      break;

    case GOODIX_GM168SEC_OPEN_NUM_STATES:
      g_assert_not_reached ();
      break;
    }
}

FpiSsm *
goodix_gm168sec_open_new (FpDevice *dev)
{
  return fpi_ssm_new (dev, goodix_gm168sec_open_handler, GOODIX_GM168SEC_OPEN_NUM_STATES);
}
