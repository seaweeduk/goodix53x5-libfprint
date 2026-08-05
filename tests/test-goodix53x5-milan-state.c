/*
 * Goodix 53x5 driver for libfprint - synthetic Milan state invariants
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "drivers/goodix53x5/milan/match/info-private.h"
#include "drivers/goodix53x5/milan/match/match.h"
#include "drivers/goodix53x5/milan/print.h"
#include "drivers/goodix53x5/milan/study/queue.h"
#include "drivers/goodix53x5/milan/template/codec-private.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static guint8
synthetic_byte (guint seed,
                gsize index)
{
  return (guint8) ((seed * 37U + index * 19U + (index >> 2)) & 0xffU);
}

static GoodixMatchInfo *
synthetic_match_info (guint seed)
{
  GoodixMatchInfo *info = goodix_match_info_new_empty ();
  guint8 template_data[19];

  for (gsize i = 0; i < sizeof(template_data); i++)
    template_data[i] = synthetic_byte (seed, i);
  info->template = g_bytes_new (template_data, sizeof(template_data));
  info->record_count = 3;
  info->partition_count = seed % (info->record_count + 1);
  info->records = g_new0 (GoodixMilanFeatureRecord, info->record_count);
  for (gsize i = 0; i < sizeof(info->feature_bitmaps); i++)
    ((guint8 *) &info->feature_bitmaps)[i] = synthetic_byte (seed + 1, i);
  for (gsize i = 0; i < sizeof(info->inline_mask); i++)
    info->inline_mask[i] = synthetic_byte (seed + 2, i);
  for (gsize i = 0; i < sizeof(info->rescue_mask); i++)
    info->rescue_mask[i] = synthetic_byte (seed + 3, i);
  for (gsize i = 0; i < sizeof(info->antifake); i++)
    ((guint8 *) &info->antifake)[i] = synthetic_byte (seed + 4, i);
  for (gint record = 0; record < info->record_count; record++)
    {
      guint8 *bytes = (guint8 *) &info->records[record];

      for (gsize i = 0; i < sizeof(info->records[record]); i++)
        bytes[i] = synthetic_byte (seed + 5 + (guint) record, i);
      info->records[record].foreground = record < info->partition_count ? 0 : 1;
      info->records[record].refined_x = (gint16) ((0x100 + record * 0x20) & ~0xf);
      info->records[record].refined_y = (gint16) ((0x200 + seed * 0x10) & ~0xf);
      info->records[record].orientation = (gint16) ((record - 1) * 0x100);
      memset (info->records[record].payload + 24, 0, 4);
      memset (info->records[record].payload + 36, 0, 8);
    }
  info->extraction_metadata.quality = 40 + (gint) seed;
  info->extraction_metadata.coverage = 60 + (gint) seed;
  info->extraction_metadata.optional_c7 = (gint32) seed;
  g_assert_true (goodix_match_info_is_complete (info));
  return info;
}

static void
assert_match_info_equal (const GoodixMatchInfo *actual,
                         const GoodixMatchInfo *expected)
{
  g_assert_true (goodix_match_info_is_complete (actual));
  g_assert_true (g_bytes_equal (actual->template, expected->template));
  g_assert_cmpint (actual->record_count, ==, expected->record_count);
  g_assert_cmpint (actual->partition_count, ==, expected->partition_count);
  g_assert_cmpmem (&actual->feature_bitmaps, sizeof(actual->feature_bitmaps),
                   &expected->feature_bitmaps, sizeof(expected->feature_bitmaps));
  g_assert_cmpmem (actual->inline_mask, sizeof(actual->inline_mask),
                   expected->inline_mask, sizeof(expected->inline_mask));
  g_assert_cmpmem (actual->rescue_mask, sizeof(actual->rescue_mask),
                   expected->rescue_mask, sizeof(expected->rescue_mask));
  g_assert_cmpmem (&actual->antifake, sizeof(actual->antifake),
                   &expected->antifake, sizeof(expected->antifake));
  g_assert_cmpmem (actual->records,
                   (gsize) actual->record_count * sizeof(*actual->records),
                   expected->records,
                   (gsize) expected->record_count * sizeof(*expected->records));
  g_assert_cmpmem (&actual->extraction_metadata,
                   sizeof(actual->extraction_metadata),
                   &expected->extraction_metadata,
                   sizeof(expected->extraction_metadata));
}

static const GoodixMatchInfo *
queue_entry_at_rank (const GoodixStudyQueue *queue,
                     gint rank)
{
  for (gsize slot = 0; slot < GOODIX_STUDY_QUEUE_CAPACITY; slot++)
    if (queue->entries[slot].rank == rank)
      return queue->entries[slot].info;
  return NULL;
}

typedef struct
{
  guint incoming_seed;
  guint newest_seed;
  gint metric;
  guint calls;
} MetricPlan;

static gboolean
planned_metric (const GoodixMatchInfo *incoming,
                const GoodixMatchInfo *newest,
                gint                  *metric,
                gpointer               user_data)
{
  MetricPlan *plan = user_data;

  g_assert_cmpint (incoming->extraction_metadata.optional_c7, ==,
                   (gint32) plan->incoming_seed);
  g_assert_cmpint (newest->extraction_metadata.optional_c7, ==,
                   (gint32) plan->newest_seed);
  *metric = plan->metric;
  plan->calls++;
  return TRUE;
}

static void
test_queue_lifecycle (void)
{
  g_autofree GoodixStudyQueue *invalid = goodix_study_queue_new (2, 0);
  GoodixStudyQueue *disabled = goodix_study_queue_new (1, 17);
  GoodixStudyQueue *queue = goodix_study_queue_new (0, 17);
  GoodixMatchInfo *incoming = synthetic_match_info (0);
  GoodixMatchInfo *snapshot = goodix_match_info_new_empty ();
  GoodixMatchInfo *incomplete = goodix_match_info_new_empty ();
  MetricPlan metric = { 0 };

  g_assert_null (invalid);
  g_assert_true (goodix_study_queue_validate (disabled));
  g_assert_cmpuint (goodix_study_queue_allocated (disabled), ==, 0);
  g_assert_cmpint (goodix_study_queue_enqueue (
                     disabled, incoming, NULL, NULL), ==,
                   GOODIX_STUDY_QUEUE_DISABLED);
  goodix_study_queue_disable (disabled);
  g_assert_true (goodix_study_queue_validate (disabled));
  goodix_study_queue_free (disabled);

  g_assert_true (goodix_study_queue_validate (queue));
  g_assert_cmpuint (goodix_study_queue_allocated (queue), ==,
                    GOODIX_STUDY_QUEUE_CAPACITY);
  g_assert_cmpint (goodix_study_queue_enqueue (
                     queue, incomplete, NULL, NULL), ==,
                   GOODIX_STUDY_QUEUE_INVALID);
  goodix_match_free_info (incomplete);
  queue->entries[0].rank = 0;
  g_assert_false (goodix_study_queue_validate (queue));
  queue->entries[0].rank = -1;
  g_assert_true (goodix_study_queue_validate (queue));

  g_assert_true (goodix_match_info_copy (snapshot, incoming));
  g_assert_cmpint (goodix_study_queue_enqueue (
                     queue, incoming, NULL, NULL), ==,
                   GOODIX_STUDY_QUEUE_ENQUEUED);
  assert_match_info_equal (incoming, snapshot);
  assert_match_info_equal (queue_entry_at_rank (queue, 0), snapshot);
  incoming->feature_bitmaps.high_bitmap[0] ^= 0xff;
  g_assert_cmpuint (queue_entry_at_rank (queue, 0)->feature_bitmaps.high_bitmap[0],
                    ==, snapshot->feature_bitmaps.high_bitmap[0]);
  goodix_match_free_info (incoming);
  goodix_match_free_info (snapshot);

  incoming = synthetic_match_info (1);
  metric = (MetricPlan) { 1, 0, 191, 0 };
  g_assert_cmpint (goodix_study_queue_enqueue (
                     queue, incoming, planned_metric, &metric), ==,
                   GOODIX_STUDY_QUEUE_DUPLICATE);
  g_assert_cmpuint (metric.calls, ==, 1);
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 1);
  metric.metric = 190;
  g_assert_cmpint (goodix_study_queue_enqueue (
                     queue, incoming, planned_metric, &metric), ==,
                   GOODIX_STUDY_QUEUE_ENQUEUED);
  g_assert_cmpuint (metric.calls, ==, 2);
  goodix_match_free_info (incoming);

  for (guint seed = 2; seed < GOODIX_STUDY_QUEUE_CAPACITY; seed++)
    {
      incoming = synthetic_match_info (seed);
      metric = (MetricPlan) { seed, seed - 1, 0, 0 };
      g_assert_cmpint (goodix_study_queue_enqueue (
                         queue, incoming, planned_metric, &metric), ==,
                       GOODIX_STUDY_QUEUE_ENQUEUED);
      g_assert_cmpuint (metric.calls, ==, 1);
      goodix_match_free_info (incoming);
    }
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==,
                    GOODIX_STUDY_QUEUE_CAPACITY);
  for (guint rank = 0; rank < GOODIX_STUDY_QUEUE_CAPACITY; rank++)
    g_assert_cmpint (queue_entry_at_rank (queue, (gint) rank)
                       ->extraction_metadata.optional_c7, ==, (gint) rank);

  incoming = synthetic_match_info (GOODIX_STUDY_QUEUE_CAPACITY);
  metric = (MetricPlan) {
    GOODIX_STUDY_QUEUE_CAPACITY, GOODIX_STUDY_QUEUE_CAPACITY - 1, 0, 0
  };
  g_assert_cmpint (goodix_study_queue_enqueue (
                     queue, incoming, planned_metric, &metric), ==,
                   GOODIX_STUDY_QUEUE_ENQUEUED);
  goodix_match_free_info (incoming);
  for (guint rank = 0; rank < GOODIX_STUDY_QUEUE_CAPACITY; rank++)
    g_assert_cmpint (queue_entry_at_rank (queue, (gint) rank)
                       ->extraction_metadata.optional_c7, ==, (gint) rank + 1);

  goodix_study_queue_disable (queue);
  g_assert_true (goodix_study_queue_validate (queue));
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 0);
  g_assert_cmpuint (goodix_study_queue_allocated (queue), ==, 0);
  goodix_study_queue_disable (queue);
  g_assert_true (goodix_study_queue_validate (queue));
  goodix_study_queue_free (queue);
}

typedef struct
{
  guint seed;
  gsize triggering_index;
  gboolean succeed;
  gsize selected_index;
} FollowupStep;

typedef struct
{
  const FollowupStep *steps;
  gsize step_count;
  gsize next_step;
} FollowupPlan;

static gboolean
planned_followup (GoodixMatchInfo *queued,
                  gsize            physical_slot,
                  gsize            triggering_index,
                  gsize           *selected_index,
                  gpointer         user_data)
{
  FollowupPlan *plan = user_data;
  const FollowupStep *step;

  g_assert_cmpuint (physical_slot, <, GOODIX_STUDY_QUEUE_CAPACITY);
  g_assert_cmpuint (plan->next_step, <, plan->step_count);
  step = &plan->steps[plan->next_step++];
  g_assert_cmpint (queued->extraction_metadata.optional_c7, ==,
                   (gint32) step->seed);
  g_assert_cmpuint (triggering_index, ==, step->triggering_index);
  *selected_index = step->selected_index;
  return step->succeed;
}

static GoodixStudyQueue *
queue_with_seeds (const guint *seeds,
                  gsize        seed_count)
{
  GoodixStudyQueue *queue = goodix_study_queue_new (0, 9);

  for (gsize i = 0; i < seed_count; i++)
    {
      GoodixMatchInfo *info = synthetic_match_info (seeds[i]);
      MetricPlan metric = { seeds[i], i == 0 ? 0 : seeds[i - 1], 0, 0 };

      g_assert_cmpint (goodix_study_queue_enqueue (
                         queue, info, i == 0 ? NULL : planned_metric,
                         i == 0 ? NULL : &metric), ==,
                       GOODIX_STUDY_QUEUE_ENQUEUED);
      goodix_match_free_info (info);
    }
  return queue;
}

static void
test_queue_process (void)
{
  static const guint seeds[] = { 1, 2, 3 };
  static const FollowupStep success_steps[] = {
    { 1, 50, TRUE, SIZE_MAX },
    { 2, 50, TRUE, 60 },
    { 3, 50, TRUE, 50 },
    { 1, 60, TRUE, 61 },
  };
  static const FollowupStep failure_steps[] = {
    { 7, 5, FALSE, SIZE_MAX },
  };
  GoodixStudyQueue *queue = queue_with_seeds (seeds, G_N_ELEMENTS (seeds));
  FollowupPlan plan = { success_steps, G_N_ELEMENTS (success_steps), 0 };
  gboolean mutated = FALSE;

  g_assert_true (goodix_study_queue_process (
    queue, 50, planned_followup, &plan, &mutated));
  g_assert_true (mutated);
  g_assert_cmpuint (plan.next_step, ==, plan.step_count);
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 0);
  g_assert_true (goodix_study_queue_validate (queue));
  goodix_study_queue_free (queue);

  {
    static const guint failure_seed[] = { 7 };

    queue = queue_with_seeds (failure_seed, G_N_ELEMENTS (failure_seed));
  }
  plan = (FollowupPlan) {
    failure_steps, G_N_ELEMENTS (failure_steps), 0
  };
  mutated = TRUE;
  g_assert_false (goodix_study_queue_process (
    queue, 5, planned_followup, &plan, &mutated));
  g_assert_false (mutated);
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 1);
  g_assert_true (goodix_study_queue_validate (queue));
  goodix_study_queue_free (queue);

  queue = goodix_study_queue_new (1, 9);
  plan = (FollowupPlan) { NULL, 0, 0 };
  mutated = TRUE;
  g_assert_false (goodix_study_queue_process (
    queue, 5, planned_followup, &plan, &mutated));
  g_assert_false (mutated);
  g_assert_true (goodix_study_queue_validate (queue));
  goodix_study_queue_free (queue);
}

typedef struct
{
  const gchar *name;
  gsize feature_count;
  gsize record_count;
  gsize relation_count;
  gboolean optional_c7;
  gboolean graph_established;
  guint64 lifecycle_mask;
} TemplateShape;

static GBytes *
synthetic_feature_element (guint    seed,
                           gsize    record_count,
                           gboolean optional_c7)
{
  guint8 high[286];
  guint8 enhanced[286];
  guint8 inline_mask[72];
  guint8 low[286];
  GoodixMilanFeatureRecord *records = g_new0 (
    GoodixMilanFeatureRecord, record_count);
  GoodixMilanAntifakeBlob antifake;
  GoodixMilanFeatureTemplateFields fields = { 0 };
  g_autofree guint8 *packed = g_malloc (7950 + record_count * 32);
  gsize packed_size = 0;

  for (gsize i = 0; i < sizeof(high); i++)
    {
      high[i] = synthetic_byte (seed, i);
      enhanced[i] = synthetic_byte (seed + 1, i);
      low[i] = synthetic_byte (seed + 2, i);
    }
  for (gsize i = 0; i < sizeof(inline_mask); i++)
    inline_mask[i] = synthetic_byte (seed + 3, i);
  for (gsize i = 0; i < sizeof(antifake); i++)
    ((guint8 *) &antifake)[i] = synthetic_byte (seed + 4, i);
  for (gsize record = 0; record < record_count; record++)
    {
      guint8 *bytes = (guint8 *) &records[record];

      for (gsize i = 0; i < sizeof(records[record]); i++)
        bytes[i] = synthetic_byte (seed + 5 + (guint) record, i);
      records[record].foreground = record % 2;
      records[record].refined_x = (gint16) ((0x1200 + record * 0x110) & ~0xf);
      records[record].refined_y = (gint16) ((0x2200 + record * 0x130) & ~0xf);
      records[record].orientation = (gint16) (((gint) record - 1) * 0x100);
    }
  for (gsize i = 0; i < G_N_ELEMENTS (fields.tagged_values); i++)
    fields.tagged_values[i] = (gint32) (seed * 100 + i * 7);
  fields.tagged_values[0] = 0;
  fields.optional_c7 = optional_c7 ? (gint32) (0x100 + seed) : 0;

  {
    g_autofree guint8 *high_before = g_memdup2 (high, sizeof(high));
    g_autofree guint8 *enhanced_before = g_memdup2 (enhanced, sizeof(enhanced));
    g_autofree guint8 *mask_before = g_memdup2 (inline_mask, sizeof(inline_mask));
    g_autofree guint8 *low_before = g_memdup2 (low, sizeof(low));
    g_autofree GoodixMilanFeatureRecord *records_before = g_memdup2 (
      records, record_count * sizeof(*records));
    g_autofree GoodixMilanAntifakeBlob *antifake_before = g_memdup2 (
      &antifake, sizeof(antifake));
    GoodixMilanFeatureTemplateFields fields_before = fields;

    g_assert_cmpint (goodix_milan_template_pack_feature_element (
                       high, enhanced, inline_mask, low, records, record_count,
                       &antifake, &fields, packed,
                       7950 + record_count * 32, &packed_size), ==, 0);
    g_assert_cmpmem (high, sizeof(high), high_before, sizeof(high));
    g_assert_cmpmem (enhanced, sizeof(enhanced), enhanced_before,
                     sizeof(enhanced));
    g_assert_cmpmem (inline_mask, sizeof(inline_mask), mask_before,
                     sizeof(inline_mask));
    g_assert_cmpmem (low, sizeof(low), low_before, sizeof(low));
    g_assert_cmpmem (records, record_count * sizeof(*records), records_before,
                     record_count * sizeof(*records));
    g_assert_cmpmem (&antifake, sizeof(antifake), antifake_before,
                     sizeof(antifake));
    g_assert_cmpmem (&fields, sizeof(fields), &fields_before, sizeof(fields));
  }
  g_free (records);
  return g_bytes_new (packed, packed_size);
}

static GBytes *
study_feature_element (guint    seed,
                       gboolean matchable,
                       gint32   active,
                       gint32   state,
                       gint32   residual,
                       gint32   ordinal,
                       gint32   marker)
{
  const gsize record_count = matchable ? 150 : 1;
  guint8 high[286];
  guint8 enhanced[286];
  guint8 inline_mask[72];
  guint8 low[286];
  g_autofree GoodixMilanFeatureRecord *records = g_new0 (
    GoodixMilanFeatureRecord, record_count);
  GoodixMilanAntifakeBlob antifake = { 0 };
  GoodixMilanFeatureTemplateFields fields = { 0 };
  g_autofree guint8 *packed = g_malloc (7950 + record_count * 32);
  gsize packed_size = 0;

  /* Exact uniform maps are rejected by overlap admission; this balanced
   * pattern supplies both zero and one agreement classes. */
  for (gsize i = 0; i < sizeof(high); i++)
    {
      high[i] = matchable ? 0xaa : synthetic_byte (seed, i);
      enhanced[i] = matchable ? 0xaa : synthetic_byte (seed + 1, i);
      low[i] = matchable ? 0xaa : synthetic_byte (seed + 2, i);
    }
  memset (inline_mask, matchable ? 0xff : 0, sizeof(inline_mask));
  for (gsize i = 0; i < record_count; i++)
    {
      records[i].foreground = 1;
      records[i].refined_x = matchable
                               ? (gint16) ((4 + (i % 15) * 6) * 0x100)
                               : (gint16) (0x1200 + seed * 0x100);
      records[i].refined_y = matchable
                               ? (gint16) ((4 + (i / 15) * 8) * 0x100)
                               : (gint16) (0x2200 + seed * 0x100);
      records[i].orientation = matchable
                                 ? (gint16) (((gint) (i % 16) - 8) * 0x100)
                                 : (gint16) (seed * 0x100);
      for (gsize byte = 0; byte < sizeof(records[i].payload); byte++)
        records[i].payload[byte] = matchable
                                     ? (guint8) (seed * 13 + i * 7 + byte * 3)
                                     : (guint8) (seed + byte);
      memset (records[i].payload + 24, 0, 4);
      memset (records[i].payload + 36, 0, 8);
    }
  fields.tagged_values[0] = active;
  fields.tagged_values[1] = ordinal == 0
                              ? 0
                              : 1 + ordinal * (ordinal - 1) / 2;
  fields.tagged_values[2] = 0;
  fields.tagged_values[3] = matchable ? 100 : 50;
  fields.tagged_values[4] = matchable ? 100 : 80;
  fields.tagged_values[5] = state;
  fields.tagged_values[6] = residual;
  fields.tagged_values[7] = ordinal;
  goodix_milan_antifake_set_calibration_scalar (&antifake, marker);

  g_assert_cmpint (goodix_milan_template_pack_feature_element (
                     high, enhanced, inline_mask, low, records, record_count,
                     &antifake, &fields, packed, 7950 + record_count * 32,
                     &packed_size), ==, 0);
  return g_bytes_new (packed, packed_size);
}

