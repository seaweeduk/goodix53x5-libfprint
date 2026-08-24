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
#include "driver-private.h"
#include "device/transport.h"
#include "device/commands.h"
#include "device/calibration.h"
#include "device/image.h"
#include "device/base.h"
#include "device/scan.h"

#include <string.h>

typedef enum
{
  GOODIX_SCAN_COORD_ENSURE_REFERENCE = 0,
  GOODIX_SCAN_COORD_ENSURE_REFERENCE_DONE,
  GOODIX_SCAN_COORD_POWER_ON,
  GOODIX_SCAN_COORD_POWER_ON_DONE,
  GOODIX_SCAN_COORD_ARM_DOWN,
  GOODIX_SCAN_COORD_WAIT_EVENT,
  GOODIX_SCAN_COORD_DISPATCH_EVENT,
  GOODIX_SCAN_COORD_DOWN_MANUAL,
  GOODIX_SCAN_COORD_DOWN_VALIDATE,
  GOODIX_SCAN_COORD_CAPTURE,
  GOODIX_SCAN_COORD_ARM_UP,
  GOODIX_SCAN_COORD_ARM_UP_DONE,
  GOODIX_SCAN_COORD_RECOVERY_ARM_UP,
  GOODIX_SCAN_COORD_RECOVERY_ARM_UP_DONE,
  GOODIX_SCAN_COORD_REFRESH,
  GOODIX_SCAN_COORD_REFRESH_DONE,
  GOODIX_SCAN_COORD_REARM_DOWN,
  GOODIX_SCAN_COORD_REARM_DOWN_DONE,
  GOODIX_SCAN_COORD_WAIT_CPU,
  GOODIX_SCAN_COORD_CYCLE_SETTLED,
  GOODIX_SCAN_COORD_CLEANUP_JOIN,
  GOODIX_SCAN_COORD_CLEANUP_SLEEP,
  GOODIX_SCAN_COORD_CLEANUP_EC_OFF,
  GOODIX_SCAN_COORD_CLEANUP_EC_OFF_DONE,
  GOODIX_SCAN_COORD_NUM_STATES,
} GoodixScanCoordinatorState;

typedef struct
{
  FpiSsm                       *parent_ssm;
  FpiSsm                       *ssm;
  GoodixScanCaptureReadyCallback capture_ready;
  GoodixScanCycleSettledCallback cycle_settled;
  gpointer                      user_data;
  GCancellable                 *event_cancel;
  GCancellable                 *action_cancel;
  gulong                        action_cancel_id;
  GoodixFdtEventType            event_type;
  GoodixProfile9FdtRefreshReason refresh_reason;
  GoodixProfile9FdtWaitMode     cleanup_drain_mode;
  GoodixScanDisposition         disposition;
  GError                       *stop_error;
  guint16                       prior_down[GOODIX_PROFILE9_FDT_AREA_COUNT];
  gboolean                      receive_active;
  gboolean                      dispatching;
  gboolean                      stop_requested;
  gboolean                      cpu_done;
  gboolean                      cpu_outstanding;
  gboolean                      refresh_deferred;
  gboolean                      cycle_active;
  gboolean                      recovering_generation;
  gboolean                      release_settled;
  gboolean                      capture_notified;
} GoodixScanCoordinatorData;

static void goodix_scan_coordinator_handler (FpiSsm *ssm, FpDevice *dev);
static void goodix_scan_start_capture_subsm (FpiSsm *parent_ssm,
                                              FpDevice *dev);

static void
goodix_scan_coordinator_data_free (GoodixScanCoordinatorData *data)
{
  if (!data)
    return;

  if (data->action_cancel && data->action_cancel_id)
    g_cancellable_disconnect (data->action_cancel, data->action_cancel_id);
  g_clear_object (&data->event_cancel);
  g_clear_object (&data->action_cancel);
  g_clear_error (&data->stop_error);
  g_free (data);
}

static void
goodix_scan_normalize_event (const GoodixProfile9FdtEvent *event,
                             guint16                       values[GOODIX_PROFILE9_FDT_AREA_COUNT])
{
  for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
    {
      guint16 raw = event->raw[i * 2] |
                    ((guint16) event->raw[i * 2 + 1] << 8);

      values[i] = raw >> 1;
    }
}

