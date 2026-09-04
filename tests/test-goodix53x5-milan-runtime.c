/*
 * Goodix 53x5 driver for libfprint - current Milan runtime contracts
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "drivers_api.h"
#include "fpi-print.h"
#include "drivers/goodix53x5/device/scan.h"
#include "drivers/goodix53x5/driver-private.h"
#include "drivers/goodix53x5/milan/match/match.h"
#include "drivers/goodix53x5/milan/print.h"
#include "drivers/goodix53x5/milan/runtime.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

#define PIXELS GOODIX_MILAN_SENSOR_PIXELS

void milan_runtime_test_auth_start (FpDevice *dev);
void milan_runtime_test_enroll_start (FpDevice *dev);
void milan_runtime_test_clear_pending_result_report (FpiDeviceGoodix53x5 *self);
GoodixSigfmTemplateStatus milan_runtime_harness_match (GoodixMatchInfo        *probe,
                                                       const guint8           *feature,
                                                       gsize                   feature_len,
                                                       GoodixMilanMatchResult *match_result,
                                                       GBytes                **after_match,
                                                       GoodixStudyQueue       *queue);
GoodixSigfmTemplateStatus milan_runtime_harness_study (GoodixMatchInfo              *probe,
                                                       const guint8                 *feature,
                                                       gsize                         feature_len,
                                                       const GoodixMilanMatchResult *match_result,
                                                       gboolean                      study_eligible,
                                                       GoodixStudyQueue             *queue,
                                                       GBytes                      **after_study,
                                                       GoodixMilanStudyAction       *action);
GoodixMilanEnrollmentAttemptStatus milan_runtime_harness_enrollment_attempt (GoodixMilanEnrollmentTransaction **transaction,
                                                                             GBytes                            *probe_template,
                                                                             guint                             *bad_record_count,
                                                                             guint                             *bad_continue_count,
                                                                             GoodixMilanEnrollmentResult       *enrollment_result);
void milan_runtime_harness_scan_start (FpiSsm                        *parent_ssm,
                                       FpDevice                      *dev,
                                       GoodixScanCaptureReadyCallback capture_ready,
                                       GoodixScanCycleSettledCallback cycle_settled,
                                       gpointer                       user_data);
void milan_runtime_harness_scan_set_disposition (FpDevice             *dev,
                                                 GoodixScanDisposition disposition,
                                                 GError               *error);
gboolean milan_runtime_harness_reinit (FpiSsm   *ssm,
                                       FpDevice *dev);
gboolean milan_runtime_harness_stale_error (const GError *error);

typedef struct
{
  const gint32          *scores;
  GBytes *const         *expected_gallery;
  gsize                  score_count;
  gsize                  match_calls;
  GBytes                *study_candidate;
  gboolean               study_failure;
  gboolean               block_study;
  gboolean               study_entered;
  gboolean               release_study;
  gboolean               cancel_called;
  gsize                  study_calls;
  GoodixMilanStudyAction study_action;
  gboolean               fail_next_combine;
  gsize                  combine_calls;
  GMutex                 mutex;
  GCond                  condition;
} HarnessPlan;

typedef struct
{
  gboolean done;
  gboolean success;
  gboolean matched;
  guint    completions;
  guint    reports;
  FpPrint *match;
  FpPrint *reported_match;
  FpPrint *reported_print;
  GError  *reported_error;
  gboolean updated;
  FpPrint *enrolled;
  GError  *error;
} AsyncResult;

typedef struct
{
  AsyncResult *result;
  guint        calls;
  gint         stage;
  gint         retry_code;
  gint         stages[GOODIX_ENROLL_SAMPLES + 1];
  gint         retry_codes[GOODIX_ENROLL_SAMPLES + 1];
  gboolean     early_publication;
} EnrollProgress;

typedef struct
{
  FpiSsm                        *parent_ssm;
  GoodixScanCaptureReadyCallback capture_ready;
  GoodixScanCycleSettledCallback cycle_settled;
  gpointer                       user_data;
} HarnessScanCoordinator;

static HarnessPlan plan;
static FpiSsm *paused_ssm;
static gboolean pause_before_capture;
static gboolean pause_cycle_settled;
static gboolean capture_enroll_stage_pattern;

static void
generate_frames (guint16 setup[PIXELS],
                 guint16 live[PIXELS],
                 guint   pattern)
{
  gint ramp_column = 54 + (10 - 2 * (gint) (pattern % 5)) % 10;
  gint ramp_radius = pattern >= 9 ? 5 : 4;

  for (guint row = 0; row < GOODIX_MILAN_SENSOR_ROWS; row++)
    for (guint column = 0; column < GOODIX_MILAN_SENSOR_COLUMNS; column++)
      {
        gsize index = (gsize) row * GOODIX_MILAN_SENSOR_COLUMNS + column;
        guint16 baseline = (guint16) (
          0x0700 + row * 3 + column * 2 +
          (((row * 7) ^ (column * 13) ^ (pattern * 11)) & 0x3f));
        gint wrapped = (column * 8 + row * 8 + pattern * 17) % 80;
        gint first = ((gint) column - 32 - (gint) pattern) *
                     ((gint) column - 32 - (gint) pattern) +
                     ((gint) row - 28) * ((gint) row - 28);
        gint second = ((gint) column - 72) * ((gint) column - 72) +
                      ((gint) row - 55 + (gint) pattern) *
                      ((gint) row - 55 + (gint) pattern);
        guint16 delta = wrapped < 40 ? 300 : 1200;

        if (first < 36 || second < 49)
          delta = 1200;
        /* Avoid an all-sharp synthetic score distribution. */
        if (ABS ((gint) row - 44) <= ramp_radius &&
            ABS ((gint) column - ramp_column) <= ramp_radius)
          {
            setup[index] = (guint16) (
              baseline - delta + 800 +
              (ramp_radius - MAX (ABS ((gint) row - 44),
                                  ABS ((gint) column - ramp_column))) * 80);
          }
        else
          {
            setup[index] = baseline;
          }
        live[index] = (guint16) (baseline - delta);
      }
}