static GoodixMatchInfo *
study_match_info (guint seed,
                  gboolean matchable,
                  gint32   marker)
{
  GoodixMatchInfo *info = goodix_match_info_new_empty ();
  g_autoptr(GBytes) feature = study_feature_element (
    seed, matchable, 0, 0, 0, 0, marker);
  const guint8 *feature_data;
  gsize feature_size;
  guint8 tail[0x520] = { 0 };
  g_autofree guint8 *packed = NULL;
  size_t packed_size = 0;
  GoodixMilanFeatureView view;

  feature_data = g_bytes_get_data (feature, &feature_size);
  packed = g_malloc (1433 + feature_size);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     feature_data, feature_size, &view), ==, 0);
  g_assert_cmpint (goodix_milan_template_pack_one_feature (
                     feature_data, feature_size, tail, sizeof(tail), packed,
                     1433 + feature_size, &packed_size), ==, 0);
  info->template = g_bytes_new_take (g_steal_pointer (&packed), packed_size);
  info->record_count = (gint) view.record_count;
  info->partition_count = view.fields.tagged_values[2];
  info->records = g_new0 (GoodixMilanFeatureRecord, view.record_count);
  g_assert_cmpint (goodix_milan_feature_unpack_template_records (
                     view.packed_records, view.record_count,
                     (gsize) info->partition_count, info->records,
                     view.record_count), ==, 0);
  memcpy (info->feature_bitmaps.high_bitmap, view.high_bitmap,
          sizeof(info->feature_bitmaps.high_bitmap));
  memcpy (info->feature_bitmaps.enhanced_bitmap, view.enhanced_bitmap,
          sizeof(info->feature_bitmaps.enhanced_bitmap));
  memcpy (info->feature_bitmaps.low_bitmap, view.low_bitmap,
          sizeof(info->feature_bitmaps.low_bitmap));
  memcpy (info->inline_mask, view.inline_mask, sizeof(info->inline_mask));
  memset (info->rescue_mask, matchable ? 0xff : 0,
          sizeof(info->rescue_mask));
  memcpy (&info->antifake, view.antifake, sizeof(info->antifake));
  info->extraction_metadata.quality = view.fields.tagged_values[3];
  info->extraction_metadata.coverage = view.fields.tagged_values[4];
  info->extraction_metadata.optional_c7 = view.fields.optional_c7;
  g_assert_true (goodix_match_info_is_complete (info));
  return info;
}