static gboolean
goodix_scan_majority_changed (const guint16 first[GOODIX_PROFILE9_FDT_AREA_COUNT],
                              const guint16 second[GOODIX_PROFILE9_FDT_AREA_COUNT],
                              guint16       threshold)
{
  guint changed = 0;

  for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
    changed += ABS ((gint) first[i] - (gint) second[i]) > threshold;

  return changed > GOODIX_PROFILE9_FDT_AREA_COUNT / 2;
}

static gboolean
goodix_scan_all_strictly_close (const guint16 first[GOODIX_PROFILE9_FDT_AREA_COUNT],
                                const guint16 second[GOODIX_PROFILE9_FDT_AREA_COUNT],
                                guint16       threshold)
{
  for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
    if (ABS ((gint) first[i] - (gint) second[i]) >= threshold)
      return FALSE;

  return TRUE;
}

static gboolean
goodix_scan_apply_anchor (FpiDeviceGoodix53x5 *self,
                          const guint16         current[GOODIX_PROFILE9_FDT_AREA_COUNT],
                          gboolean              seed_if_empty)
{
  GoodixProfile9FdtState *fdt = &self->profile9_fdt;

  if (fdt->drift_anchor_empty)
    {
      if (seed_if_empty)
        {
          memcpy (fdt->drift_anchor, current, sizeof (fdt->drift_anchor));
          fdt->drift_anchor_empty = FALSE;
        }
      return FALSE;
    }

  if (goodix_scan_majority_changed (fdt->drift_anchor, current,
                                    self->calib.delta_down))
    return TRUE;

  if (goodix_scan_all_strictly_close (fdt->drift_anchor, current,
                                      self->calib.delta_down / 3))
    {
      memset (fdt->drift_anchor, 0, sizeof (fdt->drift_anchor));
      fdt->drift_anchor_empty = TRUE;
    }

  return FALSE;
}

static gboolean
goodix_scan_disposition_waits_for_up (GoodixScanDisposition disposition)
{
  return disposition == GOODIX_SCAN_DISPOSITION_AUTH_RETRY_AFTER_UP ||
         disposition == GOODIX_SCAN_DISPOSITION_ENROLL_CONTINUE_AFTER_UP ||
         disposition == GOODIX_SCAN_DISPOSITION_ENROLL_FINAL_AFTER_UP;
}

static void
goodix_scan_finish_requested (GoodixScanCoordinatorData *data)
{
  if (data->stop_error)
    fpi_ssm_mark_failed (data->ssm, g_steal_pointer (&data->stop_error));
  else
    fpi_ssm_mark_completed (data->ssm);
}

static void
goodix_scan_maybe_finish_requested (GoodixScanCoordinatorData *data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (
    fpi_ssm_get_device (data->ssm));

  if (data->dispatching || data->receive_active || self->rx_active ||
      self->cmd_owner)
    return;
  if (data->cpu_outstanding)
    {
      if (fpi_ssm_get_cur_state (data->ssm) <
            GOODIX_SCAN_COORD_CLEANUP_JOIN &&
          fpi_ssm_get_cur_state (data->ssm) != GOODIX_SCAN_COORD_WAIT_CPU)
        fpi_ssm_jump_to_state (data->ssm, GOODIX_SCAN_COORD_WAIT_CPU);
      return;
    }
  if (data->refresh_deferred)
    {
      data->refresh_deferred = FALSE;
      data->dispatching = TRUE;
      fpi_ssm_jump_to_state (data->ssm, GOODIX_SCAN_COORD_REFRESH);
      return;
    }
  goodix_scan_finish_requested (data);
}

