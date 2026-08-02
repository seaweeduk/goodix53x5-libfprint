/*
 * Goodix 53x5 driver for libfprint — Scan flow (FDT finger detection and capture)
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
#include "goodix53x5-private.h"
#include "goodix53x5-transport.h"
#include "goodix53x5-commands.h"
#include "goodix53x5-calibration.h"
#include "goodix53x5-image.h"
#include "goodix53x5-base.h"
#include "goodix53x5-scan.h"

#include <string.h>

/* Finger-wait SSM (awaiting finger down) */
typedef enum {
  GOODIX_FINGER_WAIT_EC_POWER_ON = 0,
  GOODIX_FINGER_WAIT_EC_POWER_ON_DONE,
  GOODIX_FINGER_WAIT_FDT_DOWN_SETUP,
  GOODIX_FINGER_WAIT_RECV_EVENT,
  GOODIX_FINGER_WAIT_GEN_UP_BASE,
  GOODIX_FINGER_WAIT_FDT_CHECK,
  GOODIX_FINGER_WAIT_VALIDATE,
  GOODIX_FINGER_WAIT_NUM_STATES,
} GoodixFingerWaitState;

/* Capture SSM */
typedef enum {
  GOODIX_CAPTURE_GET_IMAGE = 0,
  GOODIX_CAPTURE_DECRYPT,
  GOODIX_CAPTURE_DECODE,
  GOODIX_CAPTURE_STORE,
  GOODIX_CAPTURE_NUM_STATES,
} GoodixCaptureState;

/* Finger-up SSM (awaiting finger off) */
typedef enum {
  GOODIX_FINGER_UP_FDT_UP_SETUP = 0,
  GOODIX_FINGER_UP_RECV_EVENT,
  GOODIX_FINGER_UP_UPDATE_DOWN_BASE,
  GOODIX_FINGER_UP_SLEEP,
  GOODIX_FINGER_UP_EC_POWER_OFF,
  GOODIX_FINGER_UP_EC_POWER_OFF_DONE,
  GOODIX_FINGER_UP_NUM_STATES,
} GoodixFingerUpState;

/* Deactivate SSM — bounded cleanup after successful verify/identify */
typedef enum {
  GOODIX_DEACTIVATE_SLEEP = 0,
  GOODIX_DEACTIVATE_EC_POWER_OFF,
  GOODIX_DEACTIVATE_EC_POWER_OFF_DONE,
  GOODIX_DEACTIVATE_NUM_STATES,
} GoodixDeactivateState;


/* Forward declarations for the SSM handlers below */
static void goodix_finger_wait_ssm_handler (FpiSsm *ssm, FpDevice *dev);
static void goodix_capture_ssm_handler (FpiSsm *ssm, FpDevice *dev);
static void goodix_finger_up_ssm_handler (FpiSsm *ssm, FpDevice *dev);
static void goodix_deactivate_ssm_handler (FpiSsm *ssm, FpDevice *dev);

/* ========================================================================
 * Finger-wait SSM (waiting for finger down)
 * ======================================================================== */

