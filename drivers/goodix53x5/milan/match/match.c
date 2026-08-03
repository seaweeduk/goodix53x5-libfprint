/*
 * Goodix 53x5 driver for libfprint - Milan matcher
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"
#include "milan/private.h"
#include "milan/template/codec-private.h"
#include "milan/match/candidate.h"
#include "milan/match/correspondence.h"
#include "milan/match/geometry.h"
#include "milan/match/overlap.h"
#include "milan/match/fallback.h"
#include "milan/match/policy.h"
#include "milan/match/rescue.h"
#include "milan/match/selection.h"
#include "milan/relations.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
  MILAN_MATCH_RANK_OWNER_NONE,
  MILAN_MATCH_RANK_OWNER_PRIMARY,
  MILAN_MATCH_RANK_OWNER_ALTERNATE,
} MilanMatchRankOwner;

typedef struct
{
  int32_t             affine[6];
  int32_t             owner_count;
  int32_t             score;
  int32_t             coverage;
  int32_t             detail;
  int32_t             low_metrics[3];
  int32_t             scale_penalty;
  int32_t             orthogonality_penalty;
  int32_t             strong_orthogonality_penalty;
  int32_t             rank;
  MilanMatchRankOwner owner;
  int                 affine_valid;
  int                 overlap_valid;
} MilanMatchRankResult;

typedef struct
{
  int32_t             affine[6];
  int32_t             owner_count;
  int32_t             raw_percent;
  int32_t             scaled_percent;
  int32_t             overlap_count;
  MilanMatchRankOwner owner;
  int                 valid;
} MilanMatchForwardOverlapResult;

typedef struct
{
  int32_t             affine[6];
  int32_t             owner_count;
  int32_t             valid_count;
  int32_t             matched_count;
  int32_t             topology_percent;
  int32_t             geometric_percent;
  int32_t             topology_bonus;
  int32_t             topology_distance;
  MilanMatchRankOwner owner;
  MilanMatchRankOwner count_owner;
  int                 valid;
} MilanMatchTopologyResult;

typedef struct
{
  int32_t                        retained_count;
  MilanMatchTopologyResult       topology;
  int                            promotion_evaluated;
  int                            promoted;
  int                            repaired;
} MilanMatchPromotionRepairResult;

static int32_t
milan_match_scale_q8_s32 (int32_t value,
                          int32_t scale)
{
  uint32_t product = (uint32_t) value * (uint32_t) scale;

  if ((product & UINT32_C (0x80000000)) == 0)
    return (int32_t) (product >> 8);
  return -1 - (int32_t) ((~product) >> 8);
}

static void
milan_match_rank_result_init (
  const MilanMatchAffineState *affine,
  MilanMatchRankOwner          owner,
  int                          evaluate_overlap,
  const GoodixMilanFeatureView *enrolled_feature,
  const GoodixMilanFeatureView *probe_feature,
  MilanMatchRankResult         *result)
{
  memset (result, 0, sizeof(*result));
  if (!affine->valid)
    return;

  memcpy (result->affine, affine->affine, sizeof(result->affine));
  result->owner_count = affine->filtered_count;
  result->owner = owner;
  result->affine_valid = 1;
  if (!evaluate_overlap)
    return;

  goodix_milan_match_affine_penalties (
    result->affine, &result->scale_penalty,
    &result->orthogonality_penalty,
    &result->strong_orthogonality_penalty);
  if (enrolled_feature && probe_feature &&
      goodix_milan_match_overlap_metrics_with_context (
        enrolled_feature, probe_feature, result->affine, &result->score,
        &result->coverage, &result->detail, result->low_metrics,
        result->owner_count) == 0)
    result->overlap_valid = 1;
  result->rank = result->detail -
                 4 * (result->scale_penalty +
                      result->orthogonality_penalty);
}

static int
milan_match_rank_result_select (MilanMatchRankResult       *current,
                                 const MilanMatchRankResult *alternate)
{
  if (!current->affine_valid || !alternate->affine_valid ||
      !alternate->overlap_valid || alternate->score <= 128 ||
      alternate->rank <= current->rank)
    return 0;

  *current = *alternate;
  return 1;
}

static void
milan_match_forward_overlap_result_init (
  const MilanMatchRankResult       *ranked,
  const GoodixMilanFeatureView     *probe_feature,
  const GoodixMilanFeatureView     *enrolled_feature,
  MilanMatchForwardOverlapResult *result)
{
  memset (result, 0, sizeof(*result));
  if (!ranked->affine_valid)
    return;

  memcpy (result->affine, ranked->affine, sizeof(result->affine));
  result->owner_count = ranked->owner_count;
  result->owner = ranked->owner;
  if (goodix_milan_feature_mask_forward_overlap (
        probe_feature, enrolled_feature, result->affine, 1,
        &result->raw_percent, &result->overlap_count) != 0)
    return;

  result->scaled_percent = milan_match_scale_q8_s32 (
    result->raw_percent, 246);
  result->valid = 1;
}

static void
milan_match_topology_result_init (
  const GoodixMilanFeatureRecord *enrolled_records,
  size_t                          enrolled_record_count,
  const GoodixMilanFeatureRecord *probe_records,
  size_t                          probe_record_count,
  const MilanMatchAffineState    *affine,
  MilanMatchRankOwner             owner,
  MilanMatchTopologyResult       *result)
{
  memset (result, 0, sizeof(*result));
  if (!affine->valid)
    return;

  memcpy (result->affine, affine->affine, sizeof(result->affine));
  result->owner_count = affine->filtered_count;
  result->owner = owner;
  result->count_owner = owner;
  goodix_milan_match_record_metrics_internal (
    enrolled_records, enrolled_record_count, probe_records,
    probe_record_count, result->affine, result->owner_count,
    &result->topology_percent, &result->geometric_percent,
    &result->topology_bonus, &result->topology_distance,
    &result->valid_count, &result->matched_count);
  result->valid = 1;
}

static int
milan_match_promotion_inputs_init (
  const GoodixMilanFeatureRecord *enrolled_records,
  size_t                          enrolled_record_count,
  const GoodixMilanFeatureRecord *probe_records,
  size_t                          probe_record_count,
  const GoodixMilanFeatureView   *enrolled_feature,
  const GoodixMilanFeatureView   *probe_feature,
  int32_t                         primary_count,
  const MilanMatchAffineState    *alternate,
  const MilanMatchRankResult     *ranked,
  MilanMatchForwardOverlapResult *forward,
  MilanMatchTopologyResult       *topology)
{
  memset (forward, 0, sizeof(*forward));
  memset (topology, 0, sizeof(*topology));
  if (!alternate->valid || alternate->filtered_count <= primary_count)
    return 0;

  milan_match_forward_overlap_result_init (
    ranked, probe_feature, enrolled_feature, forward);
  milan_match_topology_result_init (
    enrolled_records, enrolled_record_count, probe_records,
    probe_record_count, alternate, MILAN_MATCH_RANK_OWNER_ALTERNATE,
    topology);
  return 1;
}

static int
milan_match_promotion_inputs_evaluate (
  const MilanMatchForwardOverlapResult *forward,
  const MilanMatchTopologyResult       *topology)
{
  return goodix_milan_match_candidate_promote_alternate (
    forward->scaled_percent, topology->topology_percent,
    topology->geometric_percent, topology->owner_count);
}

static void
milan_match_promotion_repair_apply (
  const GoodixMilanFeatureRecord       *enrolled_records,
  size_t                                enrolled_record_count,
  const GoodixMilanFeatureRecord       *probe_records,
  size_t                                probe_record_count,
  const MilanMatchAffineState          *primary,
  const MilanMatchAffineState          *alternate,
  const MilanMatchRankResult           *ranked,
  const MilanMatchForwardOverlapResult *forward,
  const MilanMatchTopologyResult       *local_topology,
  MilanMatchPromotionRepairResult      *result)
{
  memset (result, 0, sizeof(*result));
  result->retained_count = primary->filtered_count;

  if (alternate->valid && alternate->filtered_count > primary->filtered_count)
    {
      if (local_topology->valid)
        result->topology = *local_topology;
      if (forward->valid && local_topology->valid)
        {
          result->promotion_evaluated = 1;
          result->promoted = milan_match_promotion_inputs_evaluate (
            forward, local_topology);
          if (result->promoted)
            result->retained_count = alternate->filtered_count;
        }
    }

  if (primary->filtered_count > 4 && result->topology.topology_percent < 30)
    {
      MilanMatchAffineState repair = { 0 };

      memcpy (repair.affine, ranked->affine, sizeof(repair.affine));
      repair.filtered_count = primary->filtered_count;
      repair.valid = ranked->affine_valid;
      milan_match_topology_result_init (
        enrolled_records, enrolled_record_count, probe_records,
        probe_record_count, &repair, ranked->owner, &result->topology);
      result->topology.count_owner = MILAN_MATCH_RANK_OWNER_PRIMARY;
      result->repaired = result->topology.valid;
    }
}

static void
milan_match_publish_candidate (
  GoodixMilanMatchCandidate      *candidate,
  const MilanMatchAffineState    *primary,
  const MilanMatchAffineState    *alternate,
  const MilanMatchRankResult     *ranked,
  int32_t                         retained_count,
  const MilanMatchTopologyResult *topology)
{
  goodix_milan_match_candidate_reset (candidate);
  goodix_milan_match_candidate_set_primary (
    candidate, primary->filtered_count, retained_count, ranked->affine);
  candidate->primary_valid = primary->valid;
  candidate->alternate_valid = alternate->valid;
  if (primary->filtered_count >= 3 ||
      ranked->owner == MILAN_MATCH_RANK_OWNER_ALTERNATE)
    {
      candidate->words[12] = ranked->scale_penalty;
      candidate->words[13] = ranked->orthogonality_penalty;
      candidate->words[14] = ranked->strong_orthogonality_penalty;
      if (ranked->overlap_valid)
        {
          candidate->words[4] = ranked->score;
          candidate->words[5] = ranked->detail;
          candidate->words[9] = milan_match_scale_q8_s32 (
            ranked->coverage, 246);
        }
    }
  if (topology && topology->valid)
    goodix_milan_match_candidate_set_record_metrics (
      candidate, topology->valid_count, topology->matched_count,
      topology->topology_percent, topology->geometric_percent,
      topology->topology_distance);
}

static int
milan_match_score_counts (
  const GoodixMilanFeatureRecord *enrolled_records,
  size_t                          enrolled_record_count,
  size_t                          enrolled_partition_count,
  const GoodixMilanFeatureRecord *probe_records,
  size_t                          probe_record_count,
  size_t                          probe_partition_count,
  int32_t                         transform[6],
  size_t                         *correspondence_count,
  size_t                         *primary_filtered_count,
  size_t                         *filtered_count,
  int32_t                        *score,
  int32_t                        *topology_percent_output,
  int32_t                        *geometric_percent_output,
  int32_t                        *topology_bonus_output,
  const GoodixMilanFeatureView   *enrolled_feature,
  const GoodixMilanFeatureView   *probe_feature,
  size_t                          pair_capacity,
  GoodixMilanMatchCandidate      *candidate)
{
  int32_t pairs[MILAN_MATCH_MAX_PAIRS * 2];
  int32_t initial_transform[6];
  MilanMatchAffineState primary = { 0 };
  MilanMatchAffineState alternate = { 0 };
  MilanMatchRankResult primary_rank = { 0 };
  MilanMatchRankResult alternate_rank = { 0 };
  MilanMatchRankResult ranked = { 0 };
  MilanMatchForwardOverlapResult forward_overlap = { 0 };
  MilanMatchTopologyResult local_topology = { 0 };
  MilanMatchPromotionRepairResult promotion_repair = { 0 };
  size_t match_count;

  if (!transform || !correspondence_count || !filtered_count || !score ||
      enrolled_partition_count > enrolled_record_count ||
      probe_partition_count > probe_record_count ||
      goodix_milan_match_correspondences_partitioned (
        enrolled_records, enrolled_record_count, enrolled_partition_count,
        probe_records, probe_record_count, probe_partition_count, pairs,
        pair_capacity, &match_count) != 0)
    return -1;
  memcpy (initial_transform, transform, sizeof(initial_transform));
  goodix_milan_match_fit_affine_state (
    enrolled_records, probe_records, pairs, match_count, 1, &primary);
  if (match_count < 3)
    memcpy (primary.affine, initial_transform, sizeof(primary.affine));
  if ((!primary.valid || primary.filtered_count == 0) && !candidate)
    return -1;

  milan_match_rank_result_init (
    &primary, MILAN_MATCH_RANK_OWNER_PRIMARY, primary.filtered_count >= 3,
    enrolled_feature, probe_feature, &primary_rank);
  ranked = primary_rank;
  if (!ranked.affine_valid)
    {
      memcpy (ranked.affine, primary.affine, sizeof(ranked.affine));
      ranked.owner = MILAN_MATCH_RANK_OWNER_PRIMARY;
    }
  if (primary_filtered_count)
    *primary_filtered_count = (size_t) primary.filtered_count;

  if (primary.valid && primary.filtered_count > 0 &&
      primary.filtered_count <= 15)
    {
      int32_t alternate_pairs[MILAN_MATCH_MAX_PAIRS * 2];
      size_t alternate_count;

      if (goodix_milan_match_alternate_correspondences_internal (
            enrolled_records, enrolled_record_count,
            enrolled_partition_count, probe_records, probe_record_count,
            probe_partition_count, primary.affine, alternate_pairs,
            pair_capacity, &alternate_count) != 0)
        return -1;

      goodix_milan_match_fit_affine_state (
        enrolled_records, probe_records, alternate_pairs, alternate_count,
        1, &alternate);

      if (alternate.valid)
        {
          milan_match_rank_result_init (
            &alternate, MILAN_MATCH_RANK_OWNER_ALTERNATE, 1,
            enrolled_feature, probe_feature, &alternate_rank);
          milan_match_rank_result_select (&ranked, &alternate_rank);
        }

      if (alternate.valid &&
          alternate.filtered_count > primary.filtered_count)
        {
          if (enrolled_feature && probe_feature)
            milan_match_promotion_inputs_init (
              enrolled_records, enrolled_record_count, probe_records,
              probe_record_count, enrolled_feature, probe_feature,
              primary.filtered_count, &alternate, &ranked,
              &forward_overlap, &local_topology);
          else
            milan_match_topology_result_init (
              enrolled_records, enrolled_record_count, probe_records,
              probe_record_count, &alternate,
              MILAN_MATCH_RANK_OWNER_ALTERNATE, &local_topology);
        }
    }

  milan_match_promotion_repair_apply (
    enrolled_records, enrolled_record_count, probe_records, probe_record_count,
    &primary, &alternate, &ranked, &forward_overlap, &local_topology,
    &promotion_repair);
  if (!enrolled_feature && !probe_feature && alternate.valid &&
      alternate.filtered_count > primary.filtered_count)
    promotion_repair.retained_count = alternate.filtered_count;
  if (topology_percent_output)
    *topology_percent_output = promotion_repair.topology.topology_percent;
  if (geometric_percent_output)
    *geometric_percent_output = promotion_repair.topology.geometric_percent;

  if (topology_bonus_output)
    *topology_bonus_output = promotion_repair.topology.topology_bonus;
  memcpy (transform, ranked.affine, sizeof(ranked.affine));
  if (candidate)
    milan_match_publish_candidate (
      candidate, &primary, &alternate, &ranked,
      promotion_repair.retained_count, &promotion_repair.topology);
  *correspondence_count = match_count;
  *filtered_count = (size_t) promotion_repair.retained_count;
  *score = promotion_repair.retained_count * 100 / (int32_t) pair_capacity;
  return 0;
}

static int
milan_match_secondary_diagnostics_capacity (
  const GoodixMilanFeatureRecord *enrolled_records,
  size_t                          enrolled_record_count,
  size_t                          enrolled_partition_count,
  const GoodixMilanFeatureRecord *probe_records,
  size_t                          probe_record_count,
  size_t                          probe_partition_count,
  size_t                          primary_counts[2],
  int32_t                         primary_transforms[2][6],
  size_t                         *selected_count,
  int32_t                        *selected_count_owner,
  int32_t                         selected_transform[6],
  size_t                         *alternate_count,
  int32_t                         alternate_transform[6],
  int32_t                         sibling_tail_hamming_limit,
  size_t                          pair_capacity)
{
  int32_t pairs[2][MILAN_MATCH_MAX_PAIRS * 2];
  size_t pair_counts[2];
  int32_t residual;

  if (!enrolled_records || !probe_records || !primary_counts ||
      !primary_transforms || !selected_count || !selected_transform ||
      !alternate_count || !alternate_transform || enrolled_record_count == 0 ||
      enrolled_record_count > 150 || probe_record_count == 0 ||
      probe_record_count > 150 ||
      enrolled_partition_count > enrolled_record_count ||
      probe_partition_count > probe_record_count)
    return -1;

  for (size_t group = 0; group < 2; group++)
    {
      pair_counts[group] = goodix_milan_match_cross_class_correspondences (
        enrolled_records, enrolled_record_count, enrolled_partition_count,
        probe_records, probe_record_count, probe_partition_count,
        (uint8_t) (group + 1), sibling_tail_hamming_limit, pairs[group],
        pair_capacity);
      primary_counts[group] = pair_counts[group] < 3
                                ? 0
                                 : (size_t) goodix_milan_filter_recognition_pairs_internal (
                                     enrolled_records, probe_records,
                                     pairs[group], pair_counts[group],
                                     primary_transforms[group], &residual, NULL,
                                     NULL);
    }

  size_t selected_group = primary_counts[0] > primary_counts[1] ? 0 : 1;
  *selected_count = primary_counts[selected_group];
  if (selected_count_owner &&
      (int32_t) *selected_count > *selected_count_owner)
    *selected_count_owner = (int32_t) *selected_count;
  memcpy (selected_transform, primary_transforms[selected_group],
          6 * sizeof(*selected_transform));
  int32_t alternate_pairs[MILAN_MATCH_MAX_PAIRS * 2];
  size_t alternate_pair_count =
    *selected_count < 3
      ? 0
      : goodix_milan_match_cross_class_alternate_correspondences (
          enrolled_records, enrolled_record_count, enrolled_partition_count,
          probe_records, probe_record_count, probe_partition_count,
          selected_transform, sibling_tail_hamming_limit, alternate_pairs,
          pair_capacity);
  *alternate_count = alternate_pair_count < 3
                       ? 0
                        : (size_t) goodix_milan_filter_recognition_pairs_internal (
                            enrolled_records, probe_records, alternate_pairs,
                            alternate_pair_count, alternate_transform, &residual,
                            NULL, NULL);
  return 0;
}

static void
milan_match_apply_secondary (
  const GoodixMilanFeatureRecord *enrolled_records,
  size_t                          enrolled_record_count,
  size_t                          enrolled_partition_count,
  const GoodixMilanFeatureRecord *probe_records,
  size_t                          probe_record_count,
  size_t                          probe_partition_count,
  size_t                         *primary_count,
  size_t                         *filtered_count,
  int32_t                         transform[6],
  int32_t                        *topology_percent,
  int32_t                        *geometric_percent,
  int32_t                        *topology_bonus,
  size_t                          pair_capacity,
  int32_t                         sibling_tail_hamming_limit,
  const GoodixMilanFeatureView   *enrolled_feature,
  const GoodixMilanFeatureView   *probe_feature,
  GoodixMilanMatchCandidate      *candidate)
{
  int32_t selected_transform[6];
  int32_t alternate_transform[6];
  size_t selected_count = *filtered_count;
  size_t alternate_count = 0;
  int32_t residual;

  if (enrolled_partition_count > enrolled_record_count ||
      probe_partition_count > probe_record_count)
    return;
  if (*primary_count >= 9 || *filtered_count >= 9)
    goto bonus;
  memcpy (selected_transform, transform, sizeof(selected_transform));
  if (*filtered_count < 4)
    {
      size_t cross_counts[2];
      int32_t cross_transforms[2][6];

      if (milan_match_secondary_diagnostics_capacity (
            enrolled_records, enrolled_record_count, enrolled_partition_count,
            probe_records, probe_record_count, probe_partition_count,
            cross_counts, cross_transforms, &selected_count,
            pair_capacity == 42 && candidate ? &candidate->words[0] : NULL,
            selected_transform, &alternate_count, alternate_transform,
            sibling_tail_hamming_limit, pair_capacity) != 0)
        goto bonus;
      if (selected_count > *primary_count)
        *primary_count = selected_count;
    }
  else
    {
      int32_t alternate_pairs[MILAN_MATCH_MAX_PAIRS * 2];

      size_t pair_count = goodix_milan_match_cross_class_alternate_correspondences (
        enrolled_records, enrolled_record_count, enrolled_partition_count,
        probe_records, probe_record_count, probe_partition_count,
        selected_transform, sibling_tail_hamming_limit, alternate_pairs,
        pair_capacity);
      alternate_count = pair_count < 3
                          ? 0
                           : (size_t) goodix_milan_filter_recognition_pairs_internal (
                               enrolled_records, probe_records, alternate_pairs,
                               pair_count, alternate_transform, &residual, NULL,
                               NULL);
    }

  if (alternate_count > selected_count)
    {
      int32_t distance;
      int32_t valid_count;
      int32_t matched_count;
      int32_t alternate_bonus;
      int32_t alternate_topology;
      int32_t alternate_geometric;
      int select_transform = 0;
              int32_t sibling_scale_penalty;
      int32_t sibling_orthogonality;
      int32_t sibling_strong_orthogonality;

      goodix_milan_match_record_metrics_internal (
        enrolled_records, enrolled_record_count, probe_records,
        probe_record_count, alternate_transform, (int) alternate_count,
        &alternate_topology, &alternate_geometric, &alternate_bonus, &distance,
        &valid_count, &matched_count);
      goodix_milan_match_affine_penalties (
        alternate_transform, &sibling_scale_penalty, &sibling_orthogonality,
        &sibling_strong_orthogonality);
      if (pair_capacity == 42 && candidate && enrolled_feature && probe_feature)
        {
          int32_t sibling_score;
          int32_t sibling_coverage;
          int32_t sibling_detail;
          int32_t sibling_low_metrics[3];

          if (goodix_milan_match_overlap_metrics_with_context (
                enrolled_feature, probe_feature, alternate_transform,
                &sibling_score, &sibling_coverage, &sibling_detail,
                sibling_low_metrics, (int32_t) alternate_count) == 0)
            {
              select_transform = goodix_milan_match_candidate_rank_alternate (
                candidate->words[5], candidate->words[12],
                candidate->words[13], sibling_score, sibling_detail,
                sibling_scale_penalty, sibling_orthogonality);
              if (select_transform)
                {
                  candidate->words[4] = sibling_score;
                  candidate->words[5] = sibling_detail;
                  candidate->words[9] = sibling_coverage * 246 >> 8;
                  candidate->words[12] = sibling_scale_penalty;
                  candidate->words[13] = sibling_orthogonality;
                  candidate->words[14] = sibling_strong_orthogonality;
                  memcpy (candidate->transform, alternate_transform,
                          sizeof(candidate->transform));
                }
            }
        }
      if (!sibling_scale_penalty && !sibling_strong_orthogonality &&
          ((alternate_topology > 40 && matched_count > 23) ||
           (alternate_topology > 45 && matched_count > 11) ||
           alternate_topology > 75 || alternate_count > 16))
        {
          if (pair_capacity != 42)
            memcpy (transform, alternate_transform,
                    sizeof(alternate_transform));
          if (pair_capacity == 42 && candidate)
            goodix_milan_match_candidate_set_sibling (
              candidate, (int32_t) alternate_count, alternate_transform,
              valid_count, matched_count, alternate_topology,
              alternate_geometric, distance, select_transform);
          else
            {
              *filtered_count = alternate_count;
              *topology_percent = alternate_topology;
              *geometric_percent = alternate_geometric;
              *topology_bonus = alternate_bonus;
            }
        }
    }

bonus:
  if (pair_capacity == 42 && candidate)
    {
      if (candidate->words[10] > 30 && candidate->words[70] < 10)
        {
          if (candidate->words[0] < 42)
            candidate->words[0]++;
          if (candidate->words[1] < 42)
            candidate->words[1]++;
        }
      return;
    }
  if (*topology_bonus)
    {
      if (*primary_count < pair_capacity)
        (*primary_count)++;
      if (*filtered_count < pair_capacity)
        (*filtered_count)++;
    }
}

static int
milan_match_validate_winner (const int32_t metrics[77],
                             void         *user_data,
                             int32_t      *score)
{
  const GoodixMilanMatcherPolicy *policy = user_data;

  return goodix_milan_match_final_score (
    metrics, (uint32_t) policy->configuration[15], policy->configuration[17] != 0, score);
}

static int
milan_match_prepared_probe (
  const GoodixMilanFeatureView   *probe_feature,
  const uint8_t                   probe_rescue_mask[308],
  const GoodixMilanFeatureRecord *probe_records,
  size_t                          probe_record_count,
  int32_t                         image_quality,
  int32_t                         image_coverage,
  const GoodixMilanAntifakeBlob  *probe_antifake,
  int                             caller_blocking_enabled,
  const uint8_t *enrolled_template,
  size_t         enrolled_template_size,
  const GoodixMilanFeatureRecord *const live_records[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t live_record_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t live_partition_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t *live_order,
  size_t live_order_count,
  size_t        *matched_feature_index,
  int32_t       *score,
  int32_t        match_transform[6],
  int32_t       *relation_count,
  int32_t        relation_values[7],
  uint64_t      *direct_positive_feature_mask,
  uint64_t      *contributor_feature_mask,
  uint64_t      *lifecycle_update_feature_mask,
  size_t        *retained_evidence_count,
  int32_t        retained_evidence_feature_indices[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  int32_t        retained_evidence_transforms[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY][6],
  int32_t       *retained_evidence_flag,
  int32_t       *study_finalization_gate,
  int32_t       *study_action_gate,
  int32_t       *queue_candidate_eligible
#ifdef GOODIX53X5_DEBUG
  , GoodixMilanMatchDiagnostics *diagnostics
#endif
  )
{
  GoodixMilanUnpackedTemplate *enrolled = NULL;
  GoodixMilanFeatureRecord *enrolled_records_storage = NULL;
  const GoodixMilanFeatureRecord *enrolled_records = NULL;
  int32_t aggregate[15] = { 0 };
  size_t fallback_index = SIZE_MAX;
  int32_t fallback_quality = INT32_MIN;
  int32_t fallback_count = INT32_MIN;
  int32_t fallback_coverage = INT32_MIN;
  int32_t direct_aggregate[15] = { 0 };
  int32_t direct_transform[6] = { 0 };
  size_t direct_index = SIZE_MAX;
  int32_t direct_score_sum = 0;
  int32_t direct_candidate_count = 0;
  int direct_positive = 0;
  int32_t direct_quality = INT32_MIN;
  int32_t direct_count = INT32_MIN;
  int32_t direct_coverage = INT32_MIN;
  int direct_is_candidate = 0;
#ifdef GOODIX53X5_DEBUG
  int32_t policy_aggregate[15] = { 0 };
  size_t policy_index = SIZE_MAX;
  int32_t probe_policy_metrics[15] = { 0 };
  size_t probe_policy_index = SIZE_MAX;
  int32_t probe_policy_descriptor = INT32_MIN;
  int32_t probe_policy_overlap = INT32_MIN;
#endif
  int32_t fallback_transform[6] = { 0 };
  GoodixMilanActiveRelationWinner active_relation_winner;
  size_t probe_partition_count;
  int32_t score_denominator = 31;
  int32_t rejection_count_sum = 0;
  int32_t rejection_best_detail = INT32_MIN;
  int32_t rejection_best_coverage = 0;
  int rejection_candidate_seen = 0;
  int32_t late_policy_status_counter = 0;
  GoodixMilanMatcherPolicy matcher_policy = { 0 };
  GoodixMilanMatcherLateContext late_policy_context = { 0 };
  int32_t sibling_tail_hamming_limit = MILAN_MATCH_DESCRIPTOR_DISTANCE_LIMIT;
  GoodixMilanMatchSelection match_selection = { 0 };
  int32_t rescue_records[GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT]
                        [GOODIX_MILAN_MATCH_RESCUE_METRICS] = { { 0 } };
  int32_t rescue_order[GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT] = { 0 };
  GoodixMilanMatchRescueResult rescue_result;
  GoodixMilanMatchFallback match_fallback;
  int rescue_applied = 0;
  int result = -1;

  if (!probe_feature || !probe_feature->low_bitmap || !probe_rescue_mask ||
      !probe_records ||
      probe_record_count == 0 ||
      probe_record_count > 150 || !enrolled_template ||
      !matched_feature_index || !score || !match_transform ||
      !relation_count || !relation_values || !direct_positive_feature_mask ||
      !contributor_feature_mask || !lifecycle_update_feature_mask ||
      !retained_evidence_count || !retained_evidence_feature_indices ||
      !retained_evidence_transforms || !retained_evidence_flag ||
      !study_finalization_gate || !study_action_gate ||
      !queue_candidate_eligible ||
      live_order_count > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY ||
      (live_order_count != 0 &&
       (!live_records || !live_partition_counts || !live_order)))
    return -1;
  *matched_feature_index = SIZE_MAX;
  *score = -7;
  memset (match_transform, 0, 6 * sizeof(*match_transform));
  match_transform[0] = 0x100;
  match_transform[4] = 0x100;
  *relation_count = 0;
  memset (relation_values, 0, 7 * sizeof(*relation_values));
  relation_values[1] = 0x100;
  relation_values[5] = 0x100;
  *direct_positive_feature_mask = 0;
  *contributor_feature_mask = 0;
  *lifecycle_update_feature_mask = 0;
  *retained_evidence_count = 0;
  memset (retained_evidence_transforms, 0,
          GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY * 6 *
            sizeof(*retained_evidence_transforms[0]));
  *retained_evidence_flag = 0;
  *study_finalization_gate = 0;
  *study_action_gate = 0;
  *queue_candidate_eligible = 0;
#ifdef GOODIX53X5_DEBUG
  if (diagnostics)
    {
      memset (diagnostics, 0, sizeof(*diagnostics));
      diagnostics->fallback_feature_index = -1;
      diagnostics->direct_feature_index = -1;
      diagnostics->policy_feature_index = -1;
      diagnostics->probe_policy_feature_index = -1;
      diagnostics->probe_optional_c7 = probe_feature->fields.optional_c7;
    }
#endif
  enrolled = malloc (sizeof(*enrolled));
  enrolled_records_storage = malloc (150 * sizeof(*enrolled_records_storage));
  if (!enrolled || !enrolled_records_storage ||
      goodix_milan_template_unpack (
        enrolled_template, enrolled_template_size, enrolled) != 0)
    goto out;
  matcher_policy.configuration[15] = (int32_t) enrolled->metadata.sensor_type;
  if (enrolled->metadata.sensor_type == 12)
    {
      if (probe_feature->fields.tagged_values[2] < 0 ||
          (size_t) probe_feature->fields.tagged_values[2] > probe_record_count)
        goto out;
      probe_partition_count =
        (size_t) probe_feature->fields.tagged_values[2];
      score_denominator = 42;
      goodix_milan_matcher_policy_init (&matcher_policy,
                                        probe_feature->fields.optional_c7);
      goodix_milan_matcher_late_context_init (
        &late_policy_context, probe_feature->fields.optional_c7);
      sibling_tail_hamming_limit =
        matcher_policy.configuration[GOODIX_MILAN_MATCHER_CONFIGURATION_OFFSET +
                              GOODIX_MILAN_MATCHER_TAIL_HAMMING_LIMIT_INDEX];
    }
  else
    {
      probe_partition_count = 0;
      while (probe_partition_count < probe_record_count &&
             probe_records[probe_partition_count].foreground == 0)
        probe_partition_count++;
    }
#ifdef GOODIX53X5_DEBUG
  if (diagnostics)
    diagnostics->initial_packed_policy_mode = matcher_policy.configuration[18];
#endif
  goodix_milan_match_selection_reset (&match_selection);
  goodix_milan_active_relation_winner_reset (&active_relation_winner);
  goodix_milan_match_fallback_reset (&match_fallback);

  for (size_t order_index = 0; order_index < enrolled->feature_count;
       order_index++)
    {
      size_t feature_index = SIZE_MAX;

      if (live_records)
        {
          if (live_order && order_index < live_order_count)
            {
              feature_index = live_order[order_index];
              if (feature_index >= enrolled->feature_count ||
                  !live_records[feature_index])
                goto out;
            }
          else
            {
              size_t stored_order = order_index - live_order_count;

              for (size_t i = 0; i < enrolled->feature_count; i++)
                {
                  size_t candidate = goodix_milan_template_read_u32 (
                    enrolled->tail_state + i * sizeof(uint32_t));

                  if (candidate >= enrolled->feature_count ||
                      live_records[candidate])
                    continue;
                  if (stored_order-- == 0)
                    {
                      feature_index = candidate;
                      break;
                    }
                }
            }
        }
      else
        feature_index = goodix_milan_template_read_u32 (
          enrolled->tail_state + order_index * sizeof(uint32_t));

      if (order_index >= GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT ||
          feature_index >= GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT)
        goto out;
      rescue_order[order_index] = (int32_t) feature_index;

      GoodixMilanFeatureView feature;
      int32_t transform[6];
      size_t correspondence_count;
      size_t primary_filtered_count;
      size_t filtered_count;
      int32_t descriptor_score;
      int32_t topology_percent;
      int32_t geometric_percent;
      int32_t topology_bonus;
      int32_t overlap_score;
      int32_t overlap_coverage;
      int32_t overlap_detail;
      GoodixMilanMatchCandidate direct_candidate;
      int32_t direct_metrics[77] = { 0 };
      int32_t low_bitmap_metrics[3];
      int32_t match_flag;
      int32_t candidate_flag;
      int32_t fallback_enabled;
      GoodixMilanMatchFallbackWorkspace *fallback_workspace = NULL;
      int32_t late_policy_state[3] = { 0 };
      GoodixMilanMatchContributionEvent contribution_event;
#ifdef GOODIX53X5_DEBUG
      int32_t recognition_mode_before = matcher_policy.configuration[19];
#endif
      size_t enrolled_partition_count;

      if (feature_index >= enrolled->feature_count ||
          goodix_milan_template_parse_feature_element (
            enrolled->feature_elements[feature_index],
            enrolled->feature_element_sizes[feature_index], &feature) != 0)
        continue;
      if (feature.record_count == 0 || feature.record_count > 150)
        continue;
      if (live_records && live_records[feature_index])
        {
          if (!live_record_counts || !live_partition_counts ||
              live_record_counts[feature_index] != feature.record_count ||
              live_partition_counts[feature_index] > feature.record_count)
            continue;
          enrolled_records = live_records[feature_index];
          enrolled_partition_count = live_partition_counts[feature_index];
        }
      else
        {
          if (feature.fields.tagged_values[2] < 0 ||
              (size_t) feature.fields.tagged_values[2] > feature.record_count)
            continue;
          enrolled_partition_count =
            (size_t) feature.fields.tagged_values[2];
          if (goodix_milan_feature_unpack_template_records (
                feature.packed_records, feature.record_count,
                enrolled_partition_count,
                enrolled_records_storage, 150) != 0)
            continue;
          enrolled_records = enrolled_records_storage;
        }
      if (enrolled->metadata.sensor_type != 12)
        {
          enrolled_partition_count = 0;
          while (enrolled_partition_count < feature.record_count &&
                 enrolled_records[enrolled_partition_count].foreground == 0)
            enrolled_partition_count++;
        }
      if (enrolled->metadata.sensor_type == 12)
        {
          int32_t fallback_pairs[GOODIX_MILAN_MATCH_FALLBACK_PAIR_CAPACITY * 2];
          size_t fallback_pair_count;

          fallback_pair_count = goodix_milan_match_relaxed_correspondences (
            enrolled_records, feature.record_count, enrolled_partition_count,
            probe_records, probe_record_count, probe_partition_count,
            fallback_pairs);
          if (goodix_milan_match_fallback_store (
                &match_fallback, (int32_t) feature_index, 0, fallback_pairs,
                fallback_pair_count) != 0)
            goto out;
          fallback_workspace =
            &match_fallback.workspaces[match_fallback.workspace_count - 1];
        }
      if (enrolled->metadata.sensor_type == 12)
        {
          goodix_milan_matcher_late_context_derive (
            &late_policy_context, feature.fields.optional_c7,
            late_policy_state);
          if (goodix_milan_match_candidate_skip_pre_primary (
                matcher_policy.configuration[13], matcher_policy.configuration[14],
                feature_index, enrolled->feature_count,
                (int32_t) enrolled->metadata.maximum_features,
                match_selection.rejection_evidence,
                match_selection.retained_active_evidence,
                feature.fields.tagged_values[0]))
            continue;
        }
      if (milan_match_score_counts (
             enrolled_records, feature.record_count, enrolled_partition_count,
             probe_records, probe_record_count, probe_partition_count,
             transform, &correspondence_count,
             &primary_filtered_count, &filtered_count, &descriptor_score,
               &topology_percent, &geometric_percent, &topology_bonus,
                &feature, probe_feature,
                enrolled->metadata.sensor_type == 12 ? 42 : 31,
                 enrolled->metadata.sensor_type == 12 ? &direct_candidate : NULL) != 0)
        continue;
      if (enrolled->metadata.sensor_type == 12)
        {
        }
      milan_match_apply_secondary (
        enrolled_records, feature.record_count, enrolled_partition_count,
        probe_records, probe_record_count, probe_partition_count,
        &primary_filtered_count, &filtered_count, transform, &topology_percent,
        &geometric_percent, &topology_bonus,
        enrolled->metadata.sensor_type == 12 ? 42 : 31,
        sibling_tail_hamming_limit,
        &feature, probe_feature,
        enrolled->metadata.sensor_type == 12 ? &direct_candidate : NULL);
      if (enrolled->metadata.sensor_type == 12)
        {
          if (!goodix_milan_match_candidate_admit (&direct_candidate))
            {
              fallback_workspace->enabled =
                direct_candidate.words[1] >= 3 &&
                matcher_policy.configuration[13] == 1 &&
                match_selection.acceptance_evidence == 0;
              continue;
            }
          memcpy (direct_metrics, direct_candidate.words,
                  sizeof(direct_metrics));
          memcpy (transform, direct_candidate.transform, sizeof(transform));
          primary_filtered_count = (size_t) direct_candidate.words[0];
          filtered_count = (size_t) direct_candidate.words[1];
          topology_percent = direct_candidate.words[10];
          geometric_percent = direct_candidate.words[11];
        }
      descriptor_score = (int32_t) filtered_count * 100 /
                         (enrolled->metadata.sensor_type == 12 ? 42 : 31);

      if (goodix_milan_match_overlap_metrics_with_context (
            &feature, probe_feature, transform, &overlap_score,
            &overlap_coverage, &overlap_detail, low_bitmap_metrics,
            (int32_t) filtered_count) != 0)
        continue;
#ifdef GOODIX53X5_DEBUG
      if (diagnostics && overlap_score > diagnostics->overlap_score)
        {
          diagnostics->overlap_score = overlap_score;
          diagnostics->overlap_detail = overlap_detail;
          diagnostics->overlap_coverage = overlap_coverage;
        }
#endif

      if (enrolled->metadata.sensor_type != 12)
        {
          direct_metrics[0] = (int32_t) primary_filtered_count;
          direct_metrics[1] = (int32_t) filtered_count;
        }
      direct_metrics[4] = overlap_score;
      direct_metrics[5] = overlap_detail;
      memcpy (direct_metrics + 6, low_bitmap_metrics,
              sizeof(low_bitmap_metrics));
      direct_metrics[9] = overlap_coverage * 246 >> 8;
      direct_metrics[10] = topology_percent;
      direct_metrics[11] = geometric_percent;
      if (enrolled->metadata.sensor_type != 12 &&
          (abs (transform[2]) > 51 * 0x100 ||
           abs (transform[5]) > 51 * 0x100) && direct_metrics[11] < 36)
        direct_metrics[11] = 36;
#ifdef GOODIX53X5_DEBUG
      if (diagnostics && feature_index < GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
        {
          memcpy (diagnostics->feature_metrics[feature_index][0],
                  direct_metrics, 15 * sizeof(*direct_metrics));
          memcpy (diagnostics->feature_transforms[feature_index][0], transform,
                  sizeof(transform));
        }
      if (diagnostics && descriptor_score > diagnostics->descriptor_score)
        {
          diagnostics->descriptor_score = descriptor_score;
          diagnostics->filtered_count = (int32_t) filtered_count;
          memcpy (diagnostics->transform, transform,
                  sizeof(diagnostics->transform));
        }
#endif
#ifdef GOODIX53X5_DEBUG
      if (descriptor_score > probe_policy_descriptor)
        {
          probe_policy_descriptor = descriptor_score;
          probe_policy_metrics[0] = direct_metrics[0];
          probe_policy_metrics[1] = direct_metrics[1];
          probe_policy_index = feature_index;
        }
      if (overlap_score > probe_policy_overlap)
        {
          probe_policy_overlap = overlap_score;
          probe_policy_metrics[4] = direct_metrics[4];
          probe_policy_metrics[5] = direct_metrics[5];
          probe_policy_metrics[9] = direct_metrics[9];
        }
#endif
      if (goodix_milan_match_initial_flags (
            direct_metrics, image_quality, image_coverage,
            matcher_policy.configuration, &match_flag, &candidate_flag, NULL) != 0)
        continue;
      fallback_enabled = candidate_flag;
      if (enrolled->metadata.sensor_type != 12 && direct_metrics[0] > 4 &&
          direct_metrics[5] > 195)
        {
          rejection_candidate_seen = 1;
          rejection_count_sum += direct_metrics[0];
          if (direct_metrics[5] > rejection_best_detail)
            {
              rejection_best_detail = direct_metrics[5];
              rejection_best_coverage = direct_metrics[9];
            }
        }
      if ((direct_metrics[4] < 207 || match_flag == 0 || candidate_flag == 0))
        {
          int32_t refined_transform[6];
          int32_t refined_overlap_score;
          int32_t refined_overlap_coverage;
          int32_t refined_overlap_detail;
          int32_t refined_low_metrics[3];
          int32_t refined_topology_distance;
          int32_t refined_valid_count;
          int32_t refined_matched_count;

          if (goodix_milan_refine_record_similarity (
                enrolled_records, feature.record_count,
                enrolled_partition_count, probe_records, probe_record_count,
                probe_partition_count, transform, 2,
                refined_transform) == 0 &&
              goodix_milan_match_overlap_metrics_with_context (
                &feature, probe_feature, refined_transform,
                &refined_overlap_score, &refined_overlap_coverage,
                &refined_overlap_detail, refined_low_metrics,
                (int32_t) filtered_count) == 0 &&
              goodix_milan_match_post_admission_replaces (
                direct_metrics[4], direct_metrics[5], refined_overlap_score,
                refined_overlap_detail))
            {
              memcpy (transform, refined_transform,
                      sizeof(refined_transform));
              direct_metrics[4] = refined_overlap_score;
              direct_metrics[5] = refined_overlap_detail;
              memcpy (direct_metrics + 6, refined_low_metrics,
                      sizeof(refined_low_metrics));
              direct_metrics[9] = refined_overlap_coverage * 246 >> 8;
              goodix_milan_match_record_metrics_internal (
                enrolled_records, feature.record_count, probe_records,
                probe_record_count, transform, (int) filtered_count,
                &topology_percent, &geometric_percent, &topology_bonus,
                &refined_topology_distance, &refined_valid_count,
                &refined_matched_count);
              direct_metrics[10] = topology_percent;
              direct_metrics[11] = geometric_percent;
              if (enrolled->metadata.sensor_type == 12)
                {
                  goodix_milan_match_candidate_set_record_metrics (
                    &direct_candidate, refined_valid_count,
                    refined_matched_count, topology_percent,
                    geometric_percent, refined_topology_distance);
                  memcpy (direct_metrics + 66, direct_candidate.words + 66,
                          5 * sizeof(*direct_metrics));
                }
              if (enrolled->metadata.sensor_type != 12 &&
                  (abs (transform[2]) > 51 * 0x100 ||
                   abs (transform[5]) > 51 * 0x100) &&
                  direct_metrics[11] < 36)
                direct_metrics[11] = 36;
#ifdef GOODIX53X5_DEBUG
              if (diagnostics &&
                  feature_index < GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
                memcpy (diagnostics->feature_transforms[feature_index][0],
                        transform, sizeof(transform));
#endif
            }
        }
      if (goodix_milan_match_initial_flags (
            direct_metrics, image_quality, image_coverage,
            matcher_policy.configuration, &match_flag, &candidate_flag, NULL) != 0)
        continue;
      if (enrolled->metadata.sensor_type == 12)
        {
          memcpy (direct_candidate.transform, transform, sizeof(transform));
          goodix_milan_match_candidate_materialize_dispatch (&direct_candidate);
          memcpy (direct_metrics + 15, direct_candidate.words + 15,
                  6 * sizeof(*direct_metrics));
        }
      if (enrolled->metadata.sensor_type == 12)
        {
          int status;

          goodix_milan_matcher_policy_evaluate (
            &matcher_policy, direct_metrics, image_quality, image_coverage,
            late_policy_context.accumulated_high_class,
            &match_flag, &candidate_flag);
          goodix_milan_matcher_policy_apply_late_veto (
            &matcher_policy, direct_metrics, &match_flag, &candidate_flag);
          status = goodix_milan_matcher_policy_apply_status (
            direct_metrics, transform, late_policy_state,
            (int32_t) probe_record_count, image_coverage, image_quality,
            &match_flag, &late_policy_status_counter);
          if (status)
            continue;
          /* The native profile-9 probe has no active masked-overlap context;
           * its caller class tuple and therefore its support ratio are zero. */
          goodix_milan_matcher_policy_apply_final (
            direct_metrics, image_quality,
            late_policy_context.accumulated_high_class,
            late_policy_state[1], 0, &match_flag, &candidate_flag);
        }
      memcpy (rescue_records[feature_index], direct_metrics,
              sizeof(rescue_records[feature_index]));
      memset (&contribution_event, 0, sizeof(contribution_event));
      int contributes = 0;
      int retained = 0;
      int blocked = 0;
      if (enrolled->metadata.sensor_type == 12)
        {
          contributes = goodix_milan_match_selection_contribute (
            &match_selection, direct_metrics, transform, feature_index,
            matcher_policy.configuration[0], match_flag, candidate_flag,
            &contribution_event);
          if (contributes < 0)
            goto out;
          if (contributes > 0 && caller_blocking_enabled && probe_antifake &&
              feature.antifake &&
              image_coverage > 40 && feature.fields.tagged_values[4] > 40)
            {
              int32_t comparison_metrics[5];
              if (goodix_milan_antifake_pair_metrics (
                    feature.antifake, GOODIX_MILAN_ANTIFAKE_SIZE,
                    probe_antifake, GOODIX_MILAN_ANTIFAKE_SIZE, transform,
                    comparison_metrics) != 0)
                goto out;
              blocked = goodix_milan_match_selection_block_candidate (
                &match_selection, enrolled->metadata.sensor_type,
                comparison_metrics,
                goodix_milan_antifake_texture (feature.antifake) -
                  goodix_milan_antifake_texture (probe_antifake),
                goodix_milan_antifake_mean (feature.antifake) -
                  goodix_milan_antifake_mean (probe_antifake),
                goodix_milan_antifake_threshold (feature.antifake) -
                  goodix_milan_antifake_threshold (probe_antifake),
                goodix_milan_antifake_pair_score (feature.antifake),
                direct_metrics[9], &contribution_event);
              if (blocked < 0)
                goto out;
            }
          if (!blocked)
            {
              retained = goodix_milan_match_selection_admit (
                &match_selection, direct_metrics, transform, feature_index,
                feature.fields.tagged_values[0], match_flag, candidate_flag,
                &contribution_event);
              if (retained < 0)
                goto out;
            }
        }
      if (retained > 0)
        {
          if (contribution_event.direct_published &&
              feature.fields.tagged_values[0] == 1)
            {
              if (goodix_milan_active_relation_winner_would_update (
                    &active_relation_winner, direct_metrics[1]))
                {
                  int32_t routed[7];
                  int relation_status =
                    goodix_milan_match_reference_transform (
                      enrolled, feature_index, transform,
                      routed);

                  if (relation_status == 0)
                    goodix_milan_active_relation_winner_update (
                      &active_relation_winner, 1, direct_metrics[1],
                      (int32_t) feature_index, transform, routed + 1);
                }
            }
        }
