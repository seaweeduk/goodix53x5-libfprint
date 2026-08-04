/*
 * Goodix 53x5 driver for libfprint - Milan match score finalization
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"

int32_t
goodix_milan_match_secondary_result (int32_t primary_score,
                                             int32_t secondary_score,
                                             int32_t secondary_detail)
{
  return secondary_score > primary_score && secondary_detail > 195
           ? secondary_score
           : primary_score;
}

int
goodix_milan_match_overlap_result (
  const int32_t classes[4],
  int32_t       valid_count,
  int32_t       full_count,
  int32_t       mode,
  const int32_t weights[3],
  int32_t       context_count,
  int32_t      *context_confidence,
  int32_t      *score,
  int32_t      *coverage,
  int32_t      *detail)
{
  int32_t raw_score;
  int32_t quality;

  if (!classes || !weights || !score || valid_count < 0 || full_count <= 0 ||
      classes[0] < 0 || classes[1] < 0 || classes[2] < 0 || classes[3] < 0 ||
      (int64_t) classes[0] + classes[1] + classes[2] + classes[3] !=
        valid_count)
    return -1;
  if (coverage)
    *coverage = (full_count / 2 + valid_count * 0x100) / full_count;
  if (detail)
    {
      int32_t non_match_total = classes[0] + classes[1] + classes[2];
      int32_t result = non_match_total > 0
                         ? (non_match_total / 2 + classes[0] * 0x100) /
                             (non_match_total + 1)
                         : 0;
      int32_t match_fraction =
        (valid_count / 2 + classes[3] * 0x100) / (valid_count + 1);

      if (match_fraction < 15)
        result -= ((15 - match_fraction) >> 1) + 3;
      *detail = result;
    }
  if (valid_count == 0)
    {
      *score = 128;
      return 0;
    }
  if (full_count / 2 < valid_count)
    raw_score = weights[0] + 38 +
                (valid_count / 2 + classes[0] * 0x100) / valid_count;
  else
    raw_score = (valid_count / 2 + classes[0] * 0x100) /
                  (valid_count + 1) +
                valid_count * 19 / (full_count / 2) + weights[1] + 19;
  quality = (valid_count / 2 + classes[3] * 0x100) / valid_count + weights[2];
  if (mode == 0)
    {
      *score = quality > 23 || (raw_score > 230 && quality > 16)
                 ? raw_score
                 : 128;
      return 0;
    }
  if (!context_confidence)
    return -1;
  if (*context_confidence < 0)
    *context_confidence = classes[3] * 0x100 /
                          (classes[1] + classes[2] + classes[3] + 1);
  *score = quality > 23 ||
             (quality > 18 && *context_confidence > 62) ||
             (quality > 19 && *context_confidence > 50) ||
             (quality > 17 && ((*context_confidence > 40 &&
                                 context_count > 7) ||
                                context_count > 8)) ||
             (raw_score > 230 && quality > 16)
             ? raw_score
             : 128;
  return 0;
}

int
goodix_milan_match_final_score (const int32_t metrics[15],
                                       uint32_t      template_type,
                                       int           alternate_policy,
                                       int32_t      *score)
{
  int32_t detail;
  int32_t low_detail;
  int32_t combined_detail;
  int32_t filtered_count;
  int32_t coverage;
  int32_t denominator = 31;

  if (!metrics || !score)
    return -1;
  *score = 0;
  if (template_type < 23 &&
      ((0x413000U >> (template_type & 31)) & 1) != 0)
    denominator = 42;

  filtered_count = metrics[1];
  detail = metrics[5];
  low_detail = metrics[8];
  coverage = metrics[9];
  if (template_type == 21 || template_type == 11)
    {
      int32_t penalty = metrics[12] + metrics[13] + metrics[14] +
                        (metrics[0] < 5);

      detail -= penalty * 3;
      low_detail -= penalty * 3;
    }
  combined_detail = detail + low_detail;

  if (alternate_policy)
    {
      if (metrics[0] < filtered_count)
        {
          detail -= 2;
          low_detail -= 2;
        }
      if (filtered_count < 8 ||
          detail + low_detail -
            (metrics[12] + metrics[13] + metrics[14]) * 4 < 417 ||
          coverage < 96)
        return 0;
    }
  else if (template_type == 21 || template_type == 11)
    {
      if (((metrics[0] < 5 || filtered_count < 8 || combined_detail < 425 ||
            metrics[11] < 42) &&
           (metrics[0] < 3 || metrics[4] < 235 || coverage < 120 ||
            combined_detail < 400)) &&
          (metrics[0] < 6 || coverage < 61 || combined_detail < 415 ||
           metrics[11] < 41))
        return 0;
      *score = (((filtered_count * 256 + 15) / 31) * 100) >> 8;
      return 0;
    }
  else
    {
      int weak_first = detail < 226 || low_detail < 176 ||
                       coverage < 91;
      int weak_second = detail < 218 || low_detail < 176 ||
                        coverage < 121;
      int weak_seven = filtered_count < 7 || detail < 216 ||
                       low_detail < 176 || coverage < 121;
      int weak_eight = filtered_count < 8 || detail < 209 ||
                       low_detail < 191 || coverage < 106;
      int weak_nine = filtered_count < 9 || combined_detail < 416 || coverage < 41;

      if ((filtered_count < 5 || (weak_first && weak_second)) &&
          weak_seven && weak_eight && weak_nine &&
          (filtered_count < 8 || combined_detail < 426 || coverage < 41))
        return 0;
    }

  *score = ((((denominator >> 1) + filtered_count * 256) / denominator) *
            100) >> 8;
  return 0;
}

int
goodix_milan_match_select_first_positive (const int32_t *scores,
                                            size_t         score_count,
                                            size_t        *matched_index,
                                            int32_t       *selected_score)
{
  if (!scores || score_count == 0 || !matched_index || !selected_score)
    return -1;

  *matched_index = SIZE_MAX;
  for (size_t i = 0; i < score_count; i++)
    {
      *selected_score = scores[i];
      if (scores[i] > 0)
        {
          *matched_index = i;
          break;
        }
    }
  return 0;
}
