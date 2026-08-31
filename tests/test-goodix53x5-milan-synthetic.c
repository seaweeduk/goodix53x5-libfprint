/*
 * Goodix 53x5 driver for libfprint - generated Milan parity tests
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "drivers/goodix53x5/device/base.h"
#include "drivers/goodix53x5/milan/match/match.h"
#include "drivers/goodix53x5/milan/match/info-private.h"
#include "drivers/goodix53x5/milan/match/selection.h"
#include "drivers/goodix53x5/milan/milan.h"
#include "drivers/goodix53x5/milan/preprocess/gain.h"
#include "drivers/goodix53x5/milan/private.h"
#include "drivers/goodix53x5/milan/print.h"
#include "drivers/goodix53x5/milan/study/policy.h"
#include "drivers/goodix53x5/milan/study/queue.h"
#include "drivers/goodix53x5/milan/template/codec-private.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

#define PIXELS GOODIX_MILAN_SENSOR_PIXELS

static const char accepted_preprocess_sha256[] =
  "4882d43dd7208d554362fa832c7f6a5f9b3f79b382099d3d81efc702120c6b5e";
static const char post_render_retry_sha256[] =
  "448ecc968b50d65c49667381450c20374a62d9ab3da27bf9e97035826324ed07";
static const char feature_extraction_sha256[] =
  "5c4a2989778d3ce15ffc7f928544a1fa3a84feb8279e5928b51ca014400ed6a3";
static const char feature_template_sha256[] =
  "c18597eb4204464f53cd6cbb311f85b577fbcdb6b18a16e11e37a0f79b7c1c3e";
static const char feature_antifake_sha256[] =
  "5b2763131c54a5bfbeea4409278eaa7a6f23fd019cdc00f63c5fb35596ff74dc";

static gchar *
sha256 (const void *data,
        gsize       size)
{
  return g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, size);
}

static void
assert_chip_subtype_mapping (void)
{
  guint16 subtype = 0;

  g_assert_true (goodix_milan_runtime_subtype_for_chip (
    GOODIX_MILAN_PROFILE9_CHIP_FAMILY_PREFIX, &subtype));
  g_assert_cmpuint (subtype, ==, GOODIX_MILAN_VALIDATED_SUBTYPE);
  g_assert_false (goodix_milan_runtime_subtype_for_chip (0x00220800,
                                                          &subtype));
}

static void
generate_constant_frames (uint16_t *setup,
                          uint16_t *live)
{
  for (size_t i = 0; i < PIXELS; i++)
    {
      setup[i] = 1000;
      live[i] = 2000;
    }
}

static void
generate_feature_frames (uint16_t *setup,
                         uint16_t *live)
{
  for (int row = 0; row < GOODIX_MILAN_SENSOR_ROWS; row++)
    for (int column = 0; column < GOODIX_MILAN_SENSOR_COLUMNS; column++)
      {
        size_t index = (size_t) row * GOODIX_MILAN_SENSOR_COLUMNS + column;
        uint16_t baseline = (uint16_t) (
          0x0700 + row * 3 + column * 2 +
          (((row * 7) ^ (column * 13)) & 0x3f));
        uint16_t delta;

        setup[index] = baseline;
        int wrapped = (column * 8 + row * 8) % 80;
        int first_cut = (column - 32) * (column - 32) +
                        (row - 28) * (row - 28);
        int second_cut = (column - 72) * (column - 72) +
                         (row - 55) * (row - 55);

        delta = wrapped < 40 ? 300 : 1200;
        if (first_cut < 36 || second_cut < 49)
          delta = 1200;
        /* Avoid an all-sharp synthetic score distribution. */
        if (ABS (row - 44) <= 4 && ABS (column - 54) <= 4)
          setup[index] = (uint16_t) (
            baseline - delta + 800 +
            (4 - MAX (ABS (row - 44), ABS (column - 54))) * 80);
        live[index] = (uint16_t) (baseline - delta);
      }
}

static GoodixMatchInfo *
generate_match_info (void)
{
  g_autofree GoodixMilanPreprocessState *state = g_new0 (
    GoodixMilanPreprocessState, 1);
  GoodixMilanProfileState profile = { 0 };
  g_autofree uint16_t *setup = g_new (uint16_t, PIXELS);
  g_autofree uint16_t *live = g_new (uint16_t, PIXELS);
  g_autofree uint8_t *processed = g_new (uint8_t, PIXELS);
  GoodixMatchInfo *info = NULL;
  int quality = -1;
  int coverage = -1;

  generate_feature_frames (setup, live);
  goodix_milan_preprocess_reset (state);
  g_assert_cmpint (goodix_milan_preprocess (
                     state, &profile, setup, live,
                     GOODIX_MILAN_PURPOSE_ENROLL, processed, &quality,
                     &coverage), ==, 0);
  g_assert_cmpint (quality, ==, 100);
  g_assert_cmpint (coverage, ==, 100);
  g_assert_cmpint (goodix_match_extract_native_result (
                     processed, state, live, 0, 0, 0, 12, &info), ==,
                   GOODIX_MILAN_EXTRACTION_OK);
  g_assert_true (goodix_match_info_is_complete (info));
  g_assert_cmpint (goodix_match_keypoints_count (info), ==, 150);
  return info;
}

static void
test_preprocess_accepted (void)
{
  assert_chip_subtype_mapping ();

  g_autofree GoodixMilanPreprocessState *state = g_new0 (
    GoodixMilanPreprocessState, 1);
  GoodixMilanProfileState profile = { 0 };
  g_autofree uint16_t *setup = g_new (uint16_t, PIXELS);
  g_autofree uint16_t *live = g_new (uint16_t, PIXELS);
  g_autofree uint8_t *processed = g_new (uint8_t, PIXELS);
  g_autofree gchar *digest = NULL;
  int quality = -1;
  int coverage = -1;
  int status;

  generate_constant_frames (setup, live);
  goodix_milan_preprocess_reset (state);
  status = goodix_milan_preprocess (
    state, &profile, setup, live, GOODIX_MILAN_PURPOSE_IDENTIFY, processed,
    &quality, &coverage);
  digest = sha256 (processed, PIXELS);
  g_test_message (
    "accepted status=%d quality=%d coverage=%d selected=%d samples=%u "
    "stable=%u update=%u auxiliary=%u history=%u/%u classes=%u/%u/%u "
    "primary-valid=%d hash=%s",
    status, quality, coverage, state->selected_refined, state->sample_count,
    state->stable_count, state->update_state, state->auxiliary_sample_count,
    state->profile9_history_count, state->profile9_history_update_count,
    state->profile9_class_counts.profile9_class1_count,
    state->profile9_class_counts.profile9_class2_count,
    state->profile9_class_counts.profile9_class3_count,
    state->primary_contrast_valid, digest);

  g_assert_cmpint (status, ==, 0);
  g_assert_cmpint (quality, ==, 0);
  g_assert_cmpint (coverage, ==, 0);
  g_assert_cmpint (state->selected_refined, ==, 0);
  g_assert_cmpuint (state->sample_count, ==, 1);
  g_assert_cmpuint (state->stable_count, ==, 0);
  g_assert_cmpuint (state->update_state, ==, 1);
  g_assert_cmpuint (state->auxiliary_sample_count, ==, 1);
  g_assert_cmpuint (state->profile9_history_count, ==, 1);
  g_assert_cmpuint (state->profile9_history_update_count, ==, 1);
  g_assert_cmpuint (state->profile9_class_counts.profile9_class1_count, ==,
                    PIXELS);
  g_assert_cmpuint (state->profile9_class_counts.profile9_class2_count, ==, 0);
  g_assert_cmpuint (state->profile9_class_counts.profile9_class3_count, ==, 0);
  g_assert_cmpint (state->primary_contrast_valid, ==, 1);
  g_assert_cmpstr (digest, ==, accepted_preprocess_sha256);
}