static void
goodix_finger_wait_ssm_handler (FpiSsm   *ssm,
                                FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GOODIX53X5_DEBUG_ONLY (gint64 now_us = g_get_monotonic_time ();)

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_FINGER_WAIT_EC_POWER_ON:
      GOODIX53X5_DEBUG_ONLY (
      if (self->debug_timing.finger_wait_started_us == 0)
        {
          self->debug_timing.finger_wait_started_us = now_us;
          self->debug_timing.finger_wait_false_event_retries = 0;
        }
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_wait_phase_started_us = now_us;)
      )

      /* Power on sensor (idempotent if already on) */
      fpi_device_report_finger_status_changes (dev,
                                               FP_FINGER_STATUS_NEEDED,
                                               FP_FINGER_STATUS_PRESENT);
      goodix_cmd_ec_control (ssm, dev, TRUE);
      break;

    case GOODIX_FINGER_WAIT_EC_POWER_ON_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          fpi_ssm_mark_failed (ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "EC power-on failed"));
          return;
        }

      goodix_debug_timing_log (dev, "finger_wait", "ec_power_on",
                               now_us - self->debug_timing.finger_wait_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_wait_phase_started_us = now_us;)
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_FINGER_WAIT_FDT_DOWN_SETUP:
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_wait_phase_started_us = now_us;)
      /* Set up finger-down detection (re-arms the sensor) */
      goodix_cmd_fdt_down_setup (ssm, dev, self->calib.fdt_base_down);
      break;

    case GOODIX_FINGER_WAIT_RECV_EVENT:
      goodix_debug_timing_log (dev, "finger_wait", "fdt_down_setup",
                               now_us - self->debug_timing.finger_wait_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_wait_phase_started_us = now_us;)
      /* Wait for FDT DOWN event with cancellable. The shutdown jump target
       * matches the historical RX-callback behavior of jumping to the
       * finger-up SLEEP state index on post-result cancellation. */
      self->blocking_ssm = ssm;
      self->blocking_resume_state = GOODIX_FINGER_WAIT_FDT_DOWN_SETUP;
      self->blocking_shutdown_state = GOODIX_FINGER_UP_SLEEP;
      goodix_recv_start_cancellable (ssm, dev, self->cancel);
      break;

    case GOODIX_FINGER_WAIT_GEN_UP_BASE:
      {
        goodix_debug_timing_log (dev, "finger_wait", "wait_for_event",
                                 now_us - self->debug_timing.finger_wait_phase_started_us,
                                 NULL);
        GOODIX53X5_DEBUG_ONLY (
          self->debug_timing.finger_wait_phase_started_us = now_us;)

        self->blocking_ssm = NULL;
        /* Parse FDT event */
        g_autoptr(GError) error = NULL;
        const guint8 *pl;
        gsize pl_len;

        if (!goodix_cmd_parse_fdt_event (dev, FALSE, &pl, &pl_len, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        /* irq_status = pl[0:2], touch_flag = pl[2:4], fdt_data = pl[4:28] */
        self->fdt_touch_flag = pl[2] | ((guint16) pl[3] << 8);
        g_free (self->fdt_event_data);
        self->fdt_event_data = g_memdup2 (pl + 4, GOODIX_FDT_BASE_LEN);

        /* Generate FDT up base */
        goodix_device_generate_fdt_up_base (self->fdt_event_data,
                                            self->fdt_touch_flag,
                                            &self->calib,
                                            self->calib.fdt_base_up);
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_FINGER_WAIT_FDT_CHECK:
      /* FDT manual TX-off to verify the interrupt moved away from baseline. */
      goodix_cmd_fdt_manual (ssm, dev, FALSE, self->calib.fdt_base_manual);
      break;

    case GOODIX_FINGER_WAIT_VALIDATE:
      {
        /* Parse manual FDT response and check for a false FDT-down event. */
        g_autoptr(GError) error = NULL;
        const guint8 *pl;
        gsize pl_len;

        if (!goodix_cmd_parse_fdt_manual_reply (dev, &pl, &pl_len, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        goodix_debug_timing_log (dev, "finger_wait", "fdt_manual_check",
                                 now_us - self->debug_timing.finger_wait_phase_started_us,
                                 NULL);

        /* If the event and immediate manual reading still match, the interrupt
         * was baseline drift/noise rather than a stable finger-down. */
        if (goodix_device_is_fdt_base_valid (self->fdt_event_data,
                                             pl + 4,
                                             GOODIX_FDT_BASE_LEN,
                                             self->calib.delta_fdt))
          {
            fp_dbg ("False FDT down event detected, retrying finger wait");

            GOODIX53X5_DEBUG_ONLY (
            if (goodix_debug_timing_enabled ())
              {
                g_autofree gchar *detail = NULL;

                self->debug_timing.finger_wait_false_event_retries++;
                detail = g_strdup_printf ("false_event_retries=%u",
                                          self->debug_timing.finger_wait_false_event_retries);
                goodix_debug_timing_log (dev, "finger_wait", "false_event_retry",
                                         now_us - self->debug_timing.finger_wait_started_us,
                                         detail);
              }
            )

            /* Re-arm FDT down detection and wait again */
            fpi_ssm_jump_to_state (ssm, GOODIX_FINGER_WAIT_FDT_DOWN_SETUP);
            return;
          }

        /* Real finger detected! */
        fp_dbg ("Finger detected");

        GOODIX53X5_DEBUG_ONLY (
        if (goodix_debug_timing_enabled ())
          {
            g_autofree gchar *detail = NULL;

            if (self->debug_timing.finger_wait_false_event_retries > 0)
              detail = g_strdup_printf ("false_event_retries=%u",
                                        self->debug_timing.finger_wait_false_event_retries);
            goodix_debug_timing_log (dev, "finger_wait", "total_ready",
                                     now_us - self->debug_timing.finger_wait_started_us,
                                     detail);
            self->debug_timing.finger_wait_started_us = 0;
          }
        )

        fpi_device_report_finger_status_changes (dev,
                                                 FP_FINGER_STATUS_PRESENT,
                                                 FP_FINGER_STATUS_NEEDED);
        fpi_ssm_mark_completed (ssm);
      }
      break;
    }
}

/* finger_wait is used as a sub-SSM — no standalone run/done needed */

/* ========================================================================
 * Capture SSM
 * ======================================================================== */

static void
goodix_capture_ssm_handler (FpiSsm   *ssm,
                            FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GOODIX53X5_DEBUG_ONLY (gint64 now_us = g_get_monotonic_time ();)

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_CAPTURE_GET_IMAGE:
#ifdef GOODIX53X5_DEBUG
      g_clear_pointer (&self->captured_image, g_free);
#endif
      g_clear_pointer (&self->captured_raw_image, g_free);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.capture_started_us = now_us;
        self->debug_timing.capture_phase_started_us = now_us;)
      /* Live TX-on finger frame */
      goodix_cmd_request_image (ssm, dev, TRUE, TRUE, TRUE,
                                self->calib.dac_h);
      break;

    case GOODIX_CAPTURE_DECRYPT:
      {
        guint8 cat, cmd;
        const guint8 *pl;
        gsize pl_len, dec_len;
        guint8 *decrypted;

        goodix_debug_timing_log (dev, "capture", "get_image",
                                 now_us - self->debug_timing.capture_phase_started_us,
                                 NULL);
        GOODIX53X5_DEBUG_ONLY (
          self->debug_timing.capture_phase_started_us = now_us;)

        if (!goodix_parse_reply (dev, &cat, &cmd, &pl, &pl_len, NULL))
          {
            fpi_ssm_mark_failed (ssm,
                                 fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                           "Failed to parse capture response"));
            return;
          }

        decrypted = goodix_crypto_gtls_decrypt_sensor_data (&self->gtls,
                                                             pl, pl_len,
                                                             &dec_len);
        if (decrypted == NULL)
          {
            fpi_ssm_mark_failed (ssm,
                                 fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                           "Capture image decryption failed"));
            return;
          }

        /* Decode and retain the canonical live frame. Native preprocessing is
         * performed exactly once by the bounded runtime worker. */
        {
          guint16 *img12 = goodix_device_decode_image (decrypted, dec_len);

          if (img12 == NULL)
            {
              g_free (decrypted);
              fpi_ssm_mark_failed (ssm,
                                   fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                             "Capture image decode failed"));
              return;
            }

          if (!goodix_milan_replace_raw_frame (&self->captured_raw_image,
                                                &img12,
                                                GOODIX_SENSOR_PIXELS,
                                                NULL))
            {
              g_free (decrypted);
              fpi_ssm_mark_failed (ssm,
                                   fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                             "Invalid canonical live raw frame"));
              return;
            }

          g_free (decrypted);
          g_clear_pointer (&self->reference_image, g_free);
        }

        GOODIX53X5_DEBUG_ONLY (
        if (goodix_debug_timing_enabled ())
          {
            gint64 done_us = g_get_monotonic_time ();

            goodix_debug_timing_log (dev, "capture", "process",
                                     done_us - self->debug_timing.capture_phase_started_us,
                                     NULL);
            if (self->debug_timing.capture_started_us != 0)
              goodix_debug_timing_log (dev, "capture", "total",
                                       done_us - self->debug_timing.capture_started_us,
                                       NULL);
            self->debug_timing.capture_started_us = 0;
          }
        )

        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_CAPTURE_DECODE:
      /* Already decoded in previous state, just advance */
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_CAPTURE_STORE:
      if (self->milan_generation)
        {
          guint64 use = goodix_milan_generation_note_use (self->milan_generation);

          fp_info ("Using Milan generation id=%" G_GUINT64_FORMAT
                   " use=%" G_GUINT64_FORMAT,
                   self->milan_generation->generation_id, use);
        }
      fpi_ssm_mark_completed (ssm);
      break;
    }
}

