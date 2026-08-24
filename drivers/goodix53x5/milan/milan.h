/*
 * Goodix 53x5 driver for libfprint - Milan image preprocessing
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "milan/capacity.h"

#define GOODIX_MILAN_SENSOR_ROWS 88
#define GOODIX_MILAN_SENSOR_COLUMNS 108
#define GOODIX_MILAN_SENSOR_PIXELS \
  (GOODIX_MILAN_SENSOR_ROWS * GOODIX_MILAN_SENSOR_COLUMNS)
#define GOODIX_MILAN_EXTRACTION_CLASSIFICATION_ROWS 88
#define GOODIX_MILAN_EXTRACTION_CLASSIFICATION_COLUMNS 104
#define GOODIX_MILAN_EXTRACTION_CLASSIFICATION_PIXELS \
  (GOODIX_MILAN_EXTRACTION_CLASSIFICATION_ROWS * \
   GOODIX_MILAN_EXTRACTION_CLASSIFICATION_COLUMNS)
#define GOODIX_MILAN_PREPROCESS_RETRY_RAW_ADMISSION 0x29aa
#define GOODIX_MILAN_PREPROCESS_RETRY_SETUP_ADMISSION 0x29bb
#define GOODIX_MILAN_PREPROCESS_RETRY 0x7531
#define GOODIX_MILAN_PREPROCESS_RETRY_CLASSIFICATION 0xc351
#define GOODIX_MILAN_ANTIFAKE_AMBIGUOUS 1
#define GOODIX_MILAN_ANTIFAKE_SIZE 0x1abcU
#define GOODIX_MILAN_ANTIFAKE_DEFINED_MATERIAL_SIZE GOODIX_MILAN_ANTIFAKE_SIZE
#define GOODIX_MILAN_ANTIFAKE_RECORD_SIZE 48U
#define GOODIX_MILAN_ANTIFAKE_RECORD_CAPACITY 100U
#define GOODIX_MILAN_ANTIFAKE_RECORDS_OFFSET 0U
#define GOODIX_MILAN_ANTIFAKE_RECORD_X_OFFSET 0U
#define GOODIX_MILAN_ANTIFAKE_RECORD_Y_OFFSET 4U
#define GOODIX_MILAN_ANTIFAKE_RECORD_DATA_8_OFFSET 8U
#define GOODIX_MILAN_ANTIFAKE_RECORD_DATA_16_OFFSET 16U
#define GOODIX_MILAN_ANTIFAKE_RECORD_DATA_24_OFFSET 24U
#define GOODIX_MILAN_ANTIFAKE_RECORD_DATA_32_OFFSET 32U
#define GOODIX_MILAN_ANTIFAKE_RECORD_DATA_40_OFFSET 40U
#define GOODIX_MILAN_ANTIFAKE_RECORD_DATA_44_OFFSET 44U
#define GOODIX_MILAN_ANTIFAKE_RECORDS_SIZE \
  (GOODIX_MILAN_ANTIFAKE_RECORD_SIZE * GOODIX_MILAN_ANTIFAKE_RECORD_CAPACITY)
#define GOODIX_MILAN_ANTIFAKE_CANDIDATE_COUNT_OFFSET 0x12c0U
#define GOODIX_MILAN_ANTIFAKE_TEXTURE_OFFSET 0x12c4U
#define GOODIX_MILAN_ANTIFAKE_MEAN_OFFSET 0x12c8U
#define GOODIX_MILAN_ANTIFAKE_VARIATION_OFFSET 0x12ccU
#define GOODIX_MILAN_ANTIFAKE_BOUNDARY_OFFSET 0x12d0U
#define GOODIX_MILAN_ANTIFAKE_MODEL_OFFSET 0x12d8U
#define GOODIX_MILAN_ANTIFAKE_MASK_OFFSET 0x12dcU
#define GOODIX_MILAN_ANTIFAKE_MASK_SIZE 2000U
#define GOODIX_MILAN_ANTIFAKE_CALIBRATION_SCALAR_OFFSET 0x1aacU
#define GOODIX_MILAN_ANTIFAKE_THRESHOLD_OFFSET 0x1ab0U
#define GOODIX_MILAN_ANTIFAKE_PAIR_SCORE_OFFSET 0x1ab4U
#define GOODIX_MILAN_TEMPLATE_MAX_SIZE (1024U * 1024U)

typedef struct
{
  uint8_t bytes[GOODIX_MILAN_ANTIFAKE_SIZE];
} GoodixMilanAntifakeBlob;

_Static_assert (sizeof (GoodixMilanAntifakeBlob) == GOODIX_MILAN_ANTIFAKE_SIZE,
                "Milan anti-fake blob size changed");
_Static_assert (_Alignof (GoodixMilanAntifakeBlob) == _Alignof (uint8_t),
                "Milan anti-fake blob alignment changed");
_Static_assert (offsetof (GoodixMilanAntifakeBlob, bytes) == 0,
                "Milan anti-fake blob data moved");
_Static_assert (GOODIX_MILAN_ANTIFAKE_RECORDS_SIZE ==
                  GOODIX_MILAN_ANTIFAKE_CANDIDATE_COUNT_OFFSET,
                "Milan anti-fake record extent changed");
_Static_assert (GOODIX_MILAN_ANTIFAKE_RECORD_DATA_44_OFFSET + 4U ==
                  GOODIX_MILAN_ANTIFAKE_RECORD_SIZE,
                "Milan anti-fake record layout changed");
_Static_assert (GOODIX_MILAN_ANTIFAKE_MASK_OFFSET +
                  GOODIX_MILAN_ANTIFAKE_MASK_SIZE ==
                  GOODIX_MILAN_ANTIFAKE_CALIBRATION_SCALAR_OFFSET,
                "Milan anti-fake mask extent changed");
_Static_assert (GOODIX_MILAN_ANTIFAKE_PAIR_SCORE_OFFSET + sizeof (int32_t) <=
                  GOODIX_MILAN_ANTIFAKE_SIZE,
                "Milan anti-fake pair score exceeds the blob");

static inline uint8_t *
goodix_milan_antifake_data (GoodixMilanAntifakeBlob *blob)
{
  return blob ? blob->bytes : NULL;
}

static inline const uint8_t *
goodix_milan_antifake_const_data (const GoodixMilanAntifakeBlob *blob)
{
  return blob ? blob->bytes : NULL;
}

static inline uint8_t *
goodix_milan_antifake_record (GoodixMilanAntifakeBlob *blob,
                              size_t                    index)
{
  return blob->bytes + GOODIX_MILAN_ANTIFAKE_RECORDS_OFFSET +
         index * GOODIX_MILAN_ANTIFAKE_RECORD_SIZE;
}

static inline const uint8_t *
goodix_milan_antifake_const_record (const GoodixMilanAntifakeBlob *blob,
                                    size_t                         index)
{
  return blob->bytes + GOODIX_MILAN_ANTIFAKE_RECORDS_OFFSET +
         index * GOODIX_MILAN_ANTIFAKE_RECORD_SIZE;
}

static inline int32_t
goodix_milan_antifake_record_x (const uint8_t *record)
{
  int32_t value;

  memcpy (&value, record + GOODIX_MILAN_ANTIFAKE_RECORD_X_OFFSET,
          sizeof(value));
  return value;
}

static inline void
goodix_milan_antifake_set_record_x (uint8_t *record,
                                    int32_t  value)
{
  memcpy (record + GOODIX_MILAN_ANTIFAKE_RECORD_X_OFFSET, &value,
          sizeof(value));
}

static inline int32_t
goodix_milan_antifake_record_y (const uint8_t *record)
{
  int32_t value;

  memcpy (&value, record + GOODIX_MILAN_ANTIFAKE_RECORD_Y_OFFSET,
          sizeof(value));
  return value;
}

static inline void
goodix_milan_antifake_set_record_y (uint8_t *record,
                                    int32_t  value)
{
  memcpy (record + GOODIX_MILAN_ANTIFAKE_RECORD_Y_OFFSET, &value,
          sizeof(value));
}

static inline uint8_t *
goodix_milan_antifake_mask (GoodixMilanAntifakeBlob *blob)
{
  return blob->bytes + GOODIX_MILAN_ANTIFAKE_MASK_OFFSET;
}

static inline const uint8_t *
goodix_milan_antifake_const_mask (const GoodixMilanAntifakeBlob *blob)
{
  return blob->bytes + GOODIX_MILAN_ANTIFAKE_MASK_OFFSET;
}

static inline uint32_t
goodix_milan_antifake_candidate_count (const GoodixMilanAntifakeBlob *blob)
{
  uint32_t value;

  memcpy (&value, blob->bytes + GOODIX_MILAN_ANTIFAKE_CANDIDATE_COUNT_OFFSET,
          sizeof(value));
  return value;
}

static inline void
goodix_milan_antifake_set_candidate_count (GoodixMilanAntifakeBlob *blob,
                                           uint32_t                  value)
{
  memcpy (blob->bytes + GOODIX_MILAN_ANTIFAKE_CANDIDATE_COUNT_OFFSET, &value,
          sizeof(value));
}

#define GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS(name, offset) \
  static inline int32_t \
  goodix_milan_antifake_##name (const GoodixMilanAntifakeBlob *blob) \
  { \
    int32_t value; \
    memcpy (&value, blob->bytes + (offset), sizeof(value)); \
    return value; \
  } \
  static inline void \
  goodix_milan_antifake_set_##name (GoodixMilanAntifakeBlob *blob, \
                                    int32_t                   value) \
  { \
    memcpy (blob->bytes + (offset), &value, sizeof(value)); \
  }

GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (texture,
                                        GOODIX_MILAN_ANTIFAKE_TEXTURE_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (mean,
                                        GOODIX_MILAN_ANTIFAKE_MEAN_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (variation,
                                        GOODIX_MILAN_ANTIFAKE_VARIATION_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (boundary,
                                        GOODIX_MILAN_ANTIFAKE_BOUNDARY_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (model,
                                        GOODIX_MILAN_ANTIFAKE_MODEL_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (
  calibration_scalar, GOODIX_MILAN_ANTIFAKE_CALIBRATION_SCALAR_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (threshold,
                                        GOODIX_MILAN_ANTIFAKE_THRESHOLD_OFFSET)
GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS (pair_score,
                                        GOODIX_MILAN_ANTIFAKE_PAIR_SCORE_OFFSET)

#undef GOODIX_MILAN_ANTIFAKE_SCALAR_ACCESSORS

typedef enum
{
  GOODIX_MILAN_PURPOSE_IDENTIFY = 0,
  GOODIX_MILAN_PURPOSE_ENROLL = 1,
} GoodixMilanPreprocessPurpose;

typedef struct
{
  uint32_t calibration_ready;
} GoodixMilanProfileState;

typedef enum
{
  GOODIX_MILAN_ANTIFAKE_BOUNDARY_NOT_EVALUATED,
  GOODIX_MILAN_ANTIFAKE_BOUNDARY_STABLE,
  GOODIX_MILAN_ANTIFAKE_BOUNDARY_AMBIGUOUS,
} GoodixMilanAntifakeBoundaryClass;

typedef struct
{
  GoodixMilanAntifakeBoundaryClass classification;
  uint32_t                         zero_candidate_count;
  uint32_t                         nonzero_candidate_count;
  GoodixMilanAntifakeBlob          zero_projection;
  GoodixMilanAntifakeBlob          nonzero_projection;
} GoodixMilanAntifakeBoundaryResult;

typedef struct
{
  int32_t primary_metric;
  int32_t fallback_metric;
  int32_t disagreement;
  int32_t component_score;
  int32_t component_flag;
  int32_t quality_gate;
  int32_t update_applied;
  int32_t status;
} GoodixMilanPostRenderObservations;

typedef struct
{
  uint32_t profile9_class1_count;
  uint32_t profile9_class2_count;
  uint32_t profile9_class3_count;
} GoodixMilanProfile9ClassCounts;

typedef struct
{
  uint8_t  retained_class_planes[3]
                                [GOODIX_MILAN_EXTRACTION_CLASSIFICATION_PIXELS];
  uint32_t retained_count;
  int32_t  prior_coverage;
  uint32_t high_class_hysteresis;
  int32_t  prior_merged_high_class;
} GoodixMilanExtractionClassificationState;

typedef struct
{
  uint8_t primary_histogram_state;
  uint8_t prior_selected_plane;
  uint8_t promoted_secondary_histogram_state;
} GoodixMilanExtractionAuxiliaryState;

typedef struct
{
  uint16_t setup_map[GOODIX_MILAN_SENSOR_PIXELS];
  uint16_t calibration_map[GOODIX_MILAN_SENSOR_PIXELS];
  uint16_t coarse_reference[GOODIX_MILAN_SENSOR_PIXELS / 4];
  uint16_t auxiliary_gain_map[GOODIX_MILAN_SENSOR_PIXELS];
  uint16_t secondary_auxiliary_gain_map[GOODIX_MILAN_SENSOR_PIXELS];
  uint16_t application_gain_map[GOODIX_MILAN_SENSOR_PIXELS];
  uint32_t sample_count;
  uint32_t stable_count;
  uint32_t update_state;
  uint32_t auxiliary_sample_count;
  uint32_t profile9_history_count;
  uint32_t profile9_history_update_count;
  uint32_t profile9_history_mask_threshold;
  uint32_t profile9_history_mask_average;
  uint16_t profile9_history_reference[GOODIX_MILAN_SENSOR_PIXELS];
  uint8_t  profile9_reference_age[GOODIX_MILAN_SENSOR_PIXELS];
  uint8_t  profile9_component_age[GOODIX_MILAN_SENSOR_PIXELS];
  GoodixMilanProfile9ClassCounts profile9_class_counts;
  uint8_t  primary_contrast[GOODIX_MILAN_SENSOR_PIXELS];
  int32_t  primary_contrast_valid;
  int32_t  selected_refined;
  GoodixMilanPostRenderObservations post_render;
  /* Retained planes/hysteresis persist in generation-owned preprocess state.
   * Auxiliary bytes describe the latest completed preprocessing call and feed
   * extraction/matcher policy. */
  GoodixMilanExtractionClassificationState extraction_classification;
  GoodixMilanExtractionAuxiliaryState extraction_auxiliary;
  uint8_t application_gain_initialized;
} GoodixMilanPreprocessState;

