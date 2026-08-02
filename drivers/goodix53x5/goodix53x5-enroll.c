/*
 * Goodix 53x5 driver for libfprint - native Milan enrollment flow
 * Copyright (C) 2024-2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "fpi-print.h"
#include "goodix53x5-enroll.h"
#include "goodix53x5-match.h"
#include "goodix53x5-print.h"
#include "goodix53x5-private.h"
#include "goodix53x5-runtime.h"
#include "goodix53x5-scan.h"
#include "goodix53x5-session.h"

#define GOODIX_ENROLL_RELEASE_SETTLE_MS 350

typedef enum
{
  GOODIX_ENROLL_REINIT = 0,
  GOODIX_ENROLL_REINIT_DONE,
  GOODIX_ENROLL_CAPTURE_REF,
  GOODIX_ENROLL_CAPTURE_REF_DONE,
  GOODIX_ENROLL_WAIT_FINGER,
  GOODIX_ENROLL_CAPTURE,
  GOODIX_ENROLL_PROCESS,
  GOODIX_ENROLL_WAIT_FINGER_UP,
  GOODIX_ENROLL_NEXT,
  GOODIX_ENROLL_NUM_STATES,
} GoodixEnrollState;

typedef struct
{
  GoodixMilanRuntimeInput *runtime_input;
  FpiSsm                  *ssm;
  guint64                  action_epoch;
  guint64                  generation_id;
  guint                    stage;
} GoodixEnrollTaskData;

static void
goodix_enroll_task_data_free (GoodixEnrollTaskData *data)
{
  if (!data)
    return;
  goodix_milan_runtime_input_free (data->runtime_input);
  g_free (data);
}

static gboolean
goodix_enroll_runtime_cancelled (GoodixMilanRuntimeCheckpoint checkpoint,
                                 gsize                        gallery_position,
                                 gpointer                     user_data)
{
  (void) checkpoint;
  (void) gallery_position;
  return g_cancellable_is_cancelled (G_CANCELLABLE (user_data));
}

static void
goodix_enroll_worker (GTask        *task,
                      gpointer      source_object,
                      gpointer      task_data,
                      GCancellable *cancellable)
{
  GoodixEnrollTaskData *data = task_data;

  (void) source_object;
  (void) cancellable;
  g_task_return_pointer (
    task, goodix_milan_runtime_run (data->runtime_input),
    (GDestroyNotify) goodix_milan_runtime_output_free);
}

#ifdef GOODIX53X5_DEBUG
static void
goodix_enroll_set_processed_image (FpiDeviceGoodix53x5     *self,
                                   GoodixMilanRuntimeOutput *output)
{
  const guint8 *image;
  gsize size;

  g_clear_pointer (&self->captured_image, g_free);
  image = output->processed_image
            ? g_bytes_get_data (output->processed_image, &size) : NULL;
  if (image && size == GOODIX_SENSOR_PIXELS)
    self->captured_image = g_memdup2 (image, size);
}
#endif

static void
goodix_enroll_task_done (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  FpDevice *dev = FP_DEVICE (source_object);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixEnrollTaskData *data = g_task_get_task_data (G_TASK (result));
  g_autoptr(GError) task_error = NULL;
  GoodixMilanRuntimeOutput *output = g_task_propagate_pointer (
    G_TASK (result), &task_error);
  gboolean current;
  gboolean enrollment_admitted;
  gboolean relation_combine_failed = FALSE;

  (void) user_data;
  current = self->milan_task == G_TASK (result) &&
            self->task_ssm == data->ssm &&
            self->action_epoch == data->action_epoch &&
            fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_ENROLL &&
            self->cancel && !g_cancellable_is_cancelled (self->cancel) &&
            self->milan_generation &&
            self->milan_generation->generation_id == data->generation_id &&
            self->enroll_stage == (gint) data->stage;
  if (output &&
      (output->action_epoch != data->action_epoch ||
       output->generation_id != data->generation_id))
    current = FALSE;
  if (self->milan_task == G_TASK (result))
    g_clear_object (&self->milan_task);

  if (!current || !output || task_error ||
      output->status == GOODIX_MILAN_RUNTIME_CANCELLED)
    {
      if (output)
        goodix_debug_log_runtime_result (dev, data->stage + 1, output);
      goodix_milan_runtime_output_free (output);
      g_clear_pointer (&self->captured_raw_image, g_free);
      if (self->task_ssm == data->ssm)
        fpi_ssm_mark_failed (
          data->ssm, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                          "Native Milan enrollment task was invalidated"));
      return;
    }

  if (output->preprocess_state_valid)
    {
      self->milan_generation->state = output->preprocess_state;
      self->milan_profile_state = output->profile_state;
    }
  else
    goodix_milan_generation_invalidate (&self->milan_generation);
  GOODIX53X5_DEBUG_ONLY (goodix_enroll_set_processed_image (self, output);)
  enrollment_admitted = goodix_milan_runtime_enrollment_admitted (output);

  if (enrollment_admitted)
    {
      g_autoptr(GPtrArray) tentative = g_ptr_array_new_with_free_func (
        (GDestroyNotify) g_bytes_unref);
      g_autoptr(GBytes) combined = NULL;

      for (guint i = 0; i < self->enroll_features->len; i++)
        g_ptr_array_add (tentative, g_bytes_ref (
                           g_ptr_array_index (self->enroll_features, i)));
      g_ptr_array_add (tentative, g_bytes_ref (output->probe_template));
      combined = goodix_match_combine_templates (tentative);
      relation_combine_failed = combined == NULL;
      if (relation_combine_failed)
        fp_dbg ("Native Milan enrollment retry stage %u: relation/combine "
                "failed",
                data->stage + 1);
    }

  if (enrollment_admitted && !relation_combine_failed)
    {
      GOODIX53X5_DEBUG_ONLY (
      g_autofree gchar *prefix = g_strdup_printf (
        "enroll-stage-%u", data->stage + 1);

      goodix_debug_dump_pair (prefix, self->captured_raw_image,
                              self->captured_image);
      )
      g_ptr_array_add (self->enroll_features,
                       g_bytes_ref (output->probe_template));
      goodix_milan_generation_note_enrollment_stage (self->milan_generation);
      self->enroll_stage++;
      fp_dbg ("Native Milan enrollment stage %d/%d quality=%d coverage=%d "
              "records=%u partitions=%u/%u",
              self->enroll_stage, GOODIX_ENROLL_SAMPLES, output->quality,
              output->coverage, output->probe_record_count,
              output->probe_partition0_count, output->probe_partition1_count);
      fpi_device_enroll_progress (dev, self->enroll_stage, NULL, NULL);
    }
  else if (relation_combine_failed)
    {
      GOODIX53X5_DEBUG_ONLY (
      g_autofree gchar *prefix = g_strdup_printf (
        "enroll-retry-stage-%u-relation-combine", data->stage + 1);

      goodix_debug_dump_pair (prefix, self->captured_raw_image,
                              self->captured_image);
      )
      fpi_device_enroll_progress (
        dev, self->enroll_stage, NULL,
        fpi_device_retry_new (FP_DEVICE_RETRY_REMOVE_FINGER));
    }
  else if (output->status == GOODIX_MILAN_RUNTIME_RETRY ||
           output->preprocess_state_valid)
    {
      GOODIX53X5_DEBUG_ONLY (
      const gchar *reason = output->status == GOODIX_MILAN_RUNTIME_RETRY
                              ? "extract" : "quality";
      g_autofree gchar *prefix = g_strdup_printf (
        "enroll-retry-stage-%u-%s", data->stage + 1, reason);

      goodix_debug_dump_pair (prefix, self->captured_raw_image,
                              self->captured_image);
      )
      fpi_device_enroll_progress (
        dev, self->enroll_stage, NULL,
        fpi_device_retry_new (output->coverage <= GOODIX_MILAN_ENROLL_MIN_COVERAGE
                                ? FP_DEVICE_RETRY_CENTER_FINGER
                                : FP_DEVICE_RETRY_REMOVE_FINGER));
    }
  else
    {
      GError *error = fpi_device_error_new_msg (
        FP_DEVICE_ERROR_GENERAL, "Native Milan enrollment extraction failed");

      goodix_debug_log_runtime_result (dev, data->stage + 1, output);
      goodix_milan_runtime_output_free (output);
#ifdef GOODIX53X5_DEBUG
      g_clear_pointer (&self->captured_image, g_free);
#endif
      g_clear_pointer (&self->captured_raw_image, g_free);
      fpi_ssm_mark_failed (data->ssm, error);
      return;
    }

  goodix_debug_log_runtime_result (dev, data->stage + 1, output);
  goodix_milan_runtime_output_free (output);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);
  fpi_ssm_next_state (data->ssm);
}

static void
goodix_enroll_start_task (FpiSsm   *ssm,
                          FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixEnrollTaskData *data = g_new0 (GoodixEnrollTaskData, 1);
  g_autoptr(GTask) task = NULL;

  if (!self->milan_generation || !self->captured_raw_image)
    {
      g_free (data);
      fpi_ssm_mark_failed (
        ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                       "Native Milan enrollment input is missing"));
      return;
    }
  data->ssm = ssm;
  data->action_epoch = self->action_epoch;
  data->generation_id = self->milan_generation->generation_id;
  data->stage = self->enroll_stage;
  data->runtime_input = goodix_milan_runtime_input_new (
    data->action_epoch, data->generation_id, GOODIX_MILAN_PURPOSE_ENROLL,
    &self->milan_generation->state, &self->milan_profile_state,
    self->milan_generation->setup_tx_on,
    self->captured_raw_image, self->calib.tcode, self->calib.dac_h,
    self->calib.dac_l, self->milan_sensor_subtype, NULL, 0);
  if (!data->runtime_input)
    {
      goodix_enroll_task_data_free (data);
      fpi_ssm_mark_failed (ssm,
                           fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }
  goodix_milan_runtime_input_set_cancel_check (
    data->runtime_input, goodix_enroll_runtime_cancelled,
    g_object_ref (self->cancel), g_object_unref);
  task = g_task_new (dev, self->cancel, goodix_enroll_task_done, NULL);
  g_task_set_task_data (task, data,
                        (GDestroyNotify) goodix_enroll_task_data_free);
  self->milan_task = g_object_ref (task);
  g_task_run_in_thread (task, goodix_enroll_worker);
}

static void
goodix_enroll_ssm_handler (FpiSsm   *ssm,
                           FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_ENROLL_REINIT:
      if (!goodix_maybe_start_reinit_subsm (ssm, dev))
        fpi_ssm_next_state (ssm);
      break;
    case GOODIX_ENROLL_REINIT_DONE:
      goodix_debug_timing_open_done (self, dev, "reinit");
      self->needs_reinit = FALSE;
      fpi_ssm_next_state (ssm);
      break;
    case GOODIX_ENROLL_CAPTURE_REF:
      if (self->cancel && g_cancellable_is_cancelled (self->cancel))
        fpi_ssm_mark_failed (
          ssm, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                    "Enrollment cancelled"));
      else
        goodix_scan_start_ref_capture_subsm (ssm, dev);
      break;
    case GOODIX_ENROLL_CAPTURE_REF_DONE:
      if (self->milan_base_recovery == GOODIX_MILAN_BASE_RECOVERY_NONE)
        {
          fpi_ssm_next_state (ssm);
          break;
        }

      if (self->milan_base_recovery ==
          GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER)
        fpi_device_report_finger_status_changes (
          dev, FP_FINGER_STATUS_PRESENT, FP_FINGER_STATUS_NEEDED);
      else
        fpi_device_report_finger_status_changes (
          dev, FP_FINGER_STATUS_NEEDED, FP_FINGER_STATUS_PRESENT);
      fpi_device_enroll_progress (
        dev, self->enroll_stage, NULL,
        fpi_device_retry_new (
          self->milan_base_recovery ==
            GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER
            ? FP_DEVICE_RETRY_REMOVE_FINGER : FP_DEVICE_RETRY_GENERAL));
      fpi_ssm_jump_to_state (ssm, GOODIX_ENROLL_WAIT_FINGER_UP);
      break;
    case GOODIX_ENROLL_WAIT_FINGER:
      goodix_scan_start_finger_wait_subsm (ssm, dev);
      break;
    case GOODIX_ENROLL_CAPTURE:
      goodix_scan_start_capture_subsm (ssm, dev);
      break;
    case GOODIX_ENROLL_PROCESS:
      goodix_enroll_start_task (ssm, dev);
      break;
    case GOODIX_ENROLL_WAIT_FINGER_UP:
      if (self->milan_base_recovery ==
          GOODIX_MILAN_BASE_RECOVERY_RETRY)
        goodix_scan_start_deactivate_subsm (ssm, dev);
      else
        goodix_scan_start_finger_up_subsm (ssm, dev);
      break;
    case GOODIX_ENROLL_NEXT:
      self->milan_base_recovery = GOODIX_MILAN_BASE_RECOVERY_NONE;
      if (self->enroll_stage < GOODIX_ENROLL_SAMPLES)
        {
          if (self->cancel && g_cancellable_is_cancelled (self->cancel))
            fpi_ssm_mark_failed (
              ssm, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                        "Enrollment cancelled"));
          else
            fpi_ssm_jump_to_state_delayed (ssm, GOODIX_ENROLL_CAPTURE_REF,
                                           GOODIX_ENROLL_RELEASE_SETTLE_MS);
        }
      else
        fpi_ssm_mark_completed (ssm);
      break;
    case GOODIX_ENROLL_NUM_STATES:
      g_assert_not_reached ();
    }
}

static void
goodix_enroll_ssm_done (FpiSsm   *ssm,
                        FpDevice *dev,
                        GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpPrint *print = NULL;
  g_autoptr(GBytes) combined = NULL;
  g_autoptr(GVariant) data = NULL;

  (void) ssm;
  self->task_ssm = NULL;
  self->blocking_ssm = NULL;
  self->milan_base_recovery = GOODIX_MILAN_BASE_RECOVERY_NONE;
  g_clear_pointer (&self->reference_image, g_free);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);

  if (error)
    {
      if (goodix_error_indicates_stale_device (error))
        self->needs_reinit = TRUE;
      g_clear_pointer (&self->enroll_features, g_ptr_array_unref);
      goodix_debug_timing_action_done (self, dev, error->message);
      fpi_device_enroll_complete (dev, NULL, error);
      return;
    }

  self->needs_reinit = FALSE;
  if (!self->milan_generation ||
      !self->milan_generation->identify_prelude_seen ||
      self->milan_generation->enrollment_stages != GOODIX_ENROLL_SAMPLES ||
      !self->enroll_features ||
      self->enroll_features->len != GOODIX_ENROLL_SAMPLES)
    {
      GError *chronology_error = fpi_device_error_new_msg (
        FP_DEVICE_ERROR_GENERAL,
        "Native Milan enrollment generation chronology is incomplete");

      g_clear_pointer (&self->enroll_features, g_ptr_array_unref);
      fpi_device_enroll_complete (dev, NULL, chronology_error);
      return;
    }

  combined = goodix_match_combine_templates (self->enroll_features);
  if (combined)
    data = goodix_milan_print_build_data (combined, &error);
  if (!combined || !data)
    {
      g_clear_pointer (&self->enroll_features, g_ptr_array_unref);
      if (!error)
        error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                          "Failed to build native Milan template");
      fpi_device_enroll_complete (dev, NULL, error);
      return;
    }

  fpi_device_get_enroll_data (dev, &print);
  fpi_print_set_type (print, FPI_PRINT_RAW);
  g_object_set (print, "fpi-data", data, NULL);
  g_clear_pointer (&self->enroll_features, g_ptr_array_unref);
  fp_info ("Native Milan enrollment complete with %d stages generation=%"
           G_GUINT64_FORMAT, GOODIX_ENROLL_SAMPLES,
           self->milan_generation->generation_id);
  goodix_debug_timing_action_done (self, dev, NULL);
  fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
}

void
goodix_enroll_start (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiSsm *ssm;

  g_clear_object (&self->cancel);
  self->cancel = g_cancellable_new ();
  self->action_epoch++;
  if (self->action_epoch == 0)
    self->action_epoch++;
  self->enroll_stage = 0;
  self->milan_base_recovery = GOODIX_MILAN_BASE_RECOVERY_NONE;
  if (self->milan_generation)
    self->milan_generation->enrollment_stages = 0;
  g_clear_pointer (&self->reference_image, g_free);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);
  g_clear_pointer (&self->enroll_features, g_ptr_array_unref);
  self->enroll_features = g_ptr_array_new_with_free_func (
    (GDestroyNotify) g_bytes_unref);
  goodix_debug_timing_action_start (self, dev, NULL);

  ssm = fpi_ssm_new (dev, goodix_enroll_ssm_handler,
                     GOODIX_ENROLL_NUM_STATES);
  self->task_ssm = ssm;
  fpi_ssm_start (ssm, goodix_enroll_ssm_done);
}
