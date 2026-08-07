/*
 * Goodix 53x5 driver for libfprint - Milan preprocessing classification
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"
#include "milan/private.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void
milan_profile9_filter_q16 (const uint16_t *source,
                           size_t          rows,
                           size_t          columns,
                           const uint32_t *kernel,
                           size_t          kernel_size,
                           uint16_t       *output)
{
  size_t count = rows * columns;
  ptrdiff_t radius = (ptrdiff_t) kernel_size / 2;
  uint16_t *horizontal = malloc (count * sizeof(*horizontal));

  if (!horizontal)
    {
      memset (output, 0, count * sizeof(*output));
      return;
    }
  for (size_t row = 0; row < rows; row++)
    for (size_t column = 0; column < columns; column++)
      {
        uint64_t sum = 0;

        for (size_t tap = 0; tap < kernel_size; tap++)
          {
            size_t x = goodix_milan_reflect101_index (
              (ptrdiff_t) column + (ptrdiff_t) tap - radius, columns);

            sum += (uint64_t) source[row * columns + x] * kernel[tap];
          }
        horizontal[row * columns + column] = (uint16_t) (sum >> 16);
      }
  for (size_t row = 0; row < rows; row++)
    for (size_t column = 0; column < columns; column++)
      {
        uint64_t sum = 0;

        for (size_t tap = 0; tap < kernel_size; tap++)
          {
            size_t y = goodix_milan_reflect101_index (
              (ptrdiff_t) row + (ptrdiff_t) tap - radius, rows);

            sum += (uint64_t) horizontal[y * columns + column] * kernel[tap];
          }
        output[row * columns + column] = (uint16_t) (sum >> 16);
      }
  free (horizontal);
}

static void
milan_profile9_erode_valid (const uint8_t *source,
                            uint8_t       *output,
                            size_t         rows,
                            size_t         columns,
                            size_t         remove_count)
{
  memcpy (output, source, rows * columns);
  for (size_t row = 0; row < rows; row++)
    {
      size_t removed = 0;

      for (size_t column = 0; column < columns && removed < remove_count;
           column++)
        if (source[row * columns + column] != 0)
          {
            output[row * columns + column] = 0;
            removed++;
          }
      removed = 0;
      for (size_t column = columns; column-- > 0 && removed < remove_count;)
        if (source[row * columns + column] != 0)
          {
            output[row * columns + column] = 0;
            removed++;
          }
    }
  for (size_t column = 0; column < columns; column++)
    {
      size_t removed = 0;

      for (size_t row = 0; row < rows && removed < remove_count; row++)
        if (source[row * columns + column] != 0)
          {
            output[row * columns + column] = 0;
            removed++;
          }
      removed = 0;
      for (size_t row = rows; row-- > 0 && removed < remove_count;)
        if (source[row * columns + column] != 0)
          {
            output[row * columns + column] = 0;
            removed++;
          }
    }
}

static void
milan_profile9_histogram_thresholds (const uint16_t *image,
                                     const uint8_t  *valid,
                                     size_t          count,
                                     int            *low,
                                     int            *high)
{
  int minimum = INT16_MAX;
  int maximum = 0;
  int32_t histogram[256] = { 0 };

  for (size_t i = 0; i < count; i++)
    if (valid[i] != 0)
      {
        int value = (int16_t) image[i];

        if (value < minimum)
          minimum = value;
        if (value > maximum)
          maximum = value;
      }
  if (maximum <= minimum)
    {
      *low = minimum;
      *high = minimum;
      return;
    }

  int range = maximum - minimum;
  for (size_t i = 0; i < count; i++)
    if (valid[i] != 0)
      {
        int scaled = ((int16_t) image[i] - minimum) * 255;
        int bin = scaled / range;
        int remainder = scaled - bin * range;

        histogram[bin] += range - remainder;
        if (bin < 255)
          histogram[bin + 1] += remainder;
      }

  int mode_bin = 0;
  int32_t mode_weight = 0;
  int64_t total = 0;
  for (int bin = 0; bin < 256; bin++)
    {
      if (histogram[bin] > mode_weight)
        {
          mode_weight = histogram[bin];
          mode_bin = bin;
        }
      total += histogram[bin];
    }
  int64_t target_low = total * 35 / 100;
  int64_t target_high = total * 50 / 100;
  int64_t cumulative = 0;
  int low_bin = 0;
  int high_bin = 0;
  int low_found = 0;
  for (int bin = 0; bin < 256; bin++)
    {
      cumulative += histogram[bin];
      if (!low_found && cumulative >= target_low)
        {
          low_bin = bin;
          low_found = 1;
        }
      if (cumulative >= target_high)
        {
          high_bin = bin;
          break;
        }
    }
  int mode_limit = (range * mode_bin + 128) / 255 + minimum;

  if (mode_limit > 8192)
    mode_limit = 8192;
  *low = (range * low_bin + 128) / 255 + minimum;
  *high = (range * high_bin + 128) / 255 + minimum;
  if (*low > mode_limit)
    *low = mode_limit;
  if (*high > mode_limit)
    *high = mode_limit;
}

static void
milan_profile9_flood (const int16_t *admission,
                      int16_t        threshold,
                      size_t         rows,
                      size_t         columns,
                      uint8_t       *map)
{
  static const int dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  static const int dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  size_t count = rows * columns;
  int *queue = malloc (count * sizeof(*queue));

  if (!queue)
    return;
  for (size_t i = 0; i < count; i++)
    map[i] = map[i] != 0 ? 1 : 0;
  for (size_t row = 1; row + 1 < rows; row++)
    for (size_t column = 1; column + 1 < columns; column++)
      {
        size_t seed = row * columns + column;
        size_t begin = 0;
        size_t end = 0;

        if (map[seed] != 1)
          continue;
        queue[end++] = (int) seed;
        while (begin < end)
          {
            int current = queue[begin++];
            int current_x = current % (int) columns;
            int current_y = current / (int) columns;

            if (current_x < 1 || current_x >= (int) columns - 1 ||
                current_y < 1 || current_y >= (int) rows - 1)
              continue;
            for (int direction = 0; direction < 8; direction++)
              {
                int neighbor = current + dy[direction] * (int) columns +
                               dx[direction];

                if (map[neighbor] == 0 && admission[neighbor] >= threshold)
                  {
                    map[neighbor] = 2;
                    queue[end++] = neighbor;
                  }
              }
          }
      }
  for (size_t i = 0; i < count; i++)
    map[i] = map[i] != 0 ? UINT8_MAX : 0;
  free (queue);
}

static void
milan_profile9_invalid_fill (const uint16_t *image,
                             const uint8_t  *valid,
                             size_t          rows,
                             size_t          columns,
                             uint8_t        *map)
{
  static const int dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  static const int dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  size_t count = rows * columns;
  int *queue = malloc (count * sizeof(*queue));

  if (!queue)
    return;
  for (size_t row = 1; row + 1 < rows; row++)
    for (size_t column = 1; column + 1 < columns; column++)
      {
        size_t index = row * columns + column;

        if (map[index] == 0)
          continue;
        map[index] = 1;
        for (int direction = 0; direction < 8; direction++)
          if (valid[index + dy[direction] * (int) columns + dx[direction]] == 0)
            {
              map[index] = 2;
              break;
            }
      }
  for (size_t row = 1; row + 1 < rows; row++)
    for (size_t column = 1; column + 1 < columns; column++)
      {
        size_t seed = row * columns + column;
        size_t begin = 0;
        size_t end = 0;

        if (map[seed] != 2)
          continue;
        queue[end++] = (int) seed;
        while (begin < end)
          {
            int current = queue[begin++];
            int current_x = current % (int) columns;
            int current_y = current / (int) columns;

            if (current_x < 1 || current_x >= (int) columns - 1 ||
                current_y < 1 || current_y >= (int) rows - 1)
              continue;
            for (int direction = 0; direction < 8; direction++)
              {
                int neighbor = current + dy[direction] * (int) columns +
                               dx[direction];

                if (map[neighbor] == 0 && valid[neighbor] == 0 &&
                    (int16_t) image[neighbor] >= (int16_t) image[current])
                  {
                    map[neighbor] = 2;
                    queue[end++] = neighbor;
                  }
              }
          }
      }
  for (size_t i = 0; i < count; i++)
    map[i] = map[i] != 0 ? UINT8_MAX : 0;
  free (queue);
}

int
goodix_milan_profile9_build_contrast_mask (const uint16_t *normalized_live,
                                             const uint16_t *setup_map,
                                             size_t          rows,
                                             size_t          columns,
                                             uint8_t        *contrast_mask,
                                             size_t         *admitted_pixels)
{
  size_t count;
  int16_t *difference = NULL;
  int *row_mean = NULL;
  int block_min = INT_MAX;
  int block_max = INT_MIN;
  int phase_first = 0;
  int phase_last = 0;
  int phase_count = 0;
  int split;
  int high_sum = 0;
  int low_sum = 0;
  int high_count = 0;
  int low_count = 0;
  int below_count = 0;
  int threshold;
  int result = -1;

  if (!normalized_live || !setup_map || !contrast_mask || !admitted_pixels ||
      rows < 28 ||
      columns == 0 || columns > SIZE_MAX / rows ||
      rows * columns > GOODIX_MILAN_SENSOR_PIXELS)
    return -1;
  count = rows * columns;
  *admitted_pixels = 0;
  difference = malloc (count * sizeof(*difference));
  row_mean = malloc (rows * sizeof(*row_mean));
  if (!difference || !row_mean)
    goto out;

  for (size_t i = 0; i < count; i++)
    difference[i] =
      (int16_t) ((uint16_t) (setup_map[i] - normalized_live[i]));
  memset (contrast_mask, 0, count);

  /* Profile9/subtype12 estimates the split from eligible 15x16 blocks. */
  for (size_t row_start = 0; row_start < rows; row_start += 15)
    for (size_t column_start = 0; column_start < columns; column_start += 16)
      {
        int sum = 0;
        int samples = 0;

        for (size_t row = row_start;
             row < row_start + 15 && row < rows; row++)
          for (size_t column = column_start;
               column < column_start + 16 && column < columns; column++)
            {
              size_t index = row * columns + column;

              if (normalized_live[index] > 499)
                {
                  sum += difference[index];
                  samples++;
                }
            }
        int mean = sum / (samples > 0 ? samples : 1);

        if (mean < block_min)
          block_min = mean;
        if (mean > block_max)
          block_max = mean;
      }

  for (size_t row = 0; row < rows; row++)
    {
      int sum = 0;

      for (size_t column = 0; column < columns; column++)
        sum += difference[row * columns + column];
      row_mean[row] = sum / (int) columns;
    }
  for (size_t group = 6; group < rows / 4; group++)
    {
      phase_first += row_mean[group * 4];
      phase_last += row_mean[group * 4 + 3];
      phase_count++;
    }
  phase_first /= phase_count;
  phase_last /= phase_count;
  if (abs (phase_first - phase_last) > 400)
    goto postprocess;

  int span = block_max - block_min;
  if (span > 1610)
    {
      int half_span = span * 50 / 100;

      if (block_max < 4096 || block_min > 4094)
        split = block_min + half_span;
      else
        split = block_max - half_span;
    }
  else
    split = block_min - 250;

  for (size_t i = 0; i < count; i++)
    if (split < difference[i])
      {
        high_sum += difference[i];
        high_count++;
      }
    else
      {
        low_sum += difference[i];
        low_count++;
      }
  if (high_count == 0)
    goto postprocess;

  int high_mean = high_sum / high_count;
  int low_mean = low_count > 0 ? low_sum / low_count : high_mean + 600;
  int separation = high_mean - low_mean;
  if (high_count < low_count + 150)
    threshold = split + separation / 5;
  else
    threshold = split - separation / 4;

  for (size_t i = 0; i < count; i++)
    if (threshold < difference[i] && normalized_live[i] < 3800)
      contrast_mask[i] = 1;
    else if (difference[i] < threshold && normalized_live[i] < 3800)
      below_count++;
  if (below_count * 100 > (int) count * 30)
    {
      memset (contrast_mask, 0, count);
      threshold = block_min + 150;
      for (size_t i = 0; i < count; i++)
        if (threshold < difference[i] && normalized_live[i] < 3800)
          contrast_mask[i] = 1;
    }