_Static_assert (sizeof (GoodixMilanProfile9ClassCounts) == 12,
                "Milan profile-9 class counts size changed");
_Static_assert (_Alignof (GoodixMilanProfile9ClassCounts) == 4,
                "Milan profile-9 class counts alignment changed");
_Static_assert (offsetof (GoodixMilanProfile9ClassCounts,
                          profile9_class1_count) == 0,
                "Milan profile-9 class-1 count moved");
_Static_assert (offsetof (GoodixMilanProfile9ClassCounts,
                          profile9_class2_count) == 4,
                "Milan profile-9 class-2 count moved");
_Static_assert (offsetof (GoodixMilanProfile9ClassCounts,
                          profile9_class3_count) == 8,
                "Milan profile-9 class-3 count moved");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          profile9_class_counts) == 137840,
                "Milan preprocess profile-9 class counts moved");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          profile9_class_counts) +
                  offsetof (GoodixMilanProfile9ClassCounts,
                            profile9_class1_count) == 137840,
                "Milan preprocess profile-9 class-1 count moved");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          profile9_class_counts) +
                  offsetof (GoodixMilanProfile9ClassCounts,
                            profile9_class2_count) == 137844,
                "Milan preprocess profile-9 class-2 count moved");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          profile9_class_counts) +
                  offsetof (GoodixMilanProfile9ClassCounts,
                            profile9_class3_count) == 137848,
                "Milan preprocess profile-9 class-3 count moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, primary_contrast) ==
                  137852,
                "Milan preprocess primary contrast moved");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          primary_contrast_valid) == 147356,
                "Milan preprocess primary contrast validity moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, selected_refined) ==
                  147360,
                "Milan preprocess selected output moved");