static void
goodix_scan_request_stop (GoodixScanCoordinatorData *data,
                          GError                    *error)
{
  FpDevice *dev = fpi_ssm_get_device (data->ssm);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixScanCoordinatorState state = fpi_ssm_get_cur_state (data->ssm);

  if ((state == GOODIX_SCAN_COORD_WAIT_CPU ||
       state == GOODIX_SCAN_COORD_CYCLE_SETTLED) &&
      self->profile9_fdt.wait_mode != GOODIX_PROFILE9_FDT_WAIT_NONE)
    data->cleanup_drain_mode = self->profile9_fdt.wait_mode;

  if (data->stop_requested)
    {
      if (error && !data->stop_error)
        data->stop_error = error;
      else
        g_clear_error (&error);
      goodix_scan_maybe_finish_requested (data);
      return;
    }

  data->stop_requested = TRUE;
  data->stop_error = error;

  if (data->receive_active && !data->dispatching)
    {
      g_cancellable_cancel (data->event_cancel);
      return;
    }

  goodix_scan_maybe_finish_requested (data);
}

static void
goodix_scan_action_cancelled (GCancellable *cancellable,
                              gpointer      user_data)
{
  GoodixScanCoordinatorData *data = user_data;

  (void) cancellable;
  goodix_scan_request_stop (
    data, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                               "Profile-9 scan action cancelled"));
}

static void
goodix_scan_receive_cancelled (FpiSsm   *ssm,
                               FpDevice *dev,
                               GError   *error,
                               gpointer  user_data)
{
  GoodixScanCoordinatorData *data = user_data;
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  (void) ssm;
  data->receive_active = FALSE;
  g_clear_object (&data->event_cancel);
  g_clear_error (&error);

  if (data->stop_requested &&
      self->profile9_fdt.wait_mode != GOODIX_PROFILE9_FDT_WAIT_NONE)
    data->cleanup_drain_mode = self->profile9_fdt.wait_mode;

  if (!data->stop_requested)
    {
      data->stop_requested = TRUE;
      data->stop_error = fpi_device_error_new_msg (
        FP_DEVICE_ERROR_GENERAL,
        "Profile-9 event receive was cancelled unexpectedly");
    }
  goodix_scan_maybe_finish_requested (data);
}

static gboolean
goodix_scan_begin_receive (GoodixScanCoordinatorData *data,
                           FpDevice                  *dev)
{
  g_assert (!data->receive_active);
  g_clear_object (&data->event_cancel);
  data->event_cancel = g_cancellable_new ();
  data->receive_active = TRUE;
  if (!goodix_recv_start_cancellable_full (
        data->ssm, dev, data->event_cancel,
        goodix_scan_receive_cancelled, data))
    {
      data->receive_active = FALSE;
      g_clear_object (&data->event_cancel);
      return FALSE;
    }
  return TRUE;
}

static void
goodix_scan_prepare_refresh (FpiSsm                        *ssm,
                             FpiDeviceGoodix53x5           *self,
                             GoodixScanCoordinatorData     *data,
                             GoodixProfile9FdtRefreshReason reason)
{
  data->refresh_reason = reason;
  self->profile9_fdt.base_valid = FALSE;
  if (data->cpu_outstanding)
    {
      data->refresh_deferred = TRUE;
      data->dispatching = FALSE;
      /* The matching release was consumed; no FDT arm remains to drain. */
      self->profile9_fdt.wait_mode = GOODIX_PROFILE9_FDT_WAIT_NONE;
      fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_CPU);
    }
  else
    fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_REFRESH);
}

