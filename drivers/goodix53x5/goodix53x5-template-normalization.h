/*
 * Goodix 53x5 driver for libfprint - Milan learned-feature normalization
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline int32_t
goodix_milan_template_normalization_from_bits (uint32_t bits)
{
  int32_t value;

  memcpy (&value, &bits, sizeof(value));
  return value;
}

static inline uint32_t
goodix_milan_template_normalization_to_bits (int32_t value)
{
  uint32_t bits;

  memcpy (&bits, &value, sizeof(bits));
  return bits;
}

static inline int32_t
goodix_milan_template_normalization_add (int32_t left,
                                         int32_t right)
{
  return goodix_milan_template_normalization_from_bits (
    goodix_milan_template_normalization_to_bits (left) +
    goodix_milan_template_normalization_to_bits (right));
}

static inline int32_t
goodix_milan_template_normalization_multiply (int32_t left,
                                              int32_t right)
{
  return goodix_milan_template_normalization_from_bits (
    goodix_milan_template_normalization_to_bits (left) *
    goodix_milan_template_normalization_to_bits (right));
}

static inline int32_t
goodix_milan_template_normalization_sar1 (int32_t value)
{
  uint32_t bits = goodix_milan_template_normalization_to_bits (value);

  return goodix_milan_template_normalization_from_bits (
    (bits >> 1) | (bits & UINT32_C (0x80000000)));
}

static inline int32_t
goodix_milan_template_normalization_domain_area (int32_t rows,
                                                 int32_t columns,
                                                 int32_t half_resolution)
{
  if (half_resolution)
    {
      rows = goodix_milan_template_normalization_sar1 (rows);
      columns = goodix_milan_template_normalization_sar1 (columns);
    }
  return goodix_milan_template_normalization_multiply (rows, columns);
}

static inline int
goodix_milan_template_normalization_overlap_qualifies (int32_t area,
                                                       int32_t rows,
                                                       int32_t columns,
                                                       int32_t half_resolution)
{
  int32_t scaled_area = half_resolution
                          ? goodix_milan_template_normalization_multiply (area, 4)
                          : area;
  int32_t left = goodix_milan_template_normalization_multiply (scaled_area, 100);
  int32_t right = goodix_milan_template_normalization_multiply (
    goodix_milan_template_normalization_multiply (rows, columns), 40);

  return left > right;
}

static inline int32_t
goodix_milan_template_normalization_remove_footprint (
  uint8_t      *residual,
  int32_t       destination_rows,
  int32_t       destination_columns,
  int32_t       source_rows,
  int32_t       source_columns,
  const int32_t transform[6])
{
  int32_t maximum_x;
  int32_t maximum_y;
  int32_t area = 0;

  if (!transform || destination_rows <= 0 || destination_columns <= 0 ||
      source_rows <= 0 || source_columns <= 0)
    return 0;
  maximum_x = goodix_milan_template_normalization_multiply (source_columns - 1, 256);
  maximum_y = goodix_milan_template_normalization_multiply (source_rows - 1, 256);
  for (int32_t y = 0; y < destination_rows; y++)
    for (int32_t x = 0; x < destination_columns; x++)
      {
        int32_t mapped_x = goodix_milan_template_normalization_add (
          goodix_milan_template_normalization_add (
            goodix_milan_template_normalization_multiply (transform[0], x),
            goodix_milan_template_normalization_multiply (transform[1], y)),
          transform[2]);
        int32_t mapped_y = goodix_milan_template_normalization_add (
          goodix_milan_template_normalization_add (
            goodix_milan_template_normalization_multiply (transform[3], x),
            goodix_milan_template_normalization_multiply (transform[4], y)),
          transform[5]);

        if (mapped_x < 0 || mapped_x > maximum_x ||
            mapped_y < 0 || mapped_y > maximum_y)
          continue;
        area = goodix_milan_template_normalization_add (area, 1);
        if (residual)
          residual[(size_t) y * (size_t) destination_columns + (size_t) x] = 0;
      }
  return area;
}

static inline int32_t
goodix_milan_template_normalization_residual (const uint8_t *residual,
                                              int32_t        rows,
                                              int32_t        columns)
{
  int32_t count = 0;

  if (!residual || rows <= 0 || columns <= 0)
    return 0;
  for (int32_t y = 0; y < rows; y++)
    for (int32_t x = 0; x < columns; x++)
      count = goodix_milan_template_normalization_add (
        count, residual[(size_t) y * (size_t) columns + (size_t) x] != 0);
  return count < 20 ? 0 : count;
}