static void
test_preprocess_post_render_retry (void)
{
  g_autofree GoodixMilanPreprocessState *state = g_new0 (
    GoodixMilanPreprocessState, 1);
  GoodixMilanProfileState profile = { 0 };
  g_autofree uint16_t *setup = g_new (uint16_t, PIXELS);
  g_autofree uint16_t *live = g_new (uint16_t, PIXELS);
  g_autofree uint8_t *processed = g_new (uint8_t, PIXELS);
  g_autofree gchar *digest = NULL;
  int quality = -1;
  int coverage = -1;
  int status;

  for (int row = 0; row < GOODIX_MILAN_SENSOR_ROWS; row++)
    for (int column = 0; column < GOODIX_MILAN_SENSOR_COLUMNS; column++)
      {
        size_t index = (size_t) row * GOODIX_MILAN_SENSOR_COLUMNS + column;
        uint16_t base = (uint16_t) (2600 + row * 2 + column);

        setup[index] = base;
        live[index] = (uint16_t) (base - (column % 2 == 0 ? 20 : 40));
      }

  goodix_milan_preprocess_reset (state);
  status = goodix_milan_preprocess (
    state, &profile, setup, live, GOODIX_MILAN_PURPOSE_IDENTIFY, processed,
    &quality, &coverage);
  digest = sha256 (processed, PIXELS);
  g_test_message ("post-render hash=%s", digest);

  g_assert_cmpint (status, ==, GOODIX_MILAN_PREPROCESS_RETRY);
  g_assert_cmpint (quality, ==, 0);
  g_assert_cmpint (coverage, ==, 18);
  g_assert_cmpint (state->post_render.primary_metric, ==, 120);
  g_assert_cmpint (state->post_render.fallback_metric, ==, 200);
  g_assert_cmpint (state->post_render.disagreement, ==, 100);
  g_assert_cmpint (state->post_render.component_score, ==, 0);
  g_assert_cmpint (state->post_render.component_flag, ==, 1);
  g_assert_cmpint (state->post_render.quality_gate, ==, 1);
  g_assert_cmpint (state->post_render.update_applied, ==, 1);
  g_assert_cmpint (state->post_render.status,
                   ==, GOODIX_MILAN_PREPROCESS_RETRY);
  g_assert_cmpuint (state->sample_count, ==, 1);
  g_assert_cmpstr (digest, ==, post_render_retry_sha256);
}

static void
test_first_positive (void)
{
  static const struct
  {
    int32_t scores[4];
    size_t count;
    size_t index;
    int32_t score;
  } cases[] = {
    { { 23, -1, 45, 0 }, 3, 0, 23 },
    { { -4, 0, 17, 99 }, 4, 2, 17 },
    { { -4, 0, -7, 0 }, 3, SIZE_MAX, -7 },
  };

  for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      size_t index = 0;
      int32_t score = 0;

      g_assert_cmpint (goodix_milan_match_select_first_positive (
                         cases[i].scores, cases[i].count, &index, &score),
                       ==, 0);
      g_assert_cmpuint (index, ==, cases[i].index);
      g_assert_cmpint (score, ==, cases[i].score);
    }
}

typedef struct
{
  const char *name;
  int32_t candidate_flag;
  int32_t metric1;
  int32_t metric4;
  int32_t metric5;
  int32_t metric8;
  int32_t initial_sum;
  int32_t initial_count;
  int32_t winner4;
  int32_t winner1;
  int32_t winner8;
  int expected_status;
  int32_t expected_sum;
  int32_t expected_count;
  int32_t expected_winner4;
  int32_t expected_winner1;
  int32_t expected_winner8;
  int expected_replaced;
  int32_t expected_term;
} SelectionCase;

static void
test_selection_rows (void)
{
  static const int32_t identity[6] = { 256, 0, 0, 0, 256, 0 };
  static const SelectionCase cases[] = {
    { "positive", 0, 10, 208, 196, 10, 0, 0, 0, 0, 0,
      1, 61, 1, 208, 10, 10, 1, 61 },
    { "metric4-boundary", 0, 10, 207, 196, 10, 5, 1, 300, 20, 30,
      0, 5, 1, 300, 20, 30, 0, 0 },
    { "metric5-boundary", 0, 10, 208, 195, 10, 5, 1, 300, 20, 30,
      0, 5, 1, 300, 20, 30, 0, 0 },
    { "winner", 0, 10, 210, 200, 30, 5, 1, 209, 20, 30,
      1, 66, 2, 210, 10, 30, 1, 61 },
    { "tie", 0, 10, 210, 200, 30, 5, 1, 210, 10, 30,
      1, 66, 2, 210, 10, 30, 0, 61 },
    { "positive-wrap", 1, 8388607, 0, 0, 0, 2147483640, 1, 0, 0, 0,
      1, -2096353099, 2, 0, 8388607, 0, 1, 51130557 },
    { "negative-wrap", 1, 8388608, 0, 0, 0, 0, INT32_MAX, 0, 0, 0,
      1, -51130562, INT32_MIN, 0, 8388608, 0, 1, -51130562 },
  };

  for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      const SelectionCase *test = &cases[i];
      GoodixMilanMatchSelection selection;
      GoodixMilanMatchContributionEvent event;
      int32_t metrics[GOODIX_MILAN_MATCH_SELECTION_METRICS] = { 0 };
      int status;

      goodix_milan_match_selection_reset (&selection);
      selection.q8_sum = test->initial_sum;
      selection.q8_contributor_count = test->initial_count;
      selection.winner_valid = test->initial_count != 0;
      selection.winner_metrics[4] = test->winner4;
      selection.winner_metrics[1] = test->winner1;
      selection.winner_metrics[8] = test->winner8;
      metrics[1] = test->metric1;
      metrics[4] = test->metric4;
      metrics[5] = test->metric5;
      metrics[8] = test->metric8;

      status = goodix_milan_match_selection_contribute (
        &selection, metrics, identity, 2, 0, 0, test->candidate_flag, &event);
      g_test_message ("selection row=%s", test->name);
      g_assert_cmpint (status, ==, test->expected_status);
      g_assert_cmpint (selection.q8_sum, ==, test->expected_sum);
      g_assert_cmpint (selection.q8_contributor_count,
                       ==, test->expected_count);
      g_assert_cmpint (selection.winner_metrics[4],
                       ==, test->expected_winner4);
      g_assert_cmpint (selection.winner_metrics[1],
                       ==, test->expected_winner1);
      g_assert_cmpint (selection.winner_metrics[8],
                       ==, test->expected_winner8);
      g_assert_cmpint (event.winner_replaced, ==, test->expected_replaced);
      g_assert_cmpint (event.q8_term, ==, test->expected_term);
    }

  {
    GoodixMilanMatchSelection selection;
    GoodixMilanMatchContributionEvent event;
    int32_t metrics[GOODIX_MILAN_MATCH_SELECTION_METRICS] = { 0 };

    goodix_milan_match_selection_reset (&selection);
    selection.q8_sum = 21474837;
    selection.q8_contributor_count = 1;
    selection.selected_numerator = 10;
    selection.latched_score = 99;
    metrics[1] = 9;
    g_assert_cmpint (goodix_milan_match_selection_admit (
                       &selection, metrics, identity, 0, 0, 0, 1, &event),
                     ==, 1);
    g_assert_cmpint (selection.latched_score, ==, -8388608);
  }
}