_Static_assert (sizeof (GoodixMilanPostRenderObservations) == 32,
                "Milan post-render observations size changed");
_Static_assert (_Alignof (GoodixMilanPostRenderObservations) == 4,
                "Milan post-render observations alignment changed");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, primary_metric) == 0,
                "Milan post-render primary metric moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, fallback_metric) == 4,
                "Milan post-render fallback metric moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, disagreement) == 8,
                "Milan post-render disagreement moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, component_score) == 12,
                "Milan post-render component score moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, component_flag) == 16,
                "Milan post-render component flag moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, quality_gate) == 20,
                "Milan post-render quality gate moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, update_applied) == 24,
                "Milan post-render update applied moved");
_Static_assert (offsetof (GoodixMilanPostRenderObservations, status) == 28,
                "Milan post-render status moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) == 147364,
                "Milan preprocess post-render observations moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, primary_metric) ==
                  147364,
                "Milan preprocess post-render primary metric moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, fallback_metric) ==
                  147368,
                "Milan preprocess post-render fallback metric moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, disagreement) ==
                  147372,
                "Milan preprocess post-render disagreement moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, component_score) ==
                  147376,
                "Milan preprocess post-render component score moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, component_flag) ==
                  147380,
                "Milan preprocess post-render component flag moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, quality_gate) ==
                  147384,
                "Milan preprocess post-render quality gate moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, update_applied) ==
                  147388,
                "Milan preprocess post-render update applied moved");
_Static_assert (offsetof (GoodixMilanPreprocessState, post_render) +
                  offsetof (GoodixMilanPostRenderObservations, status) == 147392,
                "Milan preprocess post-render status moved");
_Static_assert (sizeof (GoodixMilanExtractionClassificationState) == 27472,
                "Milan extraction classification state size changed");
_Static_assert (_Alignof (GoodixMilanExtractionClassificationState) == 4,
                "Milan extraction classification state alignment changed");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          extraction_classification) == 147396,
                "Milan extraction classification state moved");
_Static_assert (offsetof (GoodixMilanExtractionClassificationState,
                          retained_count) == 27456,
                "Milan extraction retained count moved");
_Static_assert (offsetof (GoodixMilanExtractionClassificationState,
                          prior_coverage) == 27460,
                "Milan extraction prior coverage moved");
_Static_assert (offsetof (GoodixMilanExtractionClassificationState,
                          high_class_hysteresis) == 27464,
                "Milan extraction hysteresis moved");
_Static_assert (offsetof (GoodixMilanExtractionClassificationState,
                          prior_merged_high_class) == 27468,
                "Milan extraction prior high class moved");
_Static_assert (sizeof (GoodixMilanExtractionAuxiliaryState) == 3,
                "Milan extraction auxiliary state size changed");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          extraction_auxiliary) == 174868,
                "Milan extraction auxiliary state moved");