static GBytes *
study_gallery (GoodixMilanStudyAction action,
               gboolean               matchable_enrolled)
{
  enum { N_FEATURES = GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT };
  GBytes *features[N_FEATURES] = { 0 };
  const guint8 *feature_data[N_FEATURES];
  gsize feature_sizes[N_FEATURES];
  GoodixMilanTemplateRelation relations[N_FEATURES - 1] = { 0 };
  GoodixMilanTemplateMetadata metadata = { 0 };
  guint8 tail[0x520] = { 0 };
  g_autofree guint8 *packed = NULL;
  gsize capacity = 1433 + G_N_ELEMENTS (relations) * 45;
  gsize packed_size = 0;
  GBytes *template_bytes;

  for (gsize i = 0; i < N_FEATURES; i++)
    {
      gint32 state = action == GOODIX_MILAN_STUDY_GEOMETRIC && i + 1 < N_FEATURES
                       ? 5 : 1;
      gboolean matchable = matchable_enrolled && i == 1;
      gint32 residual = i == 0 ? 0
                         : action == GOODIX_MILAN_STUDY_GEOMETRIC
                             ? 20 : (i == 1 ? 0 : 20);

      features[i] = study_feature_element (
        matchable ? 9 : (guint) i + 1, matchable, 1, state, residual,
        (gint32) i, 0);
      feature_data[i] = g_bytes_get_data (features[i], &feature_sizes[i]);
      capacity += feature_sizes[i];
      goodix_milan_template_write_u32 (tail + i * 4, (guint32) i);
      if (i != 0)
        {
          static const gint32 identity_relation[7] = {
            0, 0x100, 0, 0, 0, 0x100, 0,
          };

          relations[i - 1].index = 1 + (gint32) (i * (i - 1) / 2);
          memcpy (relations[i - 1].values, identity_relation,
                  sizeof(identity_relation));
        }
    }
  metadata.sensor_type = 12;
  metadata.maximum_features = N_FEATURES;
  metadata.registration_count = 1 + N_FEATURES * (N_FEATURES - 1) / 2;
  metadata.maximum_records = 150;
  metadata.queue_state = 0;
  metadata.queue_transaction_counter = 7;
  metadata.graph_reference_index = 0;
  metadata.graph_companion_f3 = -1;
  metadata.graph_companion_f4 = -1;
  metadata.graph_established = 1;

  packed = g_malloc (capacity);
  g_assert_cmpint (goodix_milan_template_pack (
                     feature_data, feature_sizes, N_FEATURES, relations,
                     G_N_ELEMENTS (relations), &metadata, tail, sizeof(tail),
                     packed, capacity, &packed_size), ==, 0);
  template_bytes = g_bytes_new_take (g_steal_pointer (&packed), packed_size);
  for (gsize i = 0; i < N_FEATURES; i++)
    g_bytes_unref (features[i]);
  return template_bytes;
}

