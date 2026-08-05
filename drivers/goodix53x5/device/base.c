/*
 * Goodix 53x5 driver for libfprint - native Milan base generation
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "device/base.h"

#include <string.h>

GQuark
goodix_milan_base_error_quark (void)
{
  return g_quark_from_static_string ("goodix-milan-base-error");
}

gboolean
goodix_milan_runtime_subtype_for_chip (guint32  chip_id,
                                       guint16 *subtype)
{
  if ((chip_id & GOODIX_MILAN_PROFILE9_CHIP_FAMILY_MASK) !=
      GOODIX_MILAN_PROFILE9_CHIP_FAMILY_PREFIX)
    return FALSE;

  if (subtype)
    *subtype = GOODIX_MILAN_VALIDATED_SUBTYPE;
  return TRUE;
}

gboolean
goodix_milan_base_pair_mad (const guint16 *tx_on,
                            gsize          tx_on_values,
                            const guint16 *tx_off,
                            gsize          tx_off_values,
                            guint64       *mad,
                            GError       **error)
{
  guint64 sum = 0;
  const gsize rows = GOODIX_MILAN_SENSOR_ROWS;
  const gsize columns = GOODIX_MILAN_SENSOR_COLUMNS;
  const gsize count = GOODIX_MILAN_SENSOR_PIXELS;
  const gsize interior =
    (rows - 2 * GOODIX_MILAN_BASE_BORDER) *
    (columns - 2 * GOODIX_MILAN_BASE_BORDER);

  if (!tx_on || !tx_off || tx_on_values != count || tx_off_values != count)
    {
      g_set_error_literal (error, GOODIX_MILAN_BASE_ERROR,
                           GOODIX_MILAN_BASE_ERROR_INVALID_FRAME,
                           "Milan base frames must each contain exactly 9504 values");
      return FALSE;
    }

  for (gsize row = GOODIX_MILAN_BASE_BORDER;
       row < rows - GOODIX_MILAN_BASE_BORDER; row++)
    for (gsize column = GOODIX_MILAN_BASE_BORDER;
         column < columns - GOODIX_MILAN_BASE_BORDER; column++)
      {
        const gsize index = row * columns + column;
        const guint16 first = tx_on[index];
        const guint16 second = tx_off[index];

        sum += first >= second ? first - second : second - first;
      }

  if (mad)
    *mad = sum / interior;
  return TRUE;
}

void
goodix_milan_base_attempt_init (GoodixMilanBaseAttempt *attempt)
{
  g_return_if_fail (attempt != NULL);
  memset (attempt, 0, sizeof (*attempt));
}

static void
goodix_milan_base_attempt_release_frames (GoodixMilanBaseAttempt *attempt)
{
  g_clear_pointer (&attempt->tx_on, g_free);
  g_clear_pointer (&attempt->tx_off, g_free);
  attempt->tx_on_values = 0;
  attempt->tx_off_values = 0;
  attempt->admitted = FALSE;
}

void
goodix_milan_base_attempt_reset (GoodixMilanBaseAttempt *attempt)
{
  if (!attempt)
    return;

  goodix_milan_base_attempt_release_frames (attempt);
  attempt->stage = GOODIX_MILAN_BASE_STAGE_IDLE;
  attempt->mad = 0;
}

void
goodix_milan_base_attempt_cancel (GoodixMilanBaseAttempt *attempt)
{
  if (!attempt)
    return;

  goodix_milan_base_attempt_release_frames (attempt);
  attempt->stage = GOODIX_MILAN_BASE_STAGE_CANCELLED;
  attempt->mad = 0;
}

void
goodix_milan_base_attempt_take_frame (GoodixMilanBaseAttempt *attempt,
                                      gboolean                tx_on,
                                      guint16               **frame,
                                      gsize                   values)
{
  guint16 **slot;
  gsize *slot_values;

  g_return_if_fail (attempt != NULL);
  g_return_if_fail (frame != NULL);

  slot = tx_on ? &attempt->tx_on : &attempt->tx_off;
  slot_values = tx_on ? &attempt->tx_on_values : &attempt->tx_off_values;
  g_clear_pointer (slot, g_free);
  *slot = g_steal_pointer (frame);
  *slot_values = values;
}

gboolean
goodix_milan_base_attempt_admit (GoodixMilanBaseAttempt *attempt,
                                 GError                **error)
{
  g_return_val_if_fail (attempt != NULL, FALSE);

  attempt->stage = GOODIX_MILAN_BASE_STAGE_ADMIT_PAIR;
  if (!goodix_milan_base_pair_mad (attempt->tx_on, attempt->tx_on_values,
                                   attempt->tx_off, attempt->tx_off_values,
                                   &attempt->mad, error))
    {
      goodix_milan_base_attempt_release_frames (attempt);
      return FALSE;
    }

  if (attempt->mad >= GOODIX_MILAN_BASE_MAD_LIMIT)
    {
      goodix_milan_base_attempt_release_frames (attempt);
      attempt->stage = GOODIX_MILAN_BASE_STAGE_REJECTED;
      return FALSE;
    }

  attempt->admitted = TRUE;
  return TRUE;
}

gboolean
goodix_milan_generation_allocate_id (guint64  *last_generation_id,
                                      guint64  *generation_id,
                                      GError  **error)
{
  g_return_val_if_fail (last_generation_id != NULL, FALSE);
  g_return_val_if_fail (generation_id != NULL, FALSE);

  if (*last_generation_id == G_MAXUINT64)
    {
      g_set_error_literal (error, GOODIX_MILAN_BASE_ERROR,
                           GOODIX_MILAN_BASE_ERROR_ID_EXHAUSTED,
                           "Milan generation IDs are exhausted");
      return FALSE;
    }

  *generation_id = ++(*last_generation_id);
  return TRUE;
}

void
goodix_milan_generation_reset_preprocess (GoodixMilanGeneration *generation)
{
  g_return_if_fail (generation != NULL);
  goodix_milan_preprocess_reset (&generation->state);
}

gboolean
goodix_milan_base_attempt_publish (GoodixMilanBaseAttempt  *attempt,
                                   guint64                  generation_id,
                                   GoodixMilanGeneration **generation,
                                   guint16                **legacy_tx_off,
                                   GError                 **error)
{
  GoodixMilanGeneration *published;

  g_return_val_if_fail (attempt != NULL, FALSE);
  g_return_val_if_fail (generation != NULL, FALSE);
  g_return_val_if_fail (legacy_tx_off != NULL, FALSE);

  if (!attempt->admitted || !attempt->tx_on || !attempt->tx_off ||
      attempt->tx_on_values != GOODIX_MILAN_SENSOR_PIXELS ||
      attempt->tx_off_values != GOODIX_MILAN_SENSOR_PIXELS ||
      generation_id == 0)
    {
      g_set_error_literal (error, GOODIX_MILAN_BASE_ERROR,
                           GOODIX_MILAN_BASE_ERROR_INCOMPLETE,
                           "Cannot publish an incomplete Milan base generation");
      goodix_milan_base_attempt_release_frames (attempt);
      return FALSE;
    }

  published = g_new0 (GoodixMilanGeneration, 1);
  published->generation_id = generation_id;
  published->setup_tx_on = g_steal_pointer (&attempt->tx_on);
  published->admitted = TRUE;
  goodix_milan_generation_reset_preprocess (published);

  *legacy_tx_off = g_steal_pointer (&attempt->tx_off);
  attempt->tx_on_values = 0;
  attempt->tx_off_values = 0;
  attempt->admitted = FALSE;
  attempt->stage = GOODIX_MILAN_BASE_STAGE_PUBLISH;
  *generation = published;
  return TRUE;
}

void
goodix_milan_generation_free (GoodixMilanGeneration *generation)
{
  if (!generation)
    return;

  g_clear_pointer (&generation->setup_tx_on, g_free);
  memset (&generation->state, 0, sizeof (generation->state));
  g_free (generation);
}

void
goodix_milan_generation_invalidate (GoodixMilanGeneration **generation)
{
  if (!generation || !*generation)
    return;

  goodix_milan_generation_free (g_steal_pointer (generation));
}

guint64
goodix_milan_generation_note_use (GoodixMilanGeneration *generation)
{
  g_return_val_if_fail (generation != NULL, 0);
  g_return_val_if_fail (generation->admitted, 0);

  if (generation->use_count < G_MAXUINT64)
    generation->use_count++;
  return generation->use_count;
}

void
goodix_milan_generation_note_identify_prelude (GoodixMilanGeneration *generation)
{
  g_return_if_fail (generation != NULL);
  generation->identify_prelude_seen = TRUE;
  if (generation->identify_prelude_count < G_MAXUINT)
    generation->identify_prelude_count++;
}

void
goodix_milan_generation_note_enrollment_stage (GoodixMilanGeneration *generation)
{
  g_return_if_fail (generation != NULL);
  if (generation->enrollment_stages < G_MAXUINT)
    generation->enrollment_stages++;
}

gboolean
goodix_milan_replace_raw_frame (guint16 **owner,
                                guint16 **frame,
                                gsize     values,
                                GError  **error)
{
  g_return_val_if_fail (owner != NULL, FALSE);
  g_return_val_if_fail (frame != NULL, FALSE);

  if (!*frame || values != GOODIX_MILAN_SENSOR_PIXELS)
    {
      g_set_error_literal (error, GOODIX_MILAN_BASE_ERROR,
                           GOODIX_MILAN_BASE_ERROR_INVALID_FRAME,
                           "Live Milan raw frame must contain exactly 9504 values");
      g_clear_pointer (frame, g_free);
      return FALSE;
    }

  g_clear_pointer (owner, g_free);
  *owner = g_steal_pointer (frame);
  return TRUE;
}

/* Preserve release source locations after removing the compile seam. */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "driver-private.h"
#include "device/calibration.h"
#include "device/commands.h"
#include "device/image.h"
#include "device/transport.h"