postprocess:
  for (size_t i = 0; i < count; i++)
    *admitted_pixels += contrast_mask[i] != 0;
  for (size_t row = 0; row < rows; row++)
    for (size_t column = 0; column < columns; column++)
      {
        size_t index = row * columns + column;
        size_t up = row;
        size_t down = row;
        size_t left = column;
        size_t right = column;
        int bounded_vertical;
        int bounded_horizontal;

        if (contrast_mask[index] != 0)
          continue;
        while (up > 0 && contrast_mask[up * columns + column] == 0)
          up--;
        while (down < rows && contrast_mask[down * columns + column] == 0)
          down++;
        while (left > 0 && contrast_mask[row * columns + left] == 0)
          left--;
        while (right < columns && contrast_mask[row * columns + right] == 0)
          right++;
        bounded_vertical = contrast_mask[up * columns + column] != 0 &&
                           up >= 2 && down < rows && down <= rows - 3;
        bounded_horizontal = contrast_mask[row * columns + left] != 0 &&
                             left >= 2 && right < columns &&
                             right <= columns - 3;
        if (bounded_vertical || bounded_horizontal)
          contrast_mask[index] = 1;
      }

  for (size_t i = 0; i < count; i++)
    if (normalized_live[i] <= 100 || normalized_live[i] >= 3800)
      contrast_mask[i] = 0;
  result = 0;