static GBytes *
generate_template (guint pattern)
{
  g_autofree GoodixMilanPreprocessState *state = g_new0 (
    GoodixMilanPreprocessState, 1);
  GoodixMilanProfileState profile = { 0 };
  g_autofree guint16 *setup = g_new (guint16, PIXELS);
  g_autofree guint16 *live = g_new (guint16, PIXELS);
  g_autofree guint8 *processed = g_new (guint8, PIXELS);

  g_autoptr(GPtrArray) features = g_ptr_array_new_with_free_func (
    (GDestroyNotify) g_bytes_unref);
  GoodixMatchInfo *info = NULL;
  g_autoptr(GBytes) feature = NULL;
  GBytes *combined;
  gint quality = -1;
  gint coverage = -1;

  generate_frames (setup, live, pattern);
  goodix_milan_preprocess_reset (state);
  g_assert_cmpint (goodix_milan_preprocess (
                     state, &profile, setup, live, GOODIX_MILAN_PURPOSE_ENROLL,
                     processed, &quality, &coverage), ==, 0);
  g_assert_cmpint (goodix_milan_match_extract_native_result (
                     processed, state, live, (guint16) pattern,
                     (guint16) pattern, 0, GOODIX_MILAN_PRINT_SENSOR_TYPE,
                     &info), ==, GOODIX_MILAN_EXTRACTION_OK);
  feature = goodix_milan_match_serialize_template (info);
  g_assert_nonnull (feature);
  g_ptr_array_add (features, g_bytes_ref (feature));
  combined = goodix_milan_match_combine_templates (features);
  g_assert_nonnull (combined);
  g_assert_true (goodix_milan_print_validate_template (combined, NULL, NULL));
  goodix_milan_match_free_info (info);
  return combined;
}

static void
reset_plan (const gint32  *scores,
            GBytes *const *expected_gallery,
            gsize          score_count,
            GBytes        *study_candidate)
{
  g_mutex_lock (&plan.mutex);
  plan.scores = scores;
  plan.expected_gallery = expected_gallery;
  plan.score_count = score_count;
  plan.match_calls = 0;
  plan.study_candidate = study_candidate;
  plan.study_failure = FALSE;
  plan.block_study = FALSE;
  plan.study_entered = FALSE;
  plan.release_study = FALSE;
  plan.cancel_called = FALSE;
  plan.study_calls = 0;
  plan.study_action = GOODIX_MILAN_STUDY_APPEND;
  plan.fail_next_combine = FALSE;
  plan.combine_calls = 0;
  g_mutex_unlock (&plan.mutex);
}

GoodixSigfmTemplateStatus
milan_runtime_harness_match (GoodixMatchInfo        *probe,
                             const guint8           *feature,
                             gsize                   feature_len,
                             GoodixMilanMatchResult *match_result,
                             GBytes                **after_match,
                             GoodixStudyQueue       *queue)
{
  GBytes *expected;
  const guint8 *expected_data;
  gsize expected_size;
  gsize index;
  gint32 score;

  (void) probe;
  (void) queue;
  g_mutex_lock (&plan.mutex);
  index = plan.match_calls++;
  g_assert_cmpuint (index, <, plan.score_count);
  score = plan.scores[index];
  expected = plan.expected_gallery[index];
  g_mutex_unlock (&plan.mutex);

  expected_data = g_bytes_get_data (expected, &expected_size);
  g_assert_cmpmem (feature, feature_len, expected_data, expected_size);
  memset (match_result, 0, sizeof (*match_result));
  match_result->score = score;
  *after_match = g_bytes_new (feature, feature_len);
  return GOODIX_SIGFM_TEMPLATE_OK;
}

GoodixSigfmTemplateStatus
milan_runtime_harness_study (GoodixMatchInfo              *probe,
                             const guint8                 *feature,
                             gsize                         feature_len,
                             const GoodixMilanMatchResult *match_result,
                             gboolean                      study_eligible,
                             GoodixStudyQueue             *queue,
                             GBytes                      **after_study,
                             GoodixMilanStudyAction       *action)
{
  GBytes *candidate;
  gboolean failure;
  GoodixMilanStudyAction study_action;

  (void) probe;
  (void) feature;
  (void) feature_len;
  (void) match_result;
  (void) study_eligible;
  (void) queue;
  g_mutex_lock (&plan.mutex);
  plan.study_calls++;
  plan.study_entered = TRUE;
  g_cond_broadcast (&plan.condition);
  while (plan.block_study && !plan.release_study)
    g_cond_wait (&plan.condition, &plan.mutex);
  failure = plan.study_failure;
  candidate = plan.study_candidate;
  study_action = plan.study_action;
  g_mutex_unlock (&plan.mutex);

  if (failure)
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  if (study_action != GOODIX_MILAN_STUDY_NONE)
    {
      g_assert_nonnull (candidate);
      *after_study = g_bytes_ref (candidate);
    }
  *action = study_action;
  return GOODIX_SIGFM_TEMPLATE_OK;
}

GoodixMilanEnrollmentAttemptStatus
milan_runtime_harness_enrollment_attempt (
  GoodixMilanEnrollmentTransaction **transaction,
  GBytes                            *probe_template,
  guint                             *bad_record_count,
  guint                             *bad_continue_count,
  GoodixMilanEnrollmentResult       *enrollment_result)
{
  gboolean fail;

  g_mutex_lock (&plan.mutex);
  plan.combine_calls++;
  fail = plan.fail_next_combine;
  plan.fail_next_combine = FALSE;
  g_mutex_unlock (&plan.mutex);
  if (fail)
    {
      memset (enrollment_result, 0, sizeof (*enrollment_result));
      if (*bad_continue_count < 3)
        *bad_continue_count = 0;
      return GOODIX_MILAN_ENROLLMENT_RETRY_REMOVE;
    }
  return goodix_milan_enrollment_transaction_attempt (
    transaction, probe_template, bad_record_count, bad_continue_count,
    enrollment_result);
}

static void
milan_runtime_harness_scan_capture (FpDevice               *dev,
                                    HarnessScanCoordinator *coordinator)
{
  static const guint enrollment_patterns[GOODIX_ENROLL_SAMPLES] = {
    0, 12, 4, 16, 20, 8, 2, 14, 6, 18, 10, 22
  };
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  guint pattern = capture_enroll_stage_pattern ?
                  enrollment_patterns[self->enroll_stage] : 0;

  g_assert_nonnull (self->milan_generation);
  g_clear_pointer (&self->captured_raw_image, g_free);
  self->captured_raw_image = g_new (guint16, PIXELS);
  generate_frames (self->milan_generation->setup_tx_on,
                   self->captured_raw_image, pattern);
  goodix_milan_generation_note_use (self->milan_generation);
  coordinator->capture_ready (dev, coordinator->user_data);
}