#ifdef GOODIX53X5_DEBUG
      int outer_eligible =
        direct_metrics[1] > 4 && direct_metrics[10] > 30 &&
        (candidate_flag == 1 ||
         goodix_milan_match_fallback_candidate_eligible (
           direct_metrics[4], 207, direct_metrics[5]));
#endif

#ifdef GOODIX53X5_DEBUG
      if (diagnostics &&
          diagnostics->candidate_count < GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
        {
          GoodixMilanMatchCandidateDiagnostic *candidate =
            &diagnostics->candidates[diagnostics->candidate_count++];

          candidate->order_index = (int32_t) order_index;
          candidate->enrolled_feature_index = (int32_t) feature_index;
          candidate->probe_feature_index = 0;
          memcpy (candidate->metrics, direct_metrics,
                  sizeof(candidate->metrics));
          memcpy (candidate->transform, transform,
                  sizeof(candidate->transform));
          candidate->final_flags[0] = match_flag;
          candidate->final_flags[1] = candidate_flag;
          candidate->recognition_mode_before = recognition_mode_before;
          candidate->recognition_mode_after = matcher_policy.configuration[19];
          candidate->outer_eligible = outer_eligible;
          candidate->contributes = contributes;
          candidate->q8_contribution = contribution_event.q8_term;
          candidate->blocking_recorded =
            contribution_event.blocking_recorded;
          candidate->blocking_metric = contribution_event.blocking_metric;
        }
#endif
      if (blocked)
        continue;
#ifdef GOODIX53X5_DEBUG
      if (direct_metrics[1] > policy_aggregate[1] ||
          (direct_metrics[1] == policy_aggregate[1] &&
           direct_metrics[5] > policy_aggregate[5]))
        {
          memcpy (policy_aggregate, direct_metrics, sizeof(policy_aggregate));
          policy_index = feature_index;
        }
#endif
      if (enrolled->metadata.sensor_type != 12 &&
          direct_metrics[1] > 4 &&
          direct_metrics[10] > 30 &&
          (candidate_flag == 1 ||
           goodix_milan_match_fallback_candidate_eligible (
             direct_metrics[4], 207, direct_metrics[5])) &&
          (match_flag > direct_is_candidate ||
           (match_flag == direct_is_candidate &&
             (direct_metrics[5] > direct_quality ||
              (direct_metrics[5] == direct_quality &&
                direct_metrics[1] > direct_count) ||
               (direct_metrics[5] == direct_quality &&
                direct_metrics[1] == direct_count &&
                direct_metrics[9] > direct_coverage)))))
        {
          memcpy (direct_aggregate, direct_metrics, sizeof(direct_aggregate));
          direct_index = feature_index;
          direct_quality = direct_metrics[5];
          direct_count = direct_metrics[1];
          direct_coverage = direct_metrics[9];
          direct_is_candidate = match_flag;
          direct_positive = candidate_flag == 1;
          memcpy (direct_transform, transform, sizeof(direct_transform));
        }
      if (enrolled->metadata.sensor_type != 12 &&
          direct_metrics[1] > 4 &&
          direct_metrics[10] > 30 &&
          (candidate_flag == 1 ||
        goodix_milan_match_fallback_candidate_eligible (
              direct_metrics[4], 207, direct_metrics[5])))
        {
          direct_score_sum +=
            ((direct_metrics[1] * 0x100 + (score_denominator >> 1)) /
             score_denominator);
          direct_candidate_count++;
        }
      int32_t reverse_transform[6];
      int32_t reverse_forward_transform[6];
      int32_t reverse_metrics[77] = { 0 };
      int32_t reverse_low_metrics[3];
      size_t reverse_correspondence_count;
      size_t reverse_primary_filtered_count;
      size_t reverse_filtered_count;
      int32_t reverse_descriptor_score;
      int32_t reverse_topology_percent;
      int32_t reverse_geometric_percent;
      int32_t reverse_topology_bonus;
      int32_t reverse_overlap_score;
      int32_t reverse_overlap_coverage;
      int32_t reverse_overlap_detail;
      int32_t reverse_match_flag;
      int32_t reverse_candidate_flag;

       if (enrolled->metadata.sensor_type != 12 &&
          milan_match_score_counts (
            probe_records, probe_record_count, probe_partition_count,
            enrolled_records, feature.record_count, enrolled_partition_count,
            reverse_transform,
            &reverse_correspondence_count, &reverse_primary_filtered_count,
            &reverse_filtered_count, &reverse_descriptor_score,
            &reverse_topology_percent, &reverse_geometric_percent,
             &reverse_topology_bonus, probe_feature, &feature,
              enrolled->metadata.sensor_type == 12 ? 42 : 31, NULL) == 0)
        {
          milan_match_apply_secondary (
            probe_records, probe_record_count, probe_partition_count,
            enrolled_records, feature.record_count, enrolled_partition_count,
            &reverse_primary_filtered_count, &reverse_filtered_count,
            reverse_transform,
            &reverse_topology_percent, &reverse_geometric_percent,
            &reverse_topology_bonus,
            enrolled->metadata.sensor_type == 12 ? 42 : 31,
            sibling_tail_hamming_limit,
            probe_feature, &feature, NULL);
          reverse_descriptor_score =
            (int32_t) reverse_filtered_count * 100 /
            (enrolled->metadata.sensor_type == 12 ? 42 : 31);
#ifdef GOODIX53X5_DEBUG
          if (diagnostics &&
              reverse_descriptor_score > diagnostics->descriptor_score)
            {
              diagnostics->descriptor_score = reverse_descriptor_score;
              diagnostics->filtered_count = (int32_t) reverse_filtered_count;
              if (milan_invert_transform (
                    reverse_transform, reverse_forward_transform) == 0)
                memcpy (diagnostics->transform, reverse_forward_transform,
                        sizeof(diagnostics->transform));
            }
#endif
          reverse_metrics[0] = (int32_t) reverse_primary_filtered_count;
          reverse_metrics[1] = (int32_t) reverse_filtered_count;
          if (goodix_milan_match_overlap_metrics_with_context (
                probe_feature, &feature, reverse_transform,
                &reverse_overlap_score, &reverse_overlap_coverage,
                &reverse_overlap_detail, reverse_low_metrics,
                (int32_t) reverse_filtered_count) == 0)
            {
              reverse_metrics[4] = reverse_overlap_score;
              reverse_metrics[5] = reverse_overlap_detail;
              memcpy (reverse_metrics + 6, reverse_low_metrics,
                      sizeof(reverse_low_metrics));
              reverse_metrics[9] = reverse_overlap_coverage * 246 >> 8;
              reverse_metrics[10] = reverse_topology_percent;
              reverse_metrics[11] = reverse_geometric_percent;
              if ((abs (reverse_transform[2]) > 51 * 0x100 ||
                   abs (reverse_transform[5]) > 51 * 0x100) &&
                  reverse_metrics[11] < 36)
                  reverse_metrics[11] = 36;
#ifdef GOODIX53X5_DEBUG
              if (diagnostics &&
                  feature_index < GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
                {
                  memcpy (diagnostics->feature_metrics[feature_index][1],
                          reverse_metrics, 15 * sizeof(*reverse_metrics));
                  if (milan_invert_transform (
                        reverse_transform, reverse_forward_transform) == 0)
                    memcpy (diagnostics->feature_transforms[feature_index][1],
                            reverse_forward_transform,
                            sizeof(reverse_forward_transform));
                }
#endif
              if (goodix_milan_match_initial_flags (
                    reverse_metrics, feature.fields.tagged_values[3],
                    feature.fields.tagged_values[4], matcher_policy.configuration,
                    &reverse_match_flag, &reverse_candidate_flag, NULL) == 0 &&
                  milan_invert_transform (
                    reverse_transform, reverse_forward_transform) == 0)
                {
#ifdef GOODIX53X5_DEBUG
                  if (reverse_descriptor_score > probe_policy_descriptor)
                    {
                      probe_policy_descriptor = reverse_descriptor_score;
                      probe_policy_metrics[0] = reverse_metrics[0];
                      probe_policy_metrics[1] = reverse_metrics[1];
                      probe_policy_index = feature_index;
                    }
                  if (reverse_metrics[1] > policy_aggregate[1] ||
                      (reverse_metrics[1] == policy_aggregate[1] &&
                       reverse_metrics[5] > policy_aggregate[5]))
                    {
                      memcpy (policy_aggregate, reverse_metrics,
                              sizeof(policy_aggregate));
                      policy_index = feature_index;
                    }
#endif
                  if (reverse_metrics[1] > 4 &&
                      reverse_metrics[10] > 30 &&
                      (reverse_match_flag == 1 ||
                       goodix_milan_match_fallback_candidate_eligible (
                         reverse_metrics[4], 207, reverse_metrics[5])) &&
                      (reverse_candidate_flag > direct_is_candidate ||
                       (reverse_candidate_flag == direct_is_candidate &&
                        (reverse_metrics[5] > direct_quality ||
                         (reverse_metrics[5] == direct_quality &&
                           reverse_metrics[1] > direct_count) ||
                          (reverse_metrics[5] == direct_quality &&
                           reverse_metrics[1] == direct_count &&
                           reverse_metrics[9] > direct_coverage)))))
                    {
                      memcpy (direct_aggregate, reverse_metrics,
                              sizeof(direct_aggregate));
                      direct_index = feature_index;
                      direct_quality = reverse_metrics[5];
                      direct_count = reverse_metrics[1];
                      direct_coverage = reverse_metrics[9];
                      direct_is_candidate = reverse_candidate_flag;
                      memcpy (direct_transform, reverse_forward_transform,
                              sizeof(direct_transform));
                    }
                  if (reverse_metrics[1] > 4 &&
                      reverse_metrics[10] > 30 &&
                      (reverse_match_flag == 1 ||
                       goodix_milan_match_fallback_candidate_eligible (
                         reverse_metrics[4], 207, reverse_metrics[5])))
                    {
                      direct_score_sum +=
                        ((reverse_metrics[1] * 0x100 +
                          (score_denominator >> 1)) /
                         score_denominator);
                      direct_candidate_count++;
                    }
                }
            }
        }
      if (enrolled->metadata.sensor_type == 12)
        fallback_workspace->enabled =
          matcher_policy.configuration[13] == 1 &&
          match_selection.acceptance_evidence == 0;
      if (fallback_enabled && enrolled->metadata.sensor_type != 12)
        {
          int32_t relaxed_pairs[62];
          int32_t relaxed_transform[6];
          int32_t relaxed_residual;
          int32_t relaxed_overlap_score;
          int32_t relaxed_overlap_coverage;
          int32_t relaxed_overlap_detail;
          int32_t relaxed_low_metrics[3];
          size_t relaxed_filtered_count;
          size_t relaxed_pair_count;

          relaxed_pair_count = goodix_milan_match_relaxed_correspondences (
            enrolled_records, feature.record_count, enrolled_partition_count,
            probe_records, probe_record_count, probe_partition_count,
            relaxed_pairs);
          if (relaxed_pair_count < 3 ||
              goodix_milan_filter_recognition_pairs (
                enrolled_records, probe_records, relaxed_pairs,
                relaxed_pair_count, relaxed_transform,
                &relaxed_filtered_count, &relaxed_residual) != 0 ||
              relaxed_filtered_count <= 4 ||
              goodix_milan_match_overlap_metrics_with_context (
                &feature, probe_feature, relaxed_transform,
                &relaxed_overlap_score, &relaxed_overlap_coverage,
                &relaxed_overlap_detail, relaxed_low_metrics,
                (int32_t) relaxed_filtered_count) != 0 ||
              !goodix_milan_match_fallback_candidate_eligible (
                relaxed_overlap_score, 207, relaxed_overlap_detail))
            continue;
#ifdef GOODIX53X5_DEBUG
          if (diagnostics)
            diagnostics->fallback_count += 2;
#endif
          int32_t scaled_coverage = relaxed_overlap_coverage * 246 >> 8;

          if (relaxed_overlap_detail > fallback_quality ||
              (relaxed_overlap_detail == fallback_quality &&
               (int32_t) relaxed_filtered_count > fallback_count) ||
              (relaxed_overlap_detail == fallback_quality &&
               (int32_t) relaxed_filtered_count == fallback_count &&
               scaled_coverage > fallback_coverage))
            {
              aggregate[0] = (int32_t) relaxed_filtered_count;
              aggregate[1] = (int32_t) relaxed_filtered_count;
              aggregate[4] = relaxed_overlap_score;
              aggregate[5] = relaxed_overlap_detail;
              memcpy (aggregate + 6, relaxed_low_metrics,
                      sizeof(relaxed_low_metrics));
              aggregate[9] = scaled_coverage;
              fallback_quality = relaxed_overlap_detail;
              fallback_count = (int32_t) relaxed_filtered_count;
              fallback_coverage = scaled_coverage;
              fallback_index = feature_index;
              memcpy (fallback_transform, relaxed_transform,
                      sizeof(fallback_transform));
            }
        }
    }

  if (enrolled->metadata.sensor_type == 12)
    {
      *contributor_feature_mask =
        goodix_milan_match_selection_contributor_mask (&match_selection);
      *direct_positive_feature_mask =
        goodix_milan_match_selection_direct_mask (&match_selection);
    }

  if (enrolled->metadata.sensor_type == 12)
    {
      *queue_candidate_eligible =
        matcher_policy.configuration[13] == 1 &&
        late_policy_context.probe_low_class == 0 && image_coverage > 65 &&
        image_quality > 15 && late_policy_context.accumulated_high_class < 4 &&
        late_policy_status_counter < 3;
#ifdef GOODIX53X5_DEBUG
      if (diagnostics)
        {
          diagnostics->queue_study_evidence = 0;
          diagnostics->queue_configuration_enabled = matcher_policy.configuration[13];
          diagnostics->queue_probe_low_class = late_policy_context.probe_low_class;
          diagnostics->queue_accumulated_high_class =
            late_policy_context.accumulated_high_class;
          diagnostics->queue_status_count = late_policy_status_counter;
        }
#endif
    }

  if (goodix_milan_match_rescue_caller_eligible (
        match_selection.q8_contributor_count,
        match_selection.postloop_blocking_count,
        match_selection.rejection_evidence,
        enrolled->metadata.sensor_type))
    {
      GoodixMilanMatchRescueInput rescue_input = {
        .type = enrolled->metadata.sensor_type,
        .width = 104,
        .height = 88,
        .half_resolution = 1,
        .source_mask = probe_rescue_mask,
        .source_mask_size = GOODIX_MILAN_MATCH_RESCUE_MASK_SIZE,
        .source_stride = GOODIX_MILAN_MATCH_RESCUE_MASK_STRIDE,
        .records = &rescue_records[0][0],
        .record_count = enrolled->feature_count,
        .ordered_features = rescue_order,
        .ordered_count = enrolled->feature_count,
        .incoming_score = match_selection.score_latched
                            ? match_selection.latched_score : 0,
      };

      if (goodix_milan_match_rescue_evaluate (
            &rescue_input, &rescue_result) != 0)
        goto out;
      rescue_applied = rescue_result.set_acceptance;
    }

#ifdef GOODIX53X5_DEBUG
  if (diagnostics)
    {
      if (enrolled->metadata.sensor_type == 12)
        memcpy (diagnostics->direct_aggregate, match_selection.winner_metrics,
                sizeof(diagnostics->direct_aggregate));
      else
        memcpy (diagnostics->direct_aggregate, direct_aggregate,
                sizeof(direct_aggregate));
      memcpy (diagnostics->policy_aggregate, policy_aggregate,
              sizeof(policy_aggregate));
      memcpy (diagnostics->probe_policy_aggregate, probe_policy_metrics,
              sizeof(probe_policy_metrics));
      diagnostics->direct_feature_index = enrolled->metadata.sensor_type == 12
                                            ? match_selection.winner_feature
                                            : (direct_index == SIZE_MAX
                                                 ? -1
                                                 : (int32_t) direct_index);
      diagnostics->policy_feature_index =
        policy_index == SIZE_MAX ? -1 : (int32_t) policy_index;
      diagnostics->probe_policy_feature_index =
        probe_policy_index == SIZE_MAX ? -1 : (int32_t) probe_policy_index;
      diagnostics->q8_sum = enrolled->metadata.sensor_type == 12
                              ? match_selection.q8_sum
                              : direct_score_sum;
      diagnostics->q8_contributor_count = enrolled->metadata.sensor_type == 12
                                             ? match_selection.q8_contributor_count
                                             : direct_candidate_count;
      diagnostics->postloop_blocking_count =
        match_selection.postloop_blocking_count;
      diagnostics->postloop_blocking_sum =
        match_selection.postloop_blocking_sum;
      diagnostics->postloop_blocking_override =
        goodix_milan_match_selection_blocking_override (&match_selection);
      diagnostics->final_selected_feature_index =
        rescue_applied
          ? rescue_result.selected_feature
          : (enrolled->metadata.sensor_type == 12
               ? match_selection.selected_feature
               : (direct_index == SIZE_MAX ? -1 : (int32_t) direct_index));
    }
#endif
  int32_t aggregate_score = 0;

  if (rescue_applied)
    {
      *score = rescue_result.score;
      *matched_feature_index = (size_t) rescue_result.selected_feature;
      *lifecycle_update_feature_mask =
        goodix_milan_match_selection_lifecycle_mask (&match_selection);
      if (rescue_result.retain_transform)
        memcpy (match_transform, rescue_result.selected_transform,
                sizeof(rescue_result.selected_transform));
      *relation_count = 0;
      int relation_status = goodix_milan_match_reference_transform (
        enrolled, *matched_feature_index, match_transform, relation_values);

      if (relation_status < 0)
        *relation_count = -1;
      result = 0;
      if (goodix_milan_match_selection_blocking_override (&match_selection))
        *score = -65536;
      goto out;
    }

  if (enrolled->metadata.sensor_type == 12 &&
      goodix_milan_match_selection_finalize (
        &match_selection, matcher_policy.configuration[18],
        match_selection.postloop_blocking_count,
        milan_match_validate_winner, &matcher_policy))
    {
      *score = match_selection.latched_score;
      *lifecycle_update_feature_mask =
        goodix_milan_match_selection_lifecycle_mask (&match_selection);
      if (match_selection.selected_feature >= 0)
        {
          *matched_feature_index = (size_t) match_selection.selected_feature;
          memcpy (match_transform, match_selection.selected_transform,
                  sizeof(match_selection.selected_transform));
          *relation_count = match_selection.selected_numerator;
          int relation_status;

          if (active_relation_winner.valid)
            {
              relation_values[0] = 0;
              memcpy (relation_values + 1, active_relation_winner.routed,
                      sizeof(active_relation_winner.routed));
              relation_status = 0;
            }
          else
            relation_status = goodix_milan_match_reference_transform (
              enrolled, *matched_feature_index, match_transform,
              relation_values);

          if (relation_status < 0)
            *relation_count = -1;
          else if (relation_status > 0)
            *relation_count = 0;
        }
      result = 0;
      if (goodix_milan_match_selection_blocking_override (&match_selection))
        *score = -65536;
      goto out;
    }
  if (enrolled->metadata.sensor_type == 12)
    {
      for (size_t i = 0; i < match_fallback.workspace_count; i++)
        {
          const GoodixMilanMatchFallbackWorkspace *workspace =
            &match_fallback.workspaces[i];
          GoodixMilanFeatureView feature;
          int32_t fallback_candidate_transform[6];
          size_t filtered_count;
          int32_t residual;
          int32_t overlap;
          int32_t coverage;
          int32_t detail;
          int32_t low_metrics[3];
          int32_t average_scale;
          int32_t absolute_dot_q16;
          int32_t transform_classifier;

          if (!workspace->enabled || workspace->pair_count < 3 ||
              workspace->feature < 0 ||
              (size_t) workspace->feature >= enrolled->feature_count ||
               goodix_milan_template_parse_feature_element (
                 enrolled->feature_elements[workspace->feature],
                 enrolled->feature_element_sizes[workspace->feature],
                 &feature) != 0 ||
               feature.record_count == 0 || feature.record_count > 150)
             continue;
          if (live_records && live_records[workspace->feature])
            {
              if (!live_record_counts ||
                  live_record_counts[workspace->feature] !=
                    feature.record_count)
                continue;
              enrolled_records = live_records[workspace->feature];
            }
          else
            {
              if (goodix_milan_feature_unpack_template_records (
                    feature.packed_records, feature.record_count,
                    (size_t) feature.fields.tagged_values[2],
                    enrolled_records_storage, 150) != 0)
                continue;
              enrolled_records = enrolled_records_storage;
            }
          if (goodix_milan_filter_recognition_pairs (
                enrolled_records, probe_records, workspace->pairs,
                workspace->pair_count, fallback_candidate_transform,
                &filtered_count, &residual) != 0)
            continue;
          int32_t decoded_high = (matcher_policy.configuration[18] >> 8) & 7;

          decoded_high = decoded_high == 0 ? 0
                         : decoded_high <= 2 ? decoded_high
                         : decoded_high <= 5 ? decoded_high + 1 : 0;
          transform_classifier = goodix_milan_match_transform_proximity (
            fallback_candidate_transform, decoded_high,
            enrolled->metadata.sensor_type);
          if (filtered_count <= 4 || transform_classifier != 0)
            continue;
          if (goodix_milan_match_overlap_metrics_with_context (
                &feature, probe_feature, fallback_candidate_transform,
                &overlap, &coverage, &detail, low_metrics,
                (int32_t) filtered_count) != 0)
            continue;
          goodix_milan_match_affine_details (
            fallback_candidate_transform, &average_scale,
            &absolute_dot_q16);
          int32_t accumulated = goodix_milan_match_fallback_consider (
            &match_fallback, workspace->feature, (int32_t) filtered_count,
            transform_classifier, overlap, matcher_policy.configuration[0] + 207,
            detail, coverage * matcher_policy.configuration[12] >> 8,
            matcher_policy.configuration[16] != 0, average_scale, absolute_dot_q16,
            fallback_candidate_transform);
          (void) accumulated;
        }

      if (match_fallback.winner_valid)
        {
          GoodixMilanFeatureView feature;
          int32_t metrics[15] = { 0 };
          int32_t fallback_score = 0;

          if (goodix_milan_template_parse_feature_element (
                enrolled->feature_elements[match_fallback.winner.feature],
                enrolled->feature_element_sizes[match_fallback.winner.feature],
                &feature) != 0 ||
              goodix_milan_match_low_bitmap_metrics (
                feature.low_bitmap, feature.inline_mask,
                probe_feature->low_bitmap, probe_feature->inline_mask,
                match_fallback.winner.transform, metrics + 6) != 0)
            goto out;
          metrics[0] = match_fallback.winner.filtered_count;
          metrics[1] = match_fallback.winner.filtered_count;
          metrics[4] = match_fallback.winner.overlap;
          metrics[5] = match_fallback.winner.raw_detail;
          metrics[9] = match_fallback.winner.coverage;
          goodix_milan_match_affine_penalties (
            match_fallback.winner.transform, metrics + 12, metrics + 13,
            metrics + 14);
          if (goodix_milan_match_final_score (
                metrics, enrolled->metadata.sensor_type,
                matcher_policy.configuration[17] != 0, &fallback_score) != 0)
            goto out;
          if (fallback_score > 0)
            {
              *score = fallback_score > 100 ? 100 : fallback_score;
              if (goodix_milan_match_selection_publish_fallback (
                    &match_selection, *score,
                    match_selection.postloop_blocking_count,
                    score) < 0)
                goto out;
              result = 0;
              goto out;
            }
        }
      *score = goodix_milan_match_fallback_rejection_score (&match_fallback);
      if (match_selection.postloop_blocking_count > 0)
        *score = -65536;
    }
  if (enrolled->metadata.sensor_type != 12 &&
      direct_index != SIZE_MAX && direct_candidate_count > 0 &&
      (direct_positive ||
       (goodix_milan_match_final_score (
          direct_aggregate, enrolled->metadata.sensor_type, 1,
          &aggregate_score) == 0 && aggregate_score > 0)))
    {
      *score = (direct_score_sum * 100 / direct_candidate_count) >> 8;
      *matched_feature_index = direct_index;
      *lifecycle_update_feature_mask = UINT64_C(1) << direct_index;
      memcpy (match_transform, direct_transform, sizeof(direct_transform));
      *relation_count = direct_count;
      int relation_status = goodix_milan_match_reference_transform (
        enrolled, direct_index, match_transform, relation_values);
      if (relation_status < 0)
        *relation_count = -1;
      else if (relation_status > 0)
        *relation_count = 0;
      result = 0;
      goto out;
    }
  if (fallback_index != SIZE_MAX &&
      goodix_milan_match_final_score (
        aggregate, 23, 0, score) == 0)
    {
#ifdef GOODIX53X5_DEBUG
      if (diagnostics)
        {
          diagnostics->fallback_feature_index = (int32_t) fallback_index;
          memcpy (diagnostics->fallback_metrics, aggregate,
                  sizeof(diagnostics->fallback_metrics));
        }
#endif
      if (*score > 0)
        {
          *matched_feature_index = fallback_index;
          *lifecycle_update_feature_mask = UINT64_C(1) << fallback_index;
          memcpy (match_transform, fallback_transform,
                  sizeof(fallback_transform));
          *relation_count = fallback_count;
          int relation_status = goodix_milan_match_reference_transform (
            enrolled, fallback_index, match_transform, relation_values);
          if (relation_status < 0)
            *relation_count = -1;
          else if (relation_status > 0)
            *relation_count = 0;
        }
    }
  if (enrolled->metadata.sensor_type != 12 &&
      *matched_feature_index == SIZE_MAX)
    {
      int rejection_reason = (rejection_count_sum < 6) << 2;

      if (rejection_candidate_seen && rejection_best_detail < 208)
        rejection_reason |= 2;
      if (rejection_candidate_seen && rejection_best_coverage < 128)
        rejection_reason |= 1;
      *score = -rejection_reason;
    }
  result = 0;

out:
  *retained_evidence_count = match_selection.retained_evidence_count;
  memcpy (retained_evidence_feature_indices,
          match_selection.retained_evidence_feature_indices,
          sizeof(match_selection.retained_evidence_feature_indices));
  memcpy (retained_evidence_transforms,
          match_selection.retained_evidence_transforms,
          sizeof(match_selection.retained_evidence_transforms));
  *retained_evidence_flag = match_selection.retained_active_evidence;
  *study_finalization_gate = match_selection.acceptance_evidence != 0 ||
                             (rescue_applied && rescue_result.set_acceptance);
  *study_action_gate = match_selection.rejection_evidence != 0 ||
                       (rescue_applied && rescue_result.set_rejection);
  free (enrolled_records_storage);
  free (enrolled);
  return result;
}

