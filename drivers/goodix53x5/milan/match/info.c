/*
 * Goodix 53x5 driver for libfprint - native Milan match information
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "milan/match/match.h"
#include "milan/match/info-private.h"
#include "milan/match/rescue.h"
#include "milan/milan.h"

#include <string.h>

#define GOODIX_MILAN_OPTIONAL_C7_LEN 5

static void
goodix_match_pack_rescue_mask (
  const guint8 dense[GOODIX_MILAN_MATCH_RESCUE_MASK_WIDTH *
                     GOODIX_MILAN_MATCH_RESCUE_MASK_HEIGHT],
  guint8       packed[GOODIX_MILAN_MATCH_RESCUE_MASK_SIZE])
{
  memset (packed, 0, GOODIX_MILAN_MATCH_RESCUE_MASK_SIZE);
  for (size_t y = 0; y < GOODIX_MILAN_MATCH_RESCUE_MASK_HEIGHT; y++)
    for (size_t x = 0; x < GOODIX_MILAN_MATCH_RESCUE_MASK_WIDTH; x++)
      if (dense[y * GOODIX_MILAN_MATCH_RESCUE_MASK_WIDTH + x])
        packed[y * GOODIX_MILAN_MATCH_RESCUE_MASK_STRIDE + x / 8] |=
          (guint8) (1U << (x & 7));
}

static size_t
goodix_match_record_limit (guint16 sensor_subtype,
                           int     coverage,
                           size_t  configured_limit)
{
  if (sensor_subtype == 12)
    {
      size_t limit = (size_t) coverage * configured_limit / 100;

      return limit > 0 ? limit : 1;
    }

  return ((size_t) coverage * configured_limit + 50) / 100;
}

static GoodixMilanExtractionStatus goodix_match_extract_planes (
  const guint8  *image,
  const guint8  *antifake_classification_plane,
  const guint16 *calibration,
  const guint16 *raw_frame,
  guint16        t_code,
  guint16        dac_high,
  guint16        dac_low,
  guint16        sensor_subtype,
  gint32         calibration_scalar,
  GoodixMatchInfo **result
#ifdef GOODIX53X5_DEBUG
  , GoodixMilanExtractionDiagnostics *diagnostics
#endif
  );

#ifdef GOODIX53X5_DEBUG
static void
goodix_match_hash_projection (
  const GoodixMilanAntifakeBlob *projection,
  gchar                          digest[GOODIX_MILAN_EXTRACTION_SHA256_SIZE])
{
  g_autofree gchar *checksum = g_compute_checksum_for_data (
    G_CHECKSUM_SHA256, goodix_milan_antifake_const_data (projection),
    GOODIX_MILAN_ANTIFAKE_DEFINED_MATERIAL_SIZE);

  g_strlcpy (digest, checksum ? checksum : "",
             GOODIX_MILAN_EXTRACTION_SHA256_SIZE);
}
#endif

GoodixMatchInfo *
goodix_match_extract_native (const guint8                     *image,
                             const GoodixMilanPreprocessState *preprocess_state,
                             const guint16                    *raw_frame,
                             guint16                           t_code,
                             guint16                           dac_high,
                             guint16                           dac_low,
                             guint16                           sensor_subtype)
{
  GoodixMatchInfo *info = NULL;

  (void) goodix_match_extract_native_result (
    image, preprocess_state, raw_frame, t_code, dac_high, dac_low,
    sensor_subtype, &info);
  return info;
}

GoodixMilanExtractionStatus
goodix_match_extract_native_result (
  const guint8                     *image,
  const GoodixMilanPreprocessState *preprocess_state,
  const guint16                    *raw_frame,
  guint16                           t_code,
  guint16                           dac_high,
  guint16                           dac_low,
  guint16                           sensor_subtype,
  GoodixMatchInfo                 **info)
{
  if (info)
    *info = NULL;
  if (!image || !preprocess_state || !raw_frame ||
      !preprocess_state->primary_contrast_valid || !info)
    return GOODIX_MILAN_EXTRACTION_INVALID;

  return goodix_match_extract_planes (
    image, preprocess_state->primary_contrast, preprocess_state->setup_map,
    raw_frame, t_code, dac_high, dac_low, sensor_subtype,
    preprocess_state->selected_refined, info
#ifdef GOODIX53X5_DEBUG
    , NULL
#endif
    );
}

#ifdef GOODIX53X5_DEBUG
GoodixMilanExtractionStatus
goodix_match_extract_native_result_debug (
  const guint8                     *image,
  const GoodixMilanPreprocessState *preprocess_state,
  const guint16                    *raw_frame,
  guint16                           t_code,
  guint16                           dac_high,
  guint16                           dac_low,
  guint16                           sensor_subtype,
  GoodixMatchInfo                 **info,
  GoodixMilanExtractionDiagnostics *diagnostics)
{
  if (info)
    *info = NULL;
  if (diagnostics)
    memset (diagnostics, 0, sizeof(*diagnostics));
  if (!image || !preprocess_state || !raw_frame ||
      !preprocess_state->primary_contrast_valid || !info)
    return GOODIX_MILAN_EXTRACTION_INVALID;

  return goodix_match_extract_planes (
    image, preprocess_state->primary_contrast, preprocess_state->setup_map,
    raw_frame, t_code, dac_high, dac_low, sensor_subtype,
    preprocess_state->selected_refined, info, diagnostics);
}
#endif

static GoodixMilanExtractionStatus
goodix_match_extract_planes (const guint8  *image,
                             const guint8  *antifake_classification_plane,
                             const guint16 *calibration,
                             const guint16 *raw_frame,
                             guint16        t_code,
                             guint16        dac_high,
                             guint16        dac_low,
                             guint16        sensor_subtype,
                             gint32         calibration_scalar,
                             GoodixMatchInfo **result
#ifdef GOODIX53X5_DEBUG
                             , GoodixMilanExtractionDiagnostics *diagnostics
#endif
                             )
{
  GoodixMatchInfo *info = NULL;
#ifdef GOODIX53X5_DEBUG
  g_autofree GoodixMilanAntifakeBlob *diagnostic_antifake = NULL;
#endif
  guint8 *cropped = NULL;
  guint8 *high = NULL;
  guint8 *low = NULL;
  guint8 *feature_mask = NULL;
  guint8 *orientation = NULL;
  guint8 *enhanced = NULL;
  guint8 *enhanced_bitmap = NULL;
  guint8 *inline_mask = NULL;
  GoodixMilanAntifakeBlob *antifake = NULL;
  GoodixMilanFeatureRecord *records = NULL;
  guint8 *feature_element = NULL;
  guint8 *milan_template = NULL;
  guint8 tail_state[0x520] = { 0 };
  GoodixMilanFeatureTemplateFields fields = { 0 };
  size_t record_count = 0;
  size_t zero_count = 0;
  size_t record_limit;
  size_t feature_element_size = 0;
  size_t milan_template_size = 0;
  guint8 enhanced_threshold;
  int quality;
  int coverage;
  GoodixMilanExtractionStatus status = GOODIX_MILAN_EXTRACTION_INVALID;

  *result = NULL;
  if (!image || !antifake_classification_plane)
    return status;
  info = g_new0 (GoodixMatchInfo, 1);
  cropped = g_malloc (104 * 88);
  high = g_malloc0 (286);
  low = g_malloc0 (286);
  feature_mask = g_malloc0 (52 * 44);
  orientation = g_malloc (104 * 88);
  enhanced = g_malloc (104 * 88);
  enhanced_bitmap = g_malloc0 (286);
  inline_mask = g_malloc0 (72);
  antifake = g_malloc0 (sizeof(*antifake));
  records = g_new0 (GoodixMilanFeatureRecord, 150);
  feature_element = g_malloc (7945 + 150 * 32 + GOODIX_MILAN_OPTIONAL_C7_LEN);
  milan_template = g_malloc (7945 + 150 * 32 + GOODIX_MILAN_OPTIONAL_C7_LEN +
                             1433);
  for (size_t row = 0; row < 88; row++)
    memcpy (cropped + row * 104,
            image + row * GOODIX_MILAN_SENSOR_COLUMNS + 2, 104);
  if (goodix_milan_preprocess_quality (
        image, GOODIX_MILAN_SENSOR_ROWS, GOODIX_MILAN_SENSOR_COLUMNS,
        &quality, &coverage) != 0)
    goto out;
  record_limit = goodix_match_record_limit (sensor_subtype, coverage, 150);
  if (goodix_milan_feature_base_maps (
        image, GOODIX_MILAN_SENSOR_ROWS, GOODIX_MILAN_SENSOR_COLUMNS,
        high, low, feature_mask, inline_mask) != 0 ||
      goodix_milan_feature_enhance (
        cropped, 88, 104, orientation, enhanced) != 0 ||
      goodix_milan_feature_enhanced_bitmap (
        enhanced, feature_mask, 88, 104, enhanced_bitmap,
        &enhanced_threshold) != 0 ||
      goodix_milan_feature_extract_records_mode (
        image, GOODIX_MILAN_SENSOR_ROWS, GOODIX_MILAN_SENSOR_COLUMNS, records,
        record_limit,
        &record_count, &zero_count, 1) != 0)
    goto out;
  if (calibration && raw_frame &&
      goodix_milan_antifake_build (
        calibration, raw_frame, antifake_classification_plane, feature_mask,
        52 * 44,
        GOODIX_MILAN_SENSOR_ROWS, GOODIX_MILAN_SENSOR_COLUMNS, t_code, dac_high,
        dac_low,
        sensor_subtype, calibration_scalar, antifake, sizeof(*antifake)) != 0)
    goto out;
#ifdef GOODIX53X5_DEBUG
  if (diagnostics && calibration && raw_frame &&
      (diagnostic_antifake = g_try_malloc0 (
         sizeof(*diagnostic_antifake))) != NULL)
    {
      GoodixMilanAntifakeBoundaryResult boundary = { 0 };
      int diagnostic_status = goodix_milan_antifake_build_with_boundary (
        calibration, raw_frame, antifake_classification_plane, feature_mask,
        52 * 44, GOODIX_MILAN_SENSOR_ROWS, GOODIX_MILAN_SENSOR_COLUMNS, t_code,
        dac_high, dac_low, sensor_subtype, calibration_scalar, diagnostic_antifake,
        sizeof(*diagnostic_antifake), &boundary);

      if (diagnostic_status == 0 ||
          diagnostic_status == GOODIX_MILAN_ANTIFAKE_AMBIGUOUS)
        {
          diagnostics->boundary_classification = boundary.classification;
          diagnostics->zero_candidate_count = boundary.zero_candidate_count;
          diagnostics->nonzero_candidate_count = boundary.nonzero_candidate_count;
          goodix_match_hash_projection (
            &boundary.zero_projection, diagnostics->zero_projection_sha256);
          goodix_match_hash_projection (
            &boundary.nonzero_projection,
            diagnostics->nonzero_projection_sha256);
        }
    }
#endif
  if (calibration && raw_frame &&
      goodix_milan_template_initialize_tail (
        image, GOODIX_MILAN_SENSOR_ROWS, GOODIX_MILAN_SENSOR_COLUMNS, tail_state,
        sizeof(tail_state)) != 0)
    goto out;
  fields.tagged_values[2] = (int32_t) zero_count;
  fields.tagged_values[3] = quality;
  fields.tagged_values[4] = coverage;
  if (sensor_subtype == 12)
    {
      size_t metadata_start = GOODIX_MILAN_SENSOR_COLUMNS - 8;
      gboolean metadata_present = TRUE;

      for (size_t column = 0; column < metadata_start; column++)
        if ((image[column] & 1) != (column & 1))
          {
            metadata_present = FALSE;
            break;
          }
      if (metadata_present)
        {
          guint32 low_bits = image[metadata_start] & 1;
          guint32 high_bits = image[metadata_start + 1] & 1;
          guint32 low_value = low_bits ? (high_bits ? 2 : 1)
                                       : (high_bits ? 3 : 0);
          guint32 high_value = (image[metadata_start + 3] & 1) |
                               ((image[metadata_start + 4] & 1) << 1) |
                               ((image[metadata_start + 5] & 1) << 2);

          fields.optional_c7 = (int32_t) (low_value + (high_value << 8));
        }
    }
  if (goodix_milan_template_pack_feature_element (
        high, enhanced_bitmap, inline_mask, low, records, record_count,
        antifake, &fields, feature_element,
        7945 + 150 * 32 + GOODIX_MILAN_OPTIONAL_C7_LEN,
        &feature_element_size) != 0 ||
      goodix_milan_template_pack_one_feature (
        feature_element, feature_element_size, tail_state, sizeof(tail_state),
        milan_template,
        7945 + 150 * 32 + GOODIX_MILAN_OPTIONAL_C7_LEN + 1433,
        &milan_template_size) != 0)
    goto out;
  info->template = g_bytes_new_take (milan_template, milan_template_size);
  milan_template = NULL;
  info->record_count = (int) record_count;
  info->partition_count = (int) zero_count;
  memcpy (info->feature_bitmaps.high_bitmap, high,
          sizeof(info->feature_bitmaps.high_bitmap));
  memcpy (info->feature_bitmaps.enhanced_bitmap, enhanced_bitmap,
          sizeof(info->feature_bitmaps.enhanced_bitmap));
  memcpy (info->feature_bitmaps.low_bitmap, low,
          sizeof(info->feature_bitmaps.low_bitmap));
  memcpy (info->inline_mask, inline_mask, sizeof(info->inline_mask));
  goodix_match_pack_rescue_mask (feature_mask, info->rescue_mask);
  memcpy (&info->antifake, antifake, sizeof(info->antifake));
  info->records = records;
  info->extraction_metadata.quality = quality;
  info->extraction_metadata.coverage = coverage;
  info->extraction_metadata.optional_c7 = fields.optional_c7;
  records = NULL;
  status = GOODIX_MILAN_EXTRACTION_OK;

out:
  g_free (milan_template);
  g_free (feature_element);
  g_free (records);
  g_free (antifake);
  g_free (inline_mask);
  g_free (enhanced_bitmap);
  g_free (enhanced);
  g_free (orientation);
  g_free (feature_mask);
  g_free (low);
  g_free (high);
  g_free (cropped);
  if (!info->template)
    g_clear_pointer (&info, g_free);
  *result = info;
  return status;
}

int
goodix_match_keypoints_count (GoodixMatchInfo *info)
{
  return info ? info->record_count : 0;
}

GoodixMatchInfo *
goodix_match_info_new_empty (void)
{
  return g_new0 (GoodixMatchInfo, 1);
}

void
goodix_match_info_clear (GoodixMatchInfo *info)
{
  if (!info)
    return;
  g_clear_pointer (&info->template, g_bytes_unref);
  g_clear_pointer (&info->records, g_free);
  memset (info, 0, sizeof(*info));
}

gboolean
goodix_match_info_is_complete (const GoodixMatchInfo *info)
{
  return info && info->template && info->records && info->record_count > 0 &&
         info->record_count <= 150 && info->partition_count >= 0 &&
         info->partition_count <= info->record_count;
}

gboolean
goodix_match_info_copy (GoodixMatchInfo       *destination,
                        const GoodixMatchInfo *source)
{
  GoodixMatchInfo copy = { 0 };
  const guint8 *template_data;
  gsize template_size;

  if (!destination || !goodix_match_info_is_complete (source))
    return FALSE;
  template_data = g_bytes_get_data (source->template, &template_size);
  copy.template = g_bytes_new (template_data, template_size);
  copy.records = g_memdup2 (
    source->records,
    (gsize) source->record_count * sizeof(*source->records));
  for (int i = 0; i < source->record_count; i++)
    {
      memset (copy.records[i].payload + 24, 0, 4);
      memset (copy.records[i].payload + 36, 0, 8);
    }
  copy.record_count = source->record_count;
  copy.partition_count = source->partition_count;
  memcpy (copy.feature_bitmaps.high_bitmap,
          source->feature_bitmaps.high_bitmap,
          sizeof(copy.feature_bitmaps.high_bitmap));
  memcpy (copy.feature_bitmaps.enhanced_bitmap,
          source->feature_bitmaps.enhanced_bitmap,
          sizeof(copy.feature_bitmaps.enhanced_bitmap));
  memcpy (copy.feature_bitmaps.low_bitmap,
          source->feature_bitmaps.low_bitmap,
          sizeof(copy.feature_bitmaps.low_bitmap));
  memcpy (copy.inline_mask, source->inline_mask, sizeof(copy.inline_mask));
  memcpy (copy.rescue_mask, source->rescue_mask, sizeof(copy.rescue_mask));
  memcpy (&copy.antifake, &source->antifake, sizeof(copy.antifake));
  copy.extraction_metadata.quality = source->extraction_metadata.quality;
  copy.extraction_metadata.coverage = source->extraction_metadata.coverage;
  copy.extraction_metadata.optional_c7 = source->extraction_metadata.optional_c7;

  goodix_match_info_clear (destination);
  *destination = copy;
  return TRUE;
}

void
goodix_match_free_info (GoodixMatchInfo *info)
{
  if (!info)
    return;
  goodix_match_info_clear (info);
  g_free (info);
}