static void
milan_runtime_harness_scan_handler (FpiSsm   *ssm,
                                    FpDevice *dev)
{
  HarnessScanCoordinator *coordinator = fpi_ssm_get_data (ssm);

  if (pause_before_capture)
    paused_ssm = ssm;
  else
    milan_runtime_harness_scan_capture (dev, coordinator);
}

static void
milan_runtime_harness_scan_done (FpiSsm   *ssm,
                                 FpDevice *dev,
                                 GError   *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  HarnessScanCoordinator *coordinator = fpi_ssm_get_data (ssm);

  if (self->profile9_fdt.owner == ssm)
    self->profile9_fdt.owner = NULL;
  self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_STOPPED;
  if (error)
    fpi_ssm_mark_failed (coordinator->parent_ssm, error);
  else
    fpi_ssm_next_state (coordinator->parent_ssm);
}

void
milan_runtime_harness_scan_start (
  FpiSsm                        *parent_ssm,
  FpDevice                      *dev,
  GoodixScanCaptureReadyCallback capture_ready,
  GoodixScanCycleSettledCallback cycle_settled,
  gpointer                       user_data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  HarnessScanCoordinator *coordinator = g_new0 (HarnessScanCoordinator, 1);
  FpiSsm *ssm;

  coordinator->parent_ssm = parent_ssm;
  coordinator->capture_ready = capture_ready;
  coordinator->cycle_settled = cycle_settled;
  coordinator->user_data = user_data;
  ssm = fpi_ssm_new (dev, milan_runtime_harness_scan_handler, 1);
  fpi_ssm_set_data (ssm, coordinator, g_free);
  self->profile9_fdt.owner = ssm;
  self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_ACTIVE;
  fpi_ssm_start (ssm, milan_runtime_harness_scan_done);
}

void
milan_runtime_harness_scan_set_disposition (
  FpDevice             *dev,
  GoodixScanDisposition disposition,
  GError               *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  FpiSsm *ssm = self->profile9_fdt.owner;
  HarnessScanCoordinator *coordinator;

  g_assert_nonnull (ssm);
  coordinator = fpi_ssm_get_data (ssm);
  if (disposition == GOODIX_SCAN_DISPOSITION_FATAL)
    {
      fpi_ssm_mark_failed (
        ssm, error ? error : fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }
  g_clear_error (&error);
  if (disposition == GOODIX_SCAN_DISPOSITION_CANCELLED)
    {
      fpi_ssm_mark_failed (
        ssm, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                  "Harness scan coordinator cancelled"));
      return;
    }
  if (disposition == GOODIX_SCAN_DISPOSITION_AUTH_SUCCESS)
    {
      if (pause_cycle_settled)
        {
          paused_ssm = ssm;
          return;
        }
      fpi_ssm_mark_completed (ssm);
      return;
    }

  if (coordinator->cycle_settled)
    coordinator->cycle_settled (dev, disposition, coordinator->user_data);
  if (pause_cycle_settled)
    {
      paused_ssm = ssm;
      return;
    }
  if (disposition == GOODIX_SCAN_DISPOSITION_ENROLL_CONTINUE_AFTER_UP)
    milan_runtime_harness_scan_capture (dev, coordinator);
  else
    fpi_ssm_mark_completed (ssm);
}

gboolean
milan_runtime_harness_reinit (FpiSsm   *ssm,
                              FpDevice *dev)
{
  (void) ssm;
  (void) dev;
  return FALSE;
}

gboolean
milan_runtime_harness_stale_error (const GError *error)
{
  (void) error;
  return FALSE;
}

static void
harness_open (FpDevice *device)
{
  fpi_device_open_complete (device, NULL);
}

static void
harness_close (FpDevice *device)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);

  g_clear_object (&self->milan_task);
  g_clear_object (&self->cancel);
  milan_runtime_test_clear_pending_result_report (self);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);
  goodix_milan_generation_invalidate (&self->milan_generation);
  g_clear_pointer (&self->enroll_transaction,
                   goodix_milan_enrollment_transaction_free);
  fpi_device_close_complete (device, NULL);
}

static void
harness_cancel (FpDevice *device)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);

  self->action_epoch++;
  if (self->cancel)
    g_cancellable_cancel (self->cancel);
  g_mutex_lock (&plan.mutex);
  plan.cancel_called = TRUE;
  g_cond_broadcast (&plan.condition);
  g_mutex_unlock (&plan.mutex);
}

static const FpIdEntry harness_ids[] = {
  { .virtual_envvar = "GOODIX53X5_MILAN_RUNTIME_TEST" },
  { .virtual_envvar = NULL },
};

static FpDevice *
new_device (void)
{
  FpDeviceClass *klass = g_type_class_ref (FPI_TYPE_DEVICE_GOODIX53X5);
  FpDevice *device;
  FpiDeviceGoodix53x5 *self;
  g_autofree guint16 *live = g_new (guint16, PIXELS);

  klass->type = FP_DEVICE_TYPE_VIRTUAL;
  klass->id_table = harness_ids;
  klass->open = harness_open;
  klass->close = harness_close;
  klass->verify = milan_runtime_test_auth_start;
  klass->identify = milan_runtime_test_auth_start;
  klass->enroll = milan_runtime_test_enroll_start;
  klass->cancel = harness_cancel;
  device = g_object_new (FPI_TYPE_DEVICE_GOODIX53X5, NULL);
  g_type_class_unref (klass);
  g_assert_true (fp_device_open_sync (device, NULL, NULL));

  self = FPI_DEVICE_GOODIX53X5 (device);
  self->milan_sensor_subtype = GOODIX_MILAN_PRINT_SENSOR_TYPE;
  self->milan_generation = g_new0 (GoodixMilanGeneration, 1);
  self->milan_generation->generation_id = 1;
  self->last_milan_generation_id = 1;
  self->milan_generation->admitted = TRUE;
  self->milan_generation->setup_tx_on = g_new (guint16, PIXELS);
  generate_frames (self->milan_generation->setup_tx_on, live, 0);
  goodix_milan_preprocess_reset (&self->milan_generation->state);
  return device;
}