static void
goodix_scan_coordinator_handler (FpiSsm   *ssm,
                                 FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixProfile9FdtState *fdt = &self->profile9_fdt;
  GoodixScanCoordinatorData *data = fpi_ssm_get_data (ssm);

  if (fpi_ssm_get_cur_state (ssm) < GOODIX_SCAN_COORD_CLEANUP_JOIN &&
      fpi_ssm_get_cur_state (ssm) != GOODIX_SCAN_COORD_WAIT_EVENT &&
      data->stop_requested && !data->dispatching && !data->receive_active)
    {
      goodix_scan_maybe_finish_requested (data);
      return;
    }

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_SCAN_COORD_ENSURE_REFERENCE:
      if (self->milan_generation || fdt->initial_recovery_pending)
        fpi_ssm_next_state (ssm);
      else
        goodix_milan_base_start_ensure_subsm (ssm, dev);
      break;

    case GOODIX_SCAN_COORD_ENSURE_REFERENCE_DONE:
      if (self->milan_generation)
        {
          fpi_ssm_next_state (ssm);
          break;
        }
      if (!fdt->initial_recovery_pending || !fdt->event.pending)
        {
          fpi_ssm_mark_failed (
            ssm, fpi_device_error_new_msg (
              FP_DEVICE_ERROR_GENERAL,
              "Milan reference acquisition produced no generation or recovery event"));
          return;
        }

      data->recovering_generation = TRUE;
      goodix_device_generate_fdt_base (fdt->event.raw, GOODIX_FDT_BASE_LEN,
                                       fdt->base_down);
      if ((fdt->event.touch_flag & 0x0fff) != 0)
        {
          goodix_device_generate_fdt_up_base (fdt->event.raw,
                                              fdt->event.touch_flag,
                                              &self->calib, fdt->base_up);
          fpi_device_report_finger_status_changes (
            dev, FP_FINGER_STATUS_PRESENT, FP_FINGER_STATUS_NEEDED);
        }
      else
        fpi_device_report_finger_status_changes (
          dev, FP_FINGER_STATUS_NEEDED, FP_FINGER_STATUS_PRESENT);
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_SCAN_COORD_POWER_ON:
      fpi_device_report_finger_status_changes (dev, FP_FINGER_STATUS_NEEDED,
                                                FP_FINGER_STATUS_PRESENT);
      goodix_cmd_ec_control (ssm, dev, TRUE);
      break;

    case GOODIX_SCAN_COORD_POWER_ON_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          self->needs_reinit = TRUE;
          fpi_ssm_mark_failed (
            ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                           "Scan EC power-on failed"));
          return;
        }
      if (data->recovering_generation &&
          (fdt->event.touch_flag & 0x0fff) != 0)
        fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_RECOVERY_ARM_UP);
      else
        fpi_ssm_next_state (ssm);
      break;

    case GOODIX_SCAN_COORD_ARM_DOWN:
      fdt->wait_mode = GOODIX_PROFILE9_FDT_WAIT_DOWN;
      goodix_cmd_fdt_down_setup (ssm, dev, fdt->base_down);
      break;

    case GOODIX_SCAN_COORD_WAIT_EVENT:
      if (!goodix_scan_begin_receive (data, dev))
        {
          fpi_ssm_mark_failed (
            ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
                                           "Profile-9 event receive ownership conflict"));
          return;
        }
      if (data->stop_requested)
        {
          g_cancellable_cancel (data->event_cancel);
          return;
        }
      if (data->cycle_active && !data->capture_notified)
        {
          if (!self->milan_generation || !self->captured_raw_image)
            {
              data->recovering_generation = TRUE;
              break;
            }
          data->capture_notified = TRUE;
          data->cpu_outstanding = TRUE;
          data->capture_ready (dev, data->user_data);
        }
      break;

    case GOODIX_SCAN_COORD_DISPATCH_EVENT:
      {
        g_autoptr(GError) error = NULL;

        data->receive_active = FALSE;
        g_clear_object (&data->event_cancel);
        if (!goodix_cmd_parse_fdt_event (dev, fdt->wait_mode,
                                         &data->event_type, &fdt->event,
                                         &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        data->dispatching = TRUE;

        if (data->event_type == GOODIX_FDT_EVENT_DOWN)
          {
            if (data->cycle_active)
              {
                if (!data->release_settled)
                  {
                    fpi_ssm_mark_failed (
                      ssm, fpi_device_error_new_msg (
                        FP_DEVICE_ERROR_PROTO,
                        "Finger-down arrived before release was established"));
                    return;
                  }

                /* A new press raced the prior CPU result. Keep the sensor
                 * event-driven by arming up and discarding this too-early
                 * enrollment press only after its matching release. */
                goodix_device_generate_fdt_up_base (
                  fdt->event.raw, fdt->event.touch_flag,
                  &self->calib, fdt->base_up);
                fpi_device_report_finger_status_changes (
                  dev, FP_FINGER_STATUS_PRESENT, FP_FINGER_STATUS_NEEDED);
                fpi_ssm_jump_to_state (
                  ssm, GOODIX_SCAN_COORD_RECOVERY_ARM_UP);
                return;
              }
            goodix_device_generate_fdt_up_base (fdt->event.raw,
                                                fdt->event.touch_flag,
                                                &self->calib, fdt->base_up);
            if (data->recovering_generation || !self->milan_generation)
              {
                data->recovering_generation = TRUE;
                fpi_ssm_jump_to_state (
                  ssm, GOODIX_SCAN_COORD_RECOVERY_ARM_UP);
                return;
              }
            fpi_ssm_next_state (ssm);
            return;
          }

        {
          guint16 current[GOODIX_PROFILE9_FDT_AREA_COUNT];
          gboolean refresh = FALSE;

          goodix_scan_normalize_event (&fdt->event, current);
          if (data->event_type == GOODIX_FDT_EVENT_REVERSE)
            for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
              data->prior_down[i] = fdt->base_down[i * 2 + 1];

          goodix_device_generate_fdt_base (fdt->event.raw,
                                           GOODIX_FDT_BASE_LEN,
                                           fdt->base_down);
          data->release_settled = data->cycle_active;

          if (data->event_type == GOODIX_FDT_EVENT_REVERSE)
            {
              if (!fdt->base_valid)
                {
                  data->refresh_reason = GOODIX_PROFILE9_FDT_REFRESH_INVALID_BASE;
                  refresh = TRUE;
                }
              else if (goodix_scan_majority_changed (
                         data->prior_down, current, self->calib.delta_down) ||
                       goodix_scan_apply_anchor (self, current, TRUE))
                refresh = TRUE;
            }
          else
            {
              if (goodix_scan_apply_anchor (self, current, FALSE))
                refresh = TRUE;
              else if (!fdt->base_valid)
                {
                  data->refresh_reason = GOODIX_PROFILE9_FDT_REFRESH_INVALID_BASE;
                  refresh = TRUE;
                }
            }

          if (data->recovering_generation)
            {
              data->refresh_reason = GOODIX_PROFILE9_FDT_REFRESH_INVALID_BASE;
              refresh = TRUE;
            }

          if (refresh)
            {
              GoodixProfile9FdtRefreshReason reason = data->refresh_reason;

              if (reason == GOODIX_PROFILE9_FDT_REFRESH_NONE)
                reason = data->event_type == GOODIX_FDT_EVENT_REVERSE
                           ? GOODIX_PROFILE9_FDT_REFRESH_REVERSE
                           : GOODIX_PROFILE9_FDT_REFRESH_UP;
              goodix_scan_prepare_refresh (
                ssm, self, data, reason);
              return;
            }
          data->refresh_reason = GOODIX_PROFILE9_FDT_REFRESH_NONE;
          fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_REARM_DOWN);
        }
      }
      break;

    case GOODIX_SCAN_COORD_DOWN_MANUAL:
      goodix_cmd_fdt_manual (ssm, dev, FALSE, fdt->base_manual);
      break;

    case GOODIX_SCAN_COORD_DOWN_VALIDATE:
      {
        g_autoptr(GError) error = NULL;
        const guint8 *payload;
        gsize payload_len;

        if (!goodix_cmd_parse_fdt_manual_reply (dev, &payload, &payload_len,
                                                &error))
          {
            fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
            return;
          }
        if (goodix_device_is_fdt_base_valid (fdt->event.raw, payload + 4,
                                             GOODIX_FDT_BASE_LEN,
                                             self->calib.delta_fdt))
          {
            goodix_scan_prepare_refresh (
              ssm, self, data, GOODIX_PROFILE9_FDT_REFRESH_FALSE_DOWN);
            return;
          }

        fpi_device_report_finger_status_changes (dev, FP_FINGER_STATUS_PRESENT,
                                                  FP_FINGER_STATUS_NEEDED);
        fpi_ssm_next_state (ssm);
      }
      break;

    case GOODIX_SCAN_COORD_CAPTURE:
      goodix_scan_start_capture_subsm (ssm, dev);
      break;

    case GOODIX_SCAN_COORD_ARM_UP:
      data->cycle_active = TRUE;
      data->release_settled = FALSE;
      data->cpu_done = FALSE;
      data->capture_notified = FALSE;
      fdt->wait_mode = GOODIX_PROFILE9_FDT_WAIT_UP;
      goodix_cmd_fdt_up_setup (ssm, dev, fdt->base_up);
      break;

    case GOODIX_SCAN_COORD_ARM_UP_DONE:
      data->dispatching = FALSE;
      fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_EVENT);
      break;

    case GOODIX_SCAN_COORD_RECOVERY_ARM_UP:
      data->dispatching = TRUE;
      fdt->wait_mode = GOODIX_PROFILE9_FDT_WAIT_UP;
      goodix_cmd_fdt_up_setup (ssm, dev, fdt->base_up);
      break;

    case GOODIX_SCAN_COORD_RECOVERY_ARM_UP_DONE:
      data->dispatching = FALSE;
      fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_EVENT);
      break;

    case GOODIX_SCAN_COORD_REFRESH:
      goodix_milan_base_start_forced_refresh_subsm (ssm, dev,
                                                    data->refresh_reason);
      break;

    case GOODIX_SCAN_COORD_REFRESH_DONE:
      /* Validation failure is recoverable: base_valid remains false and the
       * event-derived down base is still the next arm input. */
      data->refresh_reason = GOODIX_PROFILE9_FDT_REFRESH_NONE;
      if (self->milan_generation)
        data->recovering_generation = FALSE;
      fpi_ssm_next_state (ssm);
      break;

    case GOODIX_SCAN_COORD_REARM_DOWN:
      fdt->wait_mode = GOODIX_PROFILE9_FDT_WAIT_DOWN;
      goodix_cmd_fdt_down_setup (ssm, dev, fdt->base_down);
      break;

    case GOODIX_SCAN_COORD_REARM_DOWN_DONE:
      data->dispatching = FALSE;
      fdt->event.pending = FALSE;

      if (data->stop_requested)
        {
          /* Resolve the receive associated with this down arm before issuing
           * shutdown commands, even when stop raced the dispatched handler. */
          fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_EVENT);
          return;
        }
      if (data->release_settled)
        {
          fpi_device_report_finger_status_changes (
            dev, FP_FINGER_STATUS_NONE,
            FP_FINGER_STATUS_PRESENT | FP_FINGER_STATUS_NEEDED);
          if (data->cpu_done)
            fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_CYCLE_SETTLED);
          else
            {
              /* The down arm is one-shot: wait for CPU before posting the
               * next receive instead of posting one only to cancel it. */
              fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_CPU);
            }
          return;
        }
      fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_EVENT);
      break;

    case GOODIX_SCAN_COORD_WAIT_CPU:
      break;

    case GOODIX_SCAN_COORD_CYCLE_SETTLED:
      if (data->disposition == GOODIX_SCAN_DISPOSITION_AUTH_SUCCESS ||
          data->disposition == GOODIX_SCAN_DISPOSITION_AUTH_RETRY_AFTER_UP ||
          data->disposition == GOODIX_SCAN_DISPOSITION_ENROLL_FINAL_AFTER_UP)
        data->cleanup_drain_mode = fdt->wait_mode;
      if (data->cycle_settled)
        data->cycle_settled (dev, data->disposition, data->user_data);
      if (data->stop_requested)
        return;

      if (data->disposition == GOODIX_SCAN_DISPOSITION_ENROLL_CONTINUE_AFTER_UP)
        {
          data->cycle_active = FALSE;
          data->release_settled = FALSE;
          data->cpu_done = FALSE;
          fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_WAIT_EVENT);
        }
      else
        fpi_ssm_mark_completed (ssm);
      break;

    case GOODIX_SCAN_COORD_CLEANUP_JOIN:
      fdt->lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_STOPPING;
      if (data->cpu_outstanding)
        {
          if (data->action_cancel &&
              !g_cancellable_is_cancelled (data->action_cancel))
            g_cancellable_cancel (data->action_cancel);
          break;
        }
      fpi_ssm_mark_completed (ssm);
      break;

    case GOODIX_SCAN_COORD_CLEANUP_SLEEP:
      fdt->wait_mode = GOODIX_PROFILE9_FDT_WAIT_NONE;
      if (data->cleanup_drain_mode != GOODIX_PROFILE9_FDT_WAIT_NONE)
        goodix_cmd_set_sleep_mode_drain_fdt (ssm, dev,
                                             data->cleanup_drain_mode);
      else
        goodix_cmd_set_sleep_mode (ssm, dev);
      break;

    case GOODIX_SCAN_COORD_CLEANUP_EC_OFF:
      goodix_cmd_ec_control (ssm, dev, FALSE);
      break;

    case GOODIX_SCAN_COORD_CLEANUP_EC_OFF_DONE:
      if (!goodix_cmd_parse_ec_control_reply (dev))
        {
          self->needs_reinit = TRUE;
          fpi_ssm_mark_failed (
            ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                           "Scan EC power-off failed"));
          return;
        }
      if (data->stop_error)
        fpi_ssm_mark_failed (ssm, g_steal_pointer (&data->stop_error));
      else
        fpi_ssm_mark_completed (ssm);
      break;

    case GOODIX_SCAN_COORD_NUM_STATES:
      g_assert_not_reached ();
    }
}