static void
assert_study_template (GBytes                       *bytes,
                       guint32                       expected_relations,
                       GoodixMilanPrintTemplateInfo *info,
                       GoodixMilanUnpackedTemplate  *unpacked)
{
  g_autoptr(GError) error = NULL;
  const guint8 *template_data;
  gsize template_size;

  g_assert_true (goodix_milan_print_validate_template (bytes, info, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (info->feature_count, ==,
                    GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT);
  g_assert_cmpuint (info->maximum_features, ==,
                    GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT);
  g_assert_cmpuint (info->relation_count, ==, expected_relations);
  g_assert_cmpuint (info->queue_state, ==, 1);
  g_assert_cmpuint (info->queue_transaction_counter, ==, 7);
  g_assert_cmpuint (info->registration_count, ==,
                    1 + GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT *
                          (GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 1) / 2);
  g_assert_cmpuint (info->graph_established, ==, 1);
  g_assert_cmpint (info->graph_reference_index, ==, 0);
  template_data = g_bytes_get_data (bytes, &template_size);
  g_assert_cmpint (goodix_milan_template_unpack (
                     template_data, template_size, unpacked), ==, 0);
}

static void
unpack_study_template (GBytes                      *bytes,
                       GoodixMilanUnpackedTemplate *unpacked)
{
  const guint8 *template_data;
  gsize template_size;

  template_data = g_bytes_get_data (bytes, &template_size);
  g_assert_cmpint (goodix_milan_template_unpack (
                     template_data, template_size, unpacked), ==, 0);
}

static void
assert_feature_material_equal (const GoodixMilanFeatureView *actual,
                               const GoodixMilanFeatureView *expected)
{
  g_assert_cmpuint (actual->record_count, ==, expected->record_count);
  g_assert_cmpmem (actual->high_bitmap, 286, expected->high_bitmap, 286);
  g_assert_cmpmem (actual->enhanced_bitmap, 286,
                   expected->enhanced_bitmap, 286);
  g_assert_cmpmem (actual->inline_mask, 72, expected->inline_mask, 72);
  g_assert_cmpmem (actual->low_bitmap, 286, expected->low_bitmap, 286);
  g_assert_cmpmem (actual->packed_records, actual->record_count * 32,
                   expected->packed_records, expected->record_count * 32);
  g_assert_cmpmem (actual->antifake, GOODIX_MILAN_ANTIFAKE_SIZE,
                   expected->antifake, GOODIX_MILAN_ANTIFAKE_SIZE);
}

static void
assert_replacement_relations (const GoodixMilanUnpackedTemplate *unpacked,
                              GoodixMilanStudyAction              action,
                              gsize                               selected_index)
{
  static const gint32 identity_relation[7] = {
    0, 0x100, 0, 0, 0, 0x100, 0,
  };
  gsize relation = 0;

  for (gsize i = 1; i < GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT; i++)
    {
      if (action == GOODIX_MILAN_STUDY_REPLACE_NO_RELATION &&
          i == selected_index)
        continue;
      g_assert_cmpuint (relation, <, unpacked->relation_count);
      g_assert_cmpint (unpacked->relations[relation].index, ==,
                       1 + (gint32) (i * (i - 1) / 2));
      g_assert_cmpmem (unpacked->relations[relation].values,
                       sizeof(identity_relation), identity_relation,
                       sizeof(identity_relation));
      relation++;
    }
  g_assert_cmpuint (relation, ==, unpacked->relation_count);
}

static void
assert_replacement_semantics (
  const GoodixMilanUnpackedTemplate *before,
  const GoodixMilanUnpackedTemplate *after,
  const GoodixMilanUnpackedTemplate *probe,
  GoodixMilanStudyAction              action,
  gsize                               selected_index,
  gint32                              generation_count,
  gint32                              lifecycle_count,
  gboolean                            finalize_transaction)
{
  GoodixMilanFeatureView before_selected;
  GoodixMilanFeatureView after_selected;
  GoodixMilanFeatureView probe_view;
  gint32 selected_ordinal;

  g_assert_cmpuint (before->feature_count, ==, after->feature_count);
  g_assert_cmpuint (after->metadata.sensor_type, ==,
                    before->metadata.sensor_type);
  g_assert_cmpuint (after->metadata.maximum_features, ==,
                    before->metadata.maximum_features);
  g_assert_cmpuint (after->metadata.registration_count, ==,
                    before->metadata.registration_count);
  g_assert_cmpuint (after->metadata.maximum_records, ==,
                    before->metadata.maximum_records);
  g_assert_cmpuint (after->metadata.queue_state, ==, 1);
  g_assert_cmpuint (after->metadata.queue_transaction_counter, ==,
                    before->metadata.queue_transaction_counter);
  g_assert_cmpint (after->metadata.graph_reference_index, ==,
                   before->metadata.graph_reference_index);
  g_assert_cmpint (after->metadata.graph_companion_f3, ==,
                   before->metadata.graph_companion_f3);
  g_assert_cmpint (after->metadata.graph_companion_f4, ==,
                   before->metadata.graph_companion_f4);
  g_assert_cmpuint (after->metadata.graph_established, ==,
                    before->metadata.graph_established);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     before->feature_elements[selected_index],
                     before->feature_element_sizes[selected_index],
                     &before_selected), ==, 0);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     after->feature_elements[selected_index],
                     after->feature_element_sizes[selected_index],
                     &after_selected), ==, 0);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     probe->feature_elements[0], probe->feature_element_sizes[0],
                     &probe_view), ==, 0);
  assert_feature_material_equal (&after_selected, &probe_view);
  selected_ordinal = before_selected.fields.tagged_values[7];

  const gint32 expected_selected[11] = {
    action == GOODIX_MILAN_STUDY_REPLACE_NO_RELATION
      ? 0 : before_selected.fields.tagged_values[0],
    before_selected.fields.tagged_values[1],
    probe_view.fields.tagged_values[2],
    probe_view.fields.tagged_values[3],
    probe_view.fields.tagged_values[4],
    before_selected.fields.tagged_values[5] == 0 ? 0 : 2,
    action == GOODIX_MILAN_STUDY_REPLACE_NO_RELATION
      ? before_selected.fields.tagged_values[6] : 0,
    GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 1,
    before_selected.fields.tagged_values[8] + generation_count,
    lifecycle_count,
    before_selected.fields.tagged_values[10],
  };

  g_assert_cmpmem (after_selected.fields.tagged_values,
                   sizeof(expected_selected), expected_selected,
                   sizeof(expected_selected));
  g_assert_cmpint (after_selected.fields.optional_c7, ==,
                   before_selected.fields.optional_c7);

  for (gsize i = 0; i < before->feature_count; i++)
    {
      GoodixMilanFeatureView before_view;
      GoodixMilanFeatureView after_view;

      if (i == selected_index)
        continue;
      g_assert_cmpint (goodix_milan_template_parse_feature_element (
                         before->feature_elements[i],
                         before->feature_element_sizes[i], &before_view), ==, 0);
      g_assert_cmpint (goodix_milan_template_parse_feature_element (
                         after->feature_elements[i],
                         after->feature_element_sizes[i], &after_view), ==, 0);
      assert_feature_material_equal (&after_view, &before_view);
      for (gsize field = 0; field < G_N_ELEMENTS (before_view.fields.tagged_values);
           field++)
        {
          gint32 expected = before_view.fields.tagged_values[field];

          if (field == 6 && action != GOODIX_MILAN_STUDY_REPLACE_NO_RELATION)
            expected = 0;
          if (field == 7 && expected > selected_ordinal)
            expected--;
          g_assert_cmpint (after_view.fields.tagged_values[field], ==, expected);
        }
      g_assert_cmpint (after_view.fields.optional_c7, ==,
                       before_view.fields.optional_c7);
    }

  /* Feature zero has zero residual and precedes every replacement ordinal. */
  g_assert_cmpmem (after->feature_elements[0], after->feature_element_sizes[0],
                   before->feature_elements[0], before->feature_element_sizes[0]);
  assert_replacement_relations (after, action, selected_index);
  if (finalize_transaction)
    {
      gsize position = 0;

      g_assert_cmpuint (goodix_milan_template_read_u32 (
                          after->tail_state + position++ * 4), ==,
                        selected_index);
      for (gsize i = before->feature_count; i-- > 0;)
        if (i != selected_index)
          g_assert_cmpuint (goodix_milan_template_read_u32 (
                              after->tail_state + position++ * 4), ==, i);
      g_assert_cmpuint (position, ==, before->feature_count);
      g_assert_cmpmem (after->tail_state + before->feature_count * 4,
                       0x50c - before->feature_count * 4,
                       before->tail_state + before->feature_count * 4,
                       0x50c - before->feature_count * 4);
      g_assert_cmpuint (goodix_milan_template_read_u32 (
                          after->tail_state + 0x50c), ==,
                        goodix_milan_template_read_u32 (
                          before->tail_state + 0x50c) + 1);
    }
  else
    {
      g_assert_cmpmem (after->tail_state, 0x510, before->tail_state, 0x510);
      for (gsize i = 0; i < before->feature_count; i++)
        g_assert_cmpuint (goodix_milan_template_read_u32 (
                            after->tail_state + i * 4), ==, i);
    }
  g_assert_cmpuint (goodix_milan_template_read_u32 (after->tail_state + 0x510),
                    ==,
                    goodix_milan_template_read_u32 (before->tail_state + 0x510) +
                      (guint32) generation_count);
  g_assert_cmpmem (after->tail_state + 0x514,
                   sizeof(after->tail_state) - 0x514,
                   before->tail_state + 0x514,
                   sizeof(before->tail_state) - 0x514);
}