static void
replace_generation (FpiDeviceGoodix53x5 *self)
{
  guint64 generation_id = self->last_milan_generation_id + 1;
  g_autofree guint16 *live = g_new (guint16, PIXELS);

  goodix_milan_generation_invalidate (&self->milan_generation);
  self->last_milan_generation_id = generation_id;
  self->milan_generation = g_new0 (GoodixMilanGeneration, 1);
  self->milan_generation->generation_id = generation_id;
  self->milan_generation->admitted = TRUE;
  self->milan_generation->setup_tx_on = g_new (guint16, PIXELS);
  generate_frames (self->milan_generation->setup_tx_on, live, 0);
  goodix_milan_preprocess_reset (&self->milan_generation->state);
}

static FpPrint *
make_print (FpDevice *device,
            GBytes   *template_bytes)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) data = goodix_milan_print_build_data (template_bytes,
                                                            &error);
  FpPrint *print = fp_print_new (device);

  g_assert_no_error (error);
  g_assert_nonnull (data);
  g_assert_true (g_variant_is_of_type (data, G_VARIANT_TYPE ("(uuuusay)")));
  fpi_print_set_type (print, FPI_PRINT_RAW);
  g_object_set (print, "fpi-data", data, NULL);
  return g_object_ref_sink (print);
}

static FpPrint *
make_malformed_current_print (FpDevice *device)
{
  static const guint8 malformed[] = "bad";
  GVariant *payload = g_variant_new_fixed_array (
    G_VARIANT_TYPE_BYTE, malformed, sizeof (malformed) - 1, 1);

  g_autoptr(GVariant) data = g_variant_ref_sink (g_variant_new (
                                                   "(uuuus@ay)", 4U, 9U, 12U, 1U, "canonical-zero-v1", payload));
  FpPrint *print = fp_print_new (device);

  fpi_print_set_type (print, FPI_PRINT_RAW);
  g_object_set (print, "fpi-data", data, NULL);
  return g_object_ref_sink (print);
}

static GBytes *
get_print_template (FpPrint *print)
{
  g_autoptr(GVariant) data = NULL;
  g_autoptr(GError) error = NULL;
  GBytes *template_bytes = NULL;

  g_object_get (print, "fpi-data", &data, NULL);
  g_assert_true (goodix_milan_print_parse_data (data, &template_bytes, &error));
  g_assert_no_error (error);
  return template_bytes;
}

static void
match_report (FpDevice *device,
              FpPrint  *match,
              FpPrint  *print,
              gpointer  user_data,
              GError   *error)
{
  AsyncResult *result = user_data;

  (void) device;
  result->reports++;
  g_set_object (&result->reported_match, match);
  g_set_object (&result->reported_print, print);
  g_clear_error (&result->reported_error);
  if (error)
    result->reported_error = g_error_copy (error);
}

static void
verify_done (GObject      *source,
             GAsyncResult *async,
             gpointer      user_data)
{
  AsyncResult *result = user_data;

  result->success = fp_device_verify_finish_with_update (
    FP_DEVICE (source), async, &result->matched, NULL, &result->updated,
    &result->error);
  result->completions++;
  result->done = TRUE;
}

static void
identify_done (GObject      *source,
               GAsyncResult *async,
               gpointer      user_data)
{
  AsyncResult *result = user_data;

  result->success = fp_device_identify_finish_with_update (
    FP_DEVICE (source), async, &result->match, NULL, &result->updated,
    &result->error);
  result->completions++;
  result->done = TRUE;
}

static void
enroll_done (GObject      *source,
             GAsyncResult *async,
             gpointer      user_data)
{
  AsyncResult *result = user_data;

  result->enrolled = fp_device_enroll_finish (
    FP_DEVICE (source), async, &result->error);
  result->success = result->enrolled != NULL;
  result->completions++;
  result->done = TRUE;
}

static void
enroll_progress (FpDevice *device,
                 gint      completed_stages,
                 FpPrint  *print,
                 gpointer  user_data,
                 GError   *error)
{
  EnrollProgress *progress = user_data;
  guint index = progress->calls;

  (void) device;
  (void) print;
  g_assert_cmpuint (index, <, G_N_ELEMENTS (progress->stages));
  if (progress->result && progress->result->completions != 0)
    progress->early_publication = TRUE;
  progress->stages[index] = completed_stages;
  progress->retry_codes[index] = error ? error->code : -1;
  progress->calls++;
  progress->stage = completed_stages;
  progress->retry_code = error ? error->code : -1;
}

static void
wait_done (AsyncResult *result)
{
  while (!result->done)
    g_main_context_iteration (NULL, TRUE);
}

static void
wait_paused (void)
{
  while (!paused_ssm)
    g_main_context_iteration (NULL, TRUE);
}

static void
wait_study (void)
{
  g_mutex_lock (&plan.mutex);
  while (!plan.study_entered)
    g_cond_wait (&plan.condition, &plan.mutex);
  g_mutex_unlock (&plan.mutex);
}

static void
wait_cancelled (void)
{
  g_mutex_lock (&plan.mutex);
  while (!plan.cancel_called)
    {
      g_mutex_unlock (&plan.mutex);
      g_assert_true (g_main_context_iteration (NULL, FALSE));
      g_mutex_lock (&plan.mutex);
    }
  g_mutex_unlock (&plan.mutex);
}

static void
release_study (void)
{
  g_mutex_lock (&plan.mutex);
  plan.release_study = TRUE;
  g_cond_broadcast (&plan.condition);
  g_mutex_unlock (&plan.mutex);
}

static void
clear_result (AsyncResult *result)
{
  g_clear_object (&result->match);
  g_clear_object (&result->reported_match);
  g_clear_object (&result->reported_print);
  g_clear_object (&result->enrolled);
  g_clear_error (&result->reported_error);
  g_clear_error (&result->error);
  memset (result, 0, sizeof (*result));
}

static void
close_device (FpDevice *device)
{
  g_assert_true (fp_device_close_sync (device, NULL, NULL));
}