_Static_assert (offsetof (GoodixMilanPreprocessState,
                          application_gain_initialized) == 174871,
                "Milan application gain initialization state moved");
_Static_assert (sizeof (GoodixMilanPreprocessState) == 174872,
                "Milan preprocess state size changed");
_Static_assert (_Alignof (GoodixMilanPreprocessState) == 4,
                "Milan preprocess state alignment changed");

void goodix_milan_preprocess_reset (
  GoodixMilanPreprocessState *state);

int goodix_milan_preprocess_clamp_and_admit_raw (
  uint16_t *frame,
  size_t    rows,
  size_t    columns,
  size_t    border,
  unsigned  required_percent);

int goodix_milan_preprocess (
  GoodixMilanPreprocessState *state,
  GoodixMilanProfileState    *profile_state,
  const uint16_t             *setup_frame,
  const uint16_t             *live_frame,
  GoodixMilanPreprocessPurpose purpose,
  uint8_t                    *output,
  int                        *quality,
  int                        *coverage);

int goodix_milan_preprocess_normalize (uint16_t *frame,
                                   size_t    rows,
                                   size_t    columns);

int goodix_milan_preprocess_make_setup_map (const uint16_t *frame,
                                        size_t          rows,
                                        size_t          columns,
                                        uint16_t       *setup_map,
                                        uint32_t       *rounded_mean);

int goodix_milan_preprocess_build_mask (const uint16_t *normalized_live,
                                    const uint16_t *setup_map,
                                    size_t          rows,
                                    size_t          columns,
                                    uint8_t         *mask,
                                    uint16_t        *threshold,
                                    uint16_t        *valid_percent);

int goodix_milan_preprocess_no_update_frame (const uint16_t *normalized_live,
                                         const uint16_t *setup_map,
                                         size_t          rows,
                                         size_t          columns,
                                         uint16_t       *output);

int goodix_milan_profile9_build_contrast_mask (
  const uint16_t *normalized_live,
  const uint16_t *setup_map,
  size_t          rows,
  size_t          columns,
  uint8_t        *contrast_mask,
  size_t         *admitted_pixels);

int goodix_milan_profile9_build_broken_mask (
  GoodixMilanPreprocessState *state,
  const uint16_t             *difference,
  const uint16_t             *setup_map,
  const uint16_t             *normalized_live,
  const uint8_t              *contrast_mask,
  size_t                      rows,
  size_t                      columns,
  uint8_t                    *broken_mask,
  uint8_t                    *class_plane,
  int                        *mode,
  int                        *apply_mask);

int goodix_milan_preprocess_refine (const uint16_t *source,
                                const uint8_t  *mask,
                                uint16_t        valid_percent,
                                size_t          rows,
                                size_t          columns,
                                uint8_t         *output);

int goodix_milan_preprocess_selection_metric (const uint8_t *frame,
                                           const uint8_t *mask,
                                           size_t         rows,
                                           size_t         columns);

int goodix_milan_preprocess_masked_correlation (const uint8_t *first,
                                             const uint8_t *second,
                                             const uint8_t *mask,
                                             size_t         rows,
                                             size_t         columns);

int goodix_milan_preprocess_select_output (const uint8_t *contrast,
                                       const uint8_t *refined,
                                       const uint8_t *mask,
                                       size_t         rows,
                                       size_t         columns,
                                       int            threshold,
                                       uint8_t       *output,
                                       int           *selected_refined);

int goodix_milan_preprocess_quality_mask_coverage (const uint8_t *mask,
                                                size_t         count);

int goodix_milan_preprocess_quality_coverage_mask (const uint8_t *frame,
                                                size_t         rows,
                                                size_t         columns,
                                                uint8_t       *mask,
                                                int           *raw_coverage);
int goodix_milan_preprocess_selection_mask (const uint8_t *frame,
                                         size_t         rows,
                                         size_t         columns,
                                         uint8_t       *mask);
int goodix_milan_preprocess_quality_valid_mask (const uint8_t *frame,
                                             size_t         rows,
                                             size_t         columns,
                                             uint8_t       *mask,
                                             int           *valid_score);

int goodix_milan_template_initialize_tail (const uint8_t *frame,
                                                   size_t         rows,
                                                   size_t         columns,
                                                   uint8_t       *tail_state,
                                                   size_t         tail_size);

int goodix_milan_preprocess_quality (const uint8_t *frame,
                                 size_t         rows,
                                 size_t         columns,
                                 int           *quality,
                                 int           *coverage);

int goodix_milan_feature_base_maps (const uint8_t *frame,
                                             size_t         rows,
                                             size_t         columns,
                                             uint8_t       *high_bitmap,
                                             uint8_t       *low_bitmap,
                                             uint8_t       *feature_mask,
                                             uint8_t       *inline_mask);

int goodix_milan_feature_enhance (const uint8_t *frame,
                                          size_t         rows,
                                          size_t         columns,
                                          uint8_t       *orientation,
                                          uint8_t       *output);

int goodix_milan_feature_enhanced_bitmap (
  const uint8_t *enhanced,
  const uint8_t *feature_mask,
  size_t         rows,
  size_t         columns,
  uint8_t       *bitmap,
  uint8_t       *threshold);

void goodix_milan_feature_transform_record (uint8_t *record,
                                             int      reverse_bits);

size_t goodix_milan_feature_partition_records (uint8_t *records,
                                                 size_t   record_count);

typedef struct
{
  int32_t x;
  int32_t y;
  int32_t scale;
  int32_t response;
} GoodixMilanFeatureExtremum;

size_t goodix_milan_feature_collect_extrema (
  const uint16_t             *scales,
  size_t                      rows,
  size_t                      columns,
  GoodixMilanFeatureExtremum *extrema,
  size_t                      capacity);

typedef struct
{
  int32_t x;
  int32_t y;
  int32_t scale;
  int32_t strength;
  int16_t refined_x;
  int16_t refined_y;
  int32_t scale_value;
} GoodixMilanFeatureCandidate;

int goodix_milan_feature_refine_extremum (
  const uint16_t             *scales,
  size_t                      rows,
  size_t                      columns,
  GoodixMilanFeatureCandidate *candidate,
  uint32_t                   *curvature);