static void
test_gain_tail (void)
{
  uint32_t ready = 0;

  goodix_milan_profile9_update_gain_ready (0, 5, &ready);
  g_assert_cmpuint (ready, ==, 0);
  goodix_milan_profile9_update_gain_ready (0, 6, &ready);
  g_assert_cmpuint (ready, ==, 1);
  goodix_milan_profile9_update_gain_ready (14, 0, &ready);
  g_assert_cmpuint (ready, ==, 1);
  goodix_milan_profile9_update_gain_ready (15, 0, &ready);
  g_assert_cmpuint (ready, ==, 0);

  g_assert_cmpuint (
    goodix_milan_profile9_combine_gain (7001, 9001, 8102, 0), ==, 7607);
  g_assert_cmpuint (
    goodix_milan_profile9_combine_gain (7001, 9001, 8102, 1), ==, 7692);
  g_assert_cmpuint (
    goodix_milan_profile9_combine_gain (0x2000, 9001, 8102, 0), ==, 8902);
}

static void
test_negative_orientation_scaling (void)
{
  static const struct
  {
    uint8_t packed;
    int16_t expected;
  } cases[] = {
    { 0x81, -256 },
    { 0xff, -32512 },
  };

  for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      uint8_t packed_record[32] = { 0 };
      GoodixMilanFeatureRecord record;

      packed_record[0] = cases[i].packed;
      g_assert_cmpint (goodix_milan_feature_unpack_template_records (
                         packed_record, 1, 0, &record, 1), ==, 0);
      g_assert_cmpint (record.orientation, ==, cases[i].expected);
    }
}

