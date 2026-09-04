/*
 * Goodix 53x5 driver for libfprint - Milan feature records
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"

#include <string.h>

static void
milan_feature_expand_record (uint8_t *record)
{
  uint32_t bits;
  uint32_t reversed = 0;

  memcpy (&bits, record + 32, sizeof(bits));
  for (size_t i = 0; i < 32; i++)
    reversed |= ((bits >> i) & 1U) << (31 - i);
  memcpy (record + 36, &reversed, sizeof(reversed));
  record[48] = record[40];
  record[49] = (uint8_t) ~record[41];
  record[50] = (uint8_t) ~record[42];
  record[51] = record[43];
  record[52] = record[47];
  record[53] = record[46];
  record[54] = record[45];
  record[55] = record[44];
}

void
goodix_milan_feature_transform_record (uint8_t *record,
                                       int      reverse_bits)
{
  static const uint8_t swap_order[8] = { 0, 1, 1, 0, 1, 0, 0, 1 };
  uint8_t first[8];
  uint8_t second[8];

  for (size_t i = 0; i < 8; i++)
    {
      uint8_t left = record[16 + i * 2];
      uint8_t right = record[17 + i * 2];
      uint8_t high_left = ((left ^ right) & 0x0f) ^ left;
      uint8_t high_right = ((left ^ right) & 0x0f) ^ right;

      first[i] = swap_order[i] == 0 ? high_right : high_left;
      second[i] = swap_order[i] == 0 ? high_left : high_right;
    }
  memcpy (record + 16, first, sizeof(first));
  memcpy (record + 24, second, sizeof(second));
  memset (record + 36, 0, 4);

  if (reverse_bits)
    milan_feature_expand_record (record);
}

size_t
goodix_milan_feature_partition_records (uint8_t *records,
                                         size_t   record_count)
{
  size_t left = 0;
  size_t right;

  if (record_count == 0)
    return 0;
  right = record_count - 1;
  while (left < right)
    {
      while (left < right && (records[left * 56] & 3) == 0)
        left++;
      while (left < right && (records[right * 56] & 3) == 1)
        right--;
      if (left < right)
        {
          uint8_t temporary[56];

          memcpy (temporary, records + left * 56, sizeof(temporary));
          memcpy (records + left * 56, records + right * 56,
                  sizeof(temporary));
          memcpy (records + right * 56, temporary, sizeof(temporary));
        }
    }
  return left + ((records[left * 56] & 3) == 0);
}
