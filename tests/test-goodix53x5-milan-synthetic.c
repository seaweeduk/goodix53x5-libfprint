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
#include "drivers/goodix53x5/milan/match/selection.h"
#include "drivers/goodix53x5/milan/milan.h"
#include "drivers/goodix53x5/milan/preprocess/gain.h"
#include "drivers/goodix53x5/milan/print.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

#define PIXELS GOODIX_MILAN_SENSOR_PIXELS

static const char accepted_preprocess_sha256[] =
  "35645cfcc5edb705c8a225717f907987c1d90ec1a7d48b09ee378845a906a696";
static const char post_render_retry_sha256[] =
  "8b9629cb2aa7bf3d44a13c9ab087b961e02adf29644c237032597bf24401d943";
static const char feature_extraction_sha256[] =
  "ba26b6b929ecb077f911be480fecab2594f7e73a587bd83b99c2051bddb01824";
static const char feature_template_sha256[] =
  "af6b32f59d237c1e79fbec6e976612da29dc12d9d0290054c2e28f293bced9d2";
static const char feature_antifake_sha256[] =
  "2ec6a813e8a6b355693645aac1e60da4cc717bcdf57c3dbb5d12d7021b0e7572";

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
        live[index] = (uint16_t) (baseline - delta);
      }
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
  g_assert_cmpuint (state->profile9_class_counts.profile9_class1_count, ==, 0);
  g_assert_cmpuint (state->profile9_class_counts.profile9_class2_count, ==, 0);
  g_assert_cmpuint (state->profile9_class_counts.profile9_class3_count, ==,
                    PIXELS);
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
  g_assert_cmpint (state->post_render.primary_metric, ==, 110);
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
  g_autofree GoodixMilanPreprocessState *state = g_new0 (
    GoodixMilanPreprocessState, 1);
  g_autofree GoodixMilanUnpackedTemplate *unpacked = g_new0 (
    GoodixMilanUnpackedTemplate, 1);
  g_autofree uint16_t *setup = g_new (uint16_t, PIXELS);
  g_autofree uint16_t *live = g_new (uint16_t, PIXELS);
  g_autofree uint8_t *processed = g_new (uint8_t, PIXELS);
  GoodixMatchInfo *info = NULL;
  GoodixMilanProfileState profile = { 0 };
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
  int quality = -1;
  int coverage = -1;

  test_negative_orientation_scaling ();
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
  g_assert_cmpint (goodix_match_keypoints_count (info), ==, 150);

  extracted = goodix_match_serialize_template (info);
  bytes = g_bytes_get_data (extracted, &size);
  g_assert_cmpuint (size, >, 6);
  extracted_hash = sha256 (bytes, size);
  g_assert_cmpint (goodix_milan_template_unpack (
                     bytes + 6, size - 6, unpacked), ==, 0);
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
  g_assert_cmpint (goodix_milan_template_unpack (
                     bytes + 6, size - 6, unpacked), ==, 0);
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
  g_assert_true (schema == 3 && print_profile == 9 && sensor_type == 12 &&
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
  return g_test_run ();
}
