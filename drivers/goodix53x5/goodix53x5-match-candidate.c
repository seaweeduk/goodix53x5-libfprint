/*
 * Goodix 53x5 driver for libfprint - profile-9/type-12 candidate state
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This is the candidate construction boundary immediately before profile-9/
 * subtype-12 policy evaluation. It keeps the 77-word metrics layout in one
 * place instead of reconstructing selected fields throughout the matcher.
 */

#include "goodix53x5-match-candidate.h"

#include <string.h>

void
goodix_milan_match_candidate_reset (GoodixMilanMatchCandidate *candidate)
{
  memset (candidate, 0, sizeof(*candidate));
}

void
goodix_milan_match_candidate_set_primary (GoodixMilanMatchCandidate *candidate,
                                           int32_t primary_count,
                                           int32_t retained_count,
                                           const int32_t transform[6])
{
  candidate->words[0] = primary_count;
  candidate->words[1] = retained_count;
  memcpy (candidate->transform, transform, sizeof(candidate->transform));
}

int
goodix_milan_match_candidate_rank_alternate (
  int32_t primary_detail,
  int32_t primary_scale_penalty,
  int32_t primary_orthogonality_penalty,
  int32_t alternate_score,
  int32_t alternate_detail,
  int32_t alternate_scale_penalty,
  int32_t alternate_orthogonality_penalty)
{
  return alternate_score > 128 &&
         alternate_detail -
             4 * (alternate_scale_penalty +
                   alternate_orthogonality_penalty) >
           primary_detail -
             4 * (primary_scale_penalty + primary_orthogonality_penalty);
}

int
goodix_milan_match_candidate_promote_alternate (int32_t mask_overlap,
                                                 int32_t topology_percent,
                                                 int32_t geometric_percent,
                                                 int32_t alternate_count)
{
  return (mask_overlap >= 145 && topology_percent >= 40) ||
         (mask_overlap < 105 && topology_percent > 65 &&
          geometric_percent > 50) ||
         (mask_overlap >= 105 && topology_percent >= 45 &&
           geometric_percent > 25) ||
         geometric_percent > 80 || alternate_count > 18;
}

void
goodix_milan_match_candidate_set_record_metrics (
  GoodixMilanMatchCandidate *candidate,
  int32_t                    valid_count,
  int32_t                    matched_count,
  int32_t                    topology_percent,
  int32_t                    geometric_percent,
  int32_t                    topology_distance)
{
  candidate->words[10] = topology_percent;
  candidate->words[11] = geometric_percent;
  candidate->words[66] = valid_count;
  candidate->words[67] = matched_count;
  candidate->words[68] = topology_percent;
  candidate->words[69] = geometric_percent;
  candidate->words[70] = topology_distance;
}

void
goodix_milan_match_candidate_set_sibling (
  GoodixMilanMatchCandidate *candidate,
  int32_t                    sibling_count,
  const int32_t              sibling_transform[6],
  int32_t                    valid_count,
  int32_t                    matched_count,
  int32_t                    topology_percent,
  int32_t                    geometric_percent,
  int32_t                    topology_distance,
  int                         select_transform)
{
  candidate->words[2] = sibling_count;
  if (select_transform)
    memcpy (candidate->transform, sibling_transform,
            sizeof(candidate->transform));
      goodix_milan_match_candidate_set_record_metrics (
        candidate, valid_count, matched_count, topology_percent,
        geometric_percent, topology_distance);
}

int
goodix_milan_match_candidate_skip_pre_primary (
  int32_t configured_feature_mode,
  int32_t configured_feature_index,
  size_t  feature_index,
  size_t  feature_count,
  int32_t maximum_features,
  int32_t rejection_gate,
  int32_t retained_feature_flag,
  int32_t feature_active)
{
  if (configured_feature_mode == 0 &&
      feature_index != (size_t) configured_feature_index)
    return 1;

  return (feature_count == (size_t) maximum_features ||
          rejection_gate == 1) &&
         retained_feature_flag == 1 && feature_active == 1;
}

int
goodix_milan_match_candidate_admit (GoodixMilanMatchCandidate *candidate)
{
  if (candidate->words[2] > candidate->words[1])
    candidate->words[1] = candidate->words[2];

  return candidate->words[1] > 4 && candidate->words[10] >= 30;
}

void
goodix_milan_match_candidate_materialize_dispatch (
  GoodixMilanMatchCandidate *candidate)
{
  memcpy (candidate->words + 15, candidate->transform,
          sizeof(candidate->transform));
}
