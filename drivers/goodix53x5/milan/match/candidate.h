/*
 * Goodix 53x5 driver for libfprint - profile-9/type-12 candidate state
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

#define GOODIX_MILAN_CANDIDATE_WORDS 77

typedef struct
{
  int32_t words[GOODIX_MILAN_CANDIDATE_WORDS];
  int32_t transform[6];
  /* Native validity is private and does not consume recovered candidate words. */
  int     primary_valid;
  int     alternate_valid;
} GoodixMilanMatchCandidate;

void goodix_milan_match_candidate_reset (GoodixMilanMatchCandidate *candidate);

void goodix_milan_match_candidate_set_primary (
  GoodixMilanMatchCandidate *candidate,
  int32_t                    primary_count,
  int32_t                    retained_count,
  const int32_t              transform[6]);

int goodix_milan_match_candidate_rank_alternate (
  int32_t primary_detail,
  int32_t primary_scale_penalty,
  int32_t primary_orthogonality_penalty,
  int32_t alternate_score,
  int32_t alternate_detail,
  int32_t alternate_scale_penalty,
  int32_t alternate_orthogonality_penalty);

int goodix_milan_match_candidate_promote_alternate (
  int32_t mask_overlap,
  int32_t topology_percent,
  int32_t geometric_percent,
  int32_t alternate_count);

void goodix_milan_match_candidate_set_record_metrics (
  GoodixMilanMatchCandidate *candidate,
  int32_t                    valid_count,
  int32_t                    matched_count,
  int32_t                    topology_percent,
  int32_t                    geometric_percent,
  int32_t                    topology_distance);

void goodix_milan_match_candidate_set_sibling (
  GoodixMilanMatchCandidate *candidate,
  int32_t                    sibling_count,
  const int32_t              sibling_transform[6],
  int32_t                    valid_count,
  int32_t                    matched_count,
  int32_t                    topology_percent,
  int32_t                    geometric_percent,
  int32_t                    topology_distance,
  int                         select_transform);

int goodix_milan_match_candidate_skip_pre_primary (
  int32_t configured_feature_mode,
  int32_t configured_feature_index,
  size_t  feature_index,
  size_t  feature_count,
  int32_t maximum_features,
  int32_t rejection_gate,
  int32_t retained_feature_flag,
  int32_t feature_active);

int goodix_milan_match_candidate_admit (GoodixMilanMatchCandidate *candidate);

void goodix_milan_match_candidate_materialize_dispatch (
  GoodixMilanMatchCandidate *candidate);