typedef struct
{
  uint16_t foreground;
  int16_t refined_x;
  int16_t refined_y;
  int16_t orientation;
  int32_t strength;
  uint8_t payload[44];
} GoodixMilanFeatureRecord;

typedef struct
{
  int32_t tagged_values[11];
  int32_t optional_c7;
} GoodixMilanFeatureTemplateFields;

typedef struct
{
  const uint8_t *high_bitmap;
  const uint8_t *enhanced_bitmap;
  const uint8_t *inline_mask;
  const uint8_t *low_bitmap;
  const uint8_t *packed_records;
  const GoodixMilanAntifakeBlob *antifake;
  size_t         record_count;
  GoodixMilanFeatureTemplateFields fields;
} GoodixMilanFeatureView;

int goodix_milan_template_parse_feature_element (
  const uint8_t          *packed,
  size_t                  packed_size,
  GoodixMilanFeatureView *view);

GoodixMilanAntifakeBlob *goodix_milan_template_mutable_feature_antifake (
  uint8_t *packed,
  size_t   packed_size);

int goodix_milan_feature_unpack_template_records (
  const uint8_t            *packed,
  size_t                    record_count,
  size_t                    partition_count,
  GoodixMilanFeatureRecord *records,
  size_t                    record_capacity);

typedef struct
{
  int32_t index;
  int32_t values[7];
} GoodixMilanTemplateRelation;

int goodix_milan_estimate_relation (
  const GoodixMilanFeatureRecord *prior_records,
  size_t                          prior_record_count,
  const GoodixMilanFeatureRecord *current_records,
  size_t                          current_record_count,
  int32_t                         relation_index,
  GoodixMilanTemplateRelation    *relation);

int goodix_milan_registration_gate_metrics (
  const GoodixMilanFeatureView *prior,
  const GoodixMilanFeatureView *current,
  const int32_t                 transform[6],
  int32_t                      *registration_detail,
  int32_t                      *registration_coverage);

int goodix_milan_feature_mask_forward_overlap (
  const GoodixMilanFeatureView *first_feature,
  const GoodixMilanFeatureView *second_feature,
  const int32_t                 transform[6],
  int32_t                       half_resolution,
  int32_t                      *overlap,
  int32_t                      *overlap_count);
int goodix_milan_match_overlap_metrics (
  const GoodixMilanFeatureView *enrolled_feature,
  const GoodixMilanFeatureView *probe_feature,
  const int32_t                 transform[6],
  int32_t                      *overlap_score,
  int32_t                      *overlap_coverage,
  int32_t                      *overlap_detail,
  int32_t                       low_metrics[3]);

int goodix_milan_filter_recognition_pairs (
  const GoodixMilanFeatureRecord *enrolled_records,
  const GoodixMilanFeatureRecord *probe_records,
  const int32_t                  *pairs,
  size_t                          pair_count,
  int32_t                         transform[6],
  size_t                         *filtered_count,
  int32_t                        *residual);

int goodix_milan_match_initial_flags (
  const int32_t metrics[77],
  int32_t       image_quality,
  int32_t       image_coverage,
  const int32_t configuration[19],
  int32_t      *match_flag,
  int32_t      *candidate_flag,
  int32_t      *optional_flag);

int goodix_milan_match_transform_proximity (
  const int32_t transform[6],
  int32_t       mode,
  uint32_t      sensor_type);

int goodix_milan_match_bitmap_classes (
  const uint8_t *target_bitmap,
  const uint8_t *source_bitmap,
  const uint8_t *overlap_mask,
  size_t         bitmap_rows,
  size_t         bitmap_columns,
  size_t         overlap_x,
  size_t         overlap_y,
  size_t         overlap_rows,
  size_t         overlap_columns,
  const int32_t  transform[6],
  int32_t        classes[4],
  int32_t       *valid_count);

int32_t goodix_milan_match_secondary_result (
  int32_t primary_score,
  int32_t secondary_score,
  int32_t secondary_detail);

int goodix_milan_match_overlap_result (
  const int32_t classes[4],
  int32_t       valid_count,
  int32_t       full_count,
  int32_t       mode,
  const int32_t weights[3],
  int32_t       context_count,
  int32_t      *context_confidence,
  int32_t      *score,
  int32_t      *coverage,
  int32_t      *detail);

int goodix_milan_match_low_bitmap_metrics (
  const uint8_t enrolled_bitmap[286],
  const uint8_t enrolled_inline_mask[72],
  const uint8_t probe_bitmap[286],
  const uint8_t probe_inline_mask[72],
  const int32_t transform[6],
  int32_t       metrics[3]);

#ifdef GOODIX53X5_DEBUG
typedef struct
{
  int32_t  order_index;
  int32_t  enrolled_feature_index;
  int32_t  probe_feature_index;
  int32_t  metrics[77];
  int32_t  transform[6];
  int32_t  final_flags[2];
  int32_t  recognition_mode_before;
  int32_t  recognition_mode_after;
  int32_t  outer_eligible;
  int32_t  contributes;
  int32_t  q8_contribution;
  int32_t  blocking_recorded;
  int32_t  blocking_metric;
} GoodixMilanMatchCandidateDiagnostic;