static void
test_generated_extraction (void)
{
  g_autofree GoodixMilanUnpackedTemplate *unpacked = g_new0 (
    GoodixMilanUnpackedTemplate, 1);
  GoodixMatchInfo *info = generate_match_info ();
  g_autoptr(GBytes) extracted = NULL;
  g_autoptr(GPtrArray) features = g_ptr_array_new_with_free_func (
    (GDestroyNotify) g_bytes_unref);
  g_autoptr(GBytes) combined = NULL;
  g_autoptr(GVariant) print_data = NULL;
  g_autoptr(GVariant) print_payload = NULL;
  g_autoptr(GBytes) parsed = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *extracted_hash = NULL;
  g_autofree gchar *combined_hash = NULL;
  g_autofree gchar *antifake_hash = NULL;
  GoodixMilanFeatureView view;
  GoodixMilanPrintTemplateInfo print_info;
  guint32 schema;
  guint32 print_profile;
  guint32 sensor_type;
  guint32 antifake_mode;
  const gchar *boundary_policy;
  const uint8_t *bytes;
  gsize size;

  test_negative_orientation_scaling ();
  extracted = goodix_match_serialize_template (info);
  bytes = g_bytes_get_data (extracted, &size);
  g_assert_cmpuint (size, >=, 1433);
  extracted_hash = sha256 (bytes, size);
  g_assert_cmpint (goodix_milan_template_unpack (bytes, size, unpacked), ==, 0);
  g_assert_cmpuint (unpacked->feature_count, ==, 1);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     unpacked->feature_elements[0],
                     unpacked->feature_element_sizes[0], &view), ==, 0);
  antifake_hash = sha256 (
    goodix_milan_antifake_const_data (view.antifake),
    GOODIX_MILAN_ANTIFAKE_DEFINED_MATERIAL_SIZE);
  g_ptr_array_add (features, g_bytes_ref (extracted));
  combined = goodix_match_combine_templates (features);
  g_assert_nonnull (combined);
  bytes = g_bytes_get_data (combined, &size);
  combined_hash = sha256 (bytes, size);
  memset (unpacked, 0, sizeof(*unpacked));
  g_assert_cmpint (goodix_milan_template_unpack (bytes, size, unpacked), ==, 0);
  g_assert_cmpuint (unpacked->feature_count, ==, 1);
  g_assert_cmpuint (unpacked->relation_count, ==, 0);

  print_data = goodix_milan_print_build_data (combined, &error);
  g_assert_no_error (error);
  g_assert_nonnull (print_data);
  g_assert_true (g_variant_is_of_type (
    print_data, G_VARIANT_TYPE ("(uuuusay)")));
  g_variant_get (print_data, "(uuuu&s@ay)", &schema, &print_profile,
                 &sensor_type, &antifake_mode, &boundary_policy,
                 &print_payload);
  g_assert_true (schema == 4 && print_profile == 9 && sensor_type == 12 &&
                 antifake_mode == 1 &&
                 strcmp (boundary_policy, "canonical-zero-v1") == 0);
  g_assert_true (goodix_milan_print_parse_data (
    print_data, &parsed, &error));
  g_assert_no_error (error);
  g_assert_true (g_bytes_equal (combined, parsed));
  g_assert_true (goodix_milan_print_validate_template (
    parsed, &print_info, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (print_info.feature_count, ==, 1);
  g_assert_cmpuint (print_info.relation_count, ==, 0);

  g_assert_cmpstr (extracted_hash, ==, feature_extraction_sha256);
  g_assert_cmpstr (combined_hash, ==, feature_template_sha256);
  g_assert_cmpstr (antifake_hash, ==, feature_antifake_sha256);

  goodix_match_free_info (info);
}

static void
test_study_policy_actions (void)
{
  static const struct
  {
    const char *name;
    int32_t action_gate;
    size_t maximum_features;
    int32_t matched_residual;
    int32_t retained_flag;
    GoodixMilanStudyActionCode expected_action;
    size_t expected_index;
    int expected_primary;
  } cases[] = {
    { "none", 0, 3, 0, 0, GOODIX_MILAN_STUDY_ACTION_NONE, SIZE_MAX, 0 },
    { "append", 1, 4, 20, 0, GOODIX_MILAN_STUDY_ACTION_APPEND, 3, 0 },
    { "replace-no-relation", 1, 3, 0, 0,
      GOODIX_MILAN_STUDY_ACTION_REPLACE_NO_RELATION, 1, 1 },
    { "geometric", 1, 3, 20, 1,
      GOODIX_MILAN_STUDY_ACTION_GEOMETRIC, 2, 0 },
    { "replace", 1, 3, 0, 1,
      GOODIX_MILAN_STUDY_ACTION_REPLACE, 1, 1 },
  };

  for (size_t i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      GoodixMilanStudyPolicyInput input = {
        .action_gate = cases[i].action_gate,
        .mode_enabled = 1,
        .replacement_enabled = 1,
        .probe_quality = 100,
        .probe_coverage = 100,
        .feature_count = 3,
        .maximum_features = cases[i].maximum_features,
        .matched_feature_index = 1,
        .reference_feature_index = 0,
        .retained_flag = cases[i].retained_flag,
        .primary_transform_area = GOODIX_MILAN_STUDY_MASK_SIZE,
      };
      GoodixMilanStudyPolicyResult result;

      for (size_t feature = 0; feature < input.feature_count; feature++)
        {
          input.features[feature].active = 1;
          input.features[feature].quality = 50;
          input.features[feature].coverage = 80;
          input.features[feature].residual = 20;
          input.features[feature].uncovered_probe_residual = 0;
          input.features[feature].geometric_overlap_area =
            feature == 2 ? 1945 : 1602;
          input.features[feature].geometric_overlap_percent =
            input.features[feature].geometric_overlap_area * 100 /
            GOODIX_MILAN_STUDY_MASK_SIZE;
        }
      input.features[input.matched_feature_index].residual =
        cases[i].matched_residual;

      g_test_message ("study policy row=%s", cases[i].name);
      g_assert_cmpint (goodix_milan_study_policy_select (&input, &result),
                       ==, 0);
      g_assert_cmpint (result.action, ==, cases[i].expected_action);
      g_assert_cmpuint (result.selected_feature_index,
                        ==, cases[i].expected_index);
      g_assert_cmpint (result.primary_candidate,
                       ==, cases[i].expected_primary);
    }
}

static void
unpack_test_template (GBytes                       *template_bytes,
                       GoodixMilanUnpackedTemplate *unpacked)
{
  const guint8 *data;
  gsize size;

  data = g_bytes_get_data (template_bytes, &size);
  g_assert_cmpint (goodix_milan_template_unpack (data, size, unpacked), ==, 0);
}

static int32_t
distinct_fixture_scalar (int32_t probe_value,
                         int32_t fixture_value)
{
  return probe_value == fixture_value ? fixture_value + 1 : fixture_value;
}

static GoodixMatchInfo *
make_distinct_append_fixture (const GoodixMatchInfo *probe)
{
  static const struct
  {
    size_t field;
    uint8_t tag;
  } probe_owned_fields[] = {
    { 2, 0xb7 }, { 3, 0xb8 }, { 4, 0xb9 }, { 8, 0xbd }, { 10, 0xc0 },
  };
  GoodixMatchInfo *fixture = goodix_match_info_new_empty ();
  g_autofree GoodixMilanUnpackedTemplate *unpacked = g_new0 (
    GoodixMilanUnpackedTemplate, 1);
  g_autofree guint8 *feature_element = NULL;
  g_autofree guint8 *packed = NULL;
  GoodixMilanFeatureView serialized_view;
  GoodixMilanAntifakeBlob *serialized_antifake;
  GBytes *template_bytes;
  gsize template_size;
  size_t packed_size = 0;

  g_assert_true (goodix_match_info_copy (fixture, probe));
  goodix_milan_antifake_set_texture (
    &fixture->antifake,
    distinct_fixture_scalar (goodix_milan_antifake_texture (&probe->antifake),
                             0x1020304));
  goodix_milan_antifake_set_mean (
    &fixture->antifake,
    distinct_fixture_scalar (goodix_milan_antifake_mean (&probe->antifake),
                             -0x1020304));
  goodix_milan_antifake_set_threshold (
    &fixture->antifake,
    distinct_fixture_scalar (goodix_milan_antifake_threshold (&probe->antifake),
                             0x11223344));
  goodix_milan_antifake_set_pair_score (
    &fixture->antifake,
    distinct_fixture_scalar (goodix_milan_antifake_pair_score (&probe->antifake),
                             -0x11223344));
  goodix_milan_antifake_mask (&fixture->antifake)[0] ^= 1;
  g_assert_cmpint (goodix_milan_antifake_texture (&fixture->antifake),
                   !=, goodix_milan_antifake_texture (&probe->antifake));
  g_assert_cmpint (goodix_milan_antifake_mean (&fixture->antifake),
                   !=, goodix_milan_antifake_mean (&probe->antifake));
  g_assert_cmpint (goodix_milan_antifake_threshold (&fixture->antifake),
                   !=, goodix_milan_antifake_threshold (&probe->antifake));
  g_assert_cmpint (goodix_milan_antifake_pair_score (&fixture->antifake),
                   !=, goodix_milan_antifake_pair_score (&probe->antifake));

  unpack_test_template (fixture->template, unpacked);
  g_assert_cmpuint (unpacked->feature_count, ==, 1);
  feature_element = g_memdup2 (unpacked->feature_elements[0],
                               unpacked->feature_element_sizes[0]);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     feature_element, unpacked->feature_element_sizes[0],
                     &serialized_view), ==, 0);
  g_assert_cmpuint (serialized_view.record_count, >, 0);
  ((guint8 *) serialized_view.high_bitmap)[285] ^= 1;
  ((guint8 *) serialized_view.enhanced_bitmap)[285] ^= 1;
  ((guint8 *) serialized_view.inline_mask)[71] ^= 1;
  ((guint8 *) serialized_view.low_bitmap)[285] ^= 1;
  ((guint8 *) serialized_view.packed_records)[
    serialized_view.record_count * 32 - 1] ^= 1;
  for (size_t i = 0; i < G_N_ELEMENTS (probe_owned_fields); i++)
    {
      int32_t value =
        serialized_view.fields.tagged_values[probe_owned_fields[i].field];

      g_assert_cmpint (goodix_milan_template_patch_feature_scalar (
                         feature_element, unpacked->feature_element_sizes[0],
                         probe_owned_fields[i].tag,
                         value == 0 ? 1 : value - 1), ==, 0);
    }
  g_assert_cmpint (goodix_milan_template_patch_feature_scalar (
                     feature_element, unpacked->feature_element_sizes[0],
                     0xbe,
                     distinct_fixture_scalar (
                       serialized_view.fields.tagged_values[9], 1)), ==, 0);
  serialized_antifake = (GoodixMilanAntifakeBlob *) serialized_view.antifake;
  memcpy (serialized_antifake, &fixture->antifake, sizeof(*serialized_antifake));
  unpacked->feature_elements[0] = feature_element;
  template_size = g_bytes_get_size (fixture->template);
  packed = g_malloc (template_size);
  g_assert_cmpint (goodix_milan_template_pack (
                     unpacked->feature_elements,
                     unpacked->feature_element_sizes,
                     unpacked->feature_count, unpacked->relations,
                     unpacked->relation_count, &unpacked->metadata,
                     unpacked->tail_state, sizeof(unpacked->tail_state), packed,
                      template_size, &packed_size), ==, 0);
  g_assert_cmpuint (packed_size, ==, template_size);
  template_bytes = g_bytes_new_take (g_steal_pointer (&packed), packed_size);
  g_clear_pointer (&fixture->template, g_bytes_unref);
  fixture->template = template_bytes;
  return fixture;
}

