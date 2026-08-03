/*
 * Goodix 53x5 driver for libfprint - native Milan verify/identify flow
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
#include "device/auth.h"
#include "milan/print.h"
#include "driver-private.h"
#include "milan/runtime.h"
#include "device/scan.h"
#include "device/session.h"

typedef enum
{
  GOODIX_VERIFY_REINIT = 0,
  GOODIX_VERIFY_REINIT_DONE,
  GOODIX_VERIFY_CAPTURE_REF,
  GOODIX_VERIFY_CAPTURE_REF_DONE,
  GOODIX_VERIFY_WAIT_FINGER,
  GOODIX_VERIFY_CAPTURE,
  GOODIX_VERIFY_MATCH,
  GOODIX_VERIFY_FINISH,
  GOODIX_VERIFY_NUM_STATES,
} GoodixVerifyState;

typedef struct
{
  GoodixMilanRuntimeInput *runtime_input;
  GPtrArray               *originals;
  FpiSsm                  *ssm;
  FpiDeviceAction          action;
  guint64                  action_epoch;
  guint64                  generation_id;
} GoodixAuthTaskData;

static void
goodix_auth_print_unref (gpointer object)
{
  if (object)
    g_object_unref (object);
}

static void
goodix_auth_task_data_free (GoodixAuthTaskData *data)
{
  if (!data)
    return;
  goodix_milan_runtime_input_free (data->runtime_input);
  g_clear_pointer (&data->originals, g_ptr_array_unref);
  g_free (data);
}

void
goodix_clear_pending_result_report (FpiDeviceGoodix53x5 *self)
{
  self->pending_result_report = FALSE;
  self->pending_result_action = 0;
  self->pending_verify_result = 0;
  g_clear_object (&self->pending_identify_match);
  self->pending_updated = FALSE;
  g_clear_error (&self->pending_result_error);
  g_clear_error (&self->pending_action_error);
  g_clear_error (&self->pending_learning_error);
}

static void
goodix_queue_action_error (FpiDeviceGoodix53x5 *self,
                           GError              *error)
{
  goodix_clear_pending_result_report (self);
  self->pending_action_error = error;
}

static void
goodix_queue_verify_report (FpiDeviceGoodix53x5 *self,
                             FpiMatchResult       result,
                             gboolean             updated,
                             GError              *error)
{
  goodix_clear_pending_result_report (self);
  self->pending_result_report = TRUE;
  self->pending_result_action = FPI_DEVICE_ACTION_VERIFY;
  self->pending_verify_result = result;
  self->pending_updated = updated;
  self->pending_result_error = error;
}

static void
goodix_queue_identify_report (FpiDeviceGoodix53x5 *self,
                               FpPrint             *match,
                               gboolean             updated,
                               GError              *error)
{
  goodix_clear_pending_result_report (self);
  self->pending_result_report = TRUE;
  self->pending_result_action = FPI_DEVICE_ACTION_IDENTIFY;
  if (match)
    self->pending_identify_match = g_object_ref (match);
  self->pending_updated = updated;
  self->pending_result_error = error;
}

static void
goodix_flush_pending_result_report (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (!self->pending_result_report)
    return;

  self->action_result_reported = TRUE;
  if (self->pending_learning_error)
    fp_warn ("Native Milan learning was discarded after a positive match: %s",
             self->pending_learning_error->message);

  if (self->pending_result_action == FPI_DEVICE_ACTION_IDENTIFY)
    fpi_device_identify_report (
      dev, self->pending_identify_match, NULL,
      g_steal_pointer (&self->pending_result_error));
  else
    fpi_device_verify_report (
      dev, self->pending_verify_result, NULL,
      g_steal_pointer (&self->pending_result_error));

  self->pending_result_report = FALSE;
  self->pending_result_action = 0;
  self->pending_verify_result = 0;
  g_clear_object (&self->pending_identify_match);
  g_clear_error (&self->pending_learning_error);
}

static gboolean
goodix_auth_runtime_cancelled (GoodixMilanRuntimeCheckpoint checkpoint,
                               gsize                        gallery_position,
                               gpointer                     user_data)
{
  (void) checkpoint;
  (void) gallery_position;
  return g_cancellable_is_cancelled (G_CANCELLABLE (user_data));
}

static gboolean
goodix_auth_get_template (FpPrint  *print,
                          GBytes  **template_bytes,
                          GError  **error)
{
  g_autoptr(GVariant) data = NULL;
  FpiPrintType type;

  g_object_get (print, "fpi-type", &type, "fpi-data", &data, NULL);
  if (type != FPI_PRINT_RAW)
    {
      g_set_error_literal (error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_INCOMPATIBLE,
                           "Native Milan requires a raw runtime print");
      return FALSE;
    }
  return goodix_milan_print_parse_data (data, template_bytes, error);
}

static void
goodix_auth_worker (GTask        *task,
                    gpointer      source_object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  GoodixAuthTaskData *data = task_data;
  GoodixMilanRuntimeOutput *output;

  (void) source_object;
  (void) cancellable;
  output = goodix_milan_runtime_run (data->runtime_input);
  g_task_return_pointer (task, output,
                         (GDestroyNotify) goodix_milan_runtime_output_free);
}

static gboolean
goodix_auth_build_update (FpPrint                 *original,
                           GoodixMilanRuntimeOutput *output)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) data = NULL;

  if (!output->final_candidate)
    return FALSE;
  data = goodix_milan_print_build_data (output->final_candidate, &error);
  if (!data)
    {
      if (!output->learning_error)
        output->learning_error = g_steal_pointer (&error);
      return FALSE;
    }
  fpi_print_set_raw_data (original, data);
  return TRUE;
}

#ifdef GOODIX53X5_DEBUG
static void
goodix_auth_set_processed_image (FpiDeviceGoodix53x5    *self,
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
goodix_auth_task_done (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  FpDevice *dev = FP_DEVICE (source_object);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GoodixAuthTaskData *data = g_task_get_task_data (G_TASK (result));
  g_autoptr(GError) task_error = NULL;
  GoodixMilanRuntimeOutput *output = g_task_propagate_pointer (
    G_TASK (result), &task_error);
  gboolean current;

  (void) user_data;
  current = self->milan_task == G_TASK (result) &&
            self->task_ssm == data->ssm &&
            self->action_epoch == data->action_epoch &&
            fpi_device_get_current_action (dev) == data->action &&
            !fpi_device_action_is_cancelled (dev) &&
            self->cancel && !g_cancellable_is_cancelled (self->cancel) &&
            self->milan_generation &&
            self->milan_generation->generation_id == data->generation_id;
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
        goodix_debug_log_runtime_result (dev, 0, output);
      goodix_milan_runtime_output_free (output);
      g_clear_pointer (&self->captured_raw_image, g_free);
      if (self->task_ssm == data->ssm)
        fpi_ssm_mark_failed (
          data->ssm, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                          "Native Milan auth task was invalidated"));
      return;
    }

  if (output->preprocess_state_valid)
    {
      self->milan_generation->state = output->preprocess_state;
      self->milan_profile_state = output->profile_state;
      if (data->action == FPI_DEVICE_ACTION_IDENTIFY)
        goodix_milan_generation_note_identify_prelude (self->milan_generation);
    }
  else if (output->status == GOODIX_MILAN_RUNTIME_RETRY)
    goodix_milan_generation_invalidate (&self->milan_generation);

  GOODIX53X5_DEBUG_ONLY (goodix_auth_set_processed_image (self, output);)
  switch (output->status)
    {
    case GOODIX_MILAN_RUNTIME_MATCH:
      {
        FpPrint *original = output->winner_index < data->originals->len
                              ? g_ptr_array_index (data->originals,
                                                   output->winner_index)
                              : NULL;
        gboolean updated = FALSE;

        if (!original)
          {
            goodix_queue_action_error (
              self, fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
            self->verify_wait_finger_up = FALSE;
            break;
          }
        updated = goodix_auth_build_update (original, output);
        if (data->action == FPI_DEVICE_ACTION_IDENTIFY)
          {
            goodix_debug_dump_probe (data->action, "match",
                                     self->captured_raw_image,
                                     self->captured_image);
            goodix_queue_identify_report (self, original, updated, NULL);
          }
        else
          {
            goodix_debug_dump_probe (data->action, "pass",
                                     self->captured_raw_image,
                                     self->captured_image);
            goodix_queue_verify_report (self, FPI_MATCH_SUCCESS, updated, NULL);
          }
        if (output->learning_error)
          self->pending_learning_error = g_error_copy (output->learning_error);
        self->verify_wait_finger_up = FALSE;
      }
      break;

    case GOODIX_MILAN_RUNTIME_NO_MATCH:
      if (data->action == FPI_DEVICE_ACTION_IDENTIFY)
        {
          goodix_debug_dump_probe (data->action, "miss",
                                   self->captured_raw_image,
                                   self->captured_image);
          goodix_queue_identify_report (self, NULL, FALSE, NULL);
        }
      else
        {
          goodix_debug_dump_probe (data->action, "fail",
                                   self->captured_raw_image,
                                   self->captured_image);
          goodix_queue_verify_report (self, FPI_MATCH_FAIL, FALSE, NULL);
        }
      self->verify_wait_finger_up = TRUE;
      break;

    case GOODIX_MILAN_RUNTIME_RETRY:
      goodix_debug_dump_probe (data->action, "weak",
                               self->captured_raw_image,
                               self->captured_image);
      if (data->action == FPI_DEVICE_ACTION_IDENTIFY)
        goodix_queue_identify_report (
          self, NULL, FALSE,
          fpi_device_retry_new (FP_DEVICE_RETRY_REMOVE_FINGER));
      else
        goodix_queue_verify_report (
          self, FPI_MATCH_ERROR, FALSE,
          fpi_device_retry_new (FP_DEVICE_RETRY_REMOVE_FINGER));
      self->verify_wait_finger_up = TRUE;
      break;

    case GOODIX_MILAN_RUNTIME_INVALID_DATA:
      goodix_queue_action_error (self,
                                 fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
      self->verify_wait_finger_up = FALSE;
      break;

    case GOODIX_MILAN_RUNTIME_CANCELLED:
      g_assert_not_reached ();
    }

  fp_dbg ("Native Milan %s score=%d winner=%u action=%u quality=%d coverage=%d",
          data->action == FPI_DEVICE_ACTION_IDENTIFY ? "identify" : "verify",
          output->score, output->winner_index, output->study_action,
          output->quality, output->coverage);
  goodix_debug_log_runtime_result (dev, 0, output);
  goodix_milan_runtime_output_free (output);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);
  if (self->verify_wait_finger_up)
    goodix_flush_pending_result_report (dev);
  fpi_ssm_next_state (data->ssm);
}

static gboolean
goodix_auth_start_task (FpiSsm   *ssm,
                        FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiDeviceAction action = fpi_device_get_current_action (dev);
  g_autoptr(GPtrArray) runtime_gallery = g_ptr_array_new_with_free_func (
    (GDestroyNotify) goodix_milan_runtime_gallery_input_free);
  GoodixAuthTaskData *task_data = g_new0 (GoodixAuthTaskData, 1);
  GPtrArray *gallery = NULL;
  guint invalid_count = 0;

  task_data->ssm = ssm;
  task_data->action = action;
  task_data->action_epoch = self->action_epoch;
  task_data->generation_id = self->milan_generation->generation_id;

  if (action == FPI_DEVICE_ACTION_VERIFY)
    {
      FpPrint *print = NULL;
      g_autoptr(GBytes) template_bytes = NULL;
      g_autoptr(GError) error = NULL;

      task_data->originals = g_ptr_array_new_with_free_func (
        goodix_auth_print_unref);
      fpi_device_get_verify_data (dev, &print);
      if (!goodix_auth_get_template (print, &template_bytes, &error))
        {
          goodix_auth_task_data_free (task_data);
          goodix_queue_action_error (self,
                                     fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
          self->verify_wait_finger_up = FALSE;
          fpi_ssm_next_state (ssm);
          return FALSE;
        }
      g_ptr_array_add (task_data->originals, g_object_ref (print));
      g_ptr_array_add (runtime_gallery,
                       goodix_milan_runtime_gallery_input_new (0, template_bytes));
    }
  else
    {
      fpi_device_get_identify_data (dev, &gallery);
      task_data->originals = g_ptr_array_new_with_free_func (
        goodix_auth_print_unref);
      g_ptr_array_set_size (task_data->originals, gallery->len);
      for (guint i = 0; i < gallery->len; i++)
        {
          FpPrint *print = g_ptr_array_index (gallery, i);
          g_autoptr(GBytes) template_bytes = NULL;
          g_autoptr(GError) error = NULL;

          if (!goodix_auth_get_template (print, &template_bytes, &error))
            {
              invalid_count++;
              fp_warn ("Skipping invalid Milan identify gallery entry %u", i);
              continue;
            }
          task_data->originals->pdata[i] = g_object_ref (print);
          g_ptr_array_add (runtime_gallery,
                           goodix_milan_runtime_gallery_input_new (
                             i, template_bytes));
        }
      if (gallery->len > 0 && runtime_gallery->len == 0 && invalid_count > 0)
        {
          goodix_auth_task_data_free (task_data);
          goodix_queue_action_error (self,
                                     fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
          self->verify_wait_finger_up = FALSE;
          fpi_ssm_next_state (ssm);
          return FALSE;
        }
    }

  task_data->runtime_input = goodix_milan_runtime_input_new (
    task_data->action_epoch, task_data->generation_id,
    GOODIX_MILAN_PURPOSE_IDENTIFY, &self->milan_generation->state,
    &self->milan_profile_state,
    self->milan_generation->setup_tx_on, self->captured_raw_image,
    self->calib.tcode, self->calib.dac_h, self->calib.dac_l,
    self->milan_sensor_subtype,
    (GoodixMilanRuntimeGalleryInput *const *) runtime_gallery->pdata,
    runtime_gallery->len);
  if (!task_data->runtime_input)
    {
      goodix_auth_task_data_free (task_data);
      fpi_ssm_mark_failed (ssm,
                           fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return FALSE;
    }
  goodix_milan_runtime_input_set_cancel_check (
    task_data->runtime_input, goodix_auth_runtime_cancelled,
    g_object_ref (self->cancel), g_object_unref);
  g_autoptr(GTask) task = g_task_new (dev, self->cancel,
                                      goodix_auth_task_done, NULL);
  g_task_set_task_data (task, task_data,
                        (GDestroyNotify) goodix_auth_task_data_free);
  self->milan_task = g_object_ref (task);
  g_task_run_in_thread (task, goodix_auth_worker);
  return TRUE;
}

static void
goodix_verify_ssm_handler (FpiSsm   *ssm,
                           FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GOODIX_VERIFY_REINIT:
      if (!goodix_maybe_start_reinit_subsm (ssm, dev))
        fpi_ssm_next_state (ssm);
      break;
    case GOODIX_VERIFY_REINIT_DONE:
      goodix_debug_timing_open_done (self, dev, "reinit");
      self->needs_reinit = FALSE;
      fpi_ssm_next_state (ssm);
      break;
    case GOODIX_VERIFY_CAPTURE_REF:
      goodix_scan_start_ref_capture_subsm (ssm, dev);
      break;
    case GOODIX_VERIFY_CAPTURE_REF_DONE:
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
      if (fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_IDENTIFY)
        goodix_queue_identify_report (
          self, NULL, FALSE,
          fpi_device_retry_new (
            self->milan_base_recovery ==
              GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER
              ? FP_DEVICE_RETRY_REMOVE_FINGER : FP_DEVICE_RETRY_GENERAL));
      else
        goodix_queue_verify_report (
          self, FPI_MATCH_ERROR, FALSE,
          fpi_device_retry_new (
            self->milan_base_recovery ==
              GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER
              ? FP_DEVICE_RETRY_REMOVE_FINGER : FP_DEVICE_RETRY_GENERAL));
      goodix_flush_pending_result_report (dev);
      fpi_ssm_jump_to_state (ssm, GOODIX_VERIFY_FINISH);
      break;
    case GOODIX_VERIFY_WAIT_FINGER:
      goodix_scan_start_finger_wait_subsm (ssm, dev);
      break;
    case GOODIX_VERIFY_CAPTURE:
      goodix_scan_start_capture_subsm (ssm, dev);
      break;
    case GOODIX_VERIFY_MATCH:
      (void) goodix_auth_start_task (ssm, dev);
      break;
    case GOODIX_VERIFY_FINISH:
      if (self->verify_wait_finger_up ||
          self->milan_base_recovery ==
            GOODIX_MILAN_BASE_RECOVERY_REMOVE_FINGER)
        goodix_scan_start_finger_up_subsm (ssm, dev);
      else
        goodix_scan_start_deactivate_subsm (ssm, dev);
      break;
    case GOODIX_VERIFY_NUM_STATES:
      g_assert_not_reached ();
    }
}

static void
goodix_verify_ssm_done (FpiSsm   *ssm,
                        FpDevice *dev,
                        GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiDeviceAction action = fpi_device_get_current_action (dev);
  gboolean base_recovery =
    self->milan_base_recovery != GOODIX_MILAN_BASE_RECOVERY_NONE;
  gboolean updated = FALSE;

  self->task_ssm = NULL;
  self->blocking_ssm = NULL;
  g_clear_pointer (&self->reference_image, g_free);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);

  if (error && fpi_ssm_get_cur_state (ssm) >= GOODIX_VERIFY_FINISH &&
      !base_recovery &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      fp_warn ("Post-match cleanup error (non-fatal): %s", error->message);
      g_clear_error (&error);
    }
  if (!error)
    {
      if (self->pending_action_error)
        error = g_steal_pointer (&self->pending_action_error);
      else
        goodix_flush_pending_result_report (dev);
    }
  else
    goodix_clear_pending_result_report (self);

  if (!error)
    updated = self->pending_updated;
  self->pending_updated = FALSE;

  self->action_result_reported = FALSE;
  self->verify_wait_finger_up = FALSE;
  self->milan_base_recovery = GOODIX_MILAN_BASE_RECOVERY_NONE;
  if (!error)
    self->needs_reinit = FALSE;
  else if (goodix_error_indicates_stale_device (error))
    self->needs_reinit = TRUE;
  goodix_debug_timing_action_done (self, dev,
                                   error ? error->message : NULL);
  if (action == FPI_DEVICE_ACTION_IDENTIFY)
    fpi_device_identify_complete_with_update (dev, updated, error);
  else
    fpi_device_verify_complete_with_update (dev, updated, error);
}

void
goodix_auth_start (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiSsm *ssm;

  g_clear_object (&self->cancel);
  self->cancel = g_cancellable_new ();
  goodix_clear_pending_result_report (self);
  self->action_epoch++;
  if (self->action_epoch == 0)
    self->action_epoch++;
  self->action_result_reported = FALSE;
  self->verify_wait_finger_up = FALSE;
  self->milan_base_recovery = GOODIX_MILAN_BASE_RECOVERY_NONE;
  g_clear_pointer (&self->reference_image, g_free);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);
  goodix_debug_timing_action_start (self, dev, NULL);

  ssm = fpi_ssm_new (dev, goodix_verify_ssm_handler,
                     GOODIX_VERIFY_NUM_STATES);
  self->task_ssm = ssm;
  fpi_ssm_start (ssm, goodix_verify_ssm_done);
}