out:
  free (row_mean);
  free (difference);
  return result;
}

static size_t
milan_profile9_adaptive_mask (const uint16_t *scores,
                              const uint16_t *blurred,
                              const uint8_t  *valid,
                              size_t          rows,
                              size_t          columns,
                              int             ceiling,
                              int             base_seed,
                              int             seed_scale,
                              int             grow_scale,
                              int             grow_floor,
                              uint8_t        *output)
{
  size_t count = rows * columns;
  uint64_t sum = 0;
  size_t samples = 0;
  size_t selected = 0;
  int average;
  int seed_threshold;
  int capped_seed;
  int grow_threshold;

  for (size_t i = 0; i < count; i++)
    if (scores[i] > 0 && scores[i] < ceiling)
      {
        sum += scores[i];
        samples++;
      }
  average = samples != 0 ? (int) ((sum + samples / 2) / samples) : 0;
  seed_threshold = seed_scale * average >> 8;
  capped_seed = 5 * average;
  if (capped_seed > base_seed)
    capped_seed = base_seed;
  if (seed_threshold < capped_seed)
    seed_threshold = capped_seed;
  grow_threshold = grow_scale * average >> 8;
  if (grow_threshold < average)
    grow_threshold = average;
  if (grow_threshold < grow_floor)
    grow_threshold = grow_floor;

  for (size_t i = 0; i < count; i++)
    {
      output[i] = scores[i] >= seed_threshold ? UINT8_MAX : 0;
      selected += output[i] != 0;
    }
  if (selected > 50)
    milan_profile9_flood ((const int16_t *) scores,
                          (int16_t) grow_threshold, rows, columns, output);
  milan_profile9_invalid_fill (blurred, valid, rows, columns, output);
  selected = 0;
  for (size_t i = 0; i < count; i++)
    selected += output[i] != 0;
  return selected;
}