static void
assert_antifake_append_ownership (const GoodixMilanAntifakeBlob *actual,
                                  const GoodixMilanAntifakeBlob *probe,
                                  const GoodixMilanAntifakeBlob *matched)
{
  static const size_t inherited_scalar_offsets[] = {
    GOODIX_MILAN_ANTIFAKE_TEXTURE_OFFSET,
    GOODIX_MILAN_ANTIFAKE_MEAN_OFFSET,
    GOODIX_MILAN_ANTIFAKE_THRESHOLD_OFFSET,
    GOODIX_MILAN_ANTIFAKE_PAIR_SCORE_OFFSET,
  };
  const guint8 *actual_data = goodix_milan_antifake_const_data (actual);
  const guint8 *probe_data = goodix_milan_antifake_const_data (probe);
  const guint8 *matched_data = goodix_milan_antifake_const_data (matched);
  size_t start = 0;

  for (size_t i = 0; i < G_N_ELEMENTS (inherited_scalar_offsets); i++)
    {
      size_t offset = inherited_scalar_offsets[i];

      g_assert_cmpmem (actual_data + start, offset - start,
                       probe_data + start, offset - start);
      start = offset + sizeof(int32_t);
    }
  g_assert_cmpmem (actual_data + start, GOODIX_MILAN_ANTIFAKE_SIZE - start,
                   probe_data + start, GOODIX_MILAN_ANTIFAKE_SIZE - start);
  g_assert_cmpint (actual_data[GOODIX_MILAN_ANTIFAKE_MASK_OFFSET],
                   ==, probe_data[GOODIX_MILAN_ANTIFAKE_MASK_OFFSET]);
  g_assert_cmpint (actual_data[GOODIX_MILAN_ANTIFAKE_MASK_OFFSET],
                   !=, matched_data[GOODIX_MILAN_ANTIFAKE_MASK_OFFSET]);
  g_assert_cmpint (goodix_milan_antifake_texture (actual),
                   ==, goodix_milan_antifake_texture (matched));
  g_assert_cmpint (goodix_milan_antifake_mean (actual),
                   ==, goodix_milan_antifake_mean (matched));
  g_assert_cmpint (goodix_milan_antifake_threshold (actual),
                   ==, goodix_milan_antifake_threshold (matched));
  g_assert_cmpint (goodix_milan_antifake_pair_score (actual),
                   ==, goodix_milan_antifake_pair_score (matched));
  g_assert_cmpint (goodix_milan_antifake_texture (actual),
                   !=, goodix_milan_antifake_texture (probe));
  g_assert_cmpint (goodix_milan_antifake_mean (actual),
                   !=, goodix_milan_antifake_mean (probe));
  g_assert_cmpint (goodix_milan_antifake_threshold (actual),
                   !=, goodix_milan_antifake_threshold (probe));
  g_assert_cmpint (goodix_milan_antifake_pair_score (actual),
                   !=, goodix_milan_antifake_pair_score (probe));
  g_assert_cmpint (goodix_milan_antifake_texture (matched),
                   !=, goodix_milan_antifake_texture (probe));
  g_assert_cmpint (goodix_milan_antifake_mean (matched),
                   !=, goodix_milan_antifake_mean (probe));
  g_assert_cmpint (goodix_milan_antifake_threshold (matched),
                   !=, goodix_milan_antifake_threshold (probe));
  g_assert_cmpint (goodix_milan_antifake_pair_score (matched),
                   !=, goodix_milan_antifake_pair_score (probe));
}