typedef enum
{
  GOODIX_BASE_EC_POWER_ON = 0,
  GOODIX_BASE_EC_POWER_ON_DONE,
  GOODIX_BASE_FDT_TX_ON_BEFORE,
  GOODIX_BASE_FDT_TX_ON_BEFORE_DONE,
  GOODIX_BASE_CAPTURE_TX_ON,
  GOODIX_BASE_CAPTURE_TX_ON_DONE,
  GOODIX_BASE_FDT_TX_OFF,
  GOODIX_BASE_FDT_TX_OFF_DONE,
  GOODIX_BASE_CAPTURE_TX_OFF,
  GOODIX_BASE_CAPTURE_TX_OFF_DONE,
  GOODIX_BASE_FDT_TX_ON_AFTER,
  GOODIX_BASE_FDT_TX_ON_AFTER_DONE,
  GOODIX_BASE_RECOVERY_FDT_TX_ON,
  GOODIX_BASE_RECOVERY_FDT_TX_ON_DONE,
  GOODIX_BASE_CLEANUP_SLEEP,
  GOODIX_BASE_CLEANUP_EC_POWER_OFF,
  GOODIX_BASE_CLEANUP_EC_POWER_OFF_DONE,
  GOODIX_BASE_NUM_STATES,
} GoodixBaseSsmState;

