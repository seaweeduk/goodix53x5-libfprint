/*
 * Goodix 53x5 driver for libfprint - native Milan runtime transaction
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/runtime.h"

#include "milan/study/queue.h"

#include <string.h>

#if defined(GOODIX53X5_DEBUG) || defined(GOODIX53X5_PARITY)
static void
goodix_milan_runtime_hash_bytes (GBytes *bytes,
                                 gchar   digest[65])
{
  gconstpointer data;
  gsize size;
  g_autofree gchar *checksum = NULL;

  digest[0] = '\0';
  if (!bytes)
    return;
  data = g_bytes_get_data (bytes, &size);
  checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, size);
  g_strlcpy (digest, checksum ? checksum : "", 65);
}
#endif

struct _GoodixMilanRuntimeGalleryInput
{
  guint   gallery_index;
  GBytes *template_bytes;
};

struct _GoodixMilanRuntimeInput
{
  guint64                           action_epoch;
  guint64                           generation_id;
  GoodixMilanPreprocessPurpose      purpose;
  GoodixMilanPreprocessState        preprocess_state;
  GoodixMilanProfileState           profile_state;
  guint16                          *setup_tx_on;
  guint16                          *live_raw;
  guint16                           tcode;
  guint16                           dac_high;
  guint16                           dac_low;
  guint16                           sensor_subtype;
  GPtrArray                        *gallery;
  GoodixMilanRuntimeCancelFunc      cancel_func;
  gpointer                          cancel_data;
  GDestroyNotify                    cancel_destroy;
};

static void
goodix_milan_runtime_gallery_result_free (
  GoodixMilanRuntimeGalleryResult *result)
{
  if (!result)
    return;
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&result->input_template, g_bytes_unref);
  g_clear_pointer (&result->after_match_template, g_bytes_unref);
#endif
  g_clear_error (&result->validation_error);
  g_free (result);
}

static gboolean
goodix_milan_runtime_cancelled (const GoodixMilanRuntimeInput *input,
                                GoodixMilanRuntimeOutput      *output,
                                GoodixMilanRuntimeCheckpoint  checkpoint,
                                gsize                          gallery_position)
{
  if (!input->cancel_func ||
      !input->cancel_func (checkpoint, gallery_position, input->cancel_data))
    return FALSE;
  output->status = GOODIX_MILAN_RUNTIME_CANCELLED;
  output->cancellation.cancelled_at = checkpoint;
  output->cancellation.cancelled_gallery_position = gallery_position;
  output->preprocess_state_valid = FALSE;
  g_clear_pointer (&output->final_candidate, g_bytes_unref);
  return TRUE;
}

static gboolean
goodix_milan_runtime_inspect_probe (GBytes                       *template_bytes,
                                    GoodixMilanPrintTemplateInfo *info)
{
  g_autofree GoodixMilanUnpackedTemplate *unpacked = NULL;
  GoodixMilanFeatureView view;
  const guint8 *template_data;
  gsize template_size;

  memset (info, 0, sizeof(*info));
  template_data = template_bytes ? g_bytes_get_data (template_bytes, &template_size) : NULL;
  if (!template_data || template_size > GOODIX_MILAN_TEMPLATE_MAX_SIZE)
    return FALSE;
  unpacked = g_new (GoodixMilanUnpackedTemplate, 1);
  if (goodix_milan_template_unpack (
        template_data, template_size, unpacked) != 0 ||
      unpacked->feature_count != 1 ||
      unpacked->metadata.sensor_type != GOODIX_MILAN_PRINT_SENSOR_TYPE ||
      unpacked->metadata.maximum_features != 1 ||
      unpacked->metadata.maximum_records != 150 ||
      goodix_milan_template_parse_feature_element (
        unpacked->feature_elements[0], unpacked->feature_element_sizes[0],
        &view) != 0 || view.record_count == 0 || view.record_count > 150 ||
      view.fields.tagged_values[2] < 0 ||
      (gsize) view.fields.tagged_values[2] > view.record_count)
    return FALSE;
  info->byte_size = template_size;
  info->feature_count = 1;
  info->partition0_count = (guint32) view.fields.tagged_values[2];
  info->partition1_count = (guint32) view.record_count -
                           info->partition0_count;
  return TRUE;
}

GoodixMilanRuntimeGalleryInput *
goodix_milan_runtime_gallery_input_new (guint   gallery_index,
                                        GBytes *template_bytes)
{
  GoodixMilanRuntimeGalleryInput *input;

  g_return_val_if_fail (template_bytes != NULL, NULL);
  input = g_new0 (GoodixMilanRuntimeGalleryInput, 1);
  input->gallery_index = gallery_index;
  input->template_bytes = g_bytes_ref (template_bytes);
  return input;
}

void
goodix_milan_runtime_gallery_input_free (GoodixMilanRuntimeGalleryInput *input)
{
  if (!input)
    return;
  g_clear_pointer (&input->template_bytes, g_bytes_unref);
  g_free (input);
}

GoodixMilanRuntimeInput *
goodix_milan_runtime_input_new (
  guint64                               action_epoch,
  guint64                               generation_id,
  GoodixMilanPreprocessPurpose          purpose,
  const GoodixMilanPreprocessState     *preprocess_state,
  const GoodixMilanProfileState        *profile_state,
  const guint16                         setup_tx_on[GOODIX_MILAN_SENSOR_PIXELS],
  const guint16                         live_raw[GOODIX_MILAN_SENSOR_PIXELS],
  guint16                               tcode,
  guint16                               dac_high,
  guint16                               dac_low,
  guint16                               sensor_subtype,
  GoodixMilanRuntimeGalleryInput *const *gallery,
  gsize                                 gallery_count)
{
  GoodixMilanRuntimeInput *input;

  g_return_val_if_fail (preprocess_state != NULL, NULL);
  g_return_val_if_fail (profile_state != NULL, NULL);
  g_return_val_if_fail (setup_tx_on != NULL, NULL);
  g_return_val_if_fail (live_raw != NULL, NULL);
  g_return_val_if_fail (gallery_count == 0 || gallery != NULL, NULL);
  for (gsize i = 0; i < gallery_count; i++)
    g_return_val_if_fail (gallery[i] != NULL, NULL);

  input = g_new0 (GoodixMilanRuntimeInput, 1);
  input->action_epoch = action_epoch;
  input->generation_id = generation_id;
  input->purpose = purpose;
  input->preprocess_state = *preprocess_state;
  input->profile_state = *profile_state;
  input->setup_tx_on = g_memdup2 (
    setup_tx_on, GOODIX_MILAN_SENSOR_PIXELS * sizeof(*setup_tx_on));
  input->live_raw = g_memdup2 (
    live_raw, GOODIX_MILAN_SENSOR_PIXELS * sizeof(*live_raw));
  input->tcode = tcode;
  input->dac_high = dac_high;
  input->dac_low = dac_low;
  input->sensor_subtype = sensor_subtype;
  input->gallery = g_ptr_array_new_with_free_func (
    (GDestroyNotify) goodix_milan_runtime_gallery_input_free);
  for (gsize i = 0; i < gallery_count; i++)
    {
      GoodixMilanRuntimeGalleryInput *copy;

      copy = goodix_milan_runtime_gallery_input_new (
        gallery[i]->gallery_index, gallery[i]->template_bytes);
      g_ptr_array_add (input->gallery, copy);
    }
  return input;
}

void
goodix_milan_runtime_input_set_cancel_check (
  GoodixMilanRuntimeInput      *input,
  GoodixMilanRuntimeCancelFunc  cancel_func,
  gpointer                      user_data,
  GDestroyNotify                destroy)
{
  g_return_if_fail (input != NULL);
  if (input->cancel_destroy)
    input->cancel_destroy (input->cancel_data);
  input->cancel_func = cancel_func;
  input->cancel_data = user_data;
  input->cancel_destroy = destroy;
}

void
goodix_milan_runtime_input_free (GoodixMilanRuntimeInput *input)
{
  if (!input)
    return;
  if (input->cancel_destroy)
    input->cancel_destroy (input->cancel_data);
  g_clear_pointer (&input->gallery, g_ptr_array_unref);
  g_clear_pointer (&input->setup_tx_on, g_free);
  g_clear_pointer (&input->live_raw, g_free);
  g_free (input);
}

static GoodixSigfmTemplateStatus
goodix_milan_runtime_match (const GoodixMilanRuntimeInput *input,
                            GoodixMatchInfo               *probe,
                            const guint8                  *feature,
                            gsize                          feature_len,
                            GoodixMilanMatchResult        *match_result,
                            GBytes                       **after_match,
                            GoodixStudyQueue              *queue)
{
  (void) input;
  return goodix_match_serialized_feature_result_queued (
    probe, feature, feature_len, match_result, after_match, queue);
}

static GoodixSigfmTemplateStatus
goodix_milan_runtime_study (const GoodixMilanRuntimeInput *input,
                            GoodixMatchInfo               *probe,
                            const guint8                  *feature,
                            gsize                          feature_len,
                            const GoodixMilanMatchResult  *match_result,
                            GoodixStudyQueue              *queue,
                            GBytes                       **after_study,
                            GoodixMilanStudyAction        *action)
{
  (void) input;
  return goodix_match_study_feature_queued (
    probe, feature, feature_len, match_result, TRUE, queue, after_study, action);
}

static GoodixMilanRuntimeOutput *
goodix_milan_runtime_output_new (const GoodixMilanRuntimeInput *input)
{
  GoodixMilanRuntimeOutput *output = g_new0 (GoodixMilanRuntimeOutput, 1);

  output->status = GOODIX_MILAN_RUNTIME_INVALID_DATA;
  output->action_epoch = input->action_epoch;
  output->generation_id = input->generation_id;
  output->purpose = input->purpose;
  output->profile = GOODIX_MILAN_PRINT_PROFILE;
  output->tcode = input->tcode;
  output->dac_high = input->dac_high;
  output->dac_low = input->dac_low;
  output->sensor_subtype = input->sensor_subtype;
  output->winner_index = G_MAXUINT;
  output->winner_position = G_MAXSIZE;
  output->cancellation.cancelled_gallery_position = G_MAXSIZE;
  output->gallery_results = g_ptr_array_new_with_free_func (
    (GDestroyNotify) goodix_milan_runtime_gallery_result_free);
  return output;
}

static gboolean
goodix_milan_runtime_validate_input (const GoodixMilanRuntimeInput *input,
                                     GoodixMilanRuntimeOutput      *output)
{
  if (input->sensor_subtype == GOODIX_MILAN_PRINT_SENSOR_TYPE &&
      (input->purpose == GOODIX_MILAN_PURPOSE_IDENTIFY ||
       input->purpose == GOODIX_MILAN_PURPOSE_ENROLL))
    return TRUE;

  g_set_error_literal (&output->error, GOODIX_MILAN_PRINT_ERROR,
                       GOODIX_MILAN_PRINT_ERROR_INCOMPATIBLE,
                       "Runtime Milan profile or subtype is incompatible");
  return FALSE;
}

static gboolean
goodix_milan_runtime_preprocess_input (const GoodixMilanRuntimeInput *input,
                                       GoodixMilanRuntimeOutput      *output,
                                       guint8                       **processed)
{
  gint32 setup_status;
  gint32 preprocess_status;

  output->preprocess_state = input->preprocess_state;
  output->profile_state = input->profile_state;
  setup_status = goodix_milan_runtime_initialize_setup (
    &output->profile_state, input->setup_tx_on);
  if (setup_status != 0)
    {
#ifdef GOODIX53X5_DEBUG
      output->preprocess_attempted = TRUE;
      output->preprocess_status = setup_status;
      output->preprocess_status_available = TRUE;
#endif
      output->preprocess_state_valid = TRUE;
      output->status = GOODIX_MILAN_RUNTIME_RETRY;
      g_set_error_literal (&output->error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_INVALID,
                           "Native Milan setup initialization failed");
      return FALSE;
    }

  *processed = g_malloc (GOODIX_MILAN_SENSOR_PIXELS);
#ifdef GOODIX53X5_DEBUG
  output->preprocess_attempted = TRUE;
#endif
  preprocess_status = goodix_milan_preprocess (
    &output->preprocess_state, &output->profile_state,
    input->setup_tx_on, input->live_raw,
    input->purpose, *processed, &output->quality, &output->coverage);
#ifdef GOODIX53X5_DEBUG
  output->preprocess_status = preprocess_status;
  output->preprocess_status_available = TRUE;
#endif
  if (preprocess_status != 0 &&
      preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY &&
      preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY_RAW_ADMISSION &&
      preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY_CLASSIFICATION)
    {
      output->status = GOODIX_MILAN_RUNTIME_RETRY;
      g_set_error_literal (&output->error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_INVALID,
                           "Native Milan preprocessing failed");
      return FALSE;
    }
#ifdef GOODIX53X5_DEBUG
  output->preprocess_completed = preprocess_status == 0;
  if (output->preprocess_completed ||
      preprocess_status == GOODIX_MILAN_PREPROCESS_RETRY_CLASSIFICATION)
    output->processed_image = g_bytes_new (*processed,
                                           GOODIX_MILAN_SENSOR_PIXELS);
#endif
  output->preprocess_state_valid = TRUE;
  if (preprocess_status == GOODIX_MILAN_PREPROCESS_RETRY ||
      preprocess_status == GOODIX_MILAN_PREPROCESS_RETRY_RAW_ADMISSION ||
      preprocess_status == GOODIX_MILAN_PREPROCESS_RETRY_CLASSIFICATION)
    {
      output->status = GOODIX_MILAN_RUNTIME_RETRY;
      g_set_error_literal (&output->error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_INVALID,
                           "Native Milan preprocessing requested retry");
      return FALSE;
    }
  return TRUE;
}

static gboolean
goodix_milan_runtime_build_probe (const GoodixMilanRuntimeInput *input,
                                  GoodixMilanRuntimeOutput      *output,
                                  const guint8                  *processed,
                                  GoodixMatchInfo              **probe,
                                  GBytes                       **probe_template)
{
  GoodixMilanPrintTemplateInfo probe_info;

#ifdef GOODIX53X5_DEBUG
  output->extraction_attempted = TRUE;
#endif
  *probe = goodix_match_extract_native (
    processed, &output->preprocess_state, input->live_raw, input->tcode,
    input->dac_high, input->dac_low, input->sensor_subtype);
  *probe_template = *probe ? goodix_match_serialize_template (*probe) : NULL;
  if (!*probe || !*probe_template ||
      !goodix_milan_runtime_inspect_probe (*probe_template, &probe_info))
    {
      output->status = GOODIX_MILAN_RUNTIME_RETRY;
      output->error = g_error_new_literal (GOODIX_MILAN_PRINT_ERROR,
                                           GOODIX_MILAN_PRINT_ERROR_INVALID,
                                           "Native Milan extraction failed");
      goodix_match_free_info (*probe);
      *probe = NULL;
      return FALSE;
    }
#ifdef GOODIX53X5_DEBUG
  output->extraction_completed = TRUE;
#endif
  output->probe_template = g_bytes_ref (*probe_template);
  output->probe_record_count = probe_info.partition0_count +
                               probe_info.partition1_count;
  output->probe_partition0_count = probe_info.partition0_count;
  output->probe_partition1_count = probe_info.partition1_count;
  return TRUE;
}

static gboolean
goodix_milan_runtime_match_gallery (const GoodixMilanRuntimeInput *input,
                                    GoodixMilanRuntimeOutput      *output,
                                    GoodixMatchInfo               *probe,
                                    gsize                         *winner_position,
                                    GBytes                       **winner_after_match,
                                    GoodixStudyQueue             **winner_queue)
{
  for (gsize position = 0; position < input->gallery->len; position++)
    {
      GoodixMilanRuntimeGalleryInput *gallery =
        g_ptr_array_index (input->gallery, position);
      GoodixMilanRuntimeGalleryResult *result = g_new0 (
        GoodixMilanRuntimeGalleryResult, 1);
      GoodixStudyQueue *queue = NULL;
      g_autoptr(GBytes) after_match = NULL;
      const guint8 *feature;
      gsize feature_len;
      GoodixSigfmTemplateStatus status;
      gboolean template_valid;

      result->gallery_position = position;
      result->gallery_index = gallery->gallery_index;
#ifdef GOODIX53X5_DEBUG
      result->input_template = g_bytes_ref (gallery->template_bytes);
      goodix_milan_runtime_hash_bytes (
        gallery->template_bytes, result->input_template_sha256);
#endif
      g_ptr_array_add (output->gallery_results, result);
      if (goodix_milan_runtime_cancelled (
            input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_BEFORE_GALLERY,
            position))
        return FALSE;

      template_valid = goodix_milan_print_validate_template (
        gallery->template_bytes, &result->template_info,
        &result->validation_error);
#ifdef GOODIX53X5_DEBUG
      result->validation_observed = TRUE;
#endif
      if (!template_valid)
        {
          output->invalid_gallery_count++;
          goodix_study_queue_free (queue);
          continue;
        }
      result->valid = TRUE;
      output->valid_gallery_count++;
      queue = goodix_study_queue_new (
        result->template_info.queue_state,
        result->template_info.queue_transaction_counter);
      if (!queue || !goodix_study_queue_validate (queue))
        {
          result->valid = FALSE;
          output->valid_gallery_count--;
          output->invalid_gallery_count++;
          g_set_error_literal (&result->validation_error,
                               GOODIX_MILAN_PRINT_ERROR,
                               GOODIX_MILAN_PRINT_ERROR_INVALID,
                               "Milan gallery queue state is invalid");
          goodix_study_queue_free (queue);
          continue;
        }
#ifdef GOODIX53X5_DEBUG
      result->queue_before_match_observed = TRUE;
      result->queue_state_before_match = queue->enabled_state;
      result->queue_counter_before_match = queue->transaction_counter;
      result->queue_occupied_before_match = goodix_study_queue_occupied (queue);
#endif
      feature = g_bytes_get_data (gallery->template_bytes, &feature_len);
      status = goodix_milan_runtime_match (
        input, probe, feature, feature_len, &result->match_result, &after_match,
        queue);
      result->evaluated = TRUE;
#ifdef GOODIX53X5_DEBUG
      result->queue_after_match_observed = TRUE;
      result->queue_occupied_after_match = goodix_study_queue_occupied (queue);
#endif
      output->evaluated_gallery_count++;
      if (status != GOODIX_SIGFM_TEMPLATE_OK || !after_match)
        {
          result->valid = FALSE;
          output->valid_gallery_count--;
          output->invalid_gallery_count++;
          g_set_error_literal (&result->validation_error,
                               GOODIX_MILAN_PRINT_ERROR,
                               GOODIX_MILAN_PRINT_ERROR_INVALID,
                               "Native Milan gallery matching failed");
          goodix_study_queue_free (queue);
          continue;
        }
#ifdef GOODIX53X5_DEBUG
      result->after_match_template = g_bytes_ref (after_match);
#endif
#if defined(GOODIX53X5_DEBUG) || defined(GOODIX53X5_PARITY)
      goodix_milan_runtime_hash_bytes (
        after_match, result->after_match_sha256);
#endif
      result->score = result->match_result.score;
      result->accepted = result->score > 0;
      output->score = result->score;
      if (goodix_milan_runtime_cancelled (
            input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_AFTER_GALLERY,
            position))
        {
          goodix_study_queue_free (queue);
          return FALSE;
        }
      if (result->score <= 0)
        {
          goodix_study_queue_free (queue);
          continue;
        }

      output->status = GOODIX_MILAN_RUNTIME_MATCH;
      output->winner_index = gallery->gallery_index;
      output->winner_position = position;
      output->match_result = result->match_result;
      *winner_position = position;
      *winner_after_match = g_steal_pointer (&after_match);
      *winner_queue = queue;
      return TRUE;
    }

  if (input->gallery->len != 0 && output->valid_gallery_count == 0)
    {
      output->status = GOODIX_MILAN_RUNTIME_INVALID_DATA;
      g_set_error_literal (&output->error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_INVALID,
                           "All Milan gallery entries are invalid");
    }
  else
    {
      output->status = GOODIX_MILAN_RUNTIME_NO_MATCH;
    }
  return FALSE;
}

static void
goodix_milan_runtime_study_winner (const GoodixMilanRuntimeInput *input,
                                   GoodixMilanRuntimeOutput      *output,
                                   GoodixMatchInfo               *probe,
                                   gsize                          winner_position,
                                   GBytes                        *winner_after_match,
                                   GoodixStudyQueue              *winner_queue)
{
  g_autoptr(GBytes) after_study = NULL;
  const guint8 *after_match_data;
  gsize after_match_size;
  GoodixSigfmTemplateStatus study_status;
#ifdef GOODIX53X5_DEBUG
  GoodixMilanRuntimeGalleryResult *winner_result;
#endif

  after_match_data = g_bytes_get_data (winner_after_match, &after_match_size);
#ifdef GOODIX53X5_DEBUG
  output->study_attempted = TRUE;
#endif
  study_status = goodix_milan_runtime_study (
    input, probe, after_match_data, after_match_size, &output->match_result,
    winner_queue, &after_study, &output->study_action);
#ifdef GOODIX53X5_DEBUG
  winner_result = g_ptr_array_index (output->gallery_results, winner_position);

  winner_result->queue_after_study_observed = TRUE;
  winner_result->queue_state_after_study = winner_queue->enabled_state;
  winner_result->queue_counter_after_study = winner_queue->transaction_counter;
  winner_result->queue_occupied_after_study =
    goodix_study_queue_occupied (winner_queue);
#endif
  if (study_status != GOODIX_SIGFM_TEMPLATE_OK)
    {
      g_set_error_literal (&output->learning_error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_INVALID,
                           "Native Milan study failed after a positive match");
      goodix_study_queue_free (winner_queue);
      goodix_match_free_info (probe);
      return;
    }
#ifdef GOODIX53X5_DEBUG
  output->study_completed = TRUE;
#endif
  if (goodix_milan_runtime_cancelled (
        input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_AFTER_STUDY,
        winner_position))
    {
      goodix_study_queue_free (winner_queue);
      goodix_match_free_info (probe);
      return;
    }

  if (output->study_action == GOODIX_MILAN_STUDY_NONE)
    {
      if (after_study)
        {
          g_set_error_literal (&output->learning_error,
                               GOODIX_MILAN_PRINT_ERROR,
                               GOODIX_MILAN_PRINT_ERROR_INVALID,
                               "Action 0 unexpectedly produced a candidate");
        }
      goodix_study_queue_free (winner_queue);
      goodix_match_free_info (probe);
      return;
    }
  if (output->study_action > GOODIX_MILAN_STUDY_QUEUED || !after_study ||
      !goodix_milan_print_validate_template (
        after_study, &output->final_candidate_info, &output->learning_error))
    {
      if (!output->learning_error)
        {
          g_set_error_literal (&output->learning_error,
                               GOODIX_MILAN_PRINT_ERROR,
                               GOODIX_MILAN_PRINT_ERROR_INVALID,
                               "Native Milan study returned an invalid action");
        }
      goodix_study_queue_free (winner_queue);
      goodix_match_free_info (probe);
      return;
    }
  GoodixMilanRuntimeGalleryInput *winner =
    g_ptr_array_index (input->gallery, winner_position);
  if (g_bytes_equal (winner->template_bytes, after_study))
    {
      g_set_error_literal (&output->learning_error, GOODIX_MILAN_PRINT_ERROR,
                           GOODIX_MILAN_PRINT_ERROR_NONCANONICAL,
                           "Positive Milan study candidate did not change bytes");
      goodix_study_queue_free (winner_queue);
      goodix_match_free_info (probe);
      return;
    }
  output->final_candidate = g_steal_pointer (&after_study);
  goodix_study_queue_free (winner_queue);
  goodix_match_free_info (probe);
}

GoodixMilanRuntimeOutput *
goodix_milan_runtime_run (const GoodixMilanRuntimeInput *input)
{
  g_autofree guint8 *processed = NULL;

  g_autoptr(GBytes) probe_template = NULL;
  g_autoptr(GBytes) winner_after_match = NULL;
  GoodixStudyQueue *winner_queue = NULL;
  GoodixMatchInfo *probe = NULL;
  GoodixMilanRuntimeOutput *output;
  gsize winner_position = G_MAXSIZE;

  g_return_val_if_fail (input != NULL, NULL);
  output = goodix_milan_runtime_output_new (input);
  if (!goodix_milan_runtime_validate_input (input, output))
    return output;
  if (goodix_milan_runtime_cancelled (
        input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_BEFORE_PREPROCESS,
        G_MAXSIZE))
    return output;

  if (!goodix_milan_runtime_preprocess_input (input, output, &processed))
    return output;
  if (goodix_milan_runtime_cancelled (
        input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_AFTER_PREPROCESS,
        G_MAXSIZE))
    return output;

  if (!goodix_milan_runtime_build_probe (
        input, output, processed, &probe, &probe_template))
    return output;
  if (goodix_milan_runtime_cancelled (
        input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_AFTER_EXTRACT,
        G_MAXSIZE))
    {
      goodix_match_free_info (probe);
      return output;
    }

  if (!goodix_milan_runtime_match_gallery (
        input, output, probe, &winner_position, &winner_after_match,
        &winner_queue))
    {
      goodix_match_free_info (probe);
      return output;
    }

  if (goodix_milan_runtime_cancelled (
        input, output, GOODIX_MILAN_RUNTIME_CHECKPOINT_BEFORE_STUDY,
        winner_position))
    {
      goodix_study_queue_free (winner_queue);
      goodix_match_free_info (probe);
      return output;
    }

  goodix_milan_runtime_study_winner (
    input, output, probe, winner_position, winner_after_match, winner_queue);
  return output;
}

void
goodix_milan_runtime_output_free (GoodixMilanRuntimeOutput *output)
{
  if (!output)
    return;
  g_clear_pointer (&output->final_candidate, g_bytes_unref);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&output->processed_image, g_bytes_unref);
#endif
  g_clear_pointer (&output->probe_template, g_bytes_unref);
  g_clear_pointer (&output->gallery_results, g_ptr_array_unref);
  g_clear_error (&output->error);
  g_clear_error (&output->learning_error);
  g_free (output);
}

gboolean
goodix_milan_runtime_enrollment_admitted (const GoodixMilanRuntimeOutput *output)
{
  return output != NULL && output->preprocess_state_valid &&
         output->probe_template != NULL && output->probe_record_count > 0 &&
         output->quality > GOODIX_MILAN_ENROLL_MIN_QUALITY &&
         output->coverage > GOODIX_MILAN_ENROLL_MIN_COVERAGE;
}
