/*
 * Goodix 53x5 driver for libfprint - Milan transform routing
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"
#include "milan/private.h"
#include "milan/relations.h"
#include "milan/transform-private.h"

static inline int32_t
goodix_milan_transform_sar64_narrow (uint64_t value,
                                           unsigned int shift)
{
  return goodix_milan_transform_s32 ((uint32_t) (value >> shift));
}

static inline uint32_t
goodix_milan_transform_integer_sqrt (uint32_t value)
{
  uint32_t root = 0;

  if (value < 2)
    return value;
  for (uint32_t bit = 0x8000, shift = 15; bit != 0; bit >>= 1, shift--)
    {
      uint32_t candidate = (bit + root * 2) << shift;

      if (candidate <= value)
        {
          root += bit;
          value -= candidate;
        }
    }
  return root;
}

void
goodix_milan_transform_normalize (int32_t transform[6])
{
  int32_t average = goodix_milan_transform_sar32 (
    (uint32_t) transform[0] + (uint32_t) transform[4], 1);
  int32_t rotation = goodix_milan_transform_sar32 (
    (uint32_t) transform[3] - (uint32_t) transform[1], 1);
  uint32_t norm = goodix_milan_transform_integer_sqrt (
    (uint32_t) rotation * (uint32_t) rotation +
    (uint32_t) average * (uint32_t) average);

  if (norm == 0)
    {
      transform[0] = 0x100;
      transform[1] = 0;
      transform[3] = 0;
      transform[4] = 0x100;
      return;
    }
  transform[0] = goodix_milan_transform_divide (
    (uint64_t) (int64_t) goodix_milan_transform_s32 (
      (uint32_t) (norm >> 1) + ((uint32_t) average << 8)),
    (int32_t) norm);
  transform[4] = transform[0];
  transform[1] = goodix_milan_transform_divide (
    (uint64_t) (int64_t) goodix_milan_transform_s32 (
      (uint32_t) (norm >> 1) - ((uint32_t) rotation << 8)),
    (int32_t) norm);
  transform[3] = goodix_milan_transform_s32 (
    UINT32_C (0) - (uint32_t) transform[1]);
}

void
goodix_milan_transform_compose (const int32_t first[6],
                                      const int32_t second[6],
                                      int32_t       output[6])
{
  int32_t a[6];
  int32_t b[6];
  int32_t result[6];

  if (!first || !second || !output)
    return;
  memcpy (a, first, sizeof(a));
  memcpy (b, second, sizeof(b));
  result[0] = goodix_milan_transform_sar64_narrow (
    (uint64_t) (int64_t) b[0] * (uint64_t) (int64_t) a[0] +
    (uint64_t) (int64_t) b[3] * (uint64_t) (int64_t) a[1], 8);
  result[1] = goodix_milan_transform_sar64_narrow (
    (uint64_t) (int64_t) a[0] * (uint64_t) (int64_t) b[1] +
    (uint64_t) (int64_t) b[4] * (uint64_t) (int64_t) a[1], 8);
  result[2] = goodix_milan_transform_s32 (
    (uint32_t) goodix_milan_transform_sar64_narrow (
      (uint64_t) (int64_t) b[2] * (uint64_t) (int64_t) a[0] +
      (uint64_t) (int64_t) b[5] * (uint64_t) (int64_t) a[1], 8) +
    (uint32_t) a[2]);
  result[3] = goodix_milan_transform_sar64_narrow (
    (uint64_t) (int64_t) b[0] * (uint64_t) (int64_t) a[3] +
    (uint64_t) (int64_t) b[3] * (uint64_t) (int64_t) a[4], 8);
  result[4] = goodix_milan_transform_sar64_narrow (
    (uint64_t) (int64_t) a[3] * (uint64_t) (int64_t) b[1] +
    (uint64_t) (int64_t) a[4] * (uint64_t) (int64_t) b[4], 8);
  result[5] = goodix_milan_transform_s32 (
    (uint32_t) goodix_milan_transform_sar64_narrow (
      (uint64_t) (int64_t) a[3] * (uint64_t) (int64_t) b[2] +
      (uint64_t) (int64_t) a[4] * (uint64_t) (int64_t) b[5], 8) +
    (uint32_t) a[5]);
  goodix_milan_transform_normalize (result);
  memcpy (output, result, sizeof(result));
}

int
goodix_milan_transform_invert (const int32_t transform[6],
                                     int32_t       inverse[6])
{
  int32_t input[6];
  int32_t result[6];
  int32_t determinant;
  uint64_t numerator;

  if (!transform || !inverse)
    return -1;
  memcpy (input, transform, sizeof(input));
  determinant = goodix_milan_transform_s32 (
    (uint32_t) input[0] * (uint32_t) input[4] -
    (uint32_t) input[1] * (uint32_t) input[3]);
  if (determinant == 0)
    {
      memcpy (inverse, input, sizeof(input));
      return 0;
    }
  result[0] = goodix_milan_transform_divide (
    (uint64_t) (int64_t) input[4] << 16, determinant);
  result[1] = goodix_milan_transform_divide (
    (UINT64_C (0) - (uint64_t) (int64_t) input[1]) << 16, determinant);
  numerator = (uint64_t) (int64_t) input[5] *
                (uint64_t) (int64_t) input[1] -
              (uint64_t) (int64_t) input[4] *
                (uint64_t) (int64_t) input[2];
  result[2] = goodix_milan_transform_divide (numerator << 8,
                                                    determinant);
  result[3] = goodix_milan_transform_divide (
    (UINT64_C (0) - (uint64_t) (int64_t) input[3]) << 16, determinant);
  result[4] = goodix_milan_transform_divide (
    (uint64_t) (int64_t) input[0] << 16, determinant);
  numerator = (uint64_t) (int64_t) input[3] *
                (uint64_t) (int64_t) input[2] -
              (uint64_t) (int64_t) input[5] *
                (uint64_t) (int64_t) input[0];
  result[5] = goodix_milan_transform_divide (numerator << 8,
                                                    determinant);
  memcpy (inverse, result, sizeof(result));
  return 0;
}

void
goodix_milan_transform_route (const int32_t stored[6],
                                    const int32_t direct[6],
                                    int32_t       feature,
                                    int32_t       reference,
                                    int32_t       routed[6])
{
  int32_t inverse[6];

  if (feature > reference)
    goodix_milan_transform_compose (stored, direct, routed);
  else if (feature < reference)
    {
      goodix_milan_transform_invert (stored, inverse);
      goodix_milan_transform_compose (inverse, direct, routed);
    }
  else
    memmove (routed, direct, 6 * sizeof(*routed));
}

void
milan_compose_transform (const int32_t first[6],
                          const int32_t second[6],
                          int32_t       output[6])
{
  goodix_milan_transform_compose (first, second, output);
}

int
milan_invert_transform (const int32_t transform[6], int32_t inverse[6])
{
  return goodix_milan_transform_invert (transform, inverse);
}