static void
assert_match_update_semantics (const GoodixMilanUnpackedTemplate *before,
                               const GoodixMilanUnpackedTemplate *after,
                               gsize                               matched_index)
{
  g_assert_cmpmem (&after->metadata, sizeof(after->metadata),
                   &before->metadata, sizeof(before->metadata));
  g_assert_cmpuint (after->feature_count, ==, before->feature_count);
  g_assert_cmpuint (after->relation_count, ==, before->relation_count);
  g_assert_cmpmem (after->relations,
                   after->relation_count * sizeof(after->relations[0]),
                   before->relations,
                   before->relation_count * sizeof(before->relations[0]));
  g_assert_cmpmem (after->tail_state, sizeof(after->tail_state),
                   before->tail_state, sizeof(before->tail_state));

  for (gsize i = 0; i < before->feature_count; i++)
    {
      GoodixMilanFeatureView before_view;
      GoodixMilanFeatureView after_view;

      g_assert_cmpint (goodix_milan_template_parse_feature_element (
                         before->feature_elements[i],
                         before->feature_element_sizes[i], &before_view), ==, 0);
      g_assert_cmpint (goodix_milan_template_parse_feature_element (
                         after->feature_elements[i],
                         after->feature_element_sizes[i], &after_view), ==, 0);
      assert_feature_material_equal (&after_view, &before_view);
      for (gsize field = 0; field < G_N_ELEMENTS (before_view.fields.tagged_values);
           field++)
        {
          gint32 expected = before_view.fields.tagged_values[field];

          if (field == 6)
            expected = 0;
          if (field == 9 && i == matched_index)
            expected++;
          g_assert_cmpint (after_view.fields.tagged_values[field], ==, expected);
        }
      g_assert_cmpint (after_view.fields.optional_c7, ==,
                       before_view.fields.optional_c7);
    }
  g_assert_cmpmem (after->feature_elements[0], after->feature_element_sizes[0],
                   before->feature_elements[0], before->feature_element_sizes[0]);
}

static GoodixMilanMatchResult
study_primary_result (gint32 retained_flag)
{
  GoodixMilanMatchResult result = {
    .matched_feature_index = 1,
    .score = 1,
    .match_transform = { 0x100, 0, 0, 0, 0x100, 0 },
    .relation = {
      .relation_count = 1,
      .relation_values = { 0, 0x100, 0, 0, 0, 0x100, 0 },
      .relation_valid = 1,
    },
    .retained_evidence_flag = retained_flag,
    .study_control.study_action_gate = 1,
  };

  return result;
}

typedef struct
{
  const gchar *name;
  GoodixMilanStudyAction action;
  gint32 retained_flag;
  gsize selected_index;
  guint32 relation_count;
} StudyActionCase;

static GBytes *
run_study_action_case (const StudyActionCase *test_case)
{
  g_autoptr(GBytes) gallery = study_gallery (test_case->action, FALSE);
  GoodixMatchInfo *probe = study_match_info (9, FALSE, 0);
  GoodixStudyQueue *queue = goodix_study_queue_new (0, 7);
  GoodixMilanMatchResult result = study_primary_result (
    test_case->retained_flag);
  GoodixMilanStudyAction action = GOODIX_MILAN_STUDY_NONE;
  GBytes *updated = NULL;
  const guint8 *gallery_data;
  gsize gallery_size;
  GoodixMilanPrintTemplateInfo info;
  GoodixMilanUnpackedTemplate before;
  GoodixMilanUnpackedTemplate unpacked;
  GoodixMilanUnpackedTemplate probe_unpacked;

  unpack_study_template (gallery, &before);
  unpack_study_template (probe->template, &probe_unpacked);
  gallery_data = g_bytes_get_data (gallery, &gallery_size);
  g_assert_cmpint (goodix_match_study_feature_queued (
                     probe, gallery_data, gallery_size, &result, TRUE, queue,
                     &updated, &action), ==, GOODIX_SIGFM_TEMPLATE_OK);
  g_assert_cmpint (action, ==, test_case->action);
  g_assert_nonnull (updated);
  g_assert_false (g_bytes_equal (gallery, updated));
  g_assert_true (goodix_study_queue_validate (queue));
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 0);
  g_assert_cmpuint (goodix_study_queue_allocated (queue), ==, 0);
  assert_study_template (
    updated, test_case->relation_count, &info, &unpacked);
  assert_replacement_semantics (
    &before, &unpacked, &probe_unpacked, test_case->action,
    test_case->selected_index, 1, 0, FALSE);

  goodix_study_queue_free (queue);
  goodix_match_free_info (probe);
  return updated;
}