static void
goodix_scan_coordinator_done (FpiSsm   *ssm,
                              FpDevice *dev,
                              GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixScanCoordinatorData *data = fpi_ssm_get_data (ssm);

  g_assert (self->profile9_fdt.owner == ssm);
  self->profile9_fdt.owner = NULL;
  self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_STOPPED;
  self->profile9_fdt.wait_mode = GOODIX_PROFILE9_FDT_WAIT_NONE;

  if (error &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    self->needs_reinit = TRUE;

  if (error)
    fpi_ssm_mark_failed (data->parent_ssm, error);
  else
    fpi_ssm_next_state (data->parent_ssm);
}

void
goodix_scan_start_coordinator_subsm (
  FpiSsm                       *parent_ssm,
  FpDevice                     *dev,
  GoodixScanCaptureReadyCallback capture_ready,
  GoodixScanCycleSettledCallback cycle_settled,
  gpointer                      user_data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixScanCoordinatorData *data;
  FpiSsm *ssm;

  g_return_if_fail (capture_ready != NULL);
  if (self->profile9_fdt.owner || self->profile9_fdt.lifecycle !=
                                  GOODIX_PROFILE9_FDT_LIFECYCLE_STOPPED)
    {
      fpi_ssm_mark_failed (
        parent_ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
                                              "Profile-9 scan coordinator is already active"));
      return;
    }

  data = g_new0 (GoodixScanCoordinatorData, 1);
  data->parent_ssm = parent_ssm;
  data->capture_ready = capture_ready;
  data->cycle_settled = cycle_settled;
  data->user_data = user_data;
  data->action_cancel = self->cancel ? g_object_ref (self->cancel) : NULL;
  ssm = fpi_ssm_new_full (dev, goodix_scan_coordinator_handler,
                          GOODIX_SCAN_COORD_NUM_STATES,
                          GOODIX_SCAN_COORD_CLEANUP_JOIN,
                          "goodix-profile9-scan");
  data->ssm = ssm;
  fpi_ssm_set_data (ssm, data,
                    (GDestroyNotify) goodix_scan_coordinator_data_free);
  self->profile9_fdt.owner = ssm;
  self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_ACTIVE;
  fpi_ssm_start (ssm, goodix_scan_coordinator_done);
  if (self->profile9_fdt.owner == ssm && data->action_cancel)
    data->action_cancel_id = g_cancellable_connect (
      data->action_cancel, G_CALLBACK (goodix_scan_action_cancelled), data,
      NULL);
}