static void
assert_generic_append_material (GBytes *probe,
                                GBytes *before,
                                GBytes *after)
{
  static const size_t probe_owned_fields[] = { 2, 3, 4, 8, 10 };
  static const int32_t appended_relation_values[7] = {
    0, 0x100, 0, 0, 0, 0x100, 0,
  };
  g_autofree GoodixMilanUnpackedTemplate *probe_template = g_new0 (
    GoodixMilanUnpackedTemplate, 1);
  g_autofree GoodixMilanUnpackedTemplate *before_template = g_new0 (
    GoodixMilanUnpackedTemplate, 1);
  g_autofree GoodixMilanUnpackedTemplate *after_template = g_new0 (
    GoodixMilanUnpackedTemplate, 1);
  GoodixMilanFeatureView probe_view;
  GoodixMilanFeatureView matched_view;
  GoodixMilanFeatureView inserted_view;
  const GoodixMilanTemplateRelation *appended_relation = NULL;
  size_t inserted_index;

  unpack_test_template (probe, probe_template);
  unpack_test_template (before, before_template);
  unpack_test_template (after, after_template);
  inserted_index = before_template->feature_count;
  g_assert_cmpuint (probe_template->feature_count, ==, 1);
  g_assert_cmpuint (after_template->feature_count, ==, inserted_index + 1);

  for (size_t i = 0; i < before_template->feature_count; i++)
    {
      g_assert_cmpuint (after_template->feature_element_sizes[i],
                        ==, before_template->feature_element_sizes[i]);
      g_assert_cmpmem (after_template->feature_elements[i],
                       after_template->feature_element_sizes[i],
                       before_template->feature_elements[i],
                       before_template->feature_element_sizes[i]);
    }
  g_assert_cmpuint (after_template->relation_count,
                    ==, before_template->relation_count + 1);
  for (size_t i = 0; i < before_template->relation_count; i++)
    {
      gboolean found = FALSE;

      for (size_t j = 0; j < after_template->relation_count; j++)
        if (after_template->relations[j].index ==
            before_template->relations[i].index)
          {
            found = TRUE;
            for (size_t value = 0;
                 value < G_N_ELEMENTS (before_template->relations[i].values);
                 value++)
              g_assert_cmpint (after_template->relations[j].values[value],
                               ==, before_template->relations[i].values[value]);
            break;
          }
      g_assert_true (found);
    }
  for (size_t i = 0; i < after_template->relation_count; i++)
    if (after_template->relations[i].index ==
        (int32_t) before_template->metadata.registration_count)
      {
        appended_relation = &after_template->relations[i];
        break;
      }
  g_assert_nonnull (appended_relation);
  g_assert_cmpint (appended_relation->index,
                   ==, (int32_t) before_template->metadata.registration_count);
  for (size_t i = 0; i < G_N_ELEMENTS (appended_relation_values); i++)
    g_assert_cmpint (appended_relation->values[i],
                     ==, appended_relation_values[i]);

  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     probe_template->feature_elements[0],
                     probe_template->feature_element_sizes[0],
                     &probe_view), ==, 0);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     before_template->feature_elements[0],
                     before_template->feature_element_sizes[0],
                     &matched_view), ==, 0);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     after_template->feature_elements[inserted_index],
                     after_template->feature_element_sizes[inserted_index],
                     &inserted_view), ==, 0);
  g_assert_cmpuint (inserted_view.record_count, ==, probe_view.record_count);
  g_assert_cmpuint (matched_view.record_count, ==, probe_view.record_count);
  g_assert_cmpmem (inserted_view.high_bitmap, 286, probe_view.high_bitmap, 286);
  g_assert_cmpint (memcmp (inserted_view.high_bitmap,
                           matched_view.high_bitmap, 286), !=, 0);
  g_assert_cmpmem (inserted_view.enhanced_bitmap, 286,
                   probe_view.enhanced_bitmap, 286);
  g_assert_cmpint (memcmp (inserted_view.enhanced_bitmap,
                           matched_view.enhanced_bitmap, 286), !=, 0);
  g_assert_cmpmem (inserted_view.inline_mask, 72, probe_view.inline_mask, 72);
  g_assert_cmpint (memcmp (inserted_view.inline_mask,
                           matched_view.inline_mask, 72), !=, 0);
  g_assert_cmpmem (inserted_view.low_bitmap, 286, probe_view.low_bitmap, 286);
  g_assert_cmpint (memcmp (inserted_view.low_bitmap,
                           matched_view.low_bitmap, 286), !=, 0);
  g_assert_cmpmem (inserted_view.packed_records, inserted_view.record_count * 32,
                   probe_view.packed_records, probe_view.record_count * 32);
  g_assert_cmpint (memcmp (inserted_view.packed_records,
                           matched_view.packed_records,
                           inserted_view.record_count * 32), !=, 0);
  for (size_t i = 0; i < G_N_ELEMENTS (probe_owned_fields); i++)
    {
      size_t field = probe_owned_fields[i];

      g_assert_cmpint (inserted_view.fields.tagged_values[field],
                       ==, probe_view.fields.tagged_values[field]);
      g_assert_cmpint (inserted_view.fields.tagged_values[field],
                       !=, matched_view.fields.tagged_values[field]);
    }
  g_assert_cmpint (inserted_view.fields.tagged_values[0],
                   ==, matched_view.fields.tagged_values[0]);
  g_assert_cmpint (inserted_view.fields.tagged_values[0],
                   !=, probe_view.fields.tagged_values[0]);
  g_assert_cmpint (inserted_view.fields.tagged_values[1],
                   ==, (int32_t) before_template->metadata.registration_count);
  g_assert_cmpint (inserted_view.fields.tagged_values[5], ==, 1);
  g_assert_cmpint (inserted_view.fields.tagged_values[6], ==, 0);
  g_assert_cmpint (inserted_view.fields.tagged_values[7],
                   ==, (int32_t) before_template->feature_count);
  g_assert_cmpint (inserted_view.fields.tagged_values[9],
                   ==, matched_view.fields.tagged_values[9]);
  g_assert_cmpint (inserted_view.fields.tagged_values[9],
                   !=, probe_view.fields.tagged_values[9]);
  assert_antifake_append_ownership (inserted_view.antifake,
                                    probe_view.antifake,
                                    matched_view.antifake);

  g_assert_cmpuint (after_template->metadata.registration_count,
                    ==, before_template->metadata.registration_count +
                        before_template->feature_count);
  for (size_t i = 0; i < before_template->feature_count; i++)
    g_assert_cmpuint (goodix_milan_template_read_u32 (
                        after_template->tail_state + i * 4),
                      ==, goodix_milan_template_read_u32 (
                            before_template->tail_state + i * 4));
  g_assert_cmpuint (goodix_milan_template_read_u32 (
                      after_template->tail_state + inserted_index * 4),
                    ==, inserted_index);
  g_assert_cmpuint (goodix_milan_template_read_u32 (
                      after_template->tail_state + 0x50c),
                    ==, goodix_milan_template_read_u32 (
                          before_template->tail_state + 0x50c));
  g_assert_cmpuint (goodix_milan_template_read_u32 (
                      after_template->tail_state + 0x510),
                    ==, goodix_milan_template_read_u32 (
                          before_template->tail_state + 0x510));
  g_assert_cmpuint (goodix_milan_template_read_u32 (
                      after_template->tail_state + 0x514),
                    ==, goodix_milan_template_read_u32 (
                          before_template->tail_state + 0x514) + 1);
}