typedef struct
{
  int32_t descriptor_score;
  int32_t filtered_count;
  int32_t overlap_score;
  int32_t overlap_detail;
  int32_t overlap_coverage;
  int32_t transform[6];
  int32_t fallback_count;
  int32_t fallback_feature_index;
  int32_t fallback_metrics[15];
  int32_t feature_metrics[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY][2][15];
  int32_t feature_transforms[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY][2][6];
  int32_t direct_aggregate[15];
  int32_t policy_aggregate[15];
  int32_t probe_policy_aggregate[15];
  int32_t direct_feature_index;
  int32_t policy_feature_index;
  int32_t probe_policy_feature_index;
  int32_t candidate_count;
  GoodixMilanMatchCandidateDiagnostic
    candidates[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  int32_t q8_sum;
  int32_t q8_contributor_count;
  int32_t final_selected_feature_index;
  int32_t probe_optional_c7;
  int32_t initial_packed_policy_mode;
  int32_t queue_study_evidence;
  int32_t queue_configuration_enabled;
  int32_t queue_probe_low_class;
  int32_t queue_accumulated_high_class;
  int32_t queue_status_count;
  int32_t postloop_blocking_count;
  int32_t postloop_blocking_sum;
  int32_t postloop_blocking_override;
} GoodixMilanMatchDiagnostics;
#endif

typedef struct
{
  int32_t relation_count;
  int32_t relation_values[7];
  int     relation_valid;
} GoodixMilanMatchRelationPublication;

typedef struct
{
  int32_t study_finalization_gate;
  int32_t study_action_gate;
  int32_t queue_candidate_eligible;
} GoodixMilanMatchStudyControl;

typedef struct
{
  size_t  matched_feature_index;
  int32_t score;
  int32_t match_transform[6];
  GoodixMilanMatchRelationPublication relation;
  uint64_t direct_positive_feature_mask;
  uint64_t contributor_feature_mask;
  uint64_t lifecycle_update_feature_mask;
  size_t retained_evidence_count;
  int32_t retained_evidence_feature_indices[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  int32_t retained_evidence_transforms[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY][6];
  int32_t retained_evidence_flag;
  GoodixMilanMatchStudyControl study_control;
} GoodixMilanMatchResult;

_Static_assert (offsetof (GoodixMilanMatchRelationPublication,
                          relation_count) == 0,
                "match relation count offset changed");
_Static_assert (offsetof (GoodixMilanMatchRelationPublication,
                          relation_values) == 4,
                "match relation values offset changed");
_Static_assert (offsetof (GoodixMilanMatchRelationPublication,
                          relation_valid) == 32,
                "match relation validity offset changed");
_Static_assert (sizeof (GoodixMilanMatchRelationPublication) == 36,
                "match relation publication size changed");
_Static_assert (_Alignof (GoodixMilanMatchRelationPublication) == 4,
                "match relation publication alignment changed");
_Static_assert (offsetof (GoodixMilanMatchStudyControl,
                          study_finalization_gate) == 0,
                "match study finalization gate offset changed");
_Static_assert (offsetof (GoodixMilanMatchStudyControl,
                          study_action_gate) == 4,
                "match study action gate offset changed");
_Static_assert (offsetof (GoodixMilanMatchStudyControl,
                          queue_candidate_eligible) == 8,
                "match study queue eligibility offset changed");
_Static_assert (sizeof (GoodixMilanMatchStudyControl) == 12,
                "match study control size changed");
_Static_assert (_Alignof (GoodixMilanMatchStudyControl) == 4,
                "match study control alignment changed");
_Static_assert (offsetof (GoodixMilanMatchResult, relation) == 36,
                "match result relation offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, relation) +
                  offsetof (GoodixMilanMatchRelationPublication,
                            relation_count) == 36,
                "match result relation count offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, relation) +
                  offsetof (GoodixMilanMatchRelationPublication,
                            relation_values) == 40,
                "match result relation values offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, relation) +
                  offsetof (GoodixMilanMatchRelationPublication,
                            relation_valid) == 68,
                "match result relation validity offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          direct_positive_feature_mask) == 72,
                "match result direct mask offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          contributor_feature_mask) == 80,
                "match result contributor mask offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          lifecycle_update_feature_mask) == 88,
                "match result lifecycle mask offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          retained_evidence_count) == 96,
                "match result retained count offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          retained_evidence_feature_indices) == 104,
                "match result retained indices offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          retained_evidence_transforms) == 304,
                "match result retained transforms offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult,
                          retained_evidence_flag) == 1504,
                "match result retained flag offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, study_control) == 1508,
                "match result study control offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, study_control) +
                  offsetof (GoodixMilanMatchStudyControl,
                            study_finalization_gate) == 1508,
                "match result finalization gate offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, study_control) +
                  offsetof (GoodixMilanMatchStudyControl,
                            study_action_gate) == 1512,
                "match result action gate offset changed");
_Static_assert (offsetof (GoodixMilanMatchResult, study_control) +
                  offsetof (GoodixMilanMatchStudyControl,
                            queue_candidate_eligible) == 1516,
                "match result queue eligibility offset changed");
_Static_assert (sizeof (GoodixMilanMatchResult) == 1520,
                "match result size changed");
_Static_assert (_Alignof (GoodixMilanMatchResult) == 8,
                "match result alignment changed");

typedef struct
{
  size_t   feature_count;
  uint32_t order[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  int32_t  active[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  int32_t  generation[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  int32_t  ordinal[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  uint32_t transaction_tail;
  int      finalized;
  int      valid;
} GoodixMilanStudyTransientState;

#ifdef GOODIX53X5_DEBUG
int goodix_milan_match_probe_result_debug (
  const uint8_t                  probe_high_bitmap[286],
  const uint8_t                  probe_enhanced_bitmap[286],
  const uint8_t                  probe_low_bitmap[286],
  const uint8_t                  probe_inline_mask[72],
  const uint8_t                  probe_rescue_mask[308],
  const GoodixMilanFeatureRecord *probe_records,
  size_t                         probe_record_count,
  size_t                         probe_partition_count,
  int32_t                        image_quality,
  int32_t                        image_coverage,
  int32_t                        probe_optional_c7,
  const GoodixMilanAntifakeBlob *probe_antifake,
  const uint8_t                 *enrolled_template,
  size_t                         enrolled_template_size,
  GoodixMilanMatchResult        *match_result,
  GoodixMilanMatchDiagnostics   *diagnostics);
#endif

int goodix_milan_match_live_probe_result (
  const uint8_t                  probe_high_bitmap[286],
  const uint8_t                  probe_enhanced_bitmap[286],
  const uint8_t                  probe_low_bitmap[286],
  const uint8_t                  probe_inline_mask[72],
  const uint8_t                  probe_rescue_mask[308],
  const GoodixMilanFeatureRecord *probe_records,
  size_t                         probe_record_count,
  size_t                         probe_partition_count,
  int32_t                        image_quality,
  int32_t                        image_coverage,
  int32_t                        probe_optional_c7,
  const uint8_t                 *enrolled_template,
  size_t                         enrolled_template_size,
  const GoodixMilanFeatureRecord *const live_records[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                   live_record_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                   live_partition_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  size_t                         triggering_index,
  GoodixMilanMatchResult        *match_result);

#ifdef GOODIX53X5_DEBUG
int goodix_milan_match_live_probe_result_debug (
  const uint8_t                  probe_high_bitmap[286],
  const uint8_t                  probe_enhanced_bitmap[286],
  const uint8_t                  probe_low_bitmap[286],
  const uint8_t                  probe_inline_mask[72],
  const uint8_t                  probe_rescue_mask[308],
  const GoodixMilanFeatureRecord *probe_records,
  size_t                         probe_record_count,
  size_t                         probe_partition_count,
  int32_t                        image_quality,
  int32_t                        image_coverage,
  int32_t                        probe_optional_c7,
  const uint8_t                 *enrolled_template,
  size_t                         enrolled_template_size,
  const GoodixMilanFeatureRecord *const live_records[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                   live_record_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                   live_partition_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  size_t                         triggering_index,
  GoodixMilanMatchResult        *match_result,
  GoodixMilanMatchDiagnostics   *diagnostics);
#endif

int goodix_milan_template_normalize (
  const uint8_t *current_template,
  size_t         current_template_size,
  uint8_t       *packed,
  size_t         packed_capacity,
  size_t        *packed_size);

int goodix_milan_match_fallback_candidate_eligible (
  int32_t composite_score,
  int32_t score_threshold,
  int32_t quality_metric);

int goodix_milan_match_final_score (
  const int32_t metrics[15],
  uint32_t      template_type,
  int           alternate_policy,
  int32_t      *score);

int goodix_milan_match_select_first_positive (
  const int32_t *scores,
  size_t         score_count,
  size_t        *matched_index,
  int32_t       *selected_score);

typedef struct
{
  uint32_t sensor_type;
  uint32_t maximum_features;
  uint32_t registration_count;
  uint32_t maximum_records;
  uint32_t queue_state;
  uint32_t queue_transaction_counter;
  int32_t graph_reference_index;
  int32_t graph_companion_f3;
  int32_t graph_companion_f4;
  uint32_t graph_established;
} GoodixMilanTemplateMetadata;

typedef struct
{
  const uint8_t *feature_elements[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  size_t feature_element_sizes[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  size_t feature_count;
  GoodixMilanTemplateRelation relations[GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY];
  size_t relation_count;
  GoodixMilanTemplateMetadata metadata;
  uint8_t tail_state[0x520];
  int32_t normalization_overlap_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
} GoodixMilanUnpackedTemplate;

int goodix_milan_match_reference_transform (
  const GoodixMilanUnpackedTemplate *enrolled,
  size_t                              matched_feature_index,
  const int32_t                       match_transform[6],
  int32_t                             relation_values[7]);

int goodix_milan_feature_pack_template_records (
  const GoodixMilanFeatureRecord *records,
  size_t                          record_count,
  uint8_t                        *packed,
  size_t                          packed_size);

int goodix_milan_template_pack_feature_element (
  const uint8_t                         *high_bitmap,
  const uint8_t                         *enhanced_bitmap,
  const uint8_t                         *inline_mask,
  const uint8_t                         *low_bitmap,
  const GoodixMilanFeatureRecord        *records,
  size_t                                 record_count,
  const GoodixMilanAntifakeBlob         *antifake,
  const GoodixMilanFeatureTemplateFields *fields,
  uint8_t                               *packed,
  size_t                                 packed_capacity,
  size_t                                *packed_size);

int goodix_milan_template_pack_one_feature (
  const uint8_t *feature_element,
  size_t         feature_element_size,
  const uint8_t *tail_state,
  size_t         tail_state_size,
  uint8_t       *packed,
  size_t         packed_capacity,
  size_t        *packed_size);

int goodix_milan_template_pack (
  const uint8_t *const                 *feature_elements,
  const size_t                         *feature_element_sizes,
  size_t                                feature_count,
  const GoodixMilanTemplateRelation    *relations,
  size_t                                relation_count,
  const GoodixMilanTemplateMetadata    *metadata,
  const uint8_t                        *tail_state,
  size_t                                tail_state_size,
  uint8_t                              *packed,
  size_t                                packed_capacity,
  size_t                               *packed_size);

int goodix_milan_template_unpack (
  const uint8_t                 *packed,
  size_t                         packed_size,
  GoodixMilanUnpackedTemplate   *unpacked);

int goodix_milan_template_update_match_lifecycle (
  const uint8_t *current_template,
  size_t         current_template_size,
  uint64_t       feature_mask,
  uint8_t       *packed,
  size_t         packed_capacity,
  size_t        *packed_size);

int goodix_milan_study_append (
  const uint8_t  *current_template,
  size_t          current_template_size,
  const uint8_t  *probe_template,
  size_t          probe_template_size,
  size_t          matched_feature_index,
  const int32_t   relation_values[7],
  const int32_t  *retained_feature_indices,
  const int32_t   retained_transforms[][6],
  size_t          retained_count,
  int32_t         retained_flag,
  int             apply_dispatcher_prepass,
  int             complete_dispatcher_transaction,
  int             finalize_study,
  int32_t         live_overlap_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  uint8_t        *packed,
  size_t          packed_capacity,
  size_t         *packed_size);

int goodix_milan_study_replace (
  const uint8_t  *current_template,
  size_t          current_template_size,
  const uint8_t  *probe_template,
  size_t          probe_template_size,
  size_t          matched_feature_index,
  const int32_t   relation_values[7],
  const int32_t  *retained_feature_indices,
  const int32_t   retained_transforms[][6],
  size_t          retained_count,
  int32_t         retained_flag,
  int             apply_dispatcher_prepass,
  int32_t         probe_quality,
  int32_t         probe_coverage,
  const int32_t   primary_transform[6],
  int             complete_dispatcher_transaction,
  int             finalize_study,
  int32_t         live_overlap_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  uint8_t        *packed,
  size_t          packed_capacity,
  size_t         *packed_size,
  int32_t        *action_code,
  size_t         *selected_feature_index,
  GoodixMilanStudyTransientState *transient_state);

int goodix_milan_study_action0_transient (
  const uint8_t *current_template,
  size_t         current_template_size,
  const int32_t  relation_values[7],
  const int32_t *retained_feature_indices,
  const int32_t  retained_transforms[][6],
  size_t         retained_count,
  int32_t        retained_flag,
  GoodixMilanStudyTransientState *transient_state);

int goodix_milan_study_finalize_action0_transient (
  GoodixMilanStudyTransientState *transient_state);

int goodix_milan_study_finalize (
  const uint8_t *current_template,
  size_t         current_template_size,
  uint32_t       queue_state,
  uint32_t       queue_transaction_counter,
  int            finalize_transaction,
  uint8_t       *packed,
  size_t         packed_capacity,
  size_t        *packed_size);

int goodix_milan_antifake_residual (
  const uint16_t *calibration,
  const uint16_t *raw_frame,
  size_t          rows,
  size_t          columns,
  int32_t         sensor_offset,
  uint16_t        chip_type,
  uint16_t       *residual);

int goodix_milan_antifake_impulse_filter (
  uint16_t *residual,
  size_t    rows,
  size_t    columns,
  int32_t  *threshold);

int goodix_milan_antifake_statistics (
  const uint16_t *residual,
  const uint8_t  *mask,
  size_t          rows,
  size_t          columns,
  int32_t        *texture,
  int32_t        *mean);

int goodix_milan_antifake_block_variation (
  const uint16_t *residual,
  const uint8_t  *mask,
  size_t          rows,
  size_t          columns,
  size_t          block_size,
  int32_t        *variation);

int goodix_milan_antifake_model_score (
  const int32_t vector[51],
  int32_t      *score);

int goodix_milan_antifake_model_vector (
  const uint16_t *residual,
  const uint8_t  *mask,
  size_t          rows,
  size_t          columns,
  size_t          border,
  int32_t         vector[51]);

int goodix_milan_antifake_class_map (
  const uint8_t *image,
  const uint8_t *mask,
  size_t         rows,
  size_t         columns,
  uint8_t       *classes);

int goodix_milan_antifake_boundary_score (
  const uint16_t *residual,
  const uint8_t  *classes,
  size_t          rows,
  size_t          columns,
  uint8_t        *thinned,
  int32_t        *score);

int goodix_milan_antifake_build (
  const uint16_t *calibration,
  const uint16_t *raw_frame,
  const uint8_t  *classification_plane,
  const uint8_t  *feature_mask,
  size_t          feature_mask_size,
  size_t          rows,
  size_t          columns,
  uint16_t        t_code,
  uint16_t        dac_high,
  uint16_t        dac_low,
  uint16_t        chip_type,
  int32_t         calibration_scalar,
  GoodixMilanAntifakeBlob *antifake,
  size_t          antifake_size);

int goodix_milan_antifake_build_with_boundary (
  const uint16_t                   *calibration,
  const uint16_t                   *raw_frame,
  const uint8_t                    *classification_plane,
  const uint8_t                    *feature_mask,
  size_t                            feature_mask_size,
  size_t                            rows,
  size_t                            columns,
  uint16_t                          t_code,
  uint16_t                          dac_high,
  uint16_t                          dac_low,
  uint16_t                          chip_type,
  int32_t                           calibration_scalar,
  GoodixMilanAntifakeBlob          *antifake,
  size_t                            antifake_size,
  GoodixMilanAntifakeBoundaryResult *boundary_result);

int goodix_milan_antifake_score_pair (
  const GoodixMilanAntifakeBlob *prior,
  size_t          prior_size,
  const GoodixMilanAntifakeBlob *current,
  size_t          current_size,
  const int32_t   current_to_prior[6],
  int32_t        *score);

int goodix_milan_antifake_pair_metrics (
  const GoodixMilanAntifakeBlob *prior,
  size_t         prior_size,
  const GoodixMilanAntifakeBlob *current,
  size_t         current_size,
  const int32_t  current_to_prior[6],
  int32_t        metrics[5]);

int goodix_milan_antifake_feature_update (
  GoodixMilanAntifakeBlob *antifake,
  size_t          antifake_size,
  const uint16_t *source,
  size_t          rows,
  size_t          columns);

int goodix_milan_antifake_feature_maps (
  const uint16_t *source,
  size_t          rows,
  size_t          columns,
  uint8_t        *dense_orientation,
  uint32_t       *magnitude,
  int16_t        *gradient_orientation);

int goodix_milan_antifake_feature_record (
  const uint8_t            *dense_orientation,
  const uint32_t           *magnitude,
  const int16_t            *gradient_orientation,
  size_t                    rows,
  size_t                    columns,
  int32_t                   x,
  int32_t                   y,
  GoodixMilanFeatureRecord *record);

int goodix_milan_antifake_build_mask (
  const uint8_t *feature_mask,
  size_t         feature_mask_size,
  size_t         rows,
  size_t         columns,
  uint8_t       *mask,
  uint8_t       *packed,
  size_t         packed_size);

typedef struct
{
  int32_t strength;
  int32_t index;
} GoodixMilanFeatureRank;

typedef struct
{
  int32_t x;
  int32_t y;
  int32_t scale_value;
  uint32_t peak;
  uint32_t selected_peak;
  uint16_t secondary_orientation;
  uint16_t reserved;
} GoodixMilanFeatureAux;

size_t goodix_milan_feature_collect_materialized (
  const uint8_t                  *feature_source,
  const uint16_t                 *scales,
  const uint32_t                 *magnitude,
  const int16_t                  *orientation,
  size_t                          rows,
  size_t                          columns,
  GoodixMilanFeatureRecord       *records,
  GoodixMilanFeatureRank         *ranks,
  GoodixMilanFeatureAux          *auxiliary,
  size_t                          capacity);

int goodix_milan_feature_should_retry_scale_space (
  size_t       materialized_count,
  unsigned int pass_marker,
  int          configured_retry);

void goodix_milan_feature_build_descriptor_samples (
  int32_t         center_x,
  int32_t         center_y,
  int32_t         descriptor_scale,
  int16_t         feature_orientation,
  const uint32_t *magnitude,
  const int16_t  *orientation,
  size_t          rows,
  size_t          columns,
  int32_t         samples[128]);

int goodix_milan_feature_extract_records_mode (
  const uint8_t            *frame,
  size_t                    rows,
  size_t                    columns,
  GoodixMilanFeatureRecord *records,
  size_t                    capacity,
  size_t                   *record_count,
  size_t                   *zero_flag_count,
  int                       expand_records);

int goodix_milan_feature_extract_records_mode_configured (
  const uint8_t            *frame,
  size_t                    rows,
  size_t                    columns,
  GoodixMilanFeatureRecord *records,
  size_t                    capacity,
  size_t                   *record_count,
  size_t                   *zero_flag_count,
  int                       expand_records,
  int                       configured_retry);

int goodix_milan_preprocess_contrast (const uint16_t *source,
                                   const uint8_t  *mask,
                                  size_t          rows,
                                  size_t          columns,
                                 uint8_t         *output);