static void
test_auth_gallery_outcomes (void)
{
  static const gint32 first_positive[] = { -4, 37, 999 };
  static const gint32 all_negative[] = { -4, 0, -7 };
  static const gint32 positive[] = { 37 };

  g_autoptr(FpDevice) device = new_device ();
  g_autoptr(GBytes) stored0 = generate_template (0);
  g_autoptr(GBytes) stored1 = generate_template (1);
  g_autoptr(GBytes) stored2 = generate_template (2);
  g_autoptr(GBytes) update = generate_template (3);
  GBytes *templates[] = { stored0, stored1, stored2 };
  g_autoptr(GPtrArray) gallery = g_ptr_array_new_with_free_func (g_object_unref);
  AsyncResult result = { 0 };

  g_assert_false (g_bytes_equal (stored0, stored1));
  g_assert_false (g_bytes_equal (stored1, stored2));
  g_assert_false (g_bytes_equal (stored1, update));
  for (gsize i = 0; i < G_N_ELEMENTS (templates); i++)
    g_ptr_array_add (gallery, make_print (device, templates[i]));

  reset_plan (first_positive, templates, G_N_ELEMENTS (first_positive), update);
  fp_device_identify (device, gallery, NULL, match_report, &result, NULL,
                      identify_done, &result);
  wait_done (&result);
  g_assert_true (result.success);
  g_assert_true (result.match == g_ptr_array_index (gallery, 1));
  g_assert_true (result.reported_match == result.match);
  g_assert_cmpuint (result.reports, ==, 1);
  g_assert_true (result.updated);
  g_autoptr(GBytes) updated = get_print_template (result.match);
  g_assert_true (g_bytes_equal (updated, update));
  g_autoptr(GBytes) unchanged0 = get_print_template (g_ptr_array_index (gallery, 0));
  g_autoptr(GBytes) unchanged2 = get_print_template (g_ptr_array_index (gallery, 2));
  g_assert_true (g_bytes_equal (unchanged0, stored0));
  g_assert_true (g_bytes_equal (unchanged2, stored2));
  g_assert_cmpuint (plan.match_calls, ==, 2);
  g_assert_cmpuint (plan.study_calls, ==, 1);
  clear_result (&result);
  templates[1] = update;

  reset_plan (all_negative, templates, G_N_ELEMENTS (all_negative), NULL);
  fp_device_identify (device, gallery, NULL, match_report, &result, NULL,
                      identify_done, &result);
  wait_done (&result);
  g_assert_true (result.success);
  g_assert_null (result.match);
  g_assert_false (result.updated);
  g_assert_cmpuint (result.reports, ==, 1);
  g_assert_cmpuint (plan.match_calls, ==, 3);
  g_assert_cmpuint (plan.study_calls, ==, 0);
  clear_result (&result);

  reset_plan (positive, templates, G_N_ELEMENTS (positive), NULL);
  plan.study_action = GOODIX_MILAN_STUDY_NONE;
  fp_device_verify (device, g_ptr_array_index (gallery, 0), NULL,
                    match_report, &result, NULL, verify_done, &result);
  wait_done (&result);
  g_assert_true (result.success);
  g_assert_true (result.matched);
  g_assert_false (result.updated);
  g_autoptr(GBytes) action0 = get_print_template (g_ptr_array_index (gallery, 0));
  g_assert_true (g_bytes_equal (action0, stored0));
  g_assert_cmpuint (plan.study_calls, ==, 1);
  clear_result (&result);

  reset_plan (positive, templates, G_N_ELEMENTS (positive), update);
  plan.study_failure = TRUE;
  g_test_expect_message ("libfprint-goodix53x5", G_LOG_LEVEL_WARNING,
                         "*learning was discarded after a positive match*");
  fp_device_verify (device, g_ptr_array_index (gallery, 0), NULL,
                    match_report, &result, NULL, verify_done, &result);
  wait_done (&result);
  g_test_assert_expected_messages ();
  g_assert_true (result.success);
  g_assert_true (result.matched);
  g_assert_cmpuint (result.reports, ==, 1);
  g_assert_false (result.updated);
  g_assert_cmpuint (plan.match_calls, ==, 1);
  g_assert_cmpuint (plan.study_calls, ==, 1);
  clear_result (&result);
  close_device (device);
}

typedef enum {
  AUTH_PRINT_INVALID,
  AUTH_PRINT_VALID_0,
  AUTH_PRINT_VALID_1,
  AUTH_PRINT_VALID_2,
} AuthPrintKind;

typedef struct
{
  const gchar         *name;
  gboolean             identify;
  const AuthPrintKind *prints;
  gsize                print_count;
  const gint32        *scores;
  gsize                score_count;
  gint                 expected_match_index;
  guint                expected_reports;
  guint                expected_study_calls;
  gint                 expected_error_code;
} AuthPublicationCase;

