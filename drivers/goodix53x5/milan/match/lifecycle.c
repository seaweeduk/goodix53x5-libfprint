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
#include "milan/match/match.h"
#include "milan/match/info-private.h"
#include "milan/match/lifecycle-private.h"
#include "milan/milan.h"
#include "milan/study/queue.h"

#include <string.h>

gboolean
goodix_milan_match_queue_matches_template (const GoodixStudyQueue *queue,
                                     const guint8           *feature,
                                     gsize                   feature_len)
{
  GoodixMilanUnpackedTemplate *unpacked;
  gboolean matches = FALSE;

  if (!queue || !goodix_milan_study_queue_validate (queue))
    return FALSE;
  if (!feature || feature_len > GOODIX_MILAN_TEMPLATE_MAX_SIZE)
    return FALSE;
  unpacked = g_malloc (sizeof(*unpacked));
  if (goodix_milan_template_unpack (feature, feature_len, unpacked) == 0)
    matches = queue->enabled_state == unpacked->metadata.queue_state &&
              queue->transaction_counter ==
                unpacked->metadata.queue_transaction_counter;
  g_free (unpacked);
  return matches;
}

GoodixSigfmTemplateStatus
goodix_milan_match_serialized_feature_result_internal (
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
  guint8 *updated_milan = NULL;
  guint8 *normalized_milan = NULL;
  const guint8 *matched_milan;
  size_t normalized_milan_len = 0;
  size_t updated_milan_len = 0;

  if (updated_feature)
    *updated_feature = NULL;

  if (!probe_info || !probe_info->template || !feature || !match_result)
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  if (queue && !goodix_milan_study_queue_validate (queue))
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  gsize enrolled_milan_len = feature_len;
  const guint8 *enrolled_milan = feature;

  if (feature_len > GOODIX_MILAN_TEMPLATE_MAX_SIZE)
    return GOODIX_SIGFM_TEMPLATE_INVALID;
  if (queue && !goodix_milan_match_queue_matches_template (
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

  if (goodix_milan_match_info_result (
        probe_info, matched_milan, normalized_milan_len, NULL, NULL, NULL,
        SIZE_MAX, match_result
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
      if (match_result->lifecycle_update_feature_mask != 0)
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
          normalized_milan_len = updated_milan_len;
        }
      if (updated_milan)
        {
          *updated_feature = g_bytes_new_take (updated_milan,
                                               normalized_milan_len);
          updated_milan = NULL;
        }
      else
        {
          *updated_feature = g_bytes_new_take (normalized_milan,
                                               normalized_milan_len);
          normalized_milan = NULL;
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