typedef struct
{
  GoodixMilanBaseAttempt attempt;
  guint8                 fdt_tx_on_before[GOODIX_FDT_BASE_LEN];
  guint8                 fdt_tx_off[GOODIX_FDT_BASE_LEN];
  guint8                 recovery_fdt_tx_on[GOODIX_FDT_BASE_LEN];
  guint16                recovery_touch_flag;
  gboolean               leave_powered;
} GoodixBaseSsmData;

static void
goodix_base_ssm_data_free (GoodixBaseSsmData *data)
{
  if (!data)
    return;
  goodix_milan_base_attempt_reset (&data->attempt);
  g_free (data);
}

static gboolean
goodix_base_check_cancelled (FpiSsm              *ssm,
                             FpiDeviceGoodix53x5 *self,
                             GoodixBaseSsmData   *data)
{
  if (!self->cancel || !g_cancellable_is_cancelled (self->cancel))
    return FALSE;

  goodix_milan_base_attempt_cancel (&data->attempt);
  fpi_ssm_mark_failed (ssm,
                       g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                            "Milan base acquisition cancelled"));
  return TRUE;
}

static guint16 *
goodix_base_decode_reply (FpDevice     *dev,
                          const gchar  *role,
                          GError      **error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  guint8 category, command;
  const guint8 *payload;
  gsize payload_len, decrypted_len;
  g_autofree guint8 *decrypted = NULL;
  guint16 *frame;

  if (!goodix_parse_reply (dev, &category, &command,
                           &payload, &payload_len, error))
    return NULL;

  decrypted = goodix_crypto_gtls_decrypt_sensor_data (&self->gtls,
                                                       payload, payload_len,
                                                       &decrypted_len);
  if (!decrypted)
    {
      g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                   "%s image decryption failed", role);
      return NULL;
    }

  frame = goodix_device_decode_image (decrypted, decrypted_len);
  if (!frame)
    g_set_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                 "%s image decode failed", role);
  return frame;
}