static void
milan_profile9_density_class1 (const uint16_t *scores,
                               size_t          rows,
                               size_t          columns,
                               uint8_t        *classes)
{
  size_t count = rows * columns;
  uint16_t *box = calloc (count, sizeof(*box));
  size_t dense = 0;

  if (!box)
    return;
  for (size_t row = 0; row < rows; row++)
    for (size_t column = 0; column < columns; column++)
      {
        size_t y0 = row > 5 ? row - 5 : 0;
        size_t y1 = row + 5 < rows ? row + 5 : rows - 1;
        size_t x0 = column > 5 ? column - 5 : 0;
        size_t x1 = column + 5 < columns ? column + 5 : columns - 1;
        uint16_t sum = 0;

        for (size_t y = y0; y <= y1; y++)
          for (size_t x = x0; x <= x1; x++)
            {
              uint16_t score = scores[y * columns + x];

              sum += score > 0 && score < 50;
            }
        box[row * columns + column] = sum;
        dense += sum >= 60;
      }
  if (dense > 250)
    for (size_t i = 0; i < count; i++)
      if (box[i] >= 60 && classes[i] < 3)
        classes[i] = 1;
  free (box);
}

static void
milan_profile9_component_class1 (GoodixMilanPreprocessState *state,
                                  const uint16_t             *gradient,
                                  size_t                      rows,
                                  size_t                      columns,
                                  uint8_t                    *classes,
                                  int                         update_retained_state)
{
  static const int dx[4] = { -1, 0, 1, 0 };
  static const int dy[4] = { 0, -1, 0, 1 };
  size_t count = rows * columns;
  int *labels = malloc (count * sizeof(*labels));
  int *queue = malloc (count * sizeof(*queue));
  int component_sizes[72] = { 0 };
  int top[3] = { -2, -2, -2 };
  uint64_t sum = 0;
  size_t samples = 0;
  int average;
  int threshold;
  uint32_t age_threshold;
  int components = 0;
  int stop = 0;

  if (!labels || !queue)
    goto out;
  for (size_t i = 0; i < count; i++)
    if ((int16_t) gradient[i] > 0)
      {
        sum += gradient[i];
        samples++;
      }
  average = samples != 0 ? (int) ((sum + samples / 2) / samples) : 0;
  threshold = average * 6 / 5;
  age_threshold = state->profile9_history_update_count;
  if (age_threshold > 2)
    age_threshold--;
  if (age_threshold > 2)
    age_threshold = 2;
  for (size_t i = 0; i < count; i++)
    labels[i] = -1;

  for (size_t seed = 0; seed < count && !stop; seed++)
    {
      size_t begin;
      size_t end;

      if (labels[seed] != -1)
        continue;
      if ((int16_t) gradient[seed] <= threshold ||
          state->profile9_component_age[seed] < age_threshold)
        {
          labels[seed] = 0;
          continue;
        }
      if (components >= 71)
        break;
      components++;
      labels[seed] = components;
      begin = 0;
      end = 0;
      queue[end++] = (int) seed;
      while (begin < end)
        {
          int current = queue[begin++];
          int row = current / (int) columns;
          int column = current % (int) columns;

          component_sizes[components]++;
          for (int direction = 0; direction < 4; direction++)
            {
              int x = column + dx[direction];
              int y = row + dy[direction];
              int neighbor;

              if (x < 0 || x >= (int) columns || y < 0 || y >= (int) rows)
                continue;
              neighbor = y * (int) columns + x;
              if (labels[neighbor] != -1)
                continue;
              if ((int16_t) gradient[neighbor] <= threshold ||
                   state->profile9_component_age[neighbor] < age_threshold)
                {
                  labels[neighbor] = 0;
                  continue;
                }
              labels[neighbor] = components;
              queue[end++] = neighbor;
            }
        }
    }
  for (int slot = 0; slot < 3; slot++)
    {
      int best_size = 75;

      for (int label = 1; label <= components; label++)
        if (component_sizes[label] > best_size)
          {
            top[slot] = label;
            best_size = component_sizes[label];
          }
      if (top[slot] > 0)
        component_sizes[top[slot]] = 0;
    }
  for (size_t i = 0; i < count; i++)
    if (classes[i] == 0 &&
        (labels[i] == top[0] || labels[i] == top[1] ||
         labels[i] == top[2]))
      classes[i] = 1;

  if (update_retained_state)
    {
      for (size_t i = 0; i < count; i++)
        {
          int value = (int16_t) gradient[i];

          if (value > threshold && state->profile9_component_age[i] < 5)
            state->profile9_component_age[i]++;
          else if (value <= average && state->profile9_component_age[i] != 0)
            state->profile9_component_age[i]--;
        }
      if (state->profile9_history_update_count < 5)
        state->profile9_history_update_count++;
    }

out:
  free (queue);
  free (labels);
}

