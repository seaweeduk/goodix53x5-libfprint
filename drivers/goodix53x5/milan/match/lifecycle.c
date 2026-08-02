/*
 * Goodix 53x5 driver for libfprint - native Milan serialized match lifecycle
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "milan/enrollment/template-private.h"
#include "milan/match/match.h"
#include "milan/match/info-private.h"
#include "milan/match/lifecycle-private.h"
#include "milan/milan.h"
#include "milan/study/queue.h"

#include <string.h>

gboolean
goodix_match_queue_matches_template (const GoodixStudyQueue *queue,
                                     const guint8           *feature,
                                     gsize                   feature_len)
{
  GoodixMilanUnpackedTemplate *unpacked;
  GoodixSigfmTemplateStatus status;
  const guint8 *template_payload;
  gsize template_payload_len;
  gboolean matches = FALSE;

  if (!queue || !goodix_study_queue_validate (queue))
    return FALSE;
  template_payload = goodix_match_unwrap_template (
    feature, feature_len, &template_payload_len, &status);
  if (!template_payload)
    return FALSE;
  unpacked = g_malloc (sizeof(*unpacked));
  if (goodix_milan_template_unpack (template_payload, template_payload_len, unpacked) == 0)
    matches = queue->enabled_state == unpacked->metadata.queue_state &&
              queue->transaction_counter ==
                unpacked->metadata.queue_transaction_counter;
  g_free (unpacked);
  return matches;
}

GoodixSigfmTemplateStatus
goodix_match_serialized_feature_result_internal (
  GoodixMatchInfo             *probe_info,
  const guint8                *feature,
  gsize                        feature_len,
  GoodixMilanMatchResult      *match_result,
  GBytes                     **updated_feature,
#ifdef GOODIX53X5_DEBUG
  GoodixMilanMatchDiagnostics *diagnostics,
#endif
  GoodixStudyQueue            *queue,
  gboolean                     normalize,
  GoodixStudyQueueEnqueueResult (*enqueue_candidate) (
    GoodixStudyQueue      *queue,
    const GoodixMatchInfo *probe_info))
{
  GoodixSigfmTemplateStatus status;
  guint8 *updated_milan = NULL;
  guint8 *normalized_milan = NULL;
  const guint8 *matched_milan;
  size_t normalized_milan_len = 0;
  size_t updated_milan_len = 0;

  if (updated_feature)
    *updated_feature = NULL;

  if (!probe_info || !probe_info->template || !feature || !match_result)
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  if (queue && !goodix_study_queue_validate (queue))
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  gsize enrolled_milan_len;
  if (feature_len == 6 && memcmp (feature, "G53M\x03\x00", 6) == 0)
    {
      memset (match_result, 0, sizeof (*match_result));
#ifdef GOODIX53X5_DEBUG
      if (diagnostics != NULL)
        memset (diagnostics, 0, sizeof (*diagnostics));
#endif
      return GOODIX_SIGFM_TEMPLATE_OK;
    }
  const guint8 *enrolled_milan = goodix_match_unwrap_template (
    feature, feature_len, &enrolled_milan_len, &status);
  if (!enrolled_milan)
    return status;
  if (queue && !goodix_match_queue_matches_template (
        queue, feature, feature_len))
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  normalized_milan = g_malloc (enrolled_milan_len);
  if (normalize)
    {
      if (goodix_milan_template_normalize (
            enrolled_milan, enrolled_milan_len, normalized_milan,
            enrolled_milan_len, &normalized_milan_len) != 0)
        {
          g_free (normalized_milan);
          return GOODIX_SIGFM_TEMPLATE_INVALID;
        }
    }
  else
    {
      memcpy (normalized_milan, enrolled_milan, enrolled_milan_len);
      normalized_milan_len = enrolled_milan_len;
    }
  if (normalized_milan_len != enrolled_milan_len)
    {
      g_free (normalized_milan);
      return GOODIX_SIGFM_TEMPLATE_INVALID;
    }
  matched_milan = normalized_milan;

#ifdef GOODIX53X5_DEBUG
  if (goodix_milan_match_probe_result_debug (
#else
  if (goodix_milan_match_probe_result (
#endif
        probe_info->feature_bitmaps.high_bitmap,
        probe_info->feature_bitmaps.enhanced_bitmap,
        probe_info->feature_bitmaps.low_bitmap,
        probe_info->inline_mask,
        probe_info->rescue_mask,
        probe_info->records, (size_t) probe_info->record_count,
        (size_t) probe_info->partition_count,
        probe_info->extraction_metadata.quality,
        probe_info->extraction_metadata.coverage,
        probe_info->extraction_metadata.optional_c7,
        &probe_info->antifake,
        matched_milan, normalized_milan_len, match_result
#ifdef GOODIX53X5_DEBUG
        , diagnostics
#endif
        ) != 0)
    {
      g_free (normalized_milan);
      return GOODIX_SIGFM_TEMPLATE_INVALID;
    }

  if (updated_feature)
    {
      if (match_result->score > 0 &&
          match_result->lifecycle_update_feature_mask != 0)
        {
          updated_milan = g_malloc (normalized_milan_len);
          if (goodix_milan_template_update_match_lifecycle (
                matched_milan, normalized_milan_len,
                match_result->lifecycle_update_feature_mask, updated_milan,
                normalized_milan_len, &updated_milan_len) != 0 ||
              updated_milan_len != normalized_milan_len)
            {
              g_free (updated_milan);
              g_free (normalized_milan);
              return GOODIX_SIGFM_TEMPLATE_INVALID;
            }
          matched_milan = updated_milan;
          normalized_milan_len = updated_milan_len;
        }
      *updated_feature = goodix_match_wrap_template (
        matched_milan, normalized_milan_len);
      g_free (updated_milan);
      if (!*updated_feature)
        {
          g_free (normalized_milan);
          return GOODIX_SIGFM_TEMPLATE_INVALID;
        }
    }
  if (queue && match_result->study_control.queue_candidate_eligible)
    {
      GoodixStudyQueueEnqueueResult enqueue_result =
        enqueue_candidate (queue, probe_info);

      if (enqueue_result == GOODIX_STUDY_QUEUE_INVALID)
        {
          if (updated_feature)
            g_clear_pointer (updated_feature, g_bytes_unref);
          g_free (normalized_milan);
          return GOODIX_SIGFM_TEMPLATE_INVALID;
        }
    }
  g_free (normalized_milan);

  return GOODIX_SIGFM_TEMPLATE_OK;
}