static void
test_auth_publication_contracts (void)
{
  static const AuthPrintKind mixed_prints[] = {
    AUTH_PRINT_INVALID,
    AUTH_PRINT_VALID_0,
    AUTH_PRINT_INVALID,
    AUTH_PRINT_VALID_1,
    AUTH_PRINT_VALID_2,
  };
  static const AuthPrintKind invalid_prints[] = {
    AUTH_PRINT_INVALID,
    AUTH_PRINT_INVALID,
  };
  static const AuthPrintKind verify_prints[] = { AUTH_PRINT_VALID_0 };
  static const gint32 mixed_scores[] = { -4, 37 };
  static const gint32 no_match_score[] = { 0 };
  static const AuthPublicationCase rows[] = {
    {
      "mixed-identify-first-positive", TRUE,
      mixed_prints, G_N_ELEMENTS (mixed_prints),
      mixed_scores, G_N_ELEMENTS (mixed_scores), 3, 1, 1, -1,
    },
    {
      "all-invalid-identify", TRUE,
      invalid_prints, G_N_ELEMENTS (invalid_prints),
      NULL, 0, -1, 0, 0, FP_DEVICE_ERROR_DATA_INVALID,
    },
    {
      "verify-no-match", FALSE,
      verify_prints, G_N_ELEMENTS (verify_prints),
      no_match_score, G_N_ELEMENTS (no_match_score), -1, 1, 0, -1,
    },
  };

  g_autoptr(GBytes) stored0 = generate_template (0);
  g_autoptr(GBytes) stored1 = generate_template (1);
  g_autoptr(GBytes) stored2 = generate_template (2);
  GBytes *stored[] = { stored0, stored1, stored2 };

  for (gsize row_index = 0; row_index < G_N_ELEMENTS (rows); row_index++)
    {
      const AuthPublicationCase *row = &rows[row_index];
      g_autoptr(FpDevice) device = new_device ();
      g_autoptr(GPtrArray) gallery = g_ptr_array_new_with_free_func (
        g_object_unref);
      GBytes *expected_gallery[G_N_ELEMENTS (mixed_prints)] = { 0 };
      gsize valid_count = 0;
      guint invalid_count = 0;
      AsyncResult result = { 0 };

      for (gsize i = 0; i < row->print_count; i++)
        {
          if (row->prints[i] == AUTH_PRINT_INVALID)
            {
              g_ptr_array_add (gallery, make_malformed_current_print (device));
              invalid_count++;
            }
          else
            {
              GBytes *template_bytes = stored[row->prints[i] - AUTH_PRINT_VALID_0];

              g_ptr_array_add (gallery, make_print (device, template_bytes));
              expected_gallery[valid_count++] = template_bytes;
            }
        }
      g_assert_cmpuint (row->score_count, <=, valid_count);
      reset_plan (row->scores, expected_gallery, row->score_count, NULL);
      if (row->expected_study_calls != 0)
        plan.study_action = GOODIX_MILAN_STUDY_NONE;
      for (guint i = 0; i < invalid_count; i++)
        g_test_expect_message ("libfprint-goodix53x5", G_LOG_LEVEL_WARNING,
                               "*Skipping invalid Milan identify gallery entry*");

      if (row->identify)
        fp_device_identify (device, gallery, NULL, match_report, &result, NULL,
                            identify_done, &result);
      else
        fp_device_verify (device, g_ptr_array_index (gallery, 0), NULL,
                          match_report, &result, NULL, verify_done, &result);
      wait_done (&result);
      if (invalid_count != 0)
        g_test_assert_expected_messages ();

      g_test_message ("auth publication row=%s", row->name);
      g_assert_cmpuint (result.completions, ==, 1);
      g_assert_cmpuint (result.reports, ==, row->expected_reports);
      g_assert_false (result.updated);
      g_assert_cmpuint (plan.match_calls, ==, row->score_count);
      g_assert_cmpuint (plan.study_calls, ==, row->expected_study_calls);
      g_assert_null (result.reported_print);
      g_assert_null (result.reported_error);
      if (row->expected_error_code >= 0)
        {
          g_assert_false (result.success);
          g_assert_error (result.error, FP_DEVICE_ERROR,
                          row->expected_error_code);
          g_assert_null (result.match);
          g_assert_null (result.reported_match);
        }
      else if (row->expected_match_index >= 0)
        {
          FpPrint *expected = g_ptr_array_index (
            gallery, row->expected_match_index);

          g_assert_true (result.success);
          g_assert_true (result.match == expected);
          g_assert_true (result.reported_match == expected);
        }
      else
        {
          g_assert_true (result.success);
          g_assert_false (result.matched);
          g_assert_null (result.match);
          g_assert_null (result.reported_match);
        }
      clear_result (&result);
      close_device (device);
    }
}

static void
test_malformed_current_print (void)
{
  g_autoptr(FpDevice) device = new_device ();
  g_autoptr(FpPrint) print = make_malformed_current_print (device);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);
  AsyncResult result = { 0 };
  guint32 sample_count = self->milan_generation->state.sample_count;

  reset_plan (NULL, NULL, 0, NULL);
  fp_device_verify (device, print, NULL, match_report, &result, NULL,
                    verify_done, &result);
  wait_done (&result);
  g_assert_false (result.success);
  g_assert_error (result.error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_DATA_INVALID);
  g_assert_cmpuint (result.reports, ==, 0);
  g_assert_false (result.updated);
  g_assert_cmpuint (plan.match_calls, ==, 0);
  g_assert_cmpuint (plan.study_calls, ==, 0);
  g_assert_cmpuint (self->milan_generation->state.sample_count, ==,
                    sample_count);
  clear_result (&result);
  close_device (device);
}

static void
test_cancellation_no_publication (void)
{
  static const gint32 positive[] = { 37 };

  g_autoptr(FpDevice) device = new_device ();
  g_autoptr(GBytes) stored = generate_template (0);
  g_autoptr(GBytes) update = generate_template (1);
  GBytes *templates[] = { stored };
  g_autoptr(FpPrint) print = make_print (device, stored);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);
  AsyncResult result = { 0 };
  guint32 sample_count = self->milan_generation->state.sample_count;
  guint64 action_epoch;

  reset_plan (positive, templates, G_N_ELEMENTS (positive), update);
  pause_before_capture = TRUE;
  g_autoptr(GCancellable) cancel = g_cancellable_new ();
  fp_device_verify (device, print, cancel, match_report, &result, NULL,
                    verify_done, &result);
  wait_paused ();
  g_cancellable_cancel (cancel);
  fpi_ssm_mark_failed (paused_ssm, g_error_new_literal (
                         G_IO_ERROR, G_IO_ERROR_CANCELLED, "early cancellation"));
  paused_ssm = NULL;
  pause_before_capture = FALSE;
  wait_done (&result);
  g_assert_cmpuint (result.reports, ==, 0);
  g_assert_false (result.updated);
  g_assert_cmpuint (plan.match_calls, ==, 0);
  g_assert_cmpuint (self->milan_generation->state.sample_count, ==,
                    sample_count);
  clear_result (&result);

  reset_plan (positive, templates, G_N_ELEMENTS (positive), update);
  plan.block_study = TRUE;
  cancel = g_cancellable_new ();
  fp_device_verify (device, print, cancel, match_report, &result, NULL,
                    verify_done, &result);
  wait_study ();
  action_epoch = self->action_epoch;
  g_cancellable_cancel (cancel);
  wait_cancelled ();
  g_assert_cmpuint (self->action_epoch, ==, action_epoch + 1);
  release_study ();
  wait_done (&result);
  g_assert_cmpuint (result.reports, ==, 0);
  g_assert_false (result.updated);
  g_assert_cmpuint (plan.match_calls, ==, 1);
  g_assert_cmpuint (plan.study_calls, ==, 1);
  g_assert_cmpuint (self->milan_generation->state.sample_count, ==,
                    sample_count);
  clear_result (&result);

  reset_plan (positive, templates, G_N_ELEMENTS (positive), update);
  pause_cycle_settled = TRUE;
  cancel = g_cancellable_new ();
  fp_device_verify (device, print, cancel, match_report, &result, NULL,
                    verify_done, &result);
  wait_paused ();
  g_assert_true (self->pending_result_report);
  g_assert_true (self->pending_update_target == print);
  g_assert_nonnull (self->pending_update_data);
  g_autoptr(GBytes) pending = get_print_template (print);
  g_assert_true (g_bytes_equal (pending, stored));
  g_cancellable_cancel (cancel);
  wait_cancelled ();
  fpi_ssm_mark_failed (paused_ssm, g_error_new_literal (
                         G_IO_ERROR, G_IO_ERROR_CANCELLED, "late cancellation"));
  paused_ssm = NULL;
  pause_cycle_settled = FALSE;
  wait_done (&result);
  g_assert_cmpuint (result.reports, ==, 0);
  g_assert_false (result.updated);
  g_autoptr(GBytes) unchanged = get_print_template (print);
  g_assert_true (g_bytes_equal (unchanged, stored));
  g_assert_false (self->pending_result_report);
  g_assert_null (self->pending_update_target);
  g_assert_null (self->pending_update_data);
  clear_result (&result);
  close_device (device);
}

