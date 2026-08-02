/*
 * Goodix 53x5 driver for libfprint - Milan preprocessing selection
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "goodix53x5-milan.h"

#include <limits.h>
#include <string.h>

int
goodix_milan_preprocess_selection_metric (const uint8_t *frame,
                                       const uint8_t *mask,
                                       size_t         rows,
                                       size_t         columns)
{
  size_t count;
  uint64_t valid_sum = 0;
  size_t valid_count = 0;
  size_t first_row = 0;
  size_t last_row = 0;
  int found_first = 0;
  int previous_average = 0;
  uint64_t difference_sum = 0;

  if (!frame || !mask || rows == 0 || columns == 0 ||
      columns > SIZE_MAX / rows || rows > INT_MAX || columns > INT_MAX)
    return -1;

  count = rows * columns;
  for (size_t i = 0; i < count; i++)
    {
      if (mask[i] != 0)
        {
          valid_sum += frame[i];
          valid_count++;
          if (!found_first)
            {
              first_row = i / columns;
              found_first = 1;
            }
          last_row = i / columns;
        }
    }

  if (!found_first)
    return 0;

  uint32_t fallback = (uint32_t) (valid_sum / (valid_count + 1));
  for (size_t row = first_row; row <= last_row; row++)
    {
      uint32_t row_sum = 0;

      for (size_t column = 0; column < columns; column++)
        {
          size_t index = row * columns + column;

          row_sum += mask[index] != 0 ? frame[index] : fallback;
        }

      int average = (int) ((row_sum << 8) / columns);
      if (row != first_row)
        difference_sum += average > previous_average
                            ? (uint32_t) (average - previous_average)
                            : (uint32_t) (previous_average - average);
      previous_average = average;
    }

  if (last_row == first_row)
    return (int) difference_sum;
  return (int) ((((rows - 1) * difference_sum) /
                  (last_row - first_row)) >> 8);
}

static uint32_t
integer_square_root (uint64_t value)
{
  uint32_t root = 0;
  uint32_t bit = UINT32_C(0x80000000);

  if (value < 2)
    return (uint32_t) value;
  for (int shift = 31; bit != 0; shift--, bit >>= 1)
    {
      uint64_t candidate = ((uint64_t) root * 2 + bit) << shift;

      if (candidate <= value)
        {
          root += bit;
          value -= candidate;
        }
    }
  return root;
}

int
goodix_milan_preprocess_masked_correlation (const uint8_t *first,
                                         const uint8_t *second,
                                         const uint8_t *mask,
                                         size_t         rows,
                                         size_t         columns)
{
  size_t count;
  uint64_t first_sum = 0;
  uint64_t second_sum = 0;
  int64_t covariance = 0;
  uint64_t first_variance = 0;
  uint64_t second_variance = 0;

  if (!first || !second || !mask || rows == 0 || columns == 0 ||
      columns > SIZE_MAX / rows)
    return -1;

  count = rows * columns;
  for (size_t i = 0; i < count; i++)
    {
      first_sum += mask[i] != 0 ? first[i] & mask[i] : 122;
      second_sum += mask[i] != 0 ? second[i] & mask[i] : 122;
    }

  int first_mean = (int) (first_sum / count);
  int second_mean = (int) (second_sum / count);
  for (size_t i = 0; i < count; i++)
    {
      int first_value = (mask[i] != 0 ? first[i] & mask[i] : 122) -
                        first_mean;
      int second_value = (mask[i] != 0 ? second[i] & mask[i] : 122) -
                         second_mean;

      covariance += first_value * second_value;
      first_variance += (uint32_t) (first_value * first_value);
      second_variance += (uint32_t) (second_value * second_value);
    }

  uint32_t denominator =
    integer_square_root (first_variance * second_variance);
  if (denominator == 0)
    return 0;
  return (int) ((covariance * 256) / denominator);
}

int
goodix_milan_preprocess_select_output (const uint8_t *contrast,
                                   const uint8_t *refined,
                                   const uint8_t *mask,
                                   size_t         rows,
                                   size_t         columns,
                                   int            threshold,
                                   uint8_t       *output,
                                   int           *selected_refined)
{
  size_t count;
  int contrast_metric;
  int refined_metric;
  int correlation;
  int required_metric;

  if (!contrast || !refined || !mask || !output || !selected_refined ||
      threshold < 0 || rows == 0 || columns == 0 ||
      columns > SIZE_MAX / rows)
    return -1;

  count = rows * columns;
  contrast_metric = goodix_milan_preprocess_selection_metric (
    contrast, mask, rows, columns);
  if (contrast_metric < 0)
    return -1;

  memmove (output, contrast, count);
  *selected_refined = 0;
  if (contrast_metric < threshold)
    return 0;

  refined_metric = goodix_milan_preprocess_selection_metric (
    refined, mask, rows, columns);
  correlation = goodix_milan_preprocess_masked_correlation (
    contrast, refined, mask, rows, columns);
  if (refined_metric < 0)
    return -1;

  if (contrast_metric < 1800)
    required_metric = threshold * 205 >> 8;
  else if (contrast_metric < 3600)
    required_metric = (((contrast_metric - 1800) * threshold * 179) / 1800 +
                       threshold * 205) >> 8;
  else if (contrast_metric < 5400)
    required_metric = ((contrast_metric - 3600) * threshold * 2) / 1800 +
                      (threshold * 384 >> 8);
  else
    required_metric = INT_MAX;

  if (refined_metric >= required_metric ||
      (contrast_metric < 1800 && correlation >= 235))
    return 0;

  memmove (output, refined, count);
  *selected_refined = 1;
  return 0;
}
