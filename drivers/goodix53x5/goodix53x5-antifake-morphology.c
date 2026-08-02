/*
 * Goodix 53x5 driver for libfprint - Milan anti-fake morphology
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "goodix53x5-milan.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
goodix_milan_antifake_class_map (
  const uint8_t *image,
  const uint8_t *mask,
  size_t         rows,
  size_t         columns,
  uint8_t       *classes)
{
  static const int8_t dx[4] = {-1, 1, 0, 0};
  static const int8_t dy[4] = { 0, 0,-1, 1};
  int32_t heads[256];
  int32_t *next = NULL;
  size_t count;
  int current_priority = 0;
  int result = -1;

  if (!image || !mask || !classes || rows < 2 || columns < 2 ||
      columns > SIZE_MAX / rows)
    return -1;
  count = rows * columns;
  next = malloc (count * sizeof(*next));
  if (!next)
    return -1;
  for (size_t i = 0; i < 256; i++)
    heads[i] = -1;
  for (size_t i = 0; i < count; i++)
    {
      classes[i] = image[i] >= 0xe7 ? 1 : image[i] < 0x1a ? 2 : 0;
      if (mask[i] == 0)
        classes[i] = 0xfe;
      next[i] = -1;
    }
  memset (classes, 0xfe, columns);
  memset (classes + (rows - 1) * columns, 0xfe, columns);
  for (size_t row = 1; row + 1 < rows; row++)
    {
      classes[row * columns] = 0xfe;
      classes[row * columns + columns - 1] = 0xfe;
    }

  for (size_t row = 1; row + 1 < rows; row++)
    for (size_t column = 1; column + 1 < columns; column++)
      {
        size_t pixel = row * columns + column;
        int priority = 256;
        int adjacent = 0;

        if (classes[pixel] != 0)
          continue;
        for (size_t direction = 0; direction < 4; direction++)
          {
            size_t neighbor =
              (size_t) ((ptrdiff_t) row + dy[direction]) * columns +
              (size_t) ((ptrdiff_t) column + dx[direction]);

            if (classes[neighbor] != 0)
              {
                int difference = (int) image[pixel] - image[neighbor];
                if (difference < 0)
                  difference = -difference;
                if (difference < priority)
                  priority = difference;
                adjacent++;
              }
          }
        if (adjacent > 0 && priority < 256)
          {
            next[pixel] = heads[priority];
            heads[priority] = (int32_t) pixel;
            classes[pixel] = 0xfd;
          }
      }

  while (current_priority < 256 && heads[current_priority] == -1)
    current_priority++;
  while (current_priority < 256)
    {
      int32_t pixel_index = heads[current_priority];
      size_t pixel = (size_t) pixel_index;
      size_t row = pixel / columns;
      size_t column = pixel % columns;
      uint8_t label = 0;

      heads[current_priority] = next[pixel];
      next[pixel] = -1;
      for (size_t direction = 0; direction < 4; direction++)
        {
          size_t neighbor =
            (size_t) ((ptrdiff_t) row + dy[direction]) * columns +
            (size_t) ((ptrdiff_t) column + dx[direction]);
          uint8_t neighbor_label = classes[neighbor];

          if (neighbor_label != 0)
            {
              if (label != 0 && label != neighbor_label)
                label = 0xff;
              else
                label = neighbor_label;
            }
        }
      if (label != 0)
        classes[pixel] = label;
      if (label != 0xff)
        for (size_t direction = 0; direction < 4; direction++)
          {
            size_t neighbor =
              (size_t) ((ptrdiff_t) row + dy[direction]) * columns +
              (size_t) ((ptrdiff_t) column + dx[direction]);

            if (classes[neighbor] == 0)
              {
                int priority = (int) image[pixel] - image[neighbor];
                if (priority < 0)
                  priority = -priority;
                next[neighbor] = heads[priority];
                heads[priority] = (int32_t) neighbor;
                classes[neighbor] = 0xfd;
                if (priority < current_priority)
                  current_priority = priority;
              }
          }
      while (current_priority < 256 && heads[current_priority] == -1)
        current_priority++;
    }
  result = 0;

  free (next);
  return result;
}

static int
antifake_thin_delete (const uint8_t *pixels,
                      size_t         columns,
                      size_t         pixel,
                      int            second_phase)
{
  const size_t neighbors[8] = {
    pixel - columns, pixel - columns + 1, pixel + 1,
    pixel + columns + 1, pixel + columns, pixel + columns - 1,
    pixel - 1, pixel - columns - 1,
  };
  int active = 0;
  int transitions = 0;

  for (size_t i = 0; i < 8; i++)
    {
      int current = pixels[neighbors[i]] != 0;
      int next_value = pixels[neighbors[(i + 1) % 8]] != 0;

      active += current;
      transitions += !current && next_value;
    }
  if (active < 2 || active > 6 || transitions != 1)
    return 0;
  int north = pixels[neighbors[0]] != 0;
  int east = pixels[neighbors[2]] != 0;
  int south = pixels[neighbors[4]] != 0;
  int west = pixels[neighbors[6]] != 0;

  return second_phase
           ? !(north && east && west) && !(north && south && west)
           : !(north && east && south) && !(east && south && west);
}

static int
antifake_thin (uint8_t *pixels,
               size_t   rows,
               size_t   columns,
               size_t   maximum_iterations)
{
  size_t count = rows * columns;
  uint8_t *source = malloc (count);

  if (!source)
    return -1;
  for (size_t iteration = 0; iteration < maximum_iterations; iteration++)
    {
      int second_phase_changed = 0;

      memcpy (source, pixels, count);
      for (size_t row = 1; row + 1 < rows; row++)
        for (size_t column = 1; column + 1 < columns; column++)
          {
            size_t pixel = row * columns + column;

            if (source[pixel] != 0 &&
                antifake_thin_delete (source, columns, pixel, 0))
              pixels[pixel] = 0;
          }
      memcpy (source, pixels, count);
      for (size_t row = 1; row + 1 < rows; row++)
        for (size_t column = 1; column + 1 < columns; column++)
          {
            size_t pixel = row * columns + column;

            if (source[pixel] != 0 &&
                antifake_thin_delete (source, columns, pixel, 1))
              {
                pixels[pixel] = 0;
                second_phase_changed = 1;
              }
          }
      if (!second_phase_changed)
        break;
    }
  free (source);
  return 0;
}

int
goodix_milan_antifake_boundary_score (
  const uint16_t *residual,
  const uint8_t  *classes,
  size_t          rows,
  size_t          columns,
  uint8_t        *thinned,
  int32_t        *score)
{
  uint32_t maximum = 0;
  uint32_t difference_sum = 0;
  uint32_t adjacent_count = 0;

  if (!residual || !classes || !thinned || !score || rows < 7 ||
      columns < 7 || columns > SIZE_MAX / rows)
    return -1;
  for (size_t i = 0; i < rows * columns; i++)
    thinned[i] = classes[i] == 2 ? 2 : 0;
  for (size_t row = 1; row + 1 < rows; row++)
    for (size_t column = 2; column + 2 < columns; column++)
      {
        size_t pixel = row * columns + column;

        if (classes[pixel] != 0xfe && maximum < residual[pixel])
          maximum = residual[pixel];
      }
  if (antifake_thin (thinned, rows, columns, 100) != 0)
    return -1;

  const ptrdiff_t offsets[8] = {
    -(ptrdiff_t) columns - 1, -(ptrdiff_t) columns,
    1 - (ptrdiff_t) columns, 1, (ptrdiff_t) columns,
    (ptrdiff_t) columns + 1, (ptrdiff_t) columns - 1, -1,
  };
  for (size_t row = 3; row + 3 < rows; row++)
    for (size_t column = 3; column + 3 < columns; column++)
      {
        size_t pixel = row * columns + column;

        if (thinned[pixel] != 2)
          continue;
        for (size_t direction = 0; direction < 8; direction++)
          {
            size_t neighbor = (size_t) ((ptrdiff_t) pixel +
                                        offsets[direction]);

            if (thinned[neighbor] == 2)
              {
                uint32_t left = residual[pixel];
                uint32_t right = residual[neighbor];

                adjacent_count++;
                difference_sum += left < right ? right - left : left - right;
              }
          }
      }
  uint32_t divisor = adjacent_count * maximum;
  uint32_t numerator = difference_sum * 1000;

  *score = divisor == 0
             ? (int32_t) numerator
             : (int32_t) (((int64_t) (int32_t) numerator +
                            (int32_t) divisor / 2) /
                           (int32_t) divisor);
  return 0;
}