static void
test_enrollment_combine_retry (void)
{
  g_autoptr(FpDevice) device = new_device ();
  g_autoptr(FpPrint) print = g_object_ref_sink (fp_print_new (device));
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);
  AsyncResult result = { 0 };
  EnrollProgress progress = { 0 };

  reset_plan (NULL, NULL, 0, NULL);
  plan.fail_next_combine = TRUE;
  pause_cycle_settled = TRUE;
  fp_device_enroll (device, print, NULL, enroll_progress, &progress, NULL,
                    enroll_done, &result);
  wait_paused ();
  g_assert_cmpuint (plan.combine_calls, ==, 1);
  g_assert_cmpuint (progress.calls, ==, 1);
  g_assert_cmpint (progress.stage, ==, 0);
  g_assert_cmpint (progress.retry_code, ==, FP_DEVICE_RETRY_REMOVE_FINGER);
  g_assert_cmpint (self->enroll_stage, ==, 0);
  g_assert_nonnull (self->enroll_transaction);
  g_assert_cmpuint (goodix_milan_enrollment_transaction_count (
                      self->enroll_transaction), ==, 0);
  g_assert_cmpuint (self->enroll_bad_record_count, ==, 0);
  g_assert_cmpuint (self->enroll_bad_continue_count, ==, 0);

  fpi_ssm_mark_failed (paused_ssm, g_error_new_literal (
                         G_IO_ERROR, G_IO_ERROR_CANCELLED, "combine retry observed"));
  paused_ssm = NULL;
  pause_cycle_settled = FALSE;
  wait_done (&result);
  g_assert_false (result.success);
  g_assert_null (result.enrolled);
  clear_result (&result);
  close_device (device);
}

static void
test_complete_enrollment_after_combine_retry (void)
{
  static const gint32 no_match[] = { 0 };

  g_autoptr(FpDevice) device = new_device ();
  g_autoptr(FpPrint) print = g_object_ref_sink (fp_print_new (device));
  g_autoptr(GBytes) stored = generate_template (0);
  GBytes *templates[] = { stored };
  g_autoptr(GPtrArray) gallery = g_ptr_array_new_with_free_func (
    g_object_unref);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);
  AsyncResult result = { 0 };
  EnrollProgress progress = { .result = &result };
  GoodixMilanPrintTemplateInfo info = { 0 };
  GoodixMilanGeneration *generation = self->milan_generation;
  guint64 generation_id = generation->generation_id;
  FpiPrintType print_type;

  g_ptr_array_add (gallery, make_print (device, stored));
  reset_plan (no_match, templates, G_N_ELEMENTS (no_match), NULL);
  fp_device_identify (device, gallery, NULL, match_report, &result, NULL,
                      identify_done, &result);
  wait_done (&result);
  g_assert_true (result.success);
  g_assert_no_error (result.error);
  g_assert_cmpuint (result.completions, ==, 1);
  g_assert_cmpuint (result.reports, ==, 1);
  g_assert_null (result.match);
  g_assert_null (result.reported_match);
  g_assert_null (result.reported_print);
  g_assert_null (result.reported_error);
  g_assert_false (result.updated);
  g_assert_cmpuint (plan.match_calls, ==, 1);
  g_assert_cmpuint (plan.study_calls, ==, 0);
  g_assert_true (self->milan_generation == generation);
  g_assert_cmpuint (self->milan_generation->generation_id, ==, generation_id);
  g_assert_true (self->milan_generation->identify_prelude_seen);
  clear_result (&result);

  reset_plan (NULL, NULL, 0, NULL);
  plan.fail_next_combine = TRUE;
  capture_enroll_stage_pattern = TRUE;
  fp_device_enroll (device, print, NULL, enroll_progress, &progress, NULL,
                    enroll_done, &result);
  wait_done (&result);
  capture_enroll_stage_pattern = FALSE;

  g_assert_true (result.success);
  g_assert_no_error (result.error);
  g_assert_cmpuint (result.completions, ==, 1);
  g_assert_false (progress.early_publication);
  g_assert_cmpuint (progress.calls, ==, GOODIX_ENROLL_SAMPLES + 1);
  g_assert_cmpint (progress.stages[0], ==, 0);
  g_assert_cmpint (progress.retry_codes[0], ==,
                   FP_DEVICE_RETRY_REMOVE_FINGER);
  for (guint i = 1; i < G_N_ELEMENTS (progress.stages); i++)
    {
      g_assert_cmpint (progress.stages[i], ==, i);
      g_assert_cmpint (progress.retry_codes[i], ==, -1);
    }
  g_assert_cmpuint (plan.combine_calls, ==, GOODIX_ENROLL_SAMPLES + 1);
  g_assert_cmpint (self->enroll_stage, ==, GOODIX_ENROLL_SAMPLES);
  g_assert_null (self->enroll_transaction);
  g_assert_true (result.enrolled == print);
  g_object_get (result.enrolled, "fpi-type", &print_type, NULL);
  g_assert_cmpint (print_type, ==, FPI_PRINT_RAW);
  g_autoptr(GBytes) enrolled_template = get_print_template (result.enrolled);
  g_assert_true (goodix_milan_print_validate_template (
                   enrolled_template, &info, NULL));
  g_assert_cmpuint (info.byte_size, ==, g_bytes_get_size (enrolled_template));
  g_assert_cmpuint (info.feature_count, ==, GOODIX_ENROLL_SAMPLES);
  g_assert_cmpuint (info.registration_count, ==, 67);
  g_assert_cmpuint (info.relation_count, ==, 9);
  g_assert_cmpuint (info.graph_established, ==, 1);
  g_assert_cmpint (info.graph_reference_index, ==, 0);
  g_assert_cmpuint (info.maximum_features, ==,
                    GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT);
  g_assert_cmpuint (info.maximum_records, ==, 150);
  g_assert_cmpuint (info.queue_state, ==, 0);
  g_assert_cmpuint (info.queue_transaction_counter, ==, 0);

  clear_result (&result);
  close_device (device);
}

