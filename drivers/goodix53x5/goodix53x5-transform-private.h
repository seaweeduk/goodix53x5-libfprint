/*
 * Goodix 53x5 driver for libfprint - Milan fixed-width transform primitives
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <stdint.h>
#include <string.h>

static inline int32_t
goodix_milan_transform_s32 (uint32_t value)
{
  int32_t result;

  memcpy (&result, &value, sizeof(result));
  return result;
}

static inline int32_t
goodix_milan_transform_sar32 (uint32_t value, unsigned int shift)
{
  uint32_t shifted = value >> shift;

  if ((value & UINT32_C (0x80000000)) != 0)
    shifted |= UINT32_MAX << (32 - shift);
  return goodix_milan_transform_s32 (shifted);
}

static inline int32_t
goodix_milan_transform_divide (uint64_t numerator, int32_t denominator)
{
  uint64_t magnitude;
  uint64_t quotient;
  uint32_t denominator_magnitude;
  int negative = (numerator >> 63) != 0;

  magnitude = negative ? (~numerator + 1) : numerator;
  denominator_magnitude = denominator < 0
                            ? (uint32_t) (-(int64_t) denominator)
                            : (uint32_t) denominator;
  quotient = magnitude / denominator_magnitude;
  if (negative != (denominator < 0))
    quotient = ~quotient + 1;
  return goodix_milan_transform_s32 ((uint32_t) quotient);
}