static gboolean
goodix_base_parse_fdt (FpDevice *dev,
                       guint8    fdt_base[GOODIX_FDT_BASE_LEN],
                       guint16  *touch_flag,
                       GError  **error)
{
  const guint8 *payload;
  gsize payload_len;

  if (!goodix_cmd_parse_fdt_manual_reply (dev, &payload, &payload_len, error))
    return FALSE;
  if (touch_flag)
    *touch_flag = payload[2] | ((guint16) payload[3] << 8);
  memcpy (fdt_base, payload + 4, GOODIX_FDT_BASE_LEN);
  return TRUE;
}

#ifdef GOODIX53X5_DEBUG
static void goodix_base_timing_done (FpiDeviceGoodix53x5 *self,
                                     FpDevice            *dev,
                                     const gchar         *event);
#else
#define goodix_base_timing_done(...) G_STMT_START { } G_STMT_END
#endif

static void
goodix_base_complete_recovery (FpiSsm              *ssm,
                               FpDevice            *dev,
                               GoodixBaseSsmData   *data,
                               const guint8         fdt_tx_on[GOODIX_FDT_BASE_LEN],
                               guint16              touch_flag,
                               const gchar         *checkpoint)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  goodix_milan_base_attempt_reset (&data->attempt);
  goodix_milan_generation_invalidate (&self->milan_generation);
  g_clear_pointer (&self->reference_image, g_free);

  self->fdt_touch_flag = touch_flag;
  g_clear_pointer (&self->fdt_event_data, g_free);
  self->fdt_event_data = g_memdup2 (fdt_tx_on, GOODIX_FDT_BASE_LEN);
  goodix_device_generate_fdt_up_base (self->fdt_event_data,
                                      self->fdt_touch_flag,
                                      &self->calib,
                                      self->calib.fdt_base_up);
  self->milan_base_recovery = (touch_flag & 0x0fff) != 0
                                ? GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER
                                : GOODIX_MILAN_BASE_RECOVERY_RETRY;
  data->leave_powered = TRUE;

  fp_info ("Milan base acquisition needs %s at %s (touch_flag=0x%03x)",
           self->milan_base_recovery ==
             GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER
             ? "finger removal" : "retry",
           checkpoint, touch_flag & 0x0fff);
  goodix_base_timing_done (
    self, dev,
    self->milan_base_recovery == GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER
      ? "remove_finger" : "retry");
  fpi_ssm_mark_completed (ssm);
}

#ifdef GOODIX53X5_DEBUG
static void
goodix_base_timing_done (FpiDeviceGoodix53x5 *self,
                         FpDevice            *dev,
                         const gchar         *event)
{
  const gint64 now_us = g_get_monotonic_time ();

  goodix_debug_timing_log (dev, "ref_capture", event,
                           now_us - self->debug_timing.ref_capture_phase_started_us,
                           NULL);
  if (self->debug_timing.ref_capture_started_us != 0)
    goodix_debug_timing_log (dev, "ref_capture", "total",
                             now_us - self->debug_timing.ref_capture_started_us,
                             NULL);
  self->debug_timing.ref_capture_started_us = 0;
  self->debug_timing.ref_capture_phase_started_us = 0;
}
#endif