static void
milan_profile9_update_history (GoodixMilanPreprocessState *state,
                               const uint16_t             *difference,
                               const uint8_t              *contrast_mask,
                               size_t                      count)
{
  uint16_t *current = malloc (count * sizeof(*current));
  uint64_t sum = 0;
  size_t mask_count = 0;
  uint32_t mean;

  if (!current)
    return;
  if (state->profile9_history_count == 0)
    {
      state->profile9_history_count = 0;
      state->profile9_history_mask_threshold = 60;
    }
  for (size_t i = 0; i < count; i++)
    mask_count += contrast_mask[i] != 0;
  if (state->profile9_history_mask_threshold * count >= mask_count * 100)
    goto out;

  state->profile9_history_mask_threshold += 5;
  state->profile9_history_mask_average =
    ((uint32_t) (mask_count * 100 / count) +
     state->profile9_history_count * state->profile9_history_mask_average) /
    (state->profile9_history_count + 1);
  if (state->profile9_history_mask_average > 80)
    state->profile9_history_mask_threshold = 60;
  if (state->profile9_history_mask_threshold > 85)
    state->profile9_history_mask_threshold = 85;

  for (size_t i = 0; i < count; i++)
    {
      int16_t value =
        (int16_t) (((int16_t) difference[i] - 4095) * 3);

      current[i] = value > 0 ? (uint16_t) value : 0;
      sum += current[i];
    }
  mean = (uint32_t) (sum / count);
  if (mean != 0)
    for (size_t i = 0; i < count; i++)
      {
        int normalized = ((int) current[i] << 13) / (int) mean;

        state->profile9_history_reference[i] =
          (uint16_t) ((normalized +
                       (int) state->profile9_history_reference[i] *
                         (int) state->profile9_history_count) /
                      (int) (state->profile9_history_count + 1));
      }
  for (size_t i = 0; i < count; i++)
    {
      if (contrast_mask[i] == 1 && state->profile9_reference_age[i] < 50)
        state->profile9_reference_age[i]++;
      else if (contrast_mask[i] == 0 && state->profile9_reference_age[i] != 0)
        state->profile9_reference_age[i]--;
    }
  state->profile9_history_count++;
  if (state->profile9_history_count > 50)
    state->profile9_history_count = 50;

out:
  free (current);
}