/* capture is used as a sub-SSM — no standalone run/done needed */

/* ========================================================================
 * Finger-up SSM
 * ======================================================================== */

static void
goodix_finger_up_ssm_handler (FpiSsm   *ssm,
                              FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GOODIX53X5_DEBUG_ONLY (gint64 now_us = g_get_monotonic_time ();)

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_FINGER_UP_FDT_UP_SETUP:
      GOODIX53X5_DEBUG_ONLY (
      if (self->debug_timing.finger_up_started_us == 0)
        self->debug_timing.finger_up_started_us = now_us;
      self->debug_timing.finger_up_phase_started_us = now_us;
      )

      /* Set up finger-up detection before waiting */
      goodix_cmd_fdt_up_setup (ssm, dev, self->calib.fdt_base_up);
      break;

    case GOODIX_FINGER_UP_RECV_EVENT:
      goodix_debug_timing_log (dev, "finger_up", "fdt_up_setup",
                               now_us - self->debug_timing.finger_up_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_up_phase_started_us = now_us;)
      /* If libfprint cancels after the result was reported, the RX callback
       * jumps straight to the sleep/power-off tail of this SSM. */
      self->blocking_ssm = ssm;
      self->blocking_resume_state = GOODIX_FINGER_UP_FDT_UP_SETUP;
      self->blocking_shutdown_state = GOODIX_FINGER_UP_SLEEP;
      goodix_recv_start_cancellable (ssm, dev, self->cancel);
      break;

    case GOODIX_FINGER_UP_UPDATE_DOWN_BASE:
      {
        goodix_debug_timing_log (dev, "finger_up", "wait_for_lift",
                                 now_us - self->debug_timing.finger_up_phase_started_us,
                                 NULL);
        GOODIX53X5_DEBUG_ONLY (
          self->debug_timing.finger_up_phase_started_us = now_us;)

        self->blocking_ssm = NULL;
        /* Parse FDT UP event and update fdt_base_down */
        g_autoptr(GError) error = NULL;
        const guint8 *pl;
        gsize pl_len;

        if (!goodix_cmd_parse_fdt_event (dev, TRUE, &pl, &pl_len, &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }

        goodix_device_generate_fdt_base (pl + 4, GOODIX_FDT_BASE_LEN,
                                         self->calib.fdt_base_down);
        fpi_device_report_finger_status_changes (dev,
                                                 FP_FINGER_STATUS_NONE,
                                                 FP_FINGER_STATUS_PRESENT | FP_FINGER_STATUS_NEEDED);
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_FINGER_UP_SLEEP:
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_up_phase_started_us = now_us;)
      goodix_cmd_set_sleep_mode (ssm, dev);
      break;

    case GOODIX_FINGER_UP_EC_POWER_OFF:
      goodix_debug_timing_log (dev, "finger_up", "sleep",
                               now_us - self->debug_timing.finger_up_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.finger_up_phase_started_us = now_us;)
      goodix_cmd_ec_control (ssm, dev, FALSE);
      break;

    case GOODIX_FINGER_UP_EC_POWER_OFF_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          fpi_ssm_mark_failed (ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "EC power-off failed"));
          return;
        }

      goodix_debug_timing_log (dev, "finger_up", "ec_power_off",
                               now_us - self->debug_timing.finger_up_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
      if (self->debug_timing.finger_up_started_us != 0)
        goodix_debug_timing_log (dev, "finger_up", "total",
                                 now_us - self->debug_timing.finger_up_started_us,
                                 NULL);
      self->debug_timing.finger_up_started_us = 0;
      self->debug_timing.finger_up_phase_started_us = 0;
      )

      fpi_ssm_mark_completed (ssm);
      break;
    }
}