static void
test_study_actions (void)
{
  static const StudyActionCase cases[] = {
    { "replace-no-relation", GOODIX_MILAN_STUDY_REPLACE_NO_RELATION,
      0, 1, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 2 },
    { "geometric", GOODIX_MILAN_STUDY_GEOMETRIC,
      1, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 1,
      GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 1 },
    { "replace", GOODIX_MILAN_STUDY_REPLACE,
      1, 1, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 1 },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autoptr(GBytes) first = NULL;

      for (gsize repeat = 0; repeat < 2; repeat++)
        {
          g_autoptr(GBytes) current = run_study_action_case (&cases[i]);

          g_test_message ("study action=%s repeat=%zu", cases[i].name, repeat);
          if (!first)
            first = g_bytes_ref (current);
          else
            g_assert_true (g_bytes_equal (first, current));
        }
    }
}

static GBytes *
run_queued_study_case (void)
{
  static const gint32 primary_marker = INT32_C (0x13579bdf);
  static const gint32 queued_marker = INT32_C (0x2468ace0);
  g_autoptr(GBytes) baseline_gallery = study_gallery (
    GOODIX_MILAN_STUDY_REPLACE_NO_RELATION, FALSE);
  g_autoptr(GBytes) queued_gallery = study_gallery (
    GOODIX_MILAN_STUDY_REPLACE_NO_RELATION, FALSE);
  GoodixMatchInfo *primary = study_match_info (9, TRUE, primary_marker);
  GoodixMatchInfo *queued = study_match_info (9, TRUE, queued_marker);
  GoodixStudyQueue *baseline_queue = goodix_study_queue_new (0, 7);
  GoodixStudyQueue *queue = goodix_study_queue_new (0, 7);
  GoodixMilanMatchResult primary_result = study_primary_result (0);
  GoodixMilanMatchResult queued_primary_result = study_primary_result (0);
  GoodixMilanMatchResult followup = { 0 };
  GoodixMilanStudyAction action = GOODIX_MILAN_STUDY_NONE;
  GBytes *baseline = NULL;
  GBytes *updated = NULL;
  const guint8 *gallery_data;
  const guint8 *baseline_data;
  const guint8 *baseline_payload;
  gsize gallery_size;
  gsize baseline_size;
  gsize baseline_payload_size;
  const GoodixMilanFeatureRecord *live_records[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
  size_t live_record_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
  size_t live_partition_counts[GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY] = { 0 };
  GoodixMilanPrintTemplateInfo info;
  GoodixMilanUnpackedTemplate before;
  GoodixMilanUnpackedTemplate unpacked;
  GoodixMilanUnpackedTemplate baseline_unpacked;
  GoodixMilanUnpackedTemplate probe_unpacked;
  GoodixMilanUnpackedTemplate queued_probe_unpacked;
  GoodixMilanFeatureView primary_feature;
  GoodixMilanFeatureView queued_feature;
  GoodixMilanFeatureView selected_feature;

  unpack_study_template (baseline_gallery, &before);
  unpack_study_template (primary->template, &probe_unpacked);
  unpack_study_template (queued->template, &queued_probe_unpacked);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     probe_unpacked.feature_elements[0],
                     probe_unpacked.feature_element_sizes[0],
                     &primary_feature), ==, 0);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     queued_probe_unpacked.feature_elements[0],
                     queued_probe_unpacked.feature_element_sizes[0],
                     &queued_feature), ==, 0);
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     &primary->antifake), ==,
                   primary_marker);
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     &queued->antifake), ==,
                   queued_marker);
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     &primary->antifake), !=,
                   goodix_milan_antifake_calibration_scalar (
                     &queued->antifake));
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     primary_feature.antifake), ==,
                   primary_marker);
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     queued_feature.antifake), ==,
                   queued_marker);
  gallery_data = g_bytes_get_data (baseline_gallery, &gallery_size);
  g_assert_cmpint (goodix_match_study_feature_queued (
                     primary, gallery_data, gallery_size, &primary_result, TRUE,
                     baseline_queue, &baseline, &action), ==,
                   GOODIX_SIGFM_TEMPLATE_OK);
  g_assert_cmpint (action, ==, GOODIX_MILAN_STUDY_REPLACE_NO_RELATION);
  assert_study_template (
    baseline, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 2,
    &info, &baseline_unpacked);
  assert_replacement_semantics (
    &before, &baseline_unpacked, &probe_unpacked,
    GOODIX_MILAN_STUDY_REPLACE_NO_RELATION, 1, 1, 0, FALSE);

  baseline_data = g_bytes_get_data (baseline, &baseline_size);
  baseline_payload = baseline_data;
  baseline_payload_size = baseline_size;
  live_records[1] = primary->records;
  live_record_counts[1] = (size_t) primary->record_count;
  live_partition_counts[1] = (size_t) primary->partition_count;
  g_assert_cmpint (goodix_milan_match_live_probe_result (
                     queued->feature_bitmaps.high_bitmap,
                     queued->feature_bitmaps.enhanced_bitmap,
                     queued->feature_bitmaps.low_bitmap, queued->inline_mask,
                     queued->rescue_mask, queued->records,
                     (size_t) queued->record_count,
                     (size_t) queued->partition_count,
                     queued->extraction_metadata.quality,
                     queued->extraction_metadata.coverage,
                     queued->extraction_metadata.optional_c7,
                     baseline_payload, baseline_payload_size, live_records,
                     live_record_counts, live_partition_counts, 1,
                     &followup), ==, 0);
  g_assert_cmpint (followup.score, ==, 100);
  g_assert_cmpuint (followup.matched_feature_index, ==, 1);
  g_assert_true (followup.relation.relation_valid);
  g_assert_cmpint (followup.study_control.study_finalization_gate, ==, 1);
  g_assert_cmpint (followup.study_control.study_action_gate, ==, 1);
  g_assert_cmpuint (followup.lifecycle_update_feature_mask, ==, UINT64_C (2));

  g_assert_cmpint (goodix_study_queue_enqueue (
                     queue, queued, NULL, NULL), ==,
                   GOODIX_STUDY_QUEUE_ENQUEUED);
  action = GOODIX_MILAN_STUDY_NONE;
  gallery_data = g_bytes_get_data (queued_gallery, &gallery_size);
  g_assert_cmpint (goodix_match_study_feature_queued (
                     primary, gallery_data, gallery_size,
                     &queued_primary_result, TRUE, queue, &updated, &action),
                   ==, GOODIX_SIGFM_TEMPLATE_OK);
  g_assert_cmpint (action, ==, GOODIX_MILAN_STUDY_QUEUED);
  g_assert_nonnull (updated);
  g_assert_false (g_bytes_equal (queued_gallery, updated));
  g_assert_false (g_bytes_equal (baseline, updated));
  g_assert_true (goodix_study_queue_validate (queue));
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 0);
  g_assert_cmpuint (goodix_study_queue_allocated (queue), ==, 0);
  assert_study_template (
    updated, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 2,
    &info, &unpacked);
  assert_replacement_semantics (
    &before, &unpacked, &queued_probe_unpacked,
    GOODIX_MILAN_STUDY_REPLACE_NO_RELATION, 1, 2, 1, FALSE);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     unpacked.feature_elements[1],
                     unpacked.feature_element_sizes[1], &selected_feature), ==,
                   0);
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     selected_feature.antifake), ==,
                   queued_marker);
  g_assert_cmpint (goodix_milan_antifake_calibration_scalar (
                     selected_feature.antifake), !=,
                   primary_marker);

  g_bytes_unref (baseline);
  goodix_study_queue_free (queue);
  goodix_study_queue_free (baseline_queue);
  goodix_match_free_info (queued);
  goodix_match_free_info (primary);
  return updated;
}