static void
milan_profile9_temporal_class3 (GoodixMilanPreprocessState *state,
                                const uint8_t              *valid,
                                size_t                      rows,
                                size_t                      columns,
                                uint8_t                    *classes)
{
  static const int dx[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
  static const int dy[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
  size_t count = rows * columns;
  uint8_t *qualified = calloc (count, 1);
  uint16_t *gradient = calloc (count, sizeof(*gradient));
  int16_t *direction = malloc (count * sizeof(*direction));
  uint16_t *scores = calloc (count, sizeof(*scores));
  uint8_t *mask = calloc (count, 1);
  int threshold;
  int unused;
  uint64_t sum = 0;
  size_t samples = 0;
  size_t selected = 0;

  if (!qualified || !gradient || !direction || !scores || !mask)
    goto out;
  for (size_t row = 3; row + 3 < rows; row++)
    for (size_t column = 3; column + 3 < columns; column++)
      {
        size_t i = row * columns + column;

        qualified[i] = valid[i] != 0 &&
                       state->profile9_reference_age[i] * 100 >=
                         state->profile9_history_count * 50;
      }
  milan_profile9_histogram_thresholds (
    state->profile9_history_reference, qualified, count, &threshold, &unused);
  for (size_t row = 0; row < rows; row++)
    for (size_t column = 0; column < columns; column++)
      {
        size_t i = row * columns + column;

        direction[i] = -1;
        if (qualified[i] == 0)
          continue;
        for (int candidate = 0; candidate < 8; candidate++)
          {
            int x = (int) column + dx[candidate];
            int y = (int) row + dy[candidate];
            size_t neighbor;
            int16_t delta;

            if (x < 0 || x >= (int) columns || y < 0 || y >= (int) rows)
              continue;
            neighbor = (size_t) y * columns + (size_t) x;
            if (qualified[neighbor] == 0)
              continue;
            delta = (int16_t) ((uint16_t) state->profile9_history_reference[i] -
                               state->profile9_history_reference[neighbor]);
            if (delta > (int16_t) gradient[i])
              {
                gradient[i] = (uint16_t) delta;
                direction[i] = (int16_t) candidate;
              }
          }
      }
  for (size_t i = 0; i < count; i++)
    if (qualified[i] != 0 &&
        (int16_t) state->profile9_history_reference[i] > threshold)
      {
        int current = (int) i;
        uint16_t score = 0;

        for (int step = 0; step < 4; step++)
          {
            int candidate = direction[current];

            if (candidate < 0 ||
                (int16_t) state->profile9_history_reference[current] < threshold)
              break;
            int neighbor = current + dy[candidate] * (int) columns +
                           dx[candidate];
            if ((int16_t) state->profile9_history_reference[neighbor] < 0)
              break;
            score = (uint16_t) (score + gradient[current]);
            current = neighbor;
          }
        scores[i] = score;
      }
  for (size_t i = 0; i < count; i++)
    if (scores[i] > 0 && scores[i] < 800)
      {
        sum += scores[i];
        samples++;
      }
  if (samples != 0)
    {
      int average = (int) ((sum + samples / 2) / samples);
      int seed_threshold = 768 * average >> 8;
      int capped_seed = 5 * average;
      int grow_threshold = 384 * average >> 8;

      if (capped_seed > 1600)
        capped_seed = 1600;
      if (seed_threshold < capped_seed)
        seed_threshold = capped_seed;
      if (grow_threshold < average)
        grow_threshold = average;
      if (grow_threshold < 700)
        grow_threshold = 700;
      for (size_t i = 0; i < count; i++)
        {
          mask[i] = scores[i] >= seed_threshold ? UINT8_MAX : 0;
          selected += mask[i] != 0;
        }
      if (selected > 50)
        milan_profile9_flood ((const int16_t *) scores,
                              (int16_t) grow_threshold,
                              rows, columns, mask);
      for (size_t i = 0; i < count; i++)
        if (mask[i] != 0)
          classes[i] = 3;
    }

out:
  free (mask);
  free (scores);
  free (direction);
  free (gradient);
  free (qualified);
}

int
goodix_milan_profile9_build_broken_mask (
  GoodixMilanPreprocessState *state,
  const uint16_t             *difference,
  const uint16_t             *normalized_live,
  const uint8_t              *contrast_mask,
  size_t                      rows,
  size_t                      columns,
  uint8_t                    *broken_mask,
  uint8_t                    *class_plane,
  int                        *mode,
  int                        *apply_mask)
{
  static const uint32_t primary_kernel[3] = { 6980, 51576, 6980 };
  static const uint32_t edge_kernel[9] = {
    58, 791, 5088, 15549, 22564, 15549, 5088, 791, 58,
  };
  static const int dx[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
  static const int dy[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
  size_t count;
  uint8_t *valid = NULL;
  uint16_t *blurred = NULL;
  uint16_t *gradient = NULL;
  int16_t *direction = NULL;
  uint16_t *scores = NULL;
  uint16_t *edge_blurred = NULL;
  int16_t *edge_map = NULL;
  uint8_t *adaptive = NULL;
  uint16_t *class2_scores = NULL;
  int low;
  int high;
  size_t active_count = 0;
  int result = -1;

  if (!state || !difference || !normalized_live || !contrast_mask ||
      !broken_mask || !mode || !apply_mask || rows < 3 || columns < 3 ||
      columns > SIZE_MAX / rows || rows * columns > GOODIX_MILAN_SENSOR_PIXELS)
    return -1;
  count = rows * columns;
  for (size_t i = 0; i < count; i++)
    active_count += contrast_mask[i] != 0;
  valid = malloc (count);
  blurred = malloc (count * sizeof(*blurred));
  gradient = calloc (count, sizeof(*gradient));
  direction = malloc (count * sizeof(*direction));
  scores = calloc (count, sizeof(*scores));
  edge_blurred = malloc (count * sizeof(*edge_blurred));
  edge_map = calloc (count, sizeof(*edge_map));
  adaptive = malloc (count);
  class2_scores = calloc (count, sizeof(*class2_scores));
  if (!valid || !blurred || !gradient || !direction || !scores ||
      !edge_blurred || !edge_map || !adaptive || !class2_scores)
    goto out;

  milan_profile9_erode_valid (contrast_mask, valid, rows, columns, 8);
  milan_profile9_update_history (state, difference, contrast_mask, count);
  milan_profile9_filter_q16 (
    difference, rows, columns, primary_kernel, 3, blurred);
  milan_profile9_histogram_thresholds (blurred, valid, count, &low, &high);
  for (size_t row = 0; row < rows; row++)
    for (size_t column = 0; column < columns; column++)
      {
        size_t index = row * columns + column;

        direction[index] = -1;
        if (valid[index] == 0)
          continue;
        for (int candidate = 0; candidate < 8; candidate++)
          {
            int x = (int) column + dx[candidate];
            int y = (int) row + dy[candidate];

            if (x < 0 || x >= (int) columns || y < 0 || y >= (int) rows)
              continue;
            size_t neighbor = (size_t) y * columns + (size_t) x;
            if (valid[neighbor] == 0)
              continue;
            int16_t delta =
              (int16_t) ((uint16_t) blurred[index] - blurred[neighbor]);
            if (delta > (int16_t) gradient[index])
              {
                gradient[index] = (uint16_t) delta;
                direction[index] = (int16_t) candidate;
              }
          }
      }
  for (size_t i = 0; i < count; i++)
    if (valid[i] != 0 && (int16_t) blurred[i] > high)
      {
        int current = (int) i;
        uint16_t score = 0;

        for (int step = 0; step < 4; step++)
          {
            int candidate = direction[current];

            if (candidate < 0 || (int16_t) blurred[current] < low)
              break;
            int neighbor = current + dy[candidate] * (int) columns +
                           dx[candidate];
            if ((int16_t) blurred[neighbor] < 0)
              break;
            score = (uint16_t) (score + gradient[current]);
            current = neighbor;
          }
        scores[i] = score;
      }

  uint64_t score_sum = 0;
  size_t score_count = 0;
  for (size_t i = 0; i < count; i++)
    if (scores[i] > 0 && scores[i] < 400)
      {
        score_sum += scores[i];
        score_count++;
      }
  int average = score_count != 0
                  ? (int) ((score_sum + score_count / 2) / score_count)
                  : 0;
  int seed_threshold = (768 * average) >> 8;
  int five_average = 5 * average;
  if (five_average > 800)
    five_average = 800;
  if (seed_threshold < five_average)
    seed_threshold = five_average;
  int grow_threshold = (384 * average) >> 8;
  if (grow_threshold < average)
    grow_threshold = average;
  if (grow_threshold < 350)
    grow_threshold = 350;

  memset (broken_mask, 0, count);
  for (size_t i = 0; i < count; i++)
    broken_mask[i] = scores[i] >= seed_threshold ? UINT8_MAX : 0;
  milan_profile9_flood (
    (const int16_t *) scores, (int16_t) grow_threshold,
    rows, columns, broken_mask);
  size_t selected = 0;
  for (size_t i = 0; i < count; i++)
    selected += broken_mask[i] != 0;
  size_t severe_count = selected;
  if (selected > 50)
    {
      milan_profile9_filter_q16 (
        blurred, rows, columns, edge_kernel, 9, edge_blurred);
      for (size_t row = 1; row + 1 < rows; row++)
        for (size_t column = 1; column + 1 < columns; column++)
          {
            size_t index = row * columns + column;
            int center = (int16_t) edge_blurred[index] - 20;

            if (valid[index] == 0 || (int16_t) scores[index] < 100)
              continue;
            if (((int16_t) edge_blurred[index - 1] < center &&
                 (int16_t) edge_blurred[index + 1] < center) ||
                ((int16_t) edge_blurred[index - columns] < center &&
                 (int16_t) edge_blurred[index + columns] < center) ||
                ((int16_t) edge_blurred[index - columns - 1] < center &&
                 (int16_t) edge_blurred[index + columns + 1] < center) ||
                ((int16_t) edge_blurred[index - columns + 1] < center &&
                 (int16_t) edge_blurred[index + columns - 1] < center))
              edge_map[index] = UINT8_MAX;
          }
      milan_profile9_flood (edge_map, 1, rows, columns, broken_mask);
    }
  milan_profile9_invalid_fill (
    blurred, valid, rows, columns, broken_mask);

  for (size_t i = 0; i < count; i++)
    broken_mask[i] = broken_mask[i] != 0 ? 3 : 0;

  if (severe_count <= 300)
    {
      milan_profile9_adaptive_mask (
        scores, blurred, valid, rows, columns, 300, 240, 640, 448, 150,
        adaptive);
      for (size_t i = 0; i < count; i++)
        if (adaptive[i] != 0 && broken_mask[i] < 3)
          broken_mask[i] = 1;
      selected = 0;
      for (size_t i = 0; i < count; i++)
        selected += broken_mask[i] != 0;
      if (selected > 150)
        milan_profile9_density_class1 (scores, rows, columns, broken_mask);
    }

  milan_profile9_component_class1 (
    state, gradient, rows, columns, broken_mask,
    active_count * 100 >= count * 75);

  for (size_t i = 0; i < count; i++)
    if (valid[i] != 0 && (int16_t) blurred[i] < low)
      {
        int current = (int) i;
        uint16_t score = 0;

        for (int step = 0; step < 4; step++)
          {
            int candidate = direction[current];

            if (candidate < 0)
              break;
            score = (uint16_t) (score + gradient[current]);
            current += dy[candidate] * (int) columns + dx[candidate];
          }
        class2_scores[i] = score;
      }
  score_sum = 0;
  score_count = 0;
  for (size_t i = 0; i < count; i++)
    if (class2_scores[i] > 0 && class2_scores[i] < 400)
      {
        score_sum += class2_scores[i];
        score_count++;
      }
  average = score_count != 0
              ? (int) ((score_sum + score_count / 2) / score_count)
              : 0;
  int class2_threshold = average * 512 >> 8;
  if (score_count != 0 && class2_threshold < 400)
    class2_threshold = 400;
  for (size_t i = 0; i < count; i++)
    if (class2_scores[i] > class2_threshold && broken_mask[i] < 3)
      broken_mask[i] = 2;

  if (state->profile9_history_count > 20)
    milan_profile9_temporal_class3 (
      state, valid, rows, columns, broken_mask);

  for (size_t row = 3; row + 3 < rows; row++)
    for (size_t column = 3; column + 3 < columns; column++)
      {
        size_t index = row * columns + column;

        if (normalized_live[index] < 50)
          broken_mask[index] = 3;
      }

  size_t class1_count = 0;
  size_t class2_count = 0;
  size_t class3_count = 0;
  size_t mask_count = 0;
  for (size_t i = 0; i < count; i++)
    {
      class1_count += broken_mask[i] == 1;
      class2_count += broken_mask[i] == 2;
      class3_count += broken_mask[i] == 3;
      mask_count += contrast_mask[i] != 0;
      if (class_plane)
        class_plane[i] = broken_mask[i];
      if (broken_mask[i] != 3)
        broken_mask[i] = 0;
    }
  state->profile9_class_counts.profile9_class1_count = (uint32_t) class1_count;
  state->profile9_class_counts.profile9_class2_count = (uint32_t) class2_count;
  state->profile9_class_counts.profile9_class3_count = (uint32_t) class3_count;
  *mode = 0;
  *apply_mask = 0;
  if (class3_count * 100 > mask_count * 15)
    {
      *mode = 8;
      *apply_mask = 1;
    }
  else if (class3_count > 300)
    {
      *mode = 7;
      *apply_mask = 1;
    }
  else if ((class1_count + class2_count + class3_count) * 100 >
           mask_count * 25)
    {
      *mode = 7;
      if (class3_count > 150)
        *apply_mask = 1;
    }
  else if (class3_count > 150)
    {
      *mode = 6;
      *apply_mask = 1;
    }
  else if (class2_count > 600 || class1_count > 500)
    *mode = 6;
  else if (class2_count > 300 || class1_count > 300 ||
           class1_count + class2_count > 300)
    *mode = 5;
  result = 0;

out:
  free (class2_scores);
  free (adaptive);
  free (edge_map);
  free (edge_blurred);
  free (scores);
  free (direction);
  free (gradient);
  free (blurred);
  free (valid);
  return result;
}