static int
milan_match_probe_result_internal (
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
  int                            caller_blocking_enabled,
  const uint8_t                 *enrolled_template,
  size_t                         enrolled_template_size,
  const GoodixMilanFeatureRecord *const live_records[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                   live_record_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                   live_partition_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  const size_t                  *live_order,
  size_t                         live_order_count,
  GoodixMilanMatchResult        *match_result
#ifdef GOODIX53X5_DEBUG
  , GoodixMilanMatchDiagnostics *diagnostics
#endif
  )
{
  GoodixMilanFeatureView probe_feature = { 0 };
  int result;

  if (!probe_high_bitmap || !probe_enhanced_bitmap || !probe_low_bitmap ||
      !probe_inline_mask || !probe_rescue_mask || !match_result ||
      probe_record_count > 150 || probe_partition_count > probe_record_count)
    return -1;
  memset (match_result, 0, sizeof(*match_result));
  match_result->matched_feature_index = SIZE_MAX;
  match_result->score = -7;
  match_result->match_transform[0] = 0x100;
  match_result->match_transform[4] = 0x100;
  match_result->relation.relation_values[1] = 0x100;
  match_result->relation.relation_values[5] = 0x100;
  probe_feature.high_bitmap = probe_high_bitmap;
  probe_feature.enhanced_bitmap = probe_enhanced_bitmap;
  probe_feature.low_bitmap = probe_low_bitmap;
  probe_feature.inline_mask = probe_inline_mask;
  probe_feature.record_count = probe_record_count;
  probe_feature.fields.tagged_values[2] = (int32_t) probe_partition_count;
  probe_feature.fields.optional_c7 = probe_optional_c7;
  probe_feature.antifake = probe_antifake;
  result = milan_match_prepared_probe (
    &probe_feature, probe_rescue_mask, probe_records, probe_record_count,
    image_quality, image_coverage, probe_antifake, caller_blocking_enabled,
    enrolled_template,
    enrolled_template_size, live_records, live_record_counts,
    live_partition_counts, live_order, live_order_count,
    &match_result->matched_feature_index,
    &match_result->score, match_result->match_transform,
    &match_result->relation.relation_count,
    match_result->relation.relation_values,
    &match_result->direct_positive_feature_mask,
    &match_result->contributor_feature_mask,
    &match_result->lifecycle_update_feature_mask,
    &match_result->retained_evidence_count,
    match_result->retained_evidence_feature_indices,
    match_result->retained_evidence_transforms,
    &match_result->retained_evidence_flag,
    &match_result->study_control.study_finalization_gate,
    &match_result->study_control.study_action_gate,
    &match_result->study_control.queue_candidate_eligible
#ifdef GOODIX53X5_DEBUG
    , diagnostics
#endif
    );
  match_result->relation.relation_valid =
    result == 0 && match_result->matched_feature_index != SIZE_MAX &&
    match_result->relation.relation_count >= 0;
  return result;
}

int
goodix_milan_match_probe_result (
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
  GoodixMilanMatchResult        *match_result)
{
  return milan_match_probe_result_internal (
    probe_high_bitmap, probe_enhanced_bitmap, probe_low_bitmap,
    probe_inline_mask, probe_rescue_mask, probe_records, probe_record_count,
    probe_partition_count, image_quality, image_coverage, probe_optional_c7,
    probe_antifake, probe_antifake != NULL,
    enrolled_template, enrolled_template_size, NULL, NULL, NULL, NULL, 0,
    match_result
#ifdef GOODIX53X5_DEBUG
    , NULL
#endif
    );
}

#ifdef GOODIX53X5_DEBUG
int
goodix_milan_match_probe_result_debug (
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
  GoodixMilanMatchDiagnostics   *diagnostics)
{
  return milan_match_probe_result_internal (
    probe_high_bitmap, probe_enhanced_bitmap, probe_low_bitmap,
    probe_inline_mask, probe_rescue_mask, probe_records, probe_record_count,
    probe_partition_count, image_quality, image_coverage, probe_optional_c7,
    probe_antifake, probe_antifake != NULL,
    enrolled_template, enrolled_template_size, NULL, NULL, NULL, NULL, 0,
    match_result, diagnostics);
}
#endif

int
goodix_milan_match_live_probe_result (
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
  const size_t                  *live_order,
  size_t                         live_order_count,
  GoodixMilanMatchResult        *match_result)
{
  return milan_match_probe_result_internal (
    probe_high_bitmap, probe_enhanced_bitmap, probe_low_bitmap,
    probe_inline_mask, probe_rescue_mask, probe_records, probe_record_count,
    probe_partition_count, image_quality, image_coverage, probe_optional_c7,
    NULL, 0, enrolled_template, enrolled_template_size, live_records,
    live_record_counts, live_partition_counts, live_order, live_order_count,
    match_result
#ifdef GOODIX53X5_DEBUG
    , NULL
#endif
    );
}

#ifdef GOODIX53X5_DEBUG
int
goodix_milan_match_live_probe_result_debug (
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
  const size_t                  *live_order,
  size_t                         live_order_count,
  GoodixMilanMatchResult        *match_result,
  GoodixMilanMatchDiagnostics   *diagnostics)
{
  return milan_match_probe_result_internal (
    probe_high_bitmap, probe_enhanced_bitmap, probe_low_bitmap,
    probe_inline_mask, probe_rescue_mask, probe_records, probe_record_count,
    probe_partition_count, image_quality, image_coverage, probe_optional_c7,
    NULL, 0, enrolled_template, enrolled_template_size, live_records,
    live_record_counts, live_partition_counts, live_order, live_order_count,
    match_result, diagnostics);
}
#endif