/* finger_up is used as a sub-SSM — no standalone run/done needed */

/* ========================================================================
 * Deactivate SSM
 * ======================================================================== */

static void
goodix_deactivate_ssm_handler (FpiSsm   *ssm,
                               FpDevice *dev)
{
  GOODIX53X5_DEBUG_ONLY (
    FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);)
  GOODIX53X5_DEBUG_ONLY (gint64 now_us = g_get_monotonic_time ();)

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_DEACTIVATE_SLEEP:
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.deactivate_started_us = now_us;
        self->debug_timing.deactivate_phase_started_us = now_us;)
      goodix_cmd_set_sleep_mode (ssm, dev);
      break;

    case GOODIX_DEACTIVATE_EC_POWER_OFF:
      goodix_debug_timing_log (dev, "deactivate", "sleep",
                               now_us - self->debug_timing.deactivate_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
        self->debug_timing.deactivate_phase_started_us = now_us;)
      goodix_cmd_ec_control (ssm, dev, FALSE);
      break;

    case GOODIX_DEACTIVATE_EC_POWER_OFF_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          fpi_ssm_mark_failed (ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "EC power-off failed"));
          return;
        }

      goodix_debug_timing_log (dev, "deactivate", "ec_power_off",
                               now_us - self->debug_timing.deactivate_phase_started_us,
                               NULL);
      GOODIX53X5_DEBUG_ONLY (
      if (self->debug_timing.deactivate_started_us != 0)
        goodix_debug_timing_log (dev, "deactivate", "total",
                                 now_us - self->debug_timing.deactivate_started_us,
                                 NULL);
      self->debug_timing.deactivate_started_us = 0;
      )

      fpi_ssm_mark_completed (ssm);
      break;
    }
}

