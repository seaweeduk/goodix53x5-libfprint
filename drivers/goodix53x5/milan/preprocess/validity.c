/*
 * Goodix 53x5 driver for libfprint - Milan preprocessing validity
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"

#include <limits.h>
#include <stdint.h>

int
goodix_milan_preprocess_quality_mask_coverage (const uint8_t *mask,
                                           size_t         count)
{
  size_t selected = 0;

  if (!mask || count == 0 || count > INT_MAX)
    return -1;
  for (size_t i = 0; i < count; i++)
    selected += mask[i] != 0;
  return (int) ((selected * UINT32_C(0x10000)) / count);
}

int
goodix_milan_preprocess_quality_valid_mask (const uint8_t *frame,
                                        size_t         rows,
                                        size_t         columns,
                                        uint8_t       *mask,
                                        int           *valid_score)
{
  size_t count;
  size_t valid = 0;

  if (!frame || !mask || !valid_score || rows == 0 || columns == 0 ||
      columns > SIZE_MAX / rows)
    return -1;

  count = rows * columns;
  if (count > INT_MAX)
    return -1;

  for (size_t row = 0; row < rows; row++)
    {
      for (size_t column = 0; column < columns; column++)
        {
          size_t index = row * columns + column;
          int invalid = frame[index] == UINT8_MAX;

          valid += !invalid;
          if (invalid &&
              (column < 1 || frame[index - 1] == UINT8_MAX) &&
              (column + 1 >= columns || frame[index + 1] == UINT8_MAX) &&
              (row < 1 || frame[index - columns] == UINT8_MAX) &&
              (row + 1 >= rows || frame[index + columns] == UINT8_MAX) &&
              (column < 2 || frame[index - 2] == UINT8_MAX) &&
              (column + 2 >= columns || frame[index + 2] == UINT8_MAX) &&
              (row < 2 || frame[index - columns * 2] == UINT8_MAX) &&
              (row + 2 >= rows || frame[index + columns * 2] == UINT8_MAX))
            mask[index] = 0;
          else
            mask[index] = UINT8_MAX;
        }
    }

  *valid_score = (int) ((valid * UINT32_C(0x10000)) / count);
  return 0;
}