static void
test_generated_enrollment_prefix_lifecycle (void)
{
  enum { ENROLLMENT_PREFIX_COUNT = 12 };
  static const size_t complete_prefixes[] = { 1, 2, 12 };
  GoodixMatchInfo *info = generate_match_info ();
  g_autoptr(GBytes) extracted = goodix_match_serialize_template (info);
  g_autoptr(GPtrArray) sources = g_ptr_array_new_with_free_func (
    (GDestroyNotify) g_bytes_unref);
  g_autoptr(GBytes) final_prefix = NULL;
  const guint8 *source_data;
  gsize source_size;

  g_assert_nonnull (extracted);
  source_data = g_bytes_get_data (extracted, &source_size);
  for (size_t i = 0; i < ENROLLMENT_PREFIX_COUNT; i++)
    g_ptr_array_add (sources, g_bytes_new (source_data, source_size));

  for (size_t stage_index = 0;
       stage_index < G_N_ELEMENTS (complete_prefixes); stage_index++)
    {
      size_t prefix = complete_prefixes[stage_index];
      g_autoptr(GPtrArray) stage = g_ptr_array_new_with_free_func (
        (GDestroyNotify) g_bytes_unref);
      g_autoptr(GBytes) combined = NULL;
      GoodixMilanPrintTemplateInfo print_info;
      g_autoptr(GError) error = NULL;
      size_t expected_registration_count = 1 + prefix * (prefix - 1) / 2;

      for (size_t i = 0; i < prefix; i++)
        g_ptr_array_add (stage, g_bytes_ref (g_ptr_array_index (sources, i)));
      combined = goodix_match_combine_templates (stage);
      g_assert_nonnull (combined);
      g_assert_true (goodix_milan_print_validate_template (
        combined, &print_info, &error));
      g_assert_no_error (error);
      g_assert_cmpuint (print_info.feature_count, ==, prefix);
      g_assert_cmpuint (print_info.maximum_features,
                        ==, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT);
      g_assert_cmpuint (print_info.registration_count,
                        ==, expected_registration_count);
      g_assert_cmpuint (print_info.relation_count,
                        ==, prefix == 1 ? 0 : prefix - 1);
      g_assert_cmpuint (print_info.graph_established,
                        ==, prefix == 1 ? 0 : 1);
      g_assert_cmpint (print_info.graph_reference_index,
                       ==, prefix == 1 ? -1 : 0);
      g_assert_cmpuint (print_info.queue_state, ==, 0);
      g_assert_cmpuint (print_info.queue_transaction_counter, ==, 0);
      for (size_t i = 0; i < sources->len; i++)
        g_assert_true (g_bytes_equal (g_ptr_array_index (sources, i),
                                      extracted));
      if (prefix == ENROLLMENT_PREFIX_COUNT)
        final_prefix = g_bytes_ref (combined);
    }

  {
    g_autoptr(GPtrArray) fresh = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_bytes_unref);
    g_autoptr(GBytes) recombined = NULL;

    for (size_t i = 0; i < sources->len; i++)
      g_ptr_array_add (fresh, g_bytes_ref (g_ptr_array_index (sources, i)));
    recombined = goodix_match_combine_templates (fresh);
    g_assert_nonnull (recombined);
    g_assert_true (g_bytes_equal (recombined, final_prefix));
  }

  goodix_match_free_info (info);
}