typedef enum {
  STALE_ACTION_EPOCH,
  STALE_GENERATION,
  STALE_TASK_AND_SSM,
} StaleKind;

static void
test_stale_result_guards (void)
{
  static const gint32 positive[] = { 37 };
  static const StaleKind rows[] = {
    STALE_ACTION_EPOCH,
    STALE_GENERATION,
    STALE_TASK_AND_SSM,
  };

  for (gsize row = 0; row < G_N_ELEMENTS (rows); row++)
    {
      g_autoptr(FpDevice) device = new_device ();
      g_autoptr(GBytes) stored = generate_template (0);
      g_autoptr(GBytes) update = generate_template (1);
      GBytes *templates[] = { stored };
      g_autoptr(FpPrint) print = make_print (device, stored);
      FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (device);
      AsyncResult result = { 0 };
      guint32 sample_count = self->milan_generation->state.sample_count;
      guint32 calibration_ready =
        self->milan_generation->profile_state.calibration_ready;
      FpiSsm *original_ssm;
      FpiSsm *replacement_ssm = NULL;
      GTask *replacement_task = NULL;

      reset_plan (positive, templates, G_N_ELEMENTS (positive), update);
      plan.block_study = TRUE;
      fp_device_verify (device, print, NULL, match_report, &result, NULL,
                        verify_done, &result);
      wait_study ();
      original_ssm = self->profile9_fdt.owner;
      if (rows[row] == STALE_ACTION_EPOCH)
        {
          self->action_epoch++;
        }
      else if (rows[row] == STALE_GENERATION)
        {
          replace_generation (self);
          sample_count = self->milan_generation->state.sample_count;
          calibration_ready =
            self->milan_generation->profile_state.calibration_ready;
        }
      else
        {
          replacement_ssm = fpi_ssm_new (device,
                                         milan_runtime_harness_scan_handler, 1);
          replacement_task = g_task_new (device, NULL, NULL, NULL);
          g_clear_object (&self->milan_task);
          self->milan_task = g_object_ref (replacement_task);
          self->profile9_fdt.owner = replacement_ssm;
        }
      release_study ();

      if (rows[row] == STALE_TASK_AND_SSM)
        {
          while (self->captured_raw_image)
            g_main_context_iteration (NULL, TRUE);
          g_clear_object (&self->milan_task);
          g_task_return_boolean (replacement_task, TRUE);
          g_clear_object (&replacement_task);
          self->profile9_fdt.owner = original_ssm;
          fpi_ssm_free (replacement_ssm);
          fpi_ssm_mark_failed (original_ssm, g_error_new_literal (
                                 G_IO_ERROR, G_IO_ERROR_CANCELLED, "task/SSM owner replaced"));
        }
      wait_done (&result);
      g_test_message ("stale row=%u", (guint) rows[row]);
      if (rows[row] == STALE_GENERATION)
        {
          g_autoptr(GBytes) updated = get_print_template (print);

          g_assert_true (result.success);
          g_assert_true (result.matched);
          g_assert_cmpuint (result.reports, ==, 1);
          g_assert_true (result.updated);
          g_assert_true (g_bytes_equal (updated, update));
        }
      else
        {
          g_assert_cmpuint (result.reports, ==, 0);
          g_assert_false (result.updated);
        }
      g_assert_cmpuint (self->milan_generation->state.sample_count, ==,
                        sample_count);
      g_assert_cmpuint (
        self->milan_generation->profile_state.calibration_ready, ==,
        calibration_ready);
      g_assert_false (self->pending_result_report);
      clear_result (&result);
      close_device (device);
    }
}

int
main (int    argc,
      char **argv)
{
  gint status;

  g_test_init (&argc, &argv, NULL);
  g_mutex_init (&plan.mutex);
  g_cond_init (&plan.condition);
  g_test_add_func ("/goodix53x5/milan/runtime/auth-gallery-outcomes",
                   test_auth_gallery_outcomes);
  g_test_add_func ("/goodix53x5/milan/runtime/auth-publication-contracts",
                   test_auth_publication_contracts);
  g_test_add_func ("/goodix53x5/milan/runtime/malformed-current-print",
                   test_malformed_current_print);
  g_test_add_func ("/goodix53x5/milan/runtime/cancellation-no-publication",
                   test_cancellation_no_publication);
  g_test_add_func ("/goodix53x5/milan/runtime/enrollment-combine-retry",
                   test_enrollment_combine_retry);
  g_test_add_func ("/goodix53x5/milan/runtime/complete-enrollment-after-retry",
                   test_complete_enrollment_after_combine_retry);
  g_test_add_func ("/goodix53x5/milan/runtime/stale-result-guards",
                   test_stale_result_guards);
  status = g_test_run ();
  g_cond_clear (&plan.condition);
  g_mutex_clear (&plan.mutex);
  return status;
}
