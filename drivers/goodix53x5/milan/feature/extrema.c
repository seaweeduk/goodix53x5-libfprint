/*
 * Goodix 53x5 driver for libfprint - Milan feature extrema refinement
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t
goodix_milan_feature_collect_extrema (
  const uint16_t             *scales,
  size_t                      rows,
  size_t                      columns,
  GoodixMilanFeatureExtremum *extrema,
  size_t                      capacity)
{
  size_t count;
  size_t result_count = 0;

  if (!scales || rows < 13 || columns < 13 || columns > SIZE_MAX / rows)
    return 0;
  count = rows * columns;
  for (size_t scale = 1; scale < 4; scale++)
    for (size_t row = 6; row + 6 < rows; row++)
      for (size_t column = 6; column + 6 < columns; column++)
        {
          size_t pixel = row * columns + column;
          int32_t response = (int16_t) (
            scales[(scale + 1) * count + pixel] -
            scales[scale * count + pixel]);
          int32_t absolute = response < 0 ? -response : response;
          int extremum = absolute > 0x148 && response != 0;

          for (ptrdiff_t adjacent = -1; extremum && adjacent <= 1; adjacent++)
            for (ptrdiff_t delta_row = -1;
                 extremum && delta_row <= 1;
                 delta_row++)
              for (ptrdiff_t delta_column = -1; delta_column <= 1;
                   delta_column++)
                {
                  if (adjacent == 0 && delta_row == 0 && delta_column == 0)
                    continue;
                  size_t neighbor =
                    (size_t) ((ptrdiff_t) row + delta_row) * columns +
                    (size_t) ((ptrdiff_t) column + delta_column);
                  size_t neighbor_scale =
                    (size_t) ((ptrdiff_t) scale + adjacent);
                  int32_t neighbor_response =
                    (int32_t) scales[(neighbor_scale + 1) * count + neighbor] -
                    (int32_t) scales[neighbor_scale * count + neighbor];

                  if ((response > 0 && neighbor_response > response) ||
                      (response < 0 && neighbor_response < response))
                    extremum = 0;
                }
          if (!extremum)
            continue;
          if (result_count < capacity && extrema)
            extrema[result_count] = (GoodixMilanFeatureExtremum) {
              (int32_t) column, (int32_t) row, (int32_t) scale, response,
            };
          result_count++;
        }
  return result_count;
}

static int
feature_build_refinement_derivatives (const uint16_t *scales,
                                      size_t          rows,
                                      size_t          columns,
                                      int32_t         x,
                                      int32_t         y,
                                      int32_t         scale,
                                      int16_t         hessian[6],
                                      int16_t         gradient[3],
                                      int16_t        *center)
{
  size_t count = rows * columns;
  size_t pixel = (size_t) y * columns + (size_t) x;
  const uint16_t *previous = scales + (size_t) (scale - 1) * count;
  const uint16_t *current = scales + (size_t) scale * count;
  const uint16_t *next = scales + (size_t) (scale + 1) * count;
  const uint16_t *next2 = scales + (size_t) (scale + 2) * count;
  int32_t value;

  *center = (int16_t) (2 * ((int32_t) next[pixel] - current[pixel]));
  value = 2 * (((int32_t) current[pixel - 1] - next[pixel - 1]) -
               current[pixel + 1] + next[pixel + 1]);
  if (value <= -0x8000 || value >= 0x8000)
    return 0;
  gradient[0] = (int16_t) value;
  value = 2 * (((int32_t) current[pixel - columns] -
                next[pixel - columns]) -
               current[pixel + columns] + next[pixel + columns]);
  if (value <= -0x8000 || value >= 0x8000)
    return 0;
  gradient[1] = (int16_t) value;
  value = 2 * (((int32_t) previous[pixel] - current[pixel]) - next[pixel] +
               next2[pixel]);
  if (value <= -0x8000 || value >= 0x8000)
    return 0;
  gradient[2] = (int16_t) value;

  value = 4 * (((int32_t) next[pixel - 1] - current[pixel - 1]) -
               current[pixel + 1] - *center + next[pixel + 1]);
  if (value <= -0x8000 || value >= 0x8000)
    return 0;
  hessian[0] = (int16_t) value;
  value = 4 * (((int32_t) next[pixel - columns] -
                current[pixel - columns]) -
               *center - current[pixel + columns] + next[pixel + columns]);
  if (value <= -0x8000 || value >= 0x8000)
    return 0;
  hessian[1] = (int16_t) value;
  value = 4 * (((int32_t) current[pixel] - previous[pixel]) - next[pixel] -
               *center + next2[pixel]);
  if (value <= -0x8000 || value >= 0x8000)
    return 0;
  hessian[2] = (int16_t) value;

  value = ((int32_t) current[pixel + columns - 1] -
           next[pixel + columns - 1]) -
          next[pixel - columns + 1] - current[pixel + columns + 1] -
          current[pixel - columns - 1] + current[pixel - columns + 1] +
          next[pixel + columns + 1] + next[pixel - columns - 1];
  if (value <= -0x2000 || value >= 0x2000)
    return 0;
  hessian[3] = (int16_t) value;
  value = ((int32_t) previous[pixel + columns] -
           previous[pixel - columns]) -
          next2[pixel - columns] + next2[pixel + columns] -
          next[pixel + columns] - current[pixel + columns] +
          current[pixel - columns] + next[pixel - columns];
  if (value <= -0x2000 || value >= 0x2000)
    return 0;
  hessian[4] = (int16_t) value;
  value = ((int32_t) previous[pixel + 1] - previous[pixel - 1]) -
          next2[pixel - 1] + next2[pixel + 1] - current[pixel + 1] -
          next[pixel + 1] + next[pixel - 1] + current[pixel - 1];
  if (value <= -0x2000 || value >= 0x2000)
    return 0;
  hessian[5] = (int16_t) value;
  return 1;
}

static int
feature_signed_bit_length (int64_t value)
{
  uint64_t magnitude = value < 0 ? (uint64_t) -value : (uint64_t) value;
  int length = 1;

  while (magnitude != 0)
    {
      length++;
      magnitude >>= 1;
    }
  return length;
}

static void
feature_solve_refinement_offset (const int16_t hessian[6],
                                 const int16_t gradient[3],
                                 int32_t       offset[3])
{
  int64_t h00 = hessian[0], h11 = hessian[1], h22 = hessian[2];
  int64_t h01 = hessian[3], h12 = hessian[4], h02 = hessian[5];
  int64_t cofactor0 = h11 * h22 - h12 * h12;
  int64_t cofactor1 = h02 * h12 - h01 * h22;
  int64_t cofactor2 = h01 * h12 - h02 * h11;
  int64_t determinant = h00 * cofactor0 + h01 * cofactor1 +
                        h02 * cofactor2;
  int64_t numerator[3];
  int determinant_bits = feature_signed_bit_length (determinant);

  if (determinant == 0)
    {
      memset (offset, 0, 3 * sizeof(*offset));
      return;
    }
  numerator[0] = -((int64_t) gradient[0] * cofactor0 +
                   (int64_t) gradient[1] * cofactor1 +
                   (int64_t) gradient[2] * cofactor2);
  numerator[1] = -((h00 * h22 - h02 * h02) * gradient[1] +
                   (h01 * h02 - h00 * h12) * gradient[2] +
                   (int64_t) gradient[0] * cofactor1);
  numerator[2] = -((h00 * h11 - h01 * h01) * gradient[2] +
                   (h01 * h02 - h00 * h12) * gradient[1] +
                   (int64_t) gradient[0] * cofactor2);

  for (size_t axis = 0; axis < 3; axis++)
    {
      int numerator_bits = feature_signed_bit_length (numerator[axis]);
      int64_t scaled_numerator = numerator[axis];
      int64_t scaled_determinant = determinant;

      if (numerator_bits - determinant_bits >= 9)
        {
          offset[axis] = 0x00ffffff;
          continue;
        }
      int bits = numerator_bits > determinant_bits ? numerator_bits
                                                   : determinant_bits;
      if (bits > 32)
        {
          scaled_numerator >>= bits - 32;
          scaled_determinant >>= bits - 32;
        }
      offset[axis] = (int32_t) ((scaled_numerator * 0x1000) /
                                scaled_determinant);
    }
}

static int32_t
feature_round_q12_move (int32_t value)
{
  if (value < 0)
    {
      uint32_t magnitude = (uint32_t) -value;
      return -(int32_t) ((magnitude >> 12) + ((magnitude & 0x800) != 0));
    }
  return (value >> 12) + (((uint32_t) value & 0x800) != 0);
}

static uint32_t
feature_exp2_q16 (int32_t value)
{
  static const uint32_t thresholds[16] = {
    0x95c0, 0x526a, 0x2b80, 0x1664, 0x0b5d, 0x05ba, 0x02e0, 0x0171,
    0x00b8, 0x005c, 0x002e, 0x0017, 0x000c, 0x0006, 0x0003, 0x0001,
  };
  uint32_t magnitude = value < 0 ? (uint32_t) -value : (uint32_t) value;
  uint32_t fraction = magnitude & 0xffff;
  uint64_t result = UINT64_C(0x10000);

  if ((magnitude >> 16) > 0)
    result <<= magnitude >> 16;
  for (size_t bit = 0; bit < 16; bit++)
    if (fraction >= thresholds[bit])
      {
        fraction -= thresholds[bit];
        result += result >> (bit + 1);
      }
  return value > 0 ? (uint32_t) result
                   : (uint32_t) (UINT64_C(0x100000000) / result);
}

int
goodix_milan_feature_refine_extremum (
  const uint16_t              *scales,
  size_t                       rows,
  size_t                       columns,
  GoodixMilanFeatureCandidate *candidate,
  uint32_t                    *curvature)
{
  int16_t hessian[6];
  int16_t gradient[3];
  int16_t center;
  int32_t offset[3];
  int32_t x, y, scale;

  if (!scales || !candidate || !curvature || rows < 3 || columns < 3)
    return 0;
  x = candidate->x;
  y = candidate->y;
  scale = candidate->scale;
  for (size_t iteration = 0; iteration < 5; iteration++)
    {
      if (!feature_build_refinement_derivatives (
            scales, rows, columns, x, y, scale, hessian, gradient, &center))
        return 0;
      feature_solve_refinement_offset (hessian, gradient, offset);
      if (abs (offset[0]) < 0x800 && abs (offset[1]) < 0x800 &&
          abs (offset[2]) < 0x800)
        {
          int64_t contrast = (int64_t) center * 0x4000 +
                             (int64_t) gradient[0] * offset[0] +
                             (int64_t) gradient[1] * offset[1] +
                             (int64_t) gradient[2] * offset[2];
          if (contrast <= 0)
            contrast = -contrast;
          if (contrast < 0x1478000)
            return 0;
          candidate->strength = (int32_t) (contrast >> 12);
          int32_t trace = (int32_t) hessian[0] + hessian[1];
          int32_t determinant = (int32_t) hessian[0] * hessian[1] -
                                (int32_t) hessian[3] * hessian[3];
          if (determinant <= 0 ||
              (int64_t) determinant * 41 * 41 <=
                (int64_t) 40 * trace * trace)
            return 0;
          candidate->refined_x =
            (int16_t) ((x * 0x1000 + offset[0]) >> 4);
          candidate->refined_y =
            (int16_t) ((y * 0x1000 + offset[1]) >> 4);
          *curvature = (uint32_t) (((int64_t) trace * trace * 0x400) /
                                   determinant);
          int32_t exponent = ((scale * 0x1000 + offset[2]) * 0x10) / 3;
          candidate->scale_value =
            (int32_t) (((uint64_t) feature_exp2_q16 (exponent) * 0x13333) >>
                       16);
          return 1;
        }

      x += feature_round_q12_move (offset[0]);
      y += feature_round_q12_move (offset[1]);
      scale += feature_round_q12_move (offset[2]);
      candidate->x = x;
      candidate->y = y;
      candidate->scale = scale;
      if (scale < 1 || scale > 3 || x < 1 ||
          x >= (int32_t) columns - 1 || y < 1 || y >= (int32_t) rows - 1)
        return 0;
    }
  return 0;
}
