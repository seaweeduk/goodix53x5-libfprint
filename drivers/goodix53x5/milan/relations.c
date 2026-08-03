/*
 * Goodix 53x5 driver for libfprint - profile-9/type-12 study relations
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/relations.h"
#include "milan/enrollment/propagation.h"
#include "milan/private.h"

#include <string.h>

static void
milan_template_relation_transform (const GoodixMilanUnpackedTemplate *unpacked,
                                   size_t                              first,
                                   size_t                              second,
                                   int32_t                             transform[6])
{
  static const int32_t identity[6] = { 0x100, 0, 0, 0, 0x100, 0 };
  size_t high = first > second ? first : second;
  size_t low = first > second ? second : first;
  int32_t relation_index = (int32_t) (1 + high * (high - 1) / 2 + low);

  memcpy (transform, identity, sizeof(identity));
  for (size_t i = 0; i < unpacked->relation_count; i++)
    if (unpacked->relations[i].index == relation_index)
      {
        memcpy (transform, unpacked->relations[i].values + 1,
                sizeof(identity));
        return;
      }
}

int
goodix_milan_template_reference_transform (
  const GoodixMilanUnpackedTemplate *unpacked,
  size_t                              feature_index,
  int                                 reverse_above_reference,
  int32_t                             transform[6])
{
  static const int32_t identity[6] = { 0x100, 0, 0, 0, 0x100, 0 };
  int32_t relation[6];
  int32_t packed_reference = unpacked->metadata.graph_reference_index;
  size_t reference;

  if (unpacked->metadata.graph_established != 1 || packed_reference < 0 ||
      (size_t) packed_reference >= unpacked->feature_count)
    return -1;
  reference = (size_t) packed_reference;

  memcpy (transform, identity, sizeof(identity));
  if (feature_index == reference)
    return 0;
  milan_template_relation_transform (
    unpacked, feature_index, reference, relation);
  if ((feature_index > reference) == reverse_above_reference)
    return milan_invert_transform (relation, transform);
  memcpy (transform, relation, sizeof(relation));
  return 0;
}

int
goodix_milan_match_reference_transform (
  const GoodixMilanUnpackedTemplate *enrolled,
  size_t                              matched_feature_index,
  const int32_t                       match_transform[6],
  int32_t                             relation_values[7])
{
  static const int32_t identity[6] = { 0x100, 0, 0, 0, 0x100, 0 };
  uint32_t reference_index;
  size_t high_index;
  size_t low_index;
  int32_t stored_transform[6];
  int relation_found = 0;

  if (!enrolled || matched_feature_index >= enrolled->feature_count ||
      !match_transform || !relation_values)
    return -1;
  if (enrolled->metadata.graph_established != 1 ||
      enrolled->metadata.graph_reference_index < 0)
    return -1;
  reference_index = (uint32_t) enrolled->metadata.graph_reference_index;
  if (reference_index >= enrolled->feature_count)
    return -1;
  relation_values[0] = 0;
  if (matched_feature_index == reference_index)
    {
      memcpy (relation_values + 1, match_transform, sizeof(stored_transform));
      return 0;
    }

  high_index = matched_feature_index > reference_index
                 ? matched_feature_index
                 : reference_index;
  low_index = matched_feature_index > reference_index
                ? reference_index
                : matched_feature_index;
  memcpy (stored_transform, identity, sizeof(stored_transform));
  int32_t relation_index =
    (int32_t) (1 + high_index * (high_index - 1) / 2 + low_index);
  for (size_t i = 0; i < enrolled->relation_count; i++)
    if (enrolled->relations[i].index == relation_index)
      {
        memcpy (stored_transform, enrolled->relations[i].values + 1,
                sizeof(stored_transform));
        relation_found = 1;
        break;
      }
  if (!relation_found)
    {
      memcpy (relation_values + 1, identity, sizeof(stored_transform));
      return 1;
    }
  goodix_milan_transform_route (
    stored_transform, match_transform, (int32_t) matched_feature_index,
    (int32_t) reference_index, relation_values + 1);
  return 0;
}

GoodixMilanRelationSlot *
goodix_milan_enrollment_relation_slot (GoodixMilanRelationMatrix *matrix,
                                       size_t first,
                                       size_t second)
{
  int32_t index;

  if (goodix_milan_relation_matrix_slot_index (
        matrix, first, second, &index) != 0)
    return NULL;
  return goodix_milan_relation_matrix_slot (matrix, index);
}

int
goodix_milan_enrollment_feature_to_reference (
  GoodixMilanRelationMatrix *matrix,
  size_t                          feature,
  int32_t                         transform[6])
{
  GoodixMilanRelationSlot *slot;

  if (!matrix || !transform || !matrix->graph_established ||
      feature >= matrix->feature_count ||
      matrix->reference_feature_index >= matrix->feature_count)
    return -1;
  if (feature == matrix->reference_feature_index)
    {
      static const int32_t identity[6] = { 0x100, 0, 0, 0, 0x100, 0 };

      memcpy (transform, identity, sizeof(identity));
      return 0;
    }
  slot = goodix_milan_enrollment_relation_slot (
    matrix, feature, matrix->reference_feature_index);
  if (!slot || slot->values[0] < 0)
    return -1;
  if (feature > matrix->reference_feature_index)
    memcpy (transform, slot->values + 1, 6 * sizeof(*transform));
  else if (goodix_milan_transform_invert (
             slot->values + 1, transform) != 0)
    return -1;
  return 0;
}

int32_t
goodix_milan_relation_matrix_expected_row_base (size_t feature_index)
{
  if (feature_index == 0)
    return 0;
  return (int32_t) (1 + feature_index * (feature_index - 1) / 2);
}

void
goodix_milan_relation_slot_unset (GoodixMilanRelationSlot *slot)
{
  static const int32_t unset[7] = { -1, 0x100, 0, 0, 0, 0x100, 0 };

  memcpy (slot->values, unset, sizeof(unset));
}

int
goodix_milan_relation_matrix_slot_index (
  const GoodixMilanRelationMatrix *matrix,
  size_t                                first,
  size_t                                second,
  int32_t                              *slot_index)
{
  size_t high;
  size_t low;
  int64_t index;

  if (!matrix || !slot_index || first == second ||
      first >= matrix->feature_count || second >= matrix->feature_count)
    return -1;
  high = first > second ? first : second;
  low = first > second ? second : first;
  index = (int64_t) matrix->row_bases[high] + (int64_t) low;
  if (index < 1 || index > GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY)
    return -1;
  *slot_index = (int32_t) index;
  return 0;
}

GoodixMilanRelationSlot *
goodix_milan_relation_matrix_slot (GoodixMilanRelationMatrix *matrix,
                                  int32_t                         slot_index)
{
  if (!matrix || slot_index < 1 ||
      slot_index > GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY)
    return NULL;
  return &matrix->slots[slot_index - 1];
}

int
goodix_milan_relation_matrix_init (
  GoodixMilanRelationMatrix   *matrix,
  size_t                            feature_count,
  const int32_t                    row_bases[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY],
  uint32_t                          registration_count,
  int32_t                           reference_feature_index,
  int32_t                           graph_established,
  const GoodixMilanTemplateRelation *relations,
  size_t                            relation_count)
{
  uint8_t seen[GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY] = { 0 };
  uint32_t expected_registration_count;

  if (!matrix || !row_bases || feature_count == 0 ||
      feature_count > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY ||
      (graph_established != 0 && graph_established != 1) ||
      (graph_established != 0 &&
       (reference_feature_index < 0 ||
        (size_t) reference_feature_index >= feature_count)) ||
      (graph_established == 0 && reference_feature_index < -1) ||
      (graph_established == 0 && reference_feature_index >= 0 &&
       (size_t) reference_feature_index >= feature_count) ||
      relation_count > GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY ||
      (relation_count != 0 && !relations))
    return -1;
  expected_registration_count =
    (uint32_t) (1 + feature_count * (feature_count - 1) / 2);
  if (registration_count != expected_registration_count)
    return -1;

  memset (matrix, 0, sizeof(*matrix));
  matrix->feature_count = feature_count;
  matrix->reference_feature_index = reference_feature_index < 0
                                      ? SIZE_MAX
                                      : (size_t) reference_feature_index;
  matrix->graph_established = graph_established;
  if (!matrix->graph_established && relation_count != 0)
    return -1;
  for (size_t i = 0; i < GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY; i++)
    goodix_milan_relation_slot_unset (&matrix->slots[i]);
  for (size_t i = 0; i < feature_count; i++)
    {
      int32_t expected = goodix_milan_relation_matrix_expected_row_base (i);

      if (row_bases[i] != expected)
        return -1;
      matrix->row_bases[i] = row_bases[i];
    }
  for (size_t i = 0; i < relation_count; i++)
    {
      int32_t index = relations[i].index;
      GoodixMilanRelationSlot *slot;
      int reference_slot = 0;

      if (index < 1 || (uint32_t) index >= registration_count ||
          index > GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY || seen[index - 1] ||
          relations[i].values[0] < 0)
        return -1;
      for (size_t feature = 0; feature < feature_count; feature++)
        {
          int32_t candidate;

          if (feature == matrix->reference_feature_index)
            continue;
          if (goodix_milan_relation_matrix_slot_index (
                matrix, feature, matrix->reference_feature_index,
                &candidate) != 0)
            return -1;
          if (candidate == index)
            {
              reference_slot = 1;
              break;
            }
        }
      if (!reference_slot)
        return -1;
      seen[index - 1] = 1;
      slot = goodix_milan_relation_matrix_slot (matrix, index);
      memcpy (slot->values, relations[i].values, sizeof(slot->values));
    }
  return 0;
}

int
goodix_milan_relation_matrix_append_row (
  GoodixMilanRelationMatrix *matrix,
  int32_t                         row_base)
{
  size_t old_count;
  int32_t last_slot;

  if (!matrix || matrix->feature_count >= GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
    return -1;
  old_count = matrix->feature_count;
  if (row_base != goodix_milan_relation_matrix_expected_row_base (old_count))
    return -1;
  last_slot = row_base + (int32_t) old_count - 1;
  if (old_count != 0 &&
      (last_slot < 1 || last_slot > GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY))
    return -1;
  matrix->row_bases[old_count] = row_base;
  for (size_t i = 0; i < old_count; i++)
    goodix_milan_relation_slot_unset (
      goodix_milan_relation_matrix_slot (matrix, row_base + (int32_t) i));
  matrix->feature_count++;
  return 0;
}

int
goodix_milan_relation_matrix_clear_incident (
  GoodixMilanRelationMatrix *matrix,
  size_t                          feature_index)
{
  if (!matrix || feature_index >= matrix->feature_count)
    return -1;
  for (size_t other = 0; other < matrix->feature_count; other++)
    {
      int32_t slot_index;

      if (other == feature_index)
        continue;
      if (goodix_milan_relation_matrix_slot_index (
            matrix, feature_index, other, &slot_index) != 0)
        return -1;
      goodix_milan_relation_slot_unset (
        goodix_milan_relation_matrix_slot (matrix, slot_index));
    }
  return 0;
}

int
goodix_milan_relation_matrix_store_reference (
  GoodixMilanRelationMatrix *matrix,
  size_t                          feature_index,
  const int32_t                   feature_to_reference[6])
{
  int32_t slot_index;
  GoodixMilanRelationSlot *slot;

  if (!matrix || !feature_to_reference ||
      feature_index >= matrix->feature_count ||
      feature_index == matrix->reference_feature_index ||
      goodix_milan_relation_matrix_slot_index (
        matrix, feature_index, matrix->reference_feature_index,
        &slot_index) != 0)
    return -1;
  slot = goodix_milan_relation_matrix_slot (matrix, slot_index);
  slot->values[0] = 0;
  if (feature_index > matrix->reference_feature_index)
    memcpy (slot->values + 1, feature_to_reference,
            6 * sizeof(*feature_to_reference));
  else if (goodix_milan_transform_invert (
             feature_to_reference, slot->values + 1) != 0)
    return -1;
  return 0;
}

static inline int
goodix_milan_relation_matrix_reanchor (
  GoodixMilanRelationMatrix *matrix,
  const int32_t                   new_reference_to_old_reference[6])
{
  int32_t old_reference_to_new_reference[6];

  if (!matrix || !new_reference_to_old_reference ||
      goodix_milan_transform_invert (
        new_reference_to_old_reference,
        old_reference_to_new_reference) != 0)
    return -1;
  for (size_t feature = 0; feature < matrix->feature_count; feature++)
    {
      int32_t slot_index;
      GoodixMilanRelationSlot *slot;
      int32_t feature_to_old_reference[6];
      int32_t feature_to_new_reference[6];

      if (feature == matrix->reference_feature_index)
        continue;
      if (goodix_milan_relation_matrix_slot_index (
            matrix, feature, matrix->reference_feature_index,
            &slot_index) != 0)
        return -1;
      slot = goodix_milan_relation_matrix_slot (matrix, slot_index);
      if (slot->values[0] < 0)
        continue;
      if (feature > matrix->reference_feature_index)
        memcpy (feature_to_old_reference, slot->values + 1,
                sizeof(feature_to_old_reference));
      else if (goodix_milan_transform_invert (
                 slot->values + 1, feature_to_old_reference) != 0)
        return -1;
      goodix_milan_transform_compose (
        old_reference_to_new_reference, feature_to_old_reference,
        feature_to_new_reference);
      if (feature > matrix->reference_feature_index)
        memcpy (slot->values + 1, feature_to_new_reference,
                sizeof(feature_to_new_reference));
      else if (goodix_milan_transform_invert (
                 feature_to_new_reference, slot->values + 1) != 0)
        return -1;
    }
  return 0;
}

int
goodix_milan_relation_matrix_refresh (
  GoodixMilanRelationMatrix *matrix,
  size_t                          feature_index,
  const int32_t                   retained_transform[6],
  const int32_t                   evidence_relation_transform[6])
{
  int32_t reference_to_feature[6];
  int32_t feature_to_reference[6];

  if (!matrix || !retained_transform || !evidence_relation_transform ||
      feature_index >= matrix->feature_count ||
      goodix_milan_transform_invert (
        retained_transform, reference_to_feature) != 0)
    return -1;
  goodix_milan_transform_compose (
    evidence_relation_transform, reference_to_feature,
    feature_to_reference);
  if (feature_index == matrix->reference_feature_index)
    return goodix_milan_relation_matrix_reanchor (
      matrix, feature_to_reference);
  if (goodix_milan_relation_matrix_clear_incident (
        matrix, feature_index) != 0)
    return -1;
  return goodix_milan_relation_matrix_store_reference (
    matrix, feature_index, feature_to_reference);
}

int
goodix_milan_relation_matrix_replace (
  GoodixMilanRelationMatrix *matrix,
  size_t                          feature_index,
  const int32_t                   new_feature_to_reference[6])
{
  if (!matrix || !new_feature_to_reference ||
      feature_index >= matrix->feature_count)
    return -1;
  if (feature_index == matrix->reference_feature_index)
    return goodix_milan_relation_matrix_reanchor (
      matrix, new_feature_to_reference);
  if (goodix_milan_relation_matrix_clear_incident (
        matrix, feature_index) != 0)
    return -1;
  return goodix_milan_relation_matrix_store_reference (
    matrix, feature_index, new_feature_to_reference);
}

int
goodix_milan_relation_matrix_project_reference_star (
  const GoodixMilanRelationMatrix *matrix,
  GoodixMilanTemplateRelation          *relations,
  size_t                                relation_capacity,
  size_t                               *relation_count)
{
  size_t count = 0;

  if (!matrix || !relations || !relation_count ||
      matrix->feature_count > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY)
    return -1;
  if (!matrix->graph_established)
    {
      *relation_count = 0;
      return 0;
    }
  if (matrix->reference_feature_index >= matrix->feature_count)
    return -1;
  for (size_t feature = 0; feature < matrix->feature_count; feature++)
    {
      int32_t slot_index;
      const GoodixMilanRelationSlot *slot;

      if (feature == matrix->reference_feature_index)
        continue;
      if (goodix_milan_relation_matrix_slot_index (
            matrix, feature, matrix->reference_feature_index,
            &slot_index) != 0)
        return -1;
      slot = &matrix->slots[slot_index - 1];
      if (slot->values[0] < 0)
        continue;
      if (count >= relation_capacity || count >= 49)
        return -1;
      relations[count].index = slot_index;
      memcpy (relations[count].values, slot->values,
              sizeof(relations[count].values));
      count++;
    }
  *relation_count = count;
  return 0;
}
