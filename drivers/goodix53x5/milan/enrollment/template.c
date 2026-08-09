/*
 * Goodix 53x5 driver for libfprint - native Milan enrollment templates
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "milan/enrollment/propagation.h"
#include "milan/match/match.h"
#include "milan/match/info-private.h"
#include "milan/milan.h"
#include "milan/relations.h"
#include "milan/transform-private.h"

#include <string.h>

GBytes *
goodix_match_serialize_template (GoodixMatchInfo *info)
{
  return info && info->template ? g_bytes_ref (info->template) : NULL;
}

static void
goodix_match_set_feature_scalar (guint8  *element,
                                 size_t   element_size,
                                 size_t   field,
                                 gint32   value)
{
  guint32 little_endian = GUINT32_TO_LE ((guint32) value);
  size_t optional_size = element_size >= 5 && element[element_size - 5] == 0xc7
                           ? 5
                           : 0;

  memcpy (element + element_size - 54 - optional_size + field * 5,
          &little_endian,
          sizeof(little_endian));
}

static void
goodix_match_update_antifake_score (GoodixMilanAntifakeBlob *antifake,
                                    gint32                   score)
{
  gint32 current = goodix_milan_antifake_pair_score (antifake);

  if (current == -1)
    current = score;
  else
    {
      uint32_t sum = (uint32_t) current + (uint32_t) score;

      current = goodix_milan_transform_s32 (sum) / 2;
    }
  goodix_milan_antifake_set_pair_score (antifake, current);
}

typedef struct
{
  GoodixMilanRelationMatrix *matrix;
  const GoodixMilanFeatureView   *views;
} GoodixEnrollmentPropagationContext;

static int
goodix_enrollment_relation_overlap (
  void    *user_data,
  size_t   first,
  size_t   second,
  int32_t *overlap)
{
  GoodixEnrollmentPropagationContext *context = user_data;
  size_t high = MAX (first, second);
  size_t low = MIN (first, second);
  GoodixMilanRelationSlot *slot = goodix_milan_enrollment_relation_slot (
    context->matrix, high, low);
  int32_t coverage;
  int32_t detail;
  int32_t low_metrics[3];

  if (!slot || slot->values[0] < 0 ||
      goodix_milan_match_overlap_metrics (
        &context->views[low], &context->views[high], slot->values + 1,
        overlap, &coverage, &detail, low_metrics) != 0)
    return -1;
  return 0;
}

static int
goodix_enrollment_propagate_lower (
  GoodixMilanRelationMatrix *matrix,
  const GoodixMilanFeatureView    views[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  gboolean                        active[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  size_t                          pivot,
  const int32_t                   pivot_to_reference[6])
{
  GoodixEnrollmentPropagationContext context = { matrix, views };

  return goodix_milan_enrollment_propagate_lower (
    matrix, active, pivot, pivot_to_reference, 205,
    goodix_enrollment_relation_overlap, &context);
}

static int
goodix_enrollment_propagate_higher (
  GoodixMilanRelationMatrix *matrix,
  const GoodixMilanFeatureView    views[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  gboolean                        active[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  size_t                          pivot,
  size_t                          current,
  const int32_t                   pivot_to_reference[6])
{
  GoodixEnrollmentPropagationContext context = { matrix, views };

  return goodix_milan_enrollment_propagate_higher (
    matrix, active, pivot, current, pivot_to_reference, 205,
    goodix_enrollment_relation_overlap, &context);
}

static int
goodix_enrollment_bridge_feature (
  GoodixMilanRelationMatrix *matrix,
  const GoodixMilanFeatureView    views[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  gboolean                        active[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  size_t                          current,
  size_t                          pivot)
{
  GoodixMilanRelationSlot *direct = goodix_milan_enrollment_relation_slot (
    matrix, current, pivot);
  int32_t current_to_reference[6];
  int32_t pivot_to_current[6];
  int32_t pivot_to_reference[6];

  if (!direct || direct->values[0] < 0 ||
      goodix_milan_enrollment_feature_to_reference (
        matrix, current, current_to_reference) != 0 ||
      goodix_milan_transform_invert (
        direct->values + 1, pivot_to_current) != 0)
    return -1;
  goodix_milan_transform_compose (
    current_to_reference, pivot_to_current, pivot_to_reference);
  if (goodix_milan_relation_matrix_store_reference (
        matrix, pivot, pivot_to_reference) != 0)
    return -1;
  active[pivot] = TRUE;
  if (goodix_enrollment_propagate_lower (
        matrix, views, active, pivot, pivot_to_reference) != 0 ||
      goodix_enrollment_propagate_higher (
        matrix, views, active, pivot, current, pivot_to_reference) != 0)
    return -1;
  return 0;
}

GBytes *
goodix_match_combine_templates (GPtrArray *templates)
{
  GoodixMilanUnpackedTemplate *unpacked = NULL;
  GoodixMilanRelationMatrix *relation_matrix = NULL;
  const guint8 *elements[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  guint8 *element_copies[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
  GoodixMilanAntifakeBlob *mutable_antifakes[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  size_t element_sizes[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  GoodixMilanFeatureView feature_views[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  GoodixMilanTemplateRelation relations[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY];
  gboolean active[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
  size_t relation_count = 0;
  GoodixMilanTemplateMetadata metadata = { 0 };
  guint8 tail_state[0x520] = { 0 };
  guint8 *combined = NULL;
  size_t combined_capacity = 1433;
  size_t combined_size = 0;
  GBytes *result = NULL;

  if (!templates || templates->len == 0 ||
      templates->len > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
    return NULL;
  unpacked = g_new0 (GoodixMilanUnpackedTemplate, 1);
  relation_matrix = g_new0 (GoodixMilanRelationMatrix, 1);
  relation_matrix->feature_count = templates->len;
  relation_matrix->reference_feature_index = SIZE_MAX;
  for (size_t i = 0; i < GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY; i++)
    goodix_milan_relation_slot_unset (&relation_matrix->slots[i]);
  for (guint i = 0; i < templates->len; i++)
    {
      GBytes *template_bytes = g_ptr_array_index (templates, i);
      gsize template_size;
      const guint8 *template_data = g_bytes_get_data (template_bytes,
                                                       &template_size);

      if (template_size > GOODIX_MILAN_TEMPLATE_MAX_SIZE ||
          goodix_milan_template_unpack (
            template_data, template_size, unpacked) != 0 ||
          unpacked->feature_count != 1)
        goto out;
      element_sizes[i] = unpacked->feature_element_sizes[0];
      if (element_sizes[i] < 55 ||
          element_sizes[i] > GOODIX_MILAN_TEMPLATE_MAX_SIZE - combined_capacity)
        goto out;
      element_copies[i] = g_memdup2 (unpacked->feature_elements[0],
                                     element_sizes[i]);
      elements[i] = element_copies[i];
      mutable_antifakes[i] = goodix_milan_template_mutable_feature_antifake (
        element_copies[i], element_sizes[i]);
      if (!mutable_antifakes[i] ||
          goodix_milan_template_parse_feature_element (
            element_copies[i], element_sizes[i], &feature_views[i]) != 0)
        goto out;
      relation_matrix->row_bases[i] =
        goodix_milan_relation_matrix_expected_row_base (i);
      if (i == 0)
        {
          metadata.sensor_type = unpacked->metadata.sensor_type;
          memcpy (tail_state, unpacked->tail_state, sizeof(tail_state));
        }
      else if (unpacked->metadata.sensor_type != metadata.sensor_type)
        goto out;
      combined_capacity += element_sizes[i];
    }
  metadata.maximum_features = GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT;
  metadata.registration_count = 1;
  metadata.maximum_records = 150;
  metadata.graph_reference_index = -1;
  metadata.graph_companion_f3 = -1;
  metadata.graph_companion_f4 = -1;
  memset (tail_state, 0xff, 200);
  for (guint i = 0; i < templates->len; i++)
    {
      goodix_match_set_feature_scalar (
        element_copies[i], element_sizes[i], 1,
        (gint32) metadata.registration_count);
      goodix_match_set_feature_scalar (
        element_copies[i], element_sizes[i], 7, (gint32) i);
      memcpy (tail_state + i * 4, &i, sizeof(i));
      if (i == 0)
        {
          goodix_match_set_feature_scalar (
            element_copies[i], element_sizes[i], 1, 0);
          continue;
        }

      GoodixMilanFeatureView current_view;
      GoodixMilanFeatureRecord current_records[150];
      gboolean accepted_priors[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
      int32_t accepted_overlaps[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
      int32_t best_inliers = -1;
      int32_t best_connected_inliers = -1;
      guint best_prior = 0;
      guint best_connected_prior = 0;

      current_view = feature_views[i];
      if (goodix_milan_feature_unpack_template_records (
            current_view.packed_records, current_view.record_count,
            current_view.fields.tagged_values[2], current_records, 150) != 0)
        goto out;
      for (guint prior = 0; prior < i; prior++)
        {
          GoodixMilanFeatureView prior_view;
          GoodixMilanFeatureRecord prior_records[150];
          GoodixMilanTemplateRelation candidate;
          int32_t registration_detail;
          int32_t registration_coverage;

          prior_view = feature_views[prior];
          if (goodix_milan_feature_unpack_template_records (
                prior_view.packed_records, prior_view.record_count,
                prior_view.fields.tagged_values[2], prior_records, 150) != 0)
            goto out;
          if (goodix_milan_estimate_relation (
                prior_records, prior_view.record_count, current_records,
                current_view.record_count,
                (int32_t) metadata.registration_count + prior,
                &candidate) != 0)
            continue;
          if (goodix_milan_registration_gate_metrics (
                &prior_view, &current_view, candidate.values + 1,
                &registration_detail, &registration_coverage) != 0)
            continue;
          int accepted = candidate.values[0] > 10 ||
                         (candidate.values[0] > 5 && registration_detail > 215) ||
                         (candidate.values[0] > 6 && registration_detail > 208 &&
                          registration_coverage > 64);

          if (!accepted)
            continue;
          int32_t expected_index = relation_matrix->row_bases[i] + prior;

          if (candidate.index != expected_index || expected_index < 1 ||
              expected_index > GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY)
            goto out;
          memcpy (relation_matrix->slots[expected_index - 1].values,
                  candidate.values, sizeof(candidate.values));
          accepted_priors[prior] = TRUE;
          GoodixEnrollmentPropagationContext context = {
            relation_matrix, feature_views
          };

          if (goodix_enrollment_relation_overlap (
                &context, i, prior,
                &accepted_overlaps[prior]) != 0)
            goto out;
          if (active[prior] &&
              candidate.values[0] > best_connected_inliers)
            {
              best_connected_inliers = candidate.values[0];
              best_connected_prior = prior;
            }
          if (prior_view.record_count > 40 && current_view.record_count > 40)
            {
              gint32 pair_score;

              if (goodix_milan_antifake_score_pair (
                    prior_view.antifake, GOODIX_MILAN_ANTIFAKE_SIZE,
                    current_view.antifake, GOODIX_MILAN_ANTIFAKE_SIZE,
                    candidate.values + 1, &pair_score) != 0)
                goto out;
              if (pair_score >= 0)
                {
                  goodix_match_update_antifake_score (
                    mutable_antifakes[prior], pair_score);
                  goodix_match_update_antifake_score (
                    mutable_antifakes[i], pair_score);
                }
            }
          if (candidate.values[0] > best_inliers)
            {
              best_inliers = candidate.values[0];
              best_prior = prior;
            }
        }
      if (!relation_matrix->graph_established && best_inliers > 5 &&
          accepted_overlaps[best_prior] > 205)
        {
          active[i] = TRUE;
          active[best_prior] = TRUE;
          relation_matrix->reference_feature_index = best_prior;
          relation_matrix->graph_established = 1;
          metadata.graph_reference_index = (gint32) best_prior;
          metadata.graph_established = 1;
        }
      else if (best_connected_inliers >= 0)
        {
          GoodixMilanRelationSlot *direct =
            goodix_milan_enrollment_relation_slot (
              relation_matrix, i, best_connected_prior);
          GoodixMilanRelationSlot *reference_slot =
            goodix_milan_enrollment_relation_slot (
              relation_matrix, i,
              relation_matrix->reference_feature_index);
          int32_t current_to_reference[6];

          if (!direct || !reference_slot)
            goto out;
          if (best_connected_prior ==
              relation_matrix->reference_feature_index)
            memcpy (current_to_reference, direct->values + 1,
                    sizeof(current_to_reference));
          else
            {
              GoodixMilanRelationSlot *connected =
                goodix_milan_enrollment_relation_slot (
                  relation_matrix, best_connected_prior,
                  relation_matrix->reference_feature_index);

              if (!connected || connected->values[0] < 0)
                goto out;
              goodix_milan_transform_route (
                connected->values + 1, direct->values + 1,
                (int32_t) best_connected_prior,
                (int32_t) relation_matrix->reference_feature_index,
                current_to_reference);
            }
          memcpy (reference_slot->values + 1, current_to_reference,
                  sizeof(current_to_reference));
          if (reference_slot->values[0] < 0)
            reference_slot->values[0] = 0;
          active[i] = TRUE;
        }
      if (relation_matrix->graph_established && active[i])
        {
          gboolean bridge_candidates[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };

          for (guint prior = 0; prior < i; prior++)
            bridge_candidates[prior] = accepted_priors[prior] && !active[prior];
          for (guint prior = 0; prior < i; prior++)
            if (bridge_candidates[prior] && accepted_overlaps[prior] > 208 &&
                goodix_enrollment_bridge_feature (
                  relation_matrix, feature_views, active, i, prior) != 0)
              goto out;
        }
      metadata.registration_count += i;
    }
  for (guint i = 0; i < templates->len; i++)
    goodix_match_set_feature_scalar (
      element_copies[i], element_sizes[i], 0, active[i] ? 1 : 0);
  if (goodix_milan_relation_matrix_project_reference_star (
        relation_matrix, relations, G_N_ELEMENTS (relations),
        &relation_count) != 0)
    goto out;
  if (relation_count >
      (GOODIX_MILAN_TEMPLATE_MAX_SIZE - combined_capacity) / 45)
    goto out;
  combined_capacity += relation_count * 45;
  combined = g_malloc (combined_capacity);
  if (goodix_milan_template_pack (
        elements, element_sizes, templates->len, relations, relation_count,
        &metadata,
        tail_state, sizeof(tail_state), combined, combined_capacity,
        &combined_size) != 0)
    goto out;
  result = g_bytes_new_take (combined, combined_size);
  combined = NULL;

out:
  for (guint i = 0; i < GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY; i++)
    g_free (element_copies[i]);
  g_free (combined);
  g_free (relation_matrix);
  g_free (unpacked);
  return result;
}