void
goodix_scan_set_disposition (FpDevice             *dev,
                             GoodixScanDisposition disposition,
                             GError               *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixScanCoordinatorData *data;

  if (!self->profile9_fdt.owner)
    {
      fp_warn ("Ignoring disposition for an inactive scan coordinator");
      g_clear_error (&error);
      return;
    }

  data = fpi_ssm_get_data (self->profile9_fdt.owner);
  if (!data->cycle_active || data->cpu_done || !data->cpu_outstanding)
    {
      fp_warn ("Ignoring duplicate scan-cycle disposition");
      g_clear_error (&error);
      return;
    }

  data->cpu_done = TRUE;
  data->cpu_outstanding = FALSE;
  data->disposition = disposition;
  if (fpi_ssm_get_cur_state (data->ssm) >= GOODIX_SCAN_COORD_CLEANUP_JOIN)
    {
      g_clear_error (&error);
      fpi_ssm_mark_completed (data->ssm);
      return;
    }
  if (data->stop_requested)
    {
      g_clear_error (&error);
      goodix_scan_maybe_finish_requested (data);
      return;
    }
  if (disposition == GOODIX_SCAN_DISPOSITION_FATAL)
    goodix_scan_request_stop (
      data, error ? error : fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
  else if (disposition == GOODIX_SCAN_DISPOSITION_CANCELLED)
    {
      g_clear_error (&error);
      goodix_scan_request_stop (
        data, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "Profile-9 scan cycle cancelled"));
    }
  else
    {
      g_clear_error (&error);
      if (data->refresh_deferred)
        {
          data->refresh_deferred = FALSE;
          data->dispatching = TRUE;
          fpi_ssm_jump_to_state (data->ssm, GOODIX_SCAN_COORD_REFRESH);
        }
      else if (!goodix_scan_disposition_waits_for_up (disposition))
        goodix_scan_request_stop (data, NULL);
      else if (data->release_settled && !data->dispatching &&
               !data->receive_active && !self->cmd_owner)
        fpi_ssm_jump_to_state (data->ssm,
                               GOODIX_SCAN_COORD_CYCLE_SETTLED);
    }
}