static void
test_generated_production_replay (void)
{
  GoodixMatchInfo *info = generate_match_info ();
  GoodixMatchInfo *matched_info = make_distinct_append_fixture (info);
  g_autoptr(GBytes) extracted = goodix_match_serialize_template (info);
  g_autoptr(GBytes) matched_extracted = goodix_match_serialize_template (
    matched_info);
  g_autoptr(GPtrArray) features = g_ptr_array_new_with_free_func (
    (GDestroyNotify) g_bytes_unref);
  g_autoptr(GBytes) combined = NULL;
  g_autoptr(GBytes) first_after_match = NULL;
  g_autoptr(GBytes) first_append_update = NULL;
  GoodixMilanPrintTemplateInfo combined_info;
  g_autoptr(GError) error = NULL;

  g_assert_nonnull (extracted);
  g_assert_nonnull (matched_extracted);
  g_ptr_array_add (features, g_bytes_ref (matched_extracted));
  g_ptr_array_add (features, g_bytes_ref (extracted));
  goodix_match_free_info (matched_info);
  combined = goodix_match_combine_templates (features);
  g_assert_nonnull (combined);
  g_assert_true (goodix_milan_print_validate_template (
    combined, &combined_info, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (combined_info.feature_count, ==, 2);
  g_assert_cmpuint (combined_info.maximum_features,
                    ==, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT);
  g_assert_cmpuint (combined_info.registration_count, ==, 2);
  g_assert_cmpuint (combined_info.relation_count, ==, 1);
  g_assert_cmpuint (combined_info.graph_established, ==, 1);
  g_assert_cmpint (combined_info.graph_reference_index, ==, 0);

  for (size_t iteration = 0; iteration < 2; iteration++)
    {
      GoodixStudyQueue *match_queue = goodix_study_queue_new (
        combined_info.queue_state, combined_info.queue_transaction_counter);
      GoodixMilanMatchResult result;
      g_autoptr(GBytes) after_match = NULL;
      g_autoptr(GBytes) negative_update = NULL;
      GoodixMilanPrintTemplateInfo after_match_info;
      const guint8 *combined_data;
      const guint8 *after_match_data;
      gsize combined_size;
      gsize after_match_size;
      GoodixMilanStudyAction negative_action = GOODIX_MILAN_STUDY_APPEND;

      g_assert_nonnull (match_queue);
      g_assert_true (goodix_study_queue_validate (match_queue));
      combined_data = g_bytes_get_data (combined, &combined_size);
      g_assert_cmpint (goodix_match_serialized_feature_result_queued (
                         info, combined_data, combined_size, &result,
                         &after_match, match_queue), ==,
                       GOODIX_SIGFM_TEMPLATE_OK);
      g_assert_nonnull (after_match);
      g_assert_cmpint (result.score, ==, 0);
      g_assert_cmpuint (result.matched_feature_index, ==, SIZE_MAX);
      for (size_t i = 0; i < G_N_ELEMENTS (result.match_transform); i++)
        g_assert_cmpint (result.match_transform[i],
                         ==, i == 0 || i == 4 ? 0x100 : 0);
      g_assert_cmpint (result.relation.relation_count, ==, 0);
      g_assert_cmpint (result.relation.relation_valid, ==, 0);
      g_assert_cmpuint (result.direct_positive_feature_mask, ==, 0);
      g_assert_cmpuint (result.contributor_feature_mask, ==, 0);
      g_assert_cmpuint (result.lifecycle_update_feature_mask, ==, 0);
      g_assert_cmpuint (result.retained_evidence_count, ==, 0);
      g_assert_cmpint (result.retained_evidence_flag, ==, 0);
      g_assert_cmpint (result.study_control.study_finalization_gate, ==, 0);
      g_assert_cmpint (result.study_control.study_action_gate, ==, 0);
      g_assert_cmpint (result.study_control.queue_candidate_eligible, ==, 0);
      g_assert_true (goodix_study_queue_validate (match_queue));
      g_assert_cmpuint (goodix_study_queue_occupied (match_queue), ==, 0);
      g_assert_true (goodix_milan_print_validate_template (
        after_match, &after_match_info, &error));
      g_assert_no_error (error);
      g_assert_cmpuint (after_match_info.feature_count,
                        ==, combined_info.feature_count);
      g_assert_cmpuint (after_match_info.relation_count,
                         ==, combined_info.relation_count);

      after_match_data = g_bytes_get_data (after_match, &after_match_size);
      g_assert_cmpint (goodix_match_study_feature_queued (
                         info, after_match_data, after_match_size, &result,
                         TRUE, match_queue, &negative_update,
                         &negative_action), ==,
                       GOODIX_SIGFM_TEMPLATE_INVALID);
      g_assert_null (negative_update);
      g_assert_cmpint (negative_action, ==, GOODIX_MILAN_STUDY_NONE);
      g_assert_true (goodix_study_queue_validate (match_queue));
      g_assert_cmpuint (goodix_study_queue_occupied (match_queue), ==, 0);
      goodix_study_queue_free (match_queue);

      /* Independent generic action-0 transient subcase. With no retained
       * evidence the identity relation is not consumed, but keeps the
       * publication coherent and fully defined. */
      {
        GoodixStudyQueue *action0_queue = goodix_study_queue_new (
          after_match_info.queue_state,
          after_match_info.queue_transaction_counter);
        GoodixMilanMatchResult generic_action0_result = {
          .matched_feature_index = SIZE_MAX,
          .score = 1,
          .match_transform = { 0x100, 0, 0, 0, 0x100, 0 },
          .relation = {
            .relation_count = 0,
            .relation_values = { 0, 0x100, 0, 0, 0, 0x100, 0 },
            .relation_valid = 0,
          },
          .study_control.study_finalization_gate = 1,
          .study_control.study_action_gate = 1,
        };
        GoodixMilanStudyAction action0_action = GOODIX_MILAN_STUDY_APPEND;
        g_autoptr(GBytes) action0_update = NULL;

        g_assert_nonnull (action0_queue);
        g_assert_cmpint (goodix_match_study_feature_queued (
                           info, after_match_data, after_match_size,
                           &generic_action0_result, TRUE, action0_queue,
                           &action0_update, &action0_action), ==,
                         GOODIX_SIGFM_TEMPLATE_OK);
        g_assert_null (action0_update);
        g_assert_cmpint (action0_action, ==, GOODIX_MILAN_STUDY_NONE);
        g_assert_true (goodix_study_queue_validate (action0_queue));
        g_assert_cmpuint (action0_queue->enabled_state, ==, 0);
        g_assert_cmpuint (action0_queue->transaction_counter,
                          ==, after_match_info.queue_transaction_counter);
        g_assert_cmpuint (goodix_study_queue_allocated (action0_queue),
                          ==, GOODIX_STUDY_QUEUE_CAPACITY);
        g_assert_cmpuint (goodix_study_queue_occupied (action0_queue), ==, 1);
        goodix_study_queue_free (action0_queue);
      }

      /* Independent generic action-1 append API subcase, not a matcher
       * result handoff. */
      {
        GoodixStudyQueue *append_queue = goodix_study_queue_new (
          after_match_info.queue_state,
          after_match_info.queue_transaction_counter);
        GoodixMilanMatchResult generic_append_result = {
          .matched_feature_index = 0,
          .score = 1,
          .match_transform = { 0x100, 0, 0, 0, 0x100, 0 },
          .relation = {
            .relation_count = 150,
            .relation_values = { 0, 0x100, 0, 0, 0, 0x100, 0 },
            .relation_valid = 1,
          },
          .study_control.study_action_gate = 1,
        };
        GoodixMilanStudyAction append_action = GOODIX_MILAN_STUDY_NONE;
        g_autoptr(GBytes) append_update = NULL;
        GoodixMilanPrintTemplateInfo after_study_info;

        g_assert_nonnull (append_queue);
        g_assert_cmpint (goodix_match_study_feature_queued (
                           info, after_match_data, after_match_size,
                           &generic_append_result, TRUE, append_queue,
                           &append_update, &append_action), ==,
                         GOODIX_SIGFM_TEMPLATE_OK);
        g_assert_true (goodix_study_queue_validate (append_queue));
        g_assert_cmpuint (goodix_study_queue_occupied (append_queue), ==, 0);
        g_assert_nonnull (append_update);
        g_assert_cmpint (append_action, ==, GOODIX_MILAN_STUDY_APPEND);
        g_assert_true (goodix_milan_print_validate_template (
          append_update, &after_study_info, &error));
        g_assert_no_error (error);
        g_assert_cmpuint (after_study_info.feature_count,
                          ==, combined_info.feature_count + 1);
        g_assert_cmpuint (after_study_info.maximum_features,
                          ==, combined_info.maximum_features);
        g_assert_cmpuint (after_study_info.queue_state,
                          ==, combined_info.queue_state);
        g_assert_cmpuint (after_study_info.queue_transaction_counter,
                          ==, combined_info.queue_transaction_counter);
        assert_generic_append_material (extracted, after_match, append_update);
        if (iteration == 0)
          first_append_update = g_bytes_ref (append_update);
        else
          g_assert_true (g_bytes_equal (append_update, first_append_update));
        goodix_study_queue_free (append_queue);
      }

      if (iteration == 0)
        first_after_match = g_bytes_ref (after_match);
      else
        g_assert_true (g_bytes_equal (after_match, first_after_match));
      g_test_message (
        "generated replay iteration=%zu score=%d generic-action0-queue=1 "
        "generic-append-action=%d",
        iteration, result.score, GOODIX_MILAN_STUDY_APPEND);
    }

  goodix_match_free_info (info);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/goodix53x5/milan/preprocess-accepted",
                   test_preprocess_accepted);
  g_test_add_func ("/goodix53x5/milan/preprocess-post-render-retry",
                   test_preprocess_post_render_retry);
  g_test_add_func ("/goodix53x5/milan/first-positive", test_first_positive);
  g_test_add_func ("/goodix53x5/milan/selection", test_selection_rows);
  g_test_add_func ("/goodix53x5/milan/gain-tail", test_gain_tail);
  g_test_add_func ("/goodix53x5/milan/generated-extraction",
                    test_generated_extraction);
  g_test_add_func ("/goodix53x5/milan/study-policy-actions",
                    test_study_policy_actions);
  g_test_add_func ("/goodix53x5/milan/generated-enrollment-prefix-lifecycle",
                    test_generated_enrollment_prefix_lifecycle);
  g_test_add_func ("/goodix53x5/milan/generated-production-replay",
                    test_generated_production_replay);
  return g_test_run ();
}
