/*
 * Goodix 53x5 driver for libfprint - Milan parity current-side runner
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/match/match.h"
#include "milan/print.h"
#include "milan/runtime.h"

#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static gchar *case_id;
static gchar *purpose_name;
static gchar *setup_path;
static gchar **live_paths;
static gchar **live_purpose_values;
static gchar **gallery_values;
static gint tcode;
static gint dac_high;
static gint dac_low;
static gint profile_number = GOODIX_MILAN_PRINT_PROFILE;
static gint subtype = GOODIX_MILAN_PRINT_SENSOR_TYPE;
static gint prelude_count;

static GOptionEntry options[] = {
  { "case-id", 0, 0, G_OPTION_ARG_STRING, &case_id, "Opaque case ID", "ID" },
  { "purpose", 0, 0, G_OPTION_ARG_STRING, &purpose_name, "identify or enroll", "NAME" },
  { "setup", 0, 0, G_OPTION_ARG_FILENAME, &setup_path, "u16le setup frame", "PATH" },
  { "live", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &live_paths, "ordered u16le live frame", "PATH" },
  { "live-purpose", 0, 0, G_OPTION_ARG_STRING_ARRAY, &live_purpose_values,
    "purpose for the corresponding live frame", "NAME" },
  { "gallery", 0, 0, G_OPTION_ARG_STRING_ARRAY, &gallery_values,
    "INDEX=TEMPLATE_PATH", "ENTRY" },
  { "tcode", 0, 0, G_OPTION_ARG_INT, &tcode, "tcode", "U16" },
  { "dac-high", 0, 0, G_OPTION_ARG_INT, &dac_high, "DAC high", "U16" },
  { "dac-low", 0, 0, G_OPTION_ARG_INT, &dac_low, "DAC low", "U16" },
  { "profile", 0, 0, G_OPTION_ARG_INT, &profile_number, "Milan profile", "U16" },
  { "subtype", 0, 0, G_OPTION_ARG_INT, &subtype, "sensor subtype", "U16" },
  { "prelude-count", 0, 0, G_OPTION_ARG_INT, &prelude_count,
    "number of preprocessing-only live frames", "COUNT" },
  { NULL }
};

static gboolean
load_frame (const gchar *path,
            guint16     *values,
            GError     **error)
{
  g_autofree gchar *contents = NULL;
  gsize size = 0;

  if (!g_file_get_contents (path, &contents, &size, error))
    return FALSE;
  if (size != GOODIX_MILAN_SENSOR_PIXELS * sizeof (guint16))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                   "frame %s has %" G_GSIZE_FORMAT " bytes, expected %zu",
                   path, size, GOODIX_MILAN_SENSOR_PIXELS * sizeof (guint16));
      return FALSE;
    }
  for (gsize i = 0; i < GOODIX_MILAN_SENSOR_PIXELS; i++)
    {
      guint16 encoded;

      memcpy (&encoded, contents + i * sizeof (encoded), sizeof (encoded));
      values[i] = GUINT16_FROM_LE (encoded);
    }
  return TRUE;
}

static GBytes *
load_bytes (const gchar *path,
            GError     **error)
{
  gchar *contents = NULL;
  gsize size = 0;

  if (!g_file_get_contents (path, &contents, &size, error))
    return NULL;
  return g_bytes_new_take (contents, size);
}

static gchar *
hash_data (gconstpointer data,
           gsize         size)
{
  return g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, size);
}

static gchar *
hash_bytes (GBytes *bytes)
{
  gsize size;
  gconstpointer data = g_bytes_get_data (bytes, &size);

  return hash_data (data, size);
}

static void
append_phase (GString     *phases,
              gboolean    *first,
              const gchar *name,
              const gchar *outputs)
{
  if (!*first)
    g_string_append_c (phases, ',');
  *first = FALSE;
  g_string_append_printf (phases, "{\"name\":\"%s\",\"outputs\":%s}",
                          name, outputs);
}

static gboolean
parse_purpose (const gchar                  *value,
               GoodixMilanPreprocessPurpose *purpose)
{
  if (g_str_equal (value, "0") || g_str_equal (value, "identify"))
    *purpose = GOODIX_MILAN_PURPOSE_IDENTIFY;
  else if (g_str_equal (value, "1") || g_str_equal (value, "enroll"))
    *purpose = GOODIX_MILAN_PURPOSE_ENROLL;
  else
    return FALSE;
  return TRUE;
}

static gboolean
parse_gallery (GPtrArray *owned_bytes,
               GPtrArray *owned_inputs,
               GError   **error)
{
  for (gsize i = 0; gallery_values && gallery_values[i]; i++)
    {
      gchar *separator = strchr (gallery_values[i], '=');
      gchar *end = NULL;
      guint64 index;
      g_autoptr(GBytes) bytes = NULL;
      GoodixMilanRuntimeGalleryInput *input;

      if (!separator)
        {
          g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                               "gallery must be INDEX=PATH");
          return FALSE;
        }
      *separator = '\0';
      index = g_ascii_strtoull (gallery_values[i], &end, 10);
      if (!end || *end || index > G_MAXUINT)
        {
          g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                               "gallery index is invalid");
          return FALSE;
        }
      bytes = load_bytes (separator + 1, error);
      if (!bytes)
        return FALSE;
      input = goodix_milan_runtime_gallery_input_new ((guint) index, bytes);
      g_ptr_array_add (owned_bytes, g_steal_pointer (&bytes));
      g_ptr_array_add (owned_inputs, input);
    }
  return TRUE;
}

int
main (int argc, char **argv)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint16 *setup = g_new (guint16, GOODIX_MILAN_SENSOR_PIXELS);
  g_autofree guint16 *live = g_new (guint16, GOODIX_MILAN_SENSOR_PIXELS);
  g_autofree guint8 *processed = g_new (guint8, GOODIX_MILAN_SENSOR_PIXELS);
  g_autoptr(GPtrArray) gallery_bytes = g_ptr_array_new_with_free_func (
    (GDestroyNotify) g_bytes_unref);
  g_autoptr(GPtrArray) gallery_inputs = g_ptr_array_new_with_free_func (
    (GDestroyNotify) goodix_milan_runtime_gallery_input_free);
  g_autoptr(GoodixMilanEnrollmentTransaction) enrollment_transaction =
    goodix_milan_enrollment_transaction_new ();
  g_autoptr(GString) phases = g_string_new ("[");
  GoodixMilanPreprocessState state;
  GoodixMilanProfileState profile = { 0 };
  GoodixMilanPreprocessPurpose purpose;
  g_autoptr(GBytes) final_enrollment = NULL;
  gboolean first_phase = TRUE;
  guint accepted_stages = 0;
  guint enrollment_stage = 0;
  guint enrollment_bad_record_count = 0;
  guint enrollment_bad_continue_count = 0;
  GoodixMilanRuntimeStatus final_status = GOODIX_MILAN_RUNTIME_INVALID_DATA;
  gint32 final_score = 0;
  guint final_winner = G_MAXUINT;
  guint final_winner_position = G_MAXUINT;
  guint final_study_action = 0;
  gboolean final_preprocess_attempted = FALSE;
  gboolean final_preprocess_completed = FALSE;
  gboolean final_extraction_attempted = FALSE;
  gboolean final_extraction_completed = FALSE;
  gboolean final_study_attempted = FALSE;
  gboolean final_study_completed = FALSE;
  g_autofree gchar *final_candidate_hash = NULL;
  g_autofree gchar *final_study_action_value = NULL;
  g_autofree gchar *study_outputs = NULL;
  g_autoptr(GString) final_gallery = g_string_new ("[");
  gboolean first_gallery = TRUE;

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, options, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    goto fail;
  if (!case_id || !purpose_name || !setup_path || !live_paths || !live_paths[0] ||
      !enrollment_transaction ||
      tcode < 0 || tcode > G_MAXUINT16 || dac_high < 0 || dac_high > G_MAXUINT16 ||
      dac_low < 0 || dac_low > G_MAXUINT16 ||
      profile_number < 0 || profile_number > G_MAXUINT16 ||
      subtype < 0 || subtype > G_MAXUINT16)
    {
      g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "required runner argument is missing or out of range");
      goto fail;
    }
  if (!parse_purpose (purpose_name, &purpose))
    {
      g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "purpose must be identify or enroll");
      goto fail;
    }
  if (profile_number != GOODIX_MILAN_PRINT_PROFILE ||
      subtype != GOODIX_MILAN_PRINT_SENSOR_TYPE)
    {
      g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "current runner supports only profile 9 and sensor subtype 12");
      goto fail;
    }
  if (live_purpose_values &&
      g_strv_length (live_purpose_values) != g_strv_length (live_paths))
    {
      g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "live purpose count must equal live frame count");
      goto fail;
    }
  if (prelude_count < 0 || (gsize) prelude_count >= g_strv_length (live_paths))
    {
      g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "prelude count must leave at least one target frame");
      goto fail;
    }
  if (!load_frame (setup_path, setup, &error) ||
      !parse_gallery (gallery_bytes, gallery_inputs, &error))
    goto fail;
  if (purpose == GOODIX_MILAN_PURPOSE_ENROLL && gallery_inputs->len != 0)
    {
      g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "enrollment must not supply a gallery");
      goto fail;
    }
  goodix_milan_preprocess_reset (&state);

  for (gsize stage = 0; live_paths[stage]; stage++)
    {
      GoodixMilanPreprocessPurpose stage_purpose = purpose;
      GoodixMilanPreprocessState phase_state = state;
      GoodixMilanProfileState phase_profile = profile;
      GoodixMilanRuntimeInput *input = NULL;
      GoodixMilanRuntimeOutput *output;
      g_autofree gchar *processed_hash = NULL;
      g_autofree gchar *probe_hash = NULL;
      g_autofree gchar *phase_name = NULL;
      g_autofree gchar *phase_outputs = NULL;
      gboolean target_stage = live_paths[stage + 1] == NULL;
      gboolean prelude_stage = stage < (gsize) prelude_count;
      gboolean emit_stage;
      gsize phase_number;
      gint quality = 0;
      gint coverage = 0;
      gint preprocess_status;

      if (live_purpose_values && live_purpose_values[stage])
        {
          if (!parse_purpose (live_purpose_values[stage], &stage_purpose))
            {
              g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                                   "live purpose must be 0/identify or 1/enroll");
              goto fail;
            }
        }
      if (!prelude_stage && stage_purpose == GOODIX_MILAN_PURPOSE_ENROLL)
        enrollment_stage++;
      emit_stage = purpose == GOODIX_MILAN_PURPOSE_ENROLL ?
                   !prelude_stage :
                   target_stage;
      phase_number = purpose == GOODIX_MILAN_PURPOSE_IDENTIFY ?
                     1 : enrollment_stage;

      if (!load_frame (live_paths[stage], live, &error))
        goto fail;
      preprocess_status = goodix_milan_runtime_initialize_setup (
        &phase_profile, setup);
      if (preprocess_status == 0)
        preprocess_status = goodix_milan_preprocess (
          &phase_state, &phase_profile, setup, live, stage_purpose, processed,
          &quality, &coverage);
      if (preprocess_status == 0 ||
          preprocess_status == GOODIX_MILAN_PREPROCESS_RETRY_CLASSIFICATION)
        processed_hash = hash_data (processed, GOODIX_MILAN_SENSOR_PIXELS);
      phase_name = g_strdup_printf ("stage-%02" G_GSIZE_FORMAT "-preprocess",
                                    phase_number);
      phase_outputs = g_strdup_printf (
        "{\"coverage_i32\":%d,\"processed_image_sha256\":%s%s%s,\"quality_i32\":%d,"
        "\"status_i32\":%d}", coverage,
        processed_hash ? "\"" : "null", processed_hash ? processed_hash : "",
        processed_hash ? "\"" : "", quality, preprocess_status);
      if (emit_stage)
        append_phase (phases, &first_phase, phase_name, phase_outputs);

      if (prelude_stage)
        {
          if (preprocess_status != 0 &&
              preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY &&
              preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY_RAW_ADMISSION &&
              preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY_CLASSIFICATION &&
              preprocess_status != GOODIX_MILAN_PREPROCESS_RETRY_SETUP_ADMISSION &&
              preprocess_status != GOODIX_MILAN_PREPROCESS_NOT_READY)
            {
              g_set_error_literal (&error, G_OPTION_ERROR,
                                   G_OPTION_ERROR_FAILED,
                                   "preprocessing prelude failed");
              goto fail;
            }
          state = phase_state;
          profile = phase_profile;
          continue;
        }

      input = goodix_milan_runtime_input_new (
        stage + 1, 1, stage_purpose, &state, &profile, setup, live,
        (guint16) tcode, (guint16) dac_high, (guint16) dac_low, (guint16) subtype,
        (GoodixMilanRuntimeGalleryInput *const *) gallery_inputs->pdata,
        gallery_inputs->len);
      output = goodix_milan_runtime_run (input);
      goodix_milan_runtime_input_free (input);
      if (!output)
        {
          g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                               "production runtime returned no output");
          goto fail;
        }
      final_status = output->status;
      if (output->preprocess_state_valid &&
          (memcmp (&output->preprocess_state, &phase_state,
                   G_STRUCT_OFFSET (GoodixMilanPreprocessState,
                                    extraction_classification)) != 0 ||
           memcmp (&output->profile_state, &phase_profile, sizeof (phase_profile)) != 0 ||
           output->quality != quality || output->coverage != coverage))
        {
          goodix_milan_runtime_output_free (output);
          g_set_error_literal (&error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                               "phase replay differs from production runtime preprocessing");
          goto fail;
        }
      if (output->preprocess_state_valid)
        {
          state = output->preprocess_state;
          profile = output->profile_state;
        }
      if (emit_stage && output->probe_template)
        {
          probe_hash = hash_bytes (output->probe_template);
          g_free (phase_name);
          g_free (phase_outputs);
          phase_name = g_strdup_printf (
            stage_purpose == GOODIX_MILAN_PURPOSE_ENROLL ?
            "stage-%02" G_GSIZE_FORMAT "-extract" :
            "stage-%02" G_GSIZE_FORMAT "-extract-antifake",
            phase_number);
          if (stage_purpose == GOODIX_MILAN_PURPOSE_ENROLL)
            {
              phase_outputs = g_strdup_printf (
                "{\"partition0_count_u32\":%u,\"partition1_count_u32\":%u,"
                "\"probe_record_count_u32\":%u,\"probe_template_sha256\":\"%s\"}",
                output->probe_partition0_count, output->probe_partition1_count,
                output->probe_record_count, probe_hash);
            }
          else
            {
              phase_outputs = g_strdup_printf (
                "{\"active_record_count_u32\":%u,\"partition0_count_u32\":%u,"
                "\"partition1_count_u32\":%u,\"probe_record_count_u32\":%u,"
                "\"probe_template_sha256\":\"%s\"}",
                output->probe_record_count, output->probe_partition0_count,
                output->probe_partition1_count, output->probe_record_count,
                probe_hash);
            }
          append_phase (phases, &first_phase, phase_name, phase_outputs);
        }
      if (!prelude_stage && stage_purpose == GOODIX_MILAN_PURPOSE_ENROLL &&
          goodix_milan_runtime_enrollment_admitted (output))
        {
          g_autofree gchar *combined_hash = NULL;
          GoodixMilanEnrollmentResult enrollment_result = { 0 };
          GoodixMilanEnrollmentAttemptStatus enrollment_status;

          enrollment_status = goodix_milan_enrollment_transaction_attempt (
            &enrollment_transaction, output->probe_template,
            &enrollment_bad_record_count, &enrollment_bad_continue_count,
            &enrollment_result);
          accepted_stages = goodix_milan_enrollment_transaction_count (
            enrollment_transaction);
          g_clear_pointer (&final_enrollment, g_bytes_unref);
          if (accepted_stages != 0)
            final_enrollment = goodix_milan_enrollment_transaction_publish (
              enrollment_transaction);
          if (accepted_stages != 0 &&
              (!final_enrollment ||
               !goodix_milan_print_validate_template (
                 final_enrollment, NULL, &error)))
            {
              goodix_milan_runtime_output_free (output);
              goto fail;
            }
          if (final_enrollment)
            combined_hash = hash_bytes (final_enrollment);
          g_free (phase_name);
          g_free (phase_outputs);
          phase_name = g_strdup_printf ("stage-%02" G_GSIZE_FORMAT "-template",
                                        phase_number);
          phase_outputs = g_strdup_printf (
            "{\"accepted_stage_u32\":%u,\"attempt_status_u32\":%u,"
            "\"reject_detail_u32\":%u,\"template_sha256\":%s%s%s}",
            accepted_stages, enrollment_status,
            enrollment_result.reject_detail,
            combined_hash ? "\"" : "null",
            combined_hash ? combined_hash : "",
            combined_hash ? "\"" : "");
          if (emit_stage)
            append_phase (phases, &first_phase, phase_name, phase_outputs);
        }
      if (target_stage && stage_purpose == GOODIX_MILAN_PURPOSE_IDENTIFY &&
          purpose == GOODIX_MILAN_PURPOSE_IDENTIFY)
        {
          if (!output->probe_template)
            {
              append_phase (
                phases, &first_phase, "stage-01-extract-antifake",
                "{\"active_record_count_u32\":null,"
                "\"partition0_count_u32\":null,"
                "\"partition1_count_u32\":null,"
                "\"probe_record_count_u32\":null,"
                "\"probe_template_sha256\":null}");
            }
          for (gsize position = 0; position < output->gallery_results->len; position++)
            {
              GoodixMilanRuntimeGalleryResult *result =
                g_ptr_array_index (output->gallery_results, position);
              g_autofree gchar *before_match_occupancy = NULL;
              g_autofree gchar *after_match_occupancy = NULL;
              g_autofree gchar *after_study_occupancy = NULL;

              if (result->queue_before_match_observed)
                before_match_occupancy = g_strdup_printf (
                  "%" G_GSIZE_FORMAT, result->queue_occupied_before_match);
              if (result->queue_after_match_observed)
                after_match_occupancy = g_strdup_printf (
                  "%" G_GSIZE_FORMAT, result->queue_occupied_after_match);
              if (result->queue_after_study_observed)
                after_study_occupancy = g_strdup_printf (
                  "%" G_GSIZE_FORMAT, result->queue_occupied_after_study);

              if (!first_gallery)
                g_string_append_c (final_gallery, ',');
              first_gallery = FALSE;
              g_string_append_printf (
                final_gallery,
                "{\"accepted\":%s,\"after_match_sha256\":%s%s%s,"
                "\"evaluated\":%s,\"gallery_index_u32\":%u,"
                "\"gallery_position_u64\":%" G_GSIZE_FORMAT ","
                "\"queue_occupied_after_match_u64\":%s,"
                "\"queue_occupied_after_study_u64\":%s,"
                "\"queue_occupied_before_match_u64\":%s,"
                "\"score_i32\":%d,"
                "\"valid\":%s}",
                result->accepted ? "true" : "false",
                result->after_match_sha256[0] ? "\"" : "null",
                result->after_match_sha256,
                result->after_match_sha256[0] ? "\"" : "",
                result->evaluated ? "true" : "false", result->gallery_index,
                result->gallery_position,
                after_match_occupancy ? after_match_occupancy : "null",
                after_study_occupancy ? after_study_occupancy : "null",
                before_match_occupancy ? before_match_occupancy : "null",
                result->score,
                result->valid ? "true" : "false");
            }
          final_score = output->score;
          final_winner = output->winner_index;
          final_winner_position = output->winner_position > G_MAXUINT ?
                                  G_MAXUINT : (guint) output->winner_position;
          final_study_action = output->study_action;
          final_preprocess_attempted = output->preprocess_attempted;
          final_preprocess_completed = output->preprocess_completed;
          final_extraction_attempted = output->extraction_attempted;
          final_extraction_completed = output->extraction_completed;
          final_study_attempted = output->study_attempted;
          final_study_completed = output->study_completed;
          if (output->final_candidate)
            final_candidate_hash = hash_bytes (output->final_candidate);
        }
      goodix_milan_runtime_output_free (output);
      if (purpose == GOODIX_MILAN_PURPOSE_IDENTIFY && target_stage)
        break;
      if (purpose == GOODIX_MILAN_PURPOSE_ENROLL &&
          accepted_stages == 12)
        break;
    }
  if (purpose == GOODIX_MILAN_PURPOSE_IDENTIFY)
    {
      final_study_action_value = final_study_attempted ?
                                 g_strdup_printf ("%u", final_study_action) :
                                 g_strdup ("null");
      study_outputs = g_strdup_printf (
        "{\"final_candidate_sha256\":%s%s%s,\"study_action_u32\":%s}",
        final_candidate_hash ? "\"" : "null",
        final_candidate_hash ? final_candidate_hash : "",
        final_candidate_hash ? "\"" : "", final_study_action_value);
      append_phase (phases, &first_phase, "stage-01-study", study_outputs);
    }
  g_string_append_c (phases, ']');
  g_string_append_c (final_gallery, ']');

  if (purpose == GOODIX_MILAN_PURPOSE_ENROLL)
    {
      g_autofree gchar *template_hash = final_enrollment ? hash_bytes (final_enrollment) : NULL;

      g_print ("{\"case_id\":\"%s\",\"phases\":%s,\"policy\":{"
               "\"anti_fake_mode\":1,\"boundary_policy\":\"canonical-zero-v1\","
               "\"print_schema\":4,\"profile\":9,\"subtype\":12},"
               "\"result\":{\"accepted_stages_u32\":%u,"
               "\"final_template_sha256\":%s%s%s},"
               "\"schema\":\"milan-parity-record/v1\"}\n",
               case_id, phases->str, accepted_stages,
               template_hash ? "\"" : "null", template_hash ? template_hash : "",
               template_hash ? "\"" : "");
    }
  else
    {
      g_print ("{\"case_id\":\"%s\",\"phases\":%s,\"policy\":{"
               "\"anti_fake_mode\":1,\"boundary_policy\":\"canonical-zero-v1\","
               "\"print_schema\":4,\"profile\":9,\"subtype\":12},"
               "\"result\":{\"accepted\":%s,\"final_candidate_sha256\":%s%s%s,"
               "\"gallery\":%s,\"lifecycle\":{"
               "\"extraction\":{\"attempted\":%s,\"completed\":%s},"
               "\"preprocess\":{\"attempted\":%s,\"completed\":%s},"
               "\"study\":{\"attempted\":%s,\"completed\":%s}},"
               "\"score_i32\":%d,\"status_u32\":%u,"
               "\"study_action_u32\":%s,\"winner_index_u32\":%u,"
               "\"winner_position_u32\":%u},"
               "\"schema\":\"milan-parity-record/v1\"}\n",
               case_id, phases->str,
               final_status == GOODIX_MILAN_RUNTIME_MATCH ? "true" : "false",
               final_candidate_hash ? "\"" : "null",
               final_candidate_hash ? final_candidate_hash : "",
               final_candidate_hash ? "\"" : "", final_gallery->str,
               final_extraction_attempted ? "true" : "false",
               final_extraction_completed ? "true" : "false",
               final_preprocess_attempted ? "true" : "false",
               final_preprocess_completed ? "true" : "false",
               final_study_attempted ? "true" : "false",
               final_study_completed ? "true" : "false",
               final_score, final_status, final_study_action_value, final_winner,
               final_winner_position);
    }
  return 0;

fail:
  g_printerr ("current runner: %s\n", error ? error->message : "unknown failure");
  return 2;
}