void
goodix_scan_stop_coordinator (FpDevice *dev,
                              GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixScanCoordinatorData *data;

  if (!self->profile9_fdt.owner)
    {
      g_clear_error (&error);
      return;
    }

  data = fpi_ssm_get_data (self->profile9_fdt.owner);
  goodix_scan_request_stop (
    data, error ? error : g_error_new_literal (G_IO_ERROR,
                                                G_IO_ERROR_CANCELLED,
                                                "Profile-9 scan stopped"));
  if (data->cpu_outstanding && data->action_cancel &&
      !g_cancellable_is_cancelled (data->action_cancel))
    g_cancellable_cancel (data->action_cancel);
}

/* Capture SSM */
typedef enum {
  GOODIX_CAPTURE_GET_IMAGE = 0,
  GOODIX_CAPTURE_DECRYPT,
  GOODIX_CAPTURE_DECODE,
  GOODIX_CAPTURE_STORE,
  GOODIX_CAPTURE_NUM_STATES,
} GoodixCaptureState;
static void goodix_capture_ssm_handler (FpiSsm *ssm, FpDevice *dev);

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
        const guint8 *pl;
        gsize pl_len, dec_len;
        guint8 *decrypted;

        goodix_debug_timing_log (dev, "capture", "get_image",
                                 now_us - self->debug_timing.capture_phase_started_us,
                                 NULL);
        GOODIX53X5_DEBUG_ONLY (
          self->debug_timing.capture_phase_started_us = now_us;)

        if (!goodix_cmd_parse_image_reply (dev, &pl, &pl_len, NULL))
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
          GOODIX53X5_DEBUG_ONLY (
          guint64 use = goodix_milan_generation_note_use (self->milan_generation);

          fp_info ("Using Milan generation id=%" G_GUINT64_FORMAT
                   " use=%" G_GUINT64_FORMAT,
                   self->milan_generation->generation_id, use);
          )
        }
      fpi_ssm_mark_completed (ssm);
      break;
    }
}

/* capture is used as a sub-SSM — no standalone run/done needed */

/* ========================================================================
 * Sub-SSM start wrappers
 * ======================================================================== */

static void
goodix_scan_start_capture_subsm (FpiSsm *parent_ssm, FpDevice *dev)
{
  FpiSsm *sub = fpi_ssm_new (dev, goodix_capture_ssm_handler,
                             GOODIX_CAPTURE_NUM_STATES);

  fpi_ssm_start_subsm (parent_ssm, sub);
}