static void
goodix_base_ssm_handler (FpiSsm   *ssm,
                         FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixBaseSsmData *data = fpi_ssm_get_data (ssm);
  g_autoptr(GError) error = NULL;

  if (fpi_ssm_get_cur_state (ssm) < GOODIX_BASE_CLEANUP_SLEEP &&
      goodix_base_check_cancelled (ssm, self, data))
    return;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_BASE_EC_POWER_ON:
      GOODIX53X5_DEBUG_ONLY (
      self->debug_timing.ref_capture_started_us = g_get_monotonic_time ();
      self->debug_timing.ref_capture_phase_started_us =
        self->debug_timing.ref_capture_started_us;
      )
      goodix_cmd_ec_control (ssm, dev, TRUE);
      break;

    case GOODIX_BASE_EC_POWER_ON_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          fpi_ssm_mark_failed (ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "Reference EC power-on failed"));
          return;
        }
      goodix_debug_timing_log (dev, "ref_capture", "ec_power_on",
                               g_get_monotonic_time () -
                               self->debug_timing.ref_capture_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.ref_capture_phase_started_us =
          g_get_monotonic_time ();)
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_BASE_FDT_TX_ON_BEFORE:
      data->attempt.stage = GOODIX_MILAN_BASE_STAGE_FDT_TX_ON_BEFORE;
      goodix_cmd_fdt_manual (ssm, dev, TRUE, self->calib.fdt_base_manual);
      break;

    case GOODIX_BASE_FDT_TX_ON_BEFORE_DONE:
      if (!goodix_base_parse_fdt (dev, data->fdt_tx_on_before, NULL, &error))
        {
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
          return;
        }
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_BASE_CAPTURE_TX_ON:
      data->attempt.stage = GOODIX_MILAN_BASE_STAGE_CAPTURE_TX_ON;
      goodix_cmd_request_image (ssm, dev, TRUE, TRUE, FALSE,
                                self->calib.dac_l);
      break;

    case GOODIX_BASE_CAPTURE_TX_ON_DONE:
      {
        g_autofree guint16 *frame =
          goodix_base_decode_reply (dev, "TX-on base", &error);

        if (!frame)
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        goodix_milan_base_attempt_take_frame (&data->attempt, TRUE, &frame,
                                              GOODIX_SENSOR_PIXELS);
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_BASE_FDT_TX_OFF:
      data->attempt.stage = GOODIX_MILAN_BASE_STAGE_FDT_TX_OFF;
      goodix_cmd_fdt_manual (ssm, dev, FALSE, self->calib.fdt_base_manual);
      break;

    case GOODIX_BASE_FDT_TX_OFF_DONE:
      if (!goodix_base_parse_fdt (dev, data->fdt_tx_off, NULL, &error))
        {
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
          return;
        }
      if (!goodix_device_is_fdt_base_valid (data->fdt_tx_on_before,
                                             data->fdt_tx_off,
                                             GOODIX_FDT_BASE_LEN,
                                             self->calib.delta_fdt))
        {
          goodix_milan_generation_invalidate (&self->milan_generation);
          goodix_milan_base_attempt_reset (&data->attempt);
          fpi_ssm_jump_to_state (ssm, GOODIX_BASE_RECOVERY_FDT_TX_ON);
          return;
        }
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_BASE_CAPTURE_TX_OFF:
      data->attempt.stage = GOODIX_MILAN_BASE_STAGE_CAPTURE_TX_OFF;
      goodix_cmd_request_image (ssm, dev, FALSE, TRUE, FALSE,
                                self->calib.dac_l);
      break;

    case GOODIX_BASE_CAPTURE_TX_OFF_DONE:
      {
        g_autofree guint16 *frame =
          goodix_base_decode_reply (dev, "TX-off base", &error);

        if (!frame)
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        goodix_milan_base_attempt_take_frame (&data->attempt, FALSE, &frame,
                                              GOODIX_SENSOR_PIXELS);

        if (goodix_debug_dump_txon_ref_enabled ())
          goodix_debug_dump_raw12 ("raw12-ref-txon", data->attempt.tx_on,
                                   GOODIX_SENSOR_PIXELS);
        goodix_debug_dump_raw12 ("raw12-ref", data->attempt.tx_off,
                                 GOODIX_SENSOR_PIXELS);

        if (!goodix_milan_base_attempt_admit (&data->attempt, &error))
          {
            goodix_milan_generation_invalidate (&self->milan_generation);
            if (error)
              fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            else
              {
                goodix_milan_base_attempt_reset (&data->attempt);
                fpi_ssm_jump_to_state (ssm, GOODIX_BASE_RECOVERY_FDT_TX_ON);
              }
            return;
          }
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_BASE_FDT_TX_ON_AFTER:
      data->attempt.stage = GOODIX_MILAN_BASE_STAGE_FDT_TX_ON_AFTER;
      goodix_cmd_fdt_manual (ssm, dev, TRUE, self->calib.fdt_base_manual);
      break;

    case GOODIX_BASE_FDT_TX_ON_AFTER_DONE:
      {
        guint8 fdt_tx_on_after[GOODIX_FDT_BASE_LEN];
        guint64 generation_id;
        GoodixMilanGeneration *generation = NULL;
        guint16 *legacy_tx_off = NULL;
        guint16 touch_flag;

        if (!goodix_base_parse_fdt (dev, fdt_tx_on_after, &touch_flag, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        if (!goodix_device_is_fdt_base_valid (fdt_tx_on_after,
                                               data->fdt_tx_off,
                                               GOODIX_FDT_BASE_LEN,
                                               self->calib.delta_fdt))
          {
            goodix_base_complete_recovery (
              ssm, dev, data, fdt_tx_on_after, touch_flag, "tx-on-after");
            return;
          }

        /* Native update_allbase derives every FDT base from this first
         * TX-on sample after the complete sequence is admitted. */
        goodix_device_generate_fdt_base (data->fdt_tx_on_before,
                                         GOODIX_FDT_BASE_LEN,
                                         self->calib.fdt_base_down);
        memcpy (self->calib.fdt_base_up, self->calib.fdt_base_down,
                GOODIX_FDT_BASE_LEN);
        memcpy (self->calib.fdt_base_manual, self->calib.fdt_base_down,
                GOODIX_FDT_BASE_LEN);
        data->attempt.stage = GOODIX_MILAN_BASE_STAGE_PUBLISH;
        if (!goodix_milan_generation_allocate_id (&self->last_milan_generation_id,
                                                   &generation_id, &error) ||
            !goodix_milan_base_attempt_publish (&data->attempt, generation_id,
                                                &generation, &legacy_tx_off,
                                                &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        goodix_milan_generation_invalidate (&self->milan_generation);
        self->milan_generation = generation;
        g_clear_pointer (&self->reference_image, g_free);
        self->reference_image = legacy_tx_off;
        fp_info ("Admitted Milan generation id=%" G_GUINT64_FORMAT
                 " subtype=%u MAD=%" G_GUINT64_FORMAT,
                 generation_id, self->milan_sensor_subtype,
                 data->attempt.mad);
        data->leave_powered = TRUE;
        goodix_base_timing_done (self, dev, "base_generation");
        fpi_ssm_mark_completed (ssm);
      }
      break;

    case GOODIX_BASE_RECOVERY_FDT_TX_ON:
      goodix_cmd_fdt_manual (ssm, dev, TRUE, self->calib.fdt_base_manual);
      break;

    case GOODIX_BASE_RECOVERY_FDT_TX_ON_DONE:
      if (!goodix_base_parse_fdt (dev, data->recovery_fdt_tx_on,
                                  &data->recovery_touch_flag, &error))
        {
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
          return;
        }
      goodix_base_complete_recovery (
        ssm, dev, data, data->recovery_fdt_tx_on,
        data->recovery_touch_flag, "tx-on/tx-off");
      break;

    case GOODIX_BASE_CLEANUP_SLEEP:
      if (data->leave_powered)
        fpi_ssm_jump_to_state (ssm, GOODIX_BASE_NUM_STATES);
      else
        goodix_cmd_set_sleep_mode (ssm, dev);
      break;

    case GOODIX_BASE_CLEANUP_EC_POWER_OFF:
      goodix_cmd_ec_control (ssm, dev, FALSE);
      break;

    case GOODIX_BASE_CLEANUP_EC_POWER_OFF_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          fpi_ssm_mark_failed (
            ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                           "Reference EC power-off failed"));
          return;
        }
      goodix_base_timing_done (self, dev, "cleanup");
      fpi_ssm_mark_completed (ssm);
      break;

    case GOODIX_BASE_NUM_STATES:
      g_assert_not_reached ();
    }
}

void
goodix_milan_base_start_ensure_subsm (FpiSsm   *parent_ssm,
                                      FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixBaseSsmData *data;
  FpiSsm *sub;

  if (self->milan_sensor_subtype != GOODIX_MILAN_VALIDATED_SUBTYPE)
    {
      fpi_ssm_mark_failed (parent_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
                                                      "Native Milan subtype invariant failed for chip 0x%08x subtype %u",
                                                      self->chip_id,
                                                      self->milan_sensor_subtype));
      return;
    }

  if (self->milan_generation != NULL)
    {
      fpi_ssm_next_state (parent_ssm);
      return;
    }

  self->milan_base_recovery = GOODIX_MILAN_BASE_RECOVERY_NONE;
  data = g_new0 (GoodixBaseSsmData, 1);
  goodix_milan_base_attempt_init (&data->attempt);
  sub = fpi_ssm_new_full (dev, goodix_base_ssm_handler,
                          GOODIX_BASE_NUM_STATES,
                          GOODIX_BASE_CLEANUP_SLEEP,
                          "goodix-milan-base");
  fpi_ssm_set_data (sub, data, (GDestroyNotify) goodix_base_ssm_data_free);
  fpi_ssm_start_subsm (parent_ssm, sub);
}