static void
test_queued_study_action (void)
{
  g_autoptr(GBytes) first = NULL;

  for (gsize repeat = 0; repeat < 2; repeat++)
    {
      g_autoptr(GBytes) current = run_queued_study_case ();

      if (!first)
        first = g_bytes_ref (current);
      else
        g_assert_true (g_bytes_equal (first, current));
    }
}

static GBytes *
run_production_match_study_handoff (void)
{
  g_autoptr(GBytes) gallery = study_gallery (
    GOODIX_MILAN_STUDY_REPLACE_NO_RELATION, TRUE);
  g_autoptr(GError) error = NULL;
  GoodixMatchInfo *probe = study_match_info (9, TRUE, 0);
  GoodixStudyQueue *queue = goodix_study_queue_new (0, 7);
  GoodixMilanMatchResult result;
  GoodixMilanStudyAction action = GOODIX_MILAN_STUDY_NONE;
  GBytes *after_match = NULL;
  GBytes *after_study = NULL;
  const guint8 *data;
  gsize size;
  GoodixMilanPrintTemplateInfo match_info;
  GoodixMilanPrintTemplateInfo study_info;
  GoodixMilanUnpackedTemplate before;
  GoodixMilanUnpackedTemplate matched;
  GoodixMilanUnpackedTemplate studied;
  GoodixMilanUnpackedTemplate probe_unpacked;

  unpack_study_template (gallery, &before);
  unpack_study_template (probe->template, &probe_unpacked);
  data = g_bytes_get_data (gallery, &size);
  g_assert_cmpint (goodix_match_serialized_feature_result_queued (
                     probe, data, size, &result, &after_match, queue), ==,
                   GOODIX_SIGFM_TEMPLATE_OK);
  g_assert_nonnull (after_match);
  g_assert_false (g_bytes_equal (gallery, after_match));
  g_assert_cmpint (result.score, ==, 100);
  g_assert_cmpuint (result.matched_feature_index, ==, 1);
  static const gint32 identity_relation[7] = {
    0, 0x100, 0, 0, 0, 0x100, 0,
  };

  g_assert_true (result.relation.relation_valid);
  g_assert_cmpint (result.relation.relation_count, ==, 42);
  g_assert_cmpmem (result.relation.relation_values,
                   sizeof(identity_relation), identity_relation,
                   sizeof(identity_relation));
  g_assert_cmpmem (result.match_transform, 6 * sizeof(gint32),
                   identity_relation + 1, 6 * sizeof(gint32));
  g_assert_cmpuint (result.direct_positive_feature_mask, ==, UINT64_C (2));
  g_assert_cmpuint (result.contributor_feature_mask, ==, UINT64_C (2));
  g_assert_cmpuint (result.lifecycle_update_feature_mask, ==, UINT64_C (2));
  g_assert_cmpuint (result.retained_evidence_count, ==, 0);
  g_assert_cmpint (result.retained_evidence_flag, ==, 1);
  g_assert_cmpint (result.study_control.study_finalization_gate, ==, 1);
  g_assert_cmpint (result.study_control.study_action_gate, ==, 1);
  g_assert_cmpint (result.study_control.queue_candidate_eligible, ==, 0);
  g_assert_true (goodix_study_queue_validate (queue));
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 0);
  g_assert_true (goodix_milan_print_validate_template (
    after_match, &match_info, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (match_info.queue_state, ==, 0);
  unpack_study_template (after_match, &matched);
  assert_match_update_semantics (&before, &matched, 1);

  data = g_bytes_get_data (after_match, &size);
  g_assert_cmpint (goodix_match_study_feature_queued (
                     probe, data, size, &result, TRUE, queue, &after_study,
                     &action), ==, GOODIX_SIGFM_TEMPLATE_OK);
  g_assert_nonnull (after_study);
  g_assert_cmpint (action, ==, GOODIX_MILAN_STUDY_REPLACE);
  g_assert_false (g_bytes_equal (after_match, after_study));
  g_assert_true (goodix_study_queue_validate (queue));
  g_assert_cmpuint (goodix_study_queue_occupied (queue), ==, 0);
  g_assert_cmpuint (goodix_study_queue_allocated (queue), ==, 0);
  assert_study_template (
    after_study, GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT - 1,
    &study_info, &studied);
  assert_replacement_semantics (
    &before, &studied, &probe_unpacked, GOODIX_MILAN_STUDY_REPLACE,
    1, 1, 1, TRUE);

  g_bytes_unref (after_match);
  goodix_study_queue_free (queue);
  goodix_match_free_info (probe);
  return after_study;
}

static void
test_production_match_study_handoff (void)
{
  g_autoptr(GBytes) first = NULL;

  for (gsize repeat = 0; repeat < 2; repeat++)
    {
      g_autoptr(GBytes) current = run_production_match_study_handoff ();

      if (!first)
        first = g_bytes_ref (current);
      else
        g_assert_true (g_bytes_equal (first, current));
    }
}

static void
assert_feature_roundtrip (GBytes *feature,
                          gsize   partition_count)
{
  const guint8 *packed;
  gsize packed_size;
  GoodixMilanFeatureView view;
  g_autofree GoodixMilanFeatureRecord *records = NULL;
  g_autofree guint8 *repacked = NULL;
  gsize repacked_size = 0;

  packed = g_bytes_get_data (feature, &packed_size);
  g_assert_cmpint (goodix_milan_template_parse_feature_element (
                     packed, packed_size, &view), ==, 0);
  records = g_new0 (GoodixMilanFeatureRecord, view.record_count);
  repacked = g_malloc (packed_size);
  g_assert_cmpint (goodix_milan_feature_unpack_template_records (
                     view.packed_records, view.record_count, partition_count,
                     records, view.record_count), ==, 0);
  g_assert_cmpint (goodix_milan_template_pack_feature_element (
                     view.high_bitmap, view.enhanced_bitmap, view.inline_mask,
                     view.low_bitmap, records, view.record_count, view.antifake,
                     &view.fields, repacked, packed_size, &repacked_size), ==, 0);
  g_assert_cmpuint (repacked_size, ==, packed_size);
  g_assert_cmpmem (repacked, repacked_size, packed, packed_size);
}

static void
test_template_state (void)
{
  static const TemplateShape shapes[] = {
    { "single-graphless", 1, 1, 0, FALSE, FALSE, UINT64_C (1) },
    { "multi-graph", 3, 4, 2, TRUE, TRUE, UINT64_C (5) },
  };

  for (gsize shape_index = 0; shape_index < G_N_ELEMENTS (shapes);
       shape_index++)
    {
      const TemplateShape *shape = &shapes[shape_index];
      GBytes *features[3] = { NULL };
      GBytes *feature_snapshots[3] = { NULL };
      const guint8 *feature_data[3] = { NULL };
      gsize feature_sizes[3] = { 0 };
      GoodixMilanTemplateRelation relations[2] = { 0 };
      GoodixMilanTemplateMetadata metadata = { 0 };
      guint8 tail[0x520];
      g_autofree guint8 *tail_before = NULL;
      g_autofree guint8 *packed = NULL;
      g_autofree guint8 *input_before = NULL;
      g_autofree guint8 *repacked = NULL;
      g_autofree guint8 *updated = NULL;
      g_autofree guint8 *updated_twice = NULL;
      g_autofree GoodixMilanUnpackedTemplate *unpacked = g_new0 (
        GoodixMilanUnpackedTemplate, 1);
      g_autofree GoodixMilanUnpackedTemplate *twice = g_new0 (
        GoodixMilanUnpackedTemplate, 1);
      gsize capacity;
      gsize packed_size = 0;
      gsize repacked_size = 0;
      gsize updated_size = 0;
      gsize updated_twice_size = 0;
      gsize rejected_size = 0;

      g_test_message ("template shape=%s", shape->name);
      for (gsize i = 0; i < shape->feature_count; i++)
        {
          features[i] = synthetic_feature_element (
            (guint) (shape_index * 10 + i + 1), shape->record_count,
            shape->optional_c7);
          feature_data[i] = g_bytes_get_data (features[i], &feature_sizes[i]);
          feature_snapshots[i] = g_bytes_new (feature_data[i], feature_sizes[i]);
          assert_feature_roundtrip (features[i], shape->record_count / 2);
        }
      for (gsize i = 0; i < shape->relation_count; i++)
        {
          static const gint32 identity_relation[7] = {
            205, 0x100, 0, 0, 0, 0x100, 0,
          };

          relations[i].index = (gint32) i + 1;
          memcpy (relations[i].values, identity_relation,
                  sizeof(identity_relation));
        }
      metadata.sensor_type = 12;
      metadata.maximum_features = GOODIX_MILAN_PROFILE9_ACTIVE_FEATURE_LIMIT;
      metadata.registration_count = 7;
      metadata.maximum_records = 150;
      metadata.queue_state = 0;
      metadata.queue_transaction_counter = (guint32) (30 + shape_index);
      metadata.graph_reference_index = shape->graph_established ? 0 : -1;
      metadata.graph_companion_f3 = -1;
      metadata.graph_companion_f4 = -1;
      metadata.graph_established = shape->graph_established;
      for (gsize i = 0; i < sizeof(tail); i++)
        tail[i] = synthetic_byte ((guint) shape_index + 20, i);

      capacity = 1433 + shape->relation_count * 45;
      for (gsize i = 0; i < shape->feature_count; i++)
        capacity += feature_sizes[i];
      packed = g_malloc (capacity);
      repacked = g_malloc (capacity);
      updated = g_malloc (capacity);
      updated_twice = g_malloc (capacity);
      tail_before = g_memdup2 (tail, sizeof(tail));
      g_assert_cmpint (goodix_milan_template_pack (
                         feature_data, feature_sizes, shape->feature_count,
                         relations, shape->relation_count, &metadata, tail,
                         sizeof(tail), packed, capacity, &packed_size), ==, 0);
      g_assert_cmpuint (packed_size, ==, capacity);
      g_assert_cmpmem (tail, sizeof(tail), tail_before, sizeof(tail));
      for (gsize i = 0; i < shape->feature_count; i++)
        g_assert_true (feature_data[i] == g_bytes_get_data (features[i], NULL));

      input_before = g_memdup2 (packed, packed_size);
      g_assert_cmpint (goodix_milan_template_unpack (
                         packed, packed_size, unpacked), ==, 0);
      g_assert_cmpmem (packed, packed_size, input_before, packed_size);
      g_assert_cmpuint (unpacked->feature_count, ==, shape->feature_count);
      g_assert_cmpuint (unpacked->relation_count, ==, shape->relation_count);
      g_assert_cmpmem (&unpacked->metadata, sizeof(unpacked->metadata),
                       &metadata, sizeof(metadata));
      g_assert_cmpint (goodix_milan_template_pack (
                         unpacked->feature_elements,
                         unpacked->feature_element_sizes,
                         unpacked->feature_count, unpacked->relations,
                         unpacked->relation_count, &unpacked->metadata,
                         unpacked->tail_state, sizeof(unpacked->tail_state),
                         repacked, capacity, &repacked_size), ==, 0);
      g_assert_cmpuint (repacked_size, ==, packed_size);
      g_assert_cmpmem (repacked, repacked_size, packed, packed_size);

      g_assert_cmpint (goodix_milan_template_update_match_lifecycle (
                         packed, packed_size, 0, updated, capacity,
                         &updated_size), ==, 0);
      g_assert_cmpuint (updated_size, ==, packed_size);
      g_assert_cmpmem (updated, updated_size, packed, packed_size);
      g_assert_cmpmem (packed, packed_size, input_before, packed_size);
      g_assert_cmpint (goodix_milan_template_update_match_lifecycle (
                         packed, packed_size, shape->lifecycle_mask, updated,
                         capacity, &updated_size), ==, 0);
      g_assert_cmpint (goodix_milan_template_update_match_lifecycle (
                         updated, updated_size, shape->lifecycle_mask,
                         updated_twice, capacity, &updated_twice_size), ==, 0);
      g_assert_cmpuint (updated_size, ==, packed_size);
      g_assert_cmpuint (updated_twice_size, ==, packed_size);
      g_assert_cmpmem (packed, packed_size, input_before, packed_size);
      g_assert_cmpint (goodix_milan_template_update_match_lifecycle (
                         packed, packed_size,
                         UINT64_C (1) << shape->feature_count, repacked,
                         capacity, &rejected_size), ==, -1);
      g_assert_cmpmem (packed, packed_size, input_before, packed_size);
      memset (unpacked, 0, sizeof(*unpacked));
      g_assert_cmpint (goodix_milan_template_unpack (
                         updated, updated_size, unpacked), ==, 0);
      g_assert_cmpint (goodix_milan_template_unpack (
                         updated_twice, updated_twice_size, twice), ==, 0);
      for (gsize i = 0; i < shape->feature_count; i++)
        {
          GoodixMilanFeatureView before_view;
          GoodixMilanFeatureView after_view;
          gint increment = (gint) ((shape->lifecycle_mask >> i) & 1);

          g_assert_cmpint (goodix_milan_template_parse_feature_element (
                             feature_data[i], feature_sizes[i],
                             &before_view), ==, 0);
          g_assert_cmpint (goodix_milan_template_parse_feature_element (
                             unpacked->feature_elements[i],
                             unpacked->feature_element_sizes[i],
                             &after_view), ==, 0);
          g_assert_cmpint (after_view.fields.tagged_values[9], ==,
                           before_view.fields.tagged_values[9] + increment);
          g_assert_cmpint (goodix_milan_template_parse_feature_element (
                             twice->feature_elements[i],
                             twice->feature_element_sizes[i],
                             &after_view), ==, 0);
          g_assert_cmpint (after_view.fields.tagged_values[9], ==,
                           before_view.fields.tagged_values[9] + 2 * increment);
        }

      if (!shape->graph_established)
        {
          g_autofree guint8 *normalized = g_malloc (capacity);
          g_autofree guint8 *renormalized = g_malloc (capacity);
          gsize normalized_size = 0;
          gsize renormalized_size = 0;

          g_assert_cmpint (goodix_milan_template_normalize (
                             packed, packed_size, normalized, capacity,
                             &normalized_size), ==, 0);
          g_assert_cmpint (goodix_milan_template_normalize (
                             normalized, normalized_size, renormalized,
                             capacity, &renormalized_size), ==, 0);
          g_assert_cmpuint (normalized_size, ==, renormalized_size);
          g_assert_cmpmem (normalized, normalized_size, renormalized,
                           renormalized_size);
          g_assert_cmpmem (packed, packed_size, input_before, packed_size);
        }

      for (gsize i = 0; i < shape->feature_count; i++)
        {
          g_assert_true (g_bytes_equal (features[i], feature_snapshots[i]));
          g_bytes_unref (feature_snapshots[i]);
          g_bytes_unref (features[i]);
        }
    }
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/goodix53x5/milan/state/queue-lifecycle",
                   test_queue_lifecycle);
  g_test_add_func ("/goodix53x5/milan/state/queue-process",
                   test_queue_process);
  g_test_add_func ("/goodix53x5/milan/state/template", test_template_state);
  g_test_add_func ("/goodix53x5/milan/state/study-actions",
                   test_study_actions);
  g_test_add_func ("/goodix53x5/milan/state/queued-study-action",
                   test_queued_study_action);
  g_test_add_func ("/goodix53x5/milan/state/production-match-study-handoff",
                   test_production_match_study_handoff);
  return g_test_run ();
}