/* ========================================================================
 * Sub-SSM start wrappers
 * ======================================================================== */

void
goodix_scan_start_ref_capture_subsm (FpiSsm *parent_ssm, FpDevice *dev)
{
  goodix_milan_base_start_ensure_subsm (parent_ssm, dev);
}

void
goodix_scan_start_finger_wait_subsm (FpiSsm *parent_ssm, FpDevice *dev)
{
  FpiSsm *sub = fpi_ssm_new (dev, goodix_finger_wait_ssm_handler,
                             GOODIX_FINGER_WAIT_NUM_STATES);

  fpi_ssm_start_subsm (parent_ssm, sub);
}

void
goodix_scan_start_capture_subsm (FpiSsm *parent_ssm, FpDevice *dev)
{
  FpiSsm *sub = fpi_ssm_new (dev, goodix_capture_ssm_handler,
                             GOODIX_CAPTURE_NUM_STATES);

  fpi_ssm_start_subsm (parent_ssm, sub);
}

void
goodix_scan_start_finger_up_subsm (FpiSsm *parent_ssm, FpDevice *dev)
{
  FpiSsm *sub = fpi_ssm_new_full (dev, goodix_finger_up_ssm_handler,
                                  GOODIX_FINGER_UP_NUM_STATES,
                                  GOODIX_FINGER_UP_SLEEP,
                                  "goodix-finger-up");

  fpi_ssm_start_subsm (parent_ssm, sub);
}

void
goodix_scan_start_deactivate_subsm (FpiSsm *parent_ssm, FpDevice *dev)
{
  FpiSsm *sub = fpi_ssm_new (dev, goodix_deactivate_ssm_handler,
                             GOODIX_DEACTIVATE_NUM_STATES);

  fpi_ssm_start_subsm (parent_ssm, sub);
}
