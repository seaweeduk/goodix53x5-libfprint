/*
 * Goodix 53x5 driver for libfprint - Milan template codec
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "milan/milan.h"
#include "milan/print.h"
#include "milan/template/codec-private.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

enum
{
  MILAN_FEATURE_TAG_ELEMENT = 0x95,
  MILAN_FEATURE_TAG_HIGH_BITMAP = 0xb2,
  MILAN_FEATURE_TAG_RECORD_COUNT = 0xb3,
  MILAN_FEATURE_TAG_RECORDS = 0xb4,
  MILAN_FEATURE_TAG_SCALAR_B5 = 0xb5,
  MILAN_FEATURE_TAG_SCALAR_B6 = 0xb6,
  MILAN_FEATURE_TAG_SCALAR_B7 = 0xb7,
  MILAN_FEATURE_TAG_SCALAR_B8 = 0xb8,
  MILAN_FEATURE_TAG_SCALAR_B9 = 0xb9,
  MILAN_FEATURE_TAG_SCALAR_BA = 0xba,
  MILAN_FEATURE_TAG_SCALAR_BB = 0xbb,
  MILAN_FEATURE_TAG_SCALAR_BC = 0xbc,
  MILAN_FEATURE_TAG_SCALAR_BD = 0xbd,
  MILAN_FEATURE_TAG_SCALAR_BE = 0xbe,
  MILAN_FEATURE_TAG_ANTIFAKE = 0xbf,
  MILAN_FEATURE_TAG_SCALAR_C0 = 0xc0,
  MILAN_FEATURE_TAG_BITMAP_COLUMNS = 0xc1,
  MILAN_FEATURE_TAG_BITMAP_ROWS = 0xc2,
  MILAN_FEATURE_TAG_BITMAP_SENTINEL = 0xc3,
  MILAN_FEATURE_TAG_BITMAP_SCALE = 0xc4,
  MILAN_FEATURE_TAG_BITMAP_DATA = 0xc5,
  MILAN_FEATURE_TAG_OPTIONAL_C7 = 0xc7,
  MILAN_FEATURE_TAG_LOW_BITMAP = 0xcd,
  MILAN_FEATURE_TAG_INLINE_MASK = 0xce,
  MILAN_FEATURE_TAG_ENHANCED_BITMAP = 0xcf,
};

enum
{
  MILAN_TEMPLATE_TAG_PAYLOAD = 0x86,
  MILAN_TEMPLATE_TAG_ENVELOPE = 0x87,
  MILAN_TEMPLATE_TAG_HEADER_FORMAT = 0x81,
  MILAN_TEMPLATE_TAG_FEATURE_COUNT = 0x91,
  MILAN_TEMPLATE_TAG_REGISTRATION_COUNT = 0x92,
  MILAN_TEMPLATE_TAG_GRAPH = 0x93,
  MILAN_TEMPLATE_TAG_TAIL = 0x94,
  MILAN_TEMPLATE_TAG_RELATION = 0x96,
  MILAN_TEMPLATE_TAG_MAXIMUM_FEATURES = 0x97,
  MILAN_TEMPLATE_TAG_SENSOR_TYPE = 0x98,
  MILAN_TEMPLATE_TAG_ROWS = 0x9a,
  MILAN_TEMPLATE_TAG_COLUMNS = 0x9b,
  MILAN_TEMPLATE_TAG_GEOMETRY_PRIMARY = 0x9c,
  MILAN_TEMPLATE_TAG_GEOMETRY_SECONDARY = 0x9d,
  MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS = 0x9e,
  MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS_COPY = 0x9f,
  MILAN_TEMPLATE_TAG_QUEUE_STATE = 0xfa,
  MILAN_TEMPLATE_TAG_QUEUE_COUNTER = 0xfb,
  MILAN_TEMPLATE_TAG_TAIL_RECORDS = 0xa1,
  MILAN_TEMPLATE_TAG_TAIL_SCALAR = 0xa2,
  MILAN_TEMPLATE_TAG_TAIL_VECTOR = 0xa3,
  MILAN_TEMPLATE_TAG_TAIL_BITMAP = 0xa4,
  MILAN_TEMPLATE_TAG_TAIL_TRAILER_FIRST = 0xa5,
  MILAN_TEMPLATE_TAG_TAIL_TRAILER_LAST = 0xa8,
  MILAN_TEMPLATE_TAG_RELATION_LEADING = 0xe1,
  MILAN_TEMPLATE_TAG_RELATION_INDEX = 0xe3,
  MILAN_TEMPLATE_TAG_RELATION_VALUE_1 = 0xe4,
  MILAN_TEMPLATE_TAG_RELATION_VALUE_2 = 0xe5,
  MILAN_TEMPLATE_TAG_RELATION_VALUE_3 = 0xe6,
  MILAN_TEMPLATE_TAG_RELATION_VALUE_4 = 0xe7,
  MILAN_TEMPLATE_TAG_RELATION_VALUE_5 = 0xe8,
  MILAN_TEMPLATE_TAG_RELATION_VALUE_6 = 0xe9,
  MILAN_TEMPLATE_TAG_GRAPH_REFERENCE = 0xf2,
  MILAN_TEMPLATE_TAG_GRAPH_COMPANION_FIRST = 0xf3,
  MILAN_TEMPLATE_TAG_GRAPH_COMPANION_SECOND = 0xf4,
  MILAN_TEMPLATE_TAG_GRAPH_ESTABLISHED = 0xf5,
};

enum
{
  MILAN_FEATURE_BITMAP_SIZE = 286,
  MILAN_FEATURE_INLINE_MASK_SIZE = 72,
  MILAN_FEATURE_PACKED_RECORD_SIZE = 32,
};

enum
{
  MILAN_TEMPLATE_TAGGED_U32_SIZE = 5,
  MILAN_TEMPLATE_ENVELOPE_SIZE = 10,
  MILAN_TEMPLATE_HEADER_FIELD_COUNT = 13,
  MILAN_TEMPLATE_HEADER_SIZE =
    MILAN_TEMPLATE_HEADER_FIELD_COUNT * MILAN_TEMPLATE_TAGGED_U32_SIZE,
  MILAN_TEMPLATE_RELATION_PAYLOAD_SIZE = 40,
  MILAN_TEMPLATE_RELATION_SIZE =
    MILAN_TEMPLATE_TAGGED_U32_SIZE + MILAN_TEMPLATE_RELATION_PAYLOAD_SIZE,
  MILAN_TEMPLATE_GRAPH_PAYLOAD_SIZE = 20,
  MILAN_TEMPLATE_GRAPH_SIZE =
    MILAN_TEMPLATE_TAGGED_U32_SIZE + MILAN_TEMPLATE_GRAPH_PAYLOAD_SIZE,
  MILAN_TEMPLATE_TAIL_PAYLOAD_SIZE = 0x530,
  MILAN_TEMPLATE_TAIL_SIZE =
    MILAN_TEMPLATE_TAGGED_U32_SIZE + MILAN_TEMPLATE_TAIL_PAYLOAD_SIZE,
  MILAN_TEMPLATE_TAIL_STATE_SIZE = 0x51c,
  MILAN_TEMPLATE_TAIL_RECORDS_SIZE = 200,
  MILAN_TEMPLATE_TAIL_VECTOR_SIZE = 64,
  MILAN_TEMPLATE_TAIL_BITMAP_SIZE = 0x400,
  MILAN_TEMPLATE_FIXED_SIZE = MILAN_TEMPLATE_ENVELOPE_SIZE +
                              MILAN_TEMPLATE_HEADER_SIZE +
                              MILAN_TEMPLATE_GRAPH_SIZE +
                              MILAN_TEMPLATE_TAIL_SIZE,
};

int
goodix_milan_feature_pack_template_records (
  const GoodixMilanFeatureRecord *records,
  size_t                          record_count,
  uint8_t                        *packed,
  size_t                          packed_size)
{
  static const uint8_t swap_order[8] = { 0, 1, 1, 0, 1, 0, 0, 1 };
  const size_t packed_record_size = MILAN_FEATURE_PACKED_RECORD_SIZE;

  if (!records || !packed || record_count > SIZE_MAX / packed_record_size ||
      packed_size < record_count * packed_record_size)
    return -1;

  for (size_t i = 0; i < record_count; i++)
    {
      const uint8_t *record = (const uint8_t *) &records[i];
      uint8_t *output = packed + i * packed_record_size;
      uint16_t x;
      uint16_t y;
      int16_t orientation;
      uint32_t position;
      uint8_t descriptor[16];

      memcpy (&x, record + 2, sizeof(x));
      memcpy (&y, record + 4, sizeof(y));
      memcpy (&orientation, record + 6, sizeof(orientation));

      uint8_t packed_orientation = orientation < 0
                                     ? (uint8_t) ((-(int32_t) orientation >> 8) +
                                                  0x80)
                                     : (uint8_t) (orientation >> 8);
      position = (((uint32_t) x << 12) | y) << 4 | packed_orientation;
      for (size_t j = 0; j < 8; j++)
        {
          uint8_t first = record[16 + j];
          uint8_t second = record[24 + j];
          uint8_t high_first = ((first ^ second) & 0x0f) ^ first;
          uint8_t high_second = ((first ^ second) & 0x0f) ^ second;

          descriptor[j * 2] = swap_order[j] == 0 ? high_second : high_first;
          descriptor[j * 2 + 1] =
            swap_order[j] == 0 ? high_first : high_second;
        }
      memcpy (output, &position, sizeof(position));
      memcpy (output + 4, descriptor, sizeof(descriptor));
      memcpy (output + 20, record + 32, 4);
      memcpy (output + 24, record + 40, 8);
    }
  return 0;
}

int
goodix_milan_feature_unpack_template_records (
  const uint8_t            *packed,
  size_t                    record_count,
  size_t                    partition_count,
  GoodixMilanFeatureRecord *records,
  size_t                    record_capacity)
{
  static const uint8_t swap_order[8] = { 0, 1, 1, 0, 1, 0, 0, 1 };

  if (!packed || !records || partition_count > record_count ||
      record_count > record_capacity ||
      record_count > SIZE_MAX / MILAN_FEATURE_PACKED_RECORD_SIZE)
    return -1;
  memset (records, 0, record_count * sizeof(*records));
  for (size_t i = 0; i < record_count; i++)
    {
      const uint8_t *input =
        packed + i * MILAN_FEATURE_PACKED_RECORD_SIZE;
      uint8_t *record = (uint8_t *) &records[i];
      uint32_t position = goodix_milan_template_read_u32 (input);
      uint8_t packed_orientation = (uint8_t) position;

      records[i].foreground = i < partition_count ? 0 : 1;
      records[i].refined_x = (int16_t) ((position >> 16) & 0xfff0);
      records[i].refined_y = (int16_t) ((position >> 4) & 0xfff0);
      records[i].orientation = packed_orientation < 0x80
                                 ? (int16_t) (packed_orientation << 8)
                                 : (int16_t) (-(int32_t)
                                     (packed_orientation - 0x80) * 256);
      for (size_t j = 0; j < 8; j++)
        {
          uint8_t first = input[4 + j * 2];
          uint8_t second = input[5 + j * 2];
          uint8_t high_first = swap_order[j] == 0 ? second : first;
          uint8_t high_second = swap_order[j] == 0 ? first : second;

          record[16 + j] = (high_first & 0xf0) | (high_second & 0x0f);
          record[24 + j] = (high_second & 0xf0) | (high_first & 0x0f);
        }
      memcpy (record + 32, input + 20, 4);
      memcpy (record + 40, input + 24, 8);
      int32_t vertical_class = records[i].refined_y < 0x1400
                                 ? 1
                                 : records[i].refined_y >= 0x4400 ? 2 : 0;
      memcpy (record + 12, &vertical_class, sizeof(vertical_class));
    }
  return 0;
}

static int
milan_feature_read_tagged_block (const uint8_t **cursor,
                                 const uint8_t  *end,
                                 uint8_t         tag,
                                 size_t          expected_size,
                                 const uint8_t **block_payload)
{
  uint32_t size;

  if ((size_t) (end - *cursor) < 5 || **cursor != tag)
    return -1;
  size = goodix_milan_template_read_u32 (*cursor + 1);
  if (size != expected_size || (size_t) (end - *cursor - 5) < size)
    return -1;
  *block_payload = *cursor + 5;
  *cursor += 5 + size;
  return 0;
}

static int
milan_feature_read_bitmap (const uint8_t **cursor,
                           const uint8_t  *end,
                           uint8_t         tag,
                           const uint8_t **bitmap)
{
  const uint8_t *block;

  if (milan_feature_read_tagged_block (
        cursor, end, tag, MILAN_FEATURE_BITMAP_SIZE + 25, &block) != 0 ||
      block[0] != MILAN_FEATURE_TAG_BITMAP_COLUMNS ||
      goodix_milan_template_read_u32 (block + 1) != 52 ||
      block[5] != MILAN_FEATURE_TAG_BITMAP_ROWS ||
      goodix_milan_template_read_u32 (block + 6) != 44 ||
      block[10] != MILAN_FEATURE_TAG_BITMAP_SENTINEL ||
      goodix_milan_template_read_u32 (block + 11) != UINT32_MAX ||
      block[15] != MILAN_FEATURE_TAG_BITMAP_SCALE ||
      goodix_milan_template_read_u32 (block + 16) != 8 ||
      block[20] != MILAN_FEATURE_TAG_BITMAP_DATA ||
      goodix_milan_template_read_u32 (block + 21) !=
        MILAN_FEATURE_BITMAP_SIZE)
    return -1;
  *bitmap = block + 25;
  return 0;
}

int
goodix_milan_template_parse_feature_element (
  const uint8_t          *packed,
  size_t                  packed_size,
  GoodixMilanFeatureView *view)
{
  static const uint8_t scalar_tags[11] = {
    MILAN_FEATURE_TAG_SCALAR_B5, MILAN_FEATURE_TAG_SCALAR_B6,
    MILAN_FEATURE_TAG_SCALAR_B7, MILAN_FEATURE_TAG_SCALAR_B8,
    MILAN_FEATURE_TAG_SCALAR_B9, MILAN_FEATURE_TAG_SCALAR_BA,
    MILAN_FEATURE_TAG_SCALAR_BB, MILAN_FEATURE_TAG_SCALAR_BC,
    MILAN_FEATURE_TAG_SCALAR_BD, MILAN_FEATURE_TAG_SCALAR_BE,
    MILAN_FEATURE_TAG_SCALAR_C0,
  };
  const uint8_t *cursor;
  const uint8_t *end;
  const uint8_t *packed_records;
  uint32_t count;

  if (!packed || !view || packed_size < 5 ||
      packed[0] != MILAN_FEATURE_TAG_ELEMENT ||
      goodix_milan_template_read_u32 (packed + 1) != packed_size - 5)
    return -1;
  memset (view, 0, sizeof(*view));
  cursor = packed + 5;
  end = packed + packed_size;
  if (milan_feature_read_bitmap (&cursor, end, MILAN_FEATURE_TAG_HIGH_BITMAP,
                                  &view->high_bitmap) != 0 ||
      milan_feature_read_bitmap (&cursor, end,
                                 MILAN_FEATURE_TAG_ENHANCED_BITMAP,
                                 &view->enhanced_bitmap) != 0 ||
      milan_feature_read_tagged_block (&cursor, end,
                                       MILAN_FEATURE_TAG_INLINE_MASK,
                                       MILAN_FEATURE_INLINE_MASK_SIZE,
                                       &view->inline_mask) != 0 ||
      milan_feature_read_bitmap (&cursor, end, MILAN_FEATURE_TAG_LOW_BITMAP,
                                  &view->low_bitmap) != 0 ||
      (size_t) (end - cursor) < 5 ||
      cursor[0] != MILAN_FEATURE_TAG_RECORD_COUNT)
    return -1;
  count = goodix_milan_template_read_u32 (cursor + 1);
  cursor += 5;
  if ((size_t) (end - cursor) < 6 ||
      cursor[0] != MILAN_FEATURE_TAG_ANTIFAKE || cursor[1] != 1 ||
      goodix_milan_template_read_u32 (cursor + 2) != GOODIX_MILAN_ANTIFAKE_SIZE ||
      (size_t) (end - cursor - 6) < GOODIX_MILAN_ANTIFAKE_SIZE)
    return -1;
  view->antifake = (const GoodixMilanAntifakeBlob *) (cursor + 6);
  cursor += 6 + GOODIX_MILAN_ANTIFAKE_SIZE;
  if (milan_feature_read_tagged_block (
        &cursor, end, MILAN_FEATURE_TAG_RECORDS,
        (size_t) count * MILAN_FEATURE_PACKED_RECORD_SIZE,
                                       &packed_records) != 0)
    return -1;
  view->packed_records = packed_records;
  view->record_count = count;
  for (size_t i = 0; i < 11; i++)
    {
      if ((size_t) (end - cursor) < 5 || cursor[0] != scalar_tags[i])
        return -1;
      view->fields.tagged_values[i] = (int32_t) goodix_milan_template_read_u32 (cursor + 1);
      cursor += 5;
    }
  if ((size_t) (end - cursor) >= 5 &&
      cursor[0] == MILAN_FEATURE_TAG_OPTIONAL_C7)
    {
      view->fields.optional_c7 = (int32_t) goodix_milan_template_read_u32 (cursor + 1);
      cursor += 5;
    }
  return cursor == end ? 0 : -1;
}

GoodixMilanAntifakeBlob *
goodix_milan_template_mutable_feature_antifake (uint8_t *packed,
                                                 size_t   packed_size)
{
  GoodixMilanFeatureView view;

  if (goodix_milan_template_parse_feature_element (
        packed, packed_size, &view) != 0)
    return NULL;
  return (GoodixMilanAntifakeBlob *)
           (packed + ((const uint8_t *) view.antifake - packed));
}

void
goodix_milan_template_write_u32 (uint8_t  *output,
                 uint32_t  value)
{
  memcpy (output, &value, sizeof(value));
}

uint32_t
goodix_milan_template_read_u32 (const uint8_t *input)
{
  uint32_t value;

  memcpy (&value, input, sizeof(value));
  return value;
}

static uint8_t *
milan_pack_feature_bitmap (uint8_t       *output,
                           uint8_t        tag,
                           const uint8_t *bitmap)
{
  const uint32_t bitmap_size = MILAN_FEATURE_BITMAP_SIZE;

  *output++ = tag;
  goodix_milan_template_write_u32 (output, bitmap_size + 25);
  output += 4;
  *output++ = MILAN_FEATURE_TAG_BITMAP_COLUMNS;
  goodix_milan_template_write_u32 (output, 52);
  output += 4;
  *output++ = MILAN_FEATURE_TAG_BITMAP_ROWS;
  goodix_milan_template_write_u32 (output, 44);
  output += 4;
  *output++ = MILAN_FEATURE_TAG_BITMAP_SENTINEL;
  goodix_milan_template_write_u32 (output, UINT32_MAX);
  output += 4;
  *output++ = MILAN_FEATURE_TAG_BITMAP_SCALE;
  goodix_milan_template_write_u32 (output, 8);
  output += 4;
  *output++ = MILAN_FEATURE_TAG_BITMAP_DATA;
  goodix_milan_template_write_u32 (output, bitmap_size);
  output += 4;
  memcpy (output, bitmap, bitmap_size);
  return output + bitmap_size;
}

int
goodix_milan_template_pack_feature_element (
  const uint8_t                         *high_bitmap,
  const uint8_t                         *enhanced_bitmap,
  const uint8_t                         *inline_mask,
  const uint8_t                         *low_bitmap,
  const GoodixMilanFeatureRecord        *records,
  size_t                                 record_count,
  const GoodixMilanAntifakeBlob         *antifake,
  const GoodixMilanFeatureTemplateFields *fields,
  uint8_t                               *packed,
  size_t                                 packed_capacity,
  size_t                                *packed_size)
{
  static const uint8_t scalar_tags[11] = {
    MILAN_FEATURE_TAG_SCALAR_B5, MILAN_FEATURE_TAG_SCALAR_B6,
    MILAN_FEATURE_TAG_SCALAR_B7, MILAN_FEATURE_TAG_SCALAR_B8,
    MILAN_FEATURE_TAG_SCALAR_B9, MILAN_FEATURE_TAG_SCALAR_BA,
    MILAN_FEATURE_TAG_SCALAR_BB, MILAN_FEATURE_TAG_SCALAR_BC,
    MILAN_FEATURE_TAG_SCALAR_BD, MILAN_FEATURE_TAG_SCALAR_BE,
    MILAN_FEATURE_TAG_SCALAR_C0,
  };
  const size_t inline_mask_size = MILAN_FEATURE_INLINE_MASK_SIZE;
  const size_t packed_record_size = MILAN_FEATURE_PACKED_RECORD_SIZE;
  size_t total_size;
  uint8_t *output;

  if (!high_bitmap || !enhanced_bitmap || !inline_mask || !low_bitmap ||
      !records || !antifake || !fields || !packed || !packed_size ||
      record_count > UINT32_MAX ||
      record_count > (SIZE_MAX - 7945) / packed_record_size)
    return -1;
  total_size = 7945 + record_count * packed_record_size +
               (fields->optional_c7 != 0 ? 5 : 0);
  if (packed_capacity < total_size)
    return -1;

  output = packed;
  *output++ = MILAN_FEATURE_TAG_ELEMENT;
  goodix_milan_template_write_u32 (output, (uint32_t) (total_size - 5));
  output += 4;
  output = milan_pack_feature_bitmap (
    output, MILAN_FEATURE_TAG_HIGH_BITMAP, high_bitmap);
  output = milan_pack_feature_bitmap (
    output, MILAN_FEATURE_TAG_ENHANCED_BITMAP, enhanced_bitmap);
  *output++ = MILAN_FEATURE_TAG_INLINE_MASK;
  goodix_milan_template_write_u32 (output, inline_mask_size);
  output += 4;
  memcpy (output, inline_mask, inline_mask_size);
  output += inline_mask_size;
  output = milan_pack_feature_bitmap (
    output, MILAN_FEATURE_TAG_LOW_BITMAP, low_bitmap);
  *output++ = MILAN_FEATURE_TAG_RECORD_COUNT;
  goodix_milan_template_write_u32 (output, (uint32_t) record_count);
  output += 4;
  *output++ = MILAN_FEATURE_TAG_ANTIFAKE;
  *output++ = 1;
  goodix_milan_template_write_u32 (output, GOODIX_MILAN_ANTIFAKE_SIZE);
  output += 4;
  memcpy (output, goodix_milan_antifake_const_data (antifake),
          GOODIX_MILAN_ANTIFAKE_SIZE);
  output += GOODIX_MILAN_ANTIFAKE_SIZE;
  *output++ = MILAN_FEATURE_TAG_RECORDS;
  goodix_milan_template_write_u32 (output, (uint32_t) (record_count * packed_record_size));
  output += 4;
  if (goodix_milan_feature_pack_template_records (
        records, record_count, output,
        record_count * packed_record_size) != 0)
    return -1;
  output += record_count * packed_record_size;
  for (size_t i = 0; i < 11; i++)
    {
      *output++ = scalar_tags[i];
      goodix_milan_template_write_u32 (output, (uint32_t) fields->tagged_values[i]);
      output += 4;
    }
  if (fields->optional_c7 != 0)
    {
      *output++ = MILAN_FEATURE_TAG_OPTIONAL_C7;
      goodix_milan_template_write_u32 (output, (uint32_t) fields->optional_c7);
      output += 4;
    }
  *packed_size = (size_t) (output - packed);
  return *packed_size == total_size ? 0 : -1;
}

static uint32_t
milan_template_crc32 (const uint8_t *data,
                      size_t         size)
{
  /* table[n][b] is b advanced through 8 * (n + 1) reflected steps. */
  static const uint32_t table[8][256] = {
    {
      0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU,
      0x076dc419U, 0x706af48fU, 0xe963a535U, 0x9e6495a3U,
      0x0edb8832U, 0x79dcb8a4U, 0xe0d5e91eU, 0x97d2d988U,
      0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U, 0x90bf1d91U,
      0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
      0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U,
      0x136c9856U, 0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU,
      0x14015c4fU, 0x63066cd9U, 0xfa0f3d63U, 0x8d080df5U,
      0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U, 0xa2677172U,
      0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
      0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U,
      0x32d86ce3U, 0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U,
      0x26d930acU, 0x51de003aU, 0xc8d75180U, 0xbfd06116U,
      0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U, 0xb8bda50fU,
      0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
      0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU,
      0x76dc4190U, 0x01db7106U, 0x98d220bcU, 0xefd5102aU,
      0x71b18589U, 0x06b6b51fU, 0x9fbfe4a5U, 0xe8b8d433U,
      0x7807c9a2U, 0x0f00f934U, 0x9609a88eU, 0xe10e9818U,
      0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
      0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU,
      0x6c0695edU, 0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U,
      0x65b0d9c6U, 0x12b7e950U, 0x8bbeb8eaU, 0xfcb9887cU,
      0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U, 0xfbd44c65U,
      0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
      0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU,
      0x4369e96aU, 0x346ed9fcU, 0xad678846U, 0xda60b8d0U,
      0x44042d73U, 0x33031de5U, 0xaa0a4c5fU, 0xdd0d7cc9U,
      0x5005713cU, 0x270241aaU, 0xbe0b1010U, 0xc90c2086U,
      0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
      0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U,
      0x59b33d17U, 0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU,
      0xedb88320U, 0x9abfb3b6U, 0x03b6e20cU, 0x74b1d29aU,
      0xead54739U, 0x9dd277afU, 0x04db2615U, 0x73dc1683U,
      0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
      0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U,
      0xf00f9344U, 0x8708a3d2U, 0x1e01f268U, 0x6906c2feU,
      0xf762575dU, 0x806567cbU, 0x196c3671U, 0x6e6b06e7U,
      0xfed41b76U, 0x89d32be0U, 0x10da7a5aU, 0x67dd4accU,
      0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
      0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U,
      0xd1bb67f1U, 0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU,
      0xd80d2bdaU, 0xaf0a1b4cU, 0x36034af6U, 0x41047a60U,
      0xdf60efc3U, 0xa867df55U, 0x316e8eefU, 0x4669be79U,
      0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
      0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU,
      0xc5ba3bbeU, 0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U,
      0xc2d7ffa7U, 0xb5d0cf31U, 0x2cd99e8bU, 0x5bdeae1dU,
      0x9b64c2b0U, 0xec63f226U, 0x756aa39cU, 0x026d930aU,
      0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
      0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U,
      0x92d28e9bU, 0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U,
      0x86d3d2d4U, 0xf1d4e242U, 0x68ddb3f8U, 0x1fda836eU,
      0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U, 0x18b74777U,
      0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
      0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U,
      0xa00ae278U, 0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U,
      0xa7672661U, 0xd06016f7U, 0x4969474dU, 0x3e6e77dbU,
      0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U, 0x37d83bf0U,
      0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
      0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U,
      0xbad03605U, 0xcdd70693U, 0x54de5729U, 0x23d967bfU,
      0xb3667a2eU, 0xc4614ab8U, 0x5d681b02U, 0x2a6f2b94U,
      0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU, 0x2d02ef8dU,
    },
    {
      0x00000000U, 0x191b3141U, 0x32366282U, 0x2b2d53c3U,
      0x646cc504U, 0x7d77f445U, 0x565aa786U, 0x4f4196c7U,
      0xc8d98a08U, 0xd1c2bb49U, 0xfaefe88aU, 0xe3f4d9cbU,
      0xacb54f0cU, 0xb5ae7e4dU, 0x9e832d8eU, 0x87981ccfU,
      0x4ac21251U, 0x53d92310U, 0x78f470d3U, 0x61ef4192U,
      0x2eaed755U, 0x37b5e614U, 0x1c98b5d7U, 0x05838496U,
      0x821b9859U, 0x9b00a918U, 0xb02dfadbU, 0xa936cb9aU,
      0xe6775d5dU, 0xff6c6c1cU, 0xd4413fdfU, 0xcd5a0e9eU,
      0x958424a2U, 0x8c9f15e3U, 0xa7b24620U, 0xbea97761U,
      0xf1e8e1a6U, 0xe8f3d0e7U, 0xc3de8324U, 0xdac5b265U,
      0x5d5daeaaU, 0x44469febU, 0x6f6bcc28U, 0x7670fd69U,
      0x39316baeU, 0x202a5aefU, 0x0b07092cU, 0x121c386dU,
      0xdf4636f3U, 0xc65d07b2U, 0xed705471U, 0xf46b6530U,
      0xbb2af3f7U, 0xa231c2b6U, 0x891c9175U, 0x9007a034U,
      0x179fbcfbU, 0x0e848dbaU, 0x25a9de79U, 0x3cb2ef38U,
      0x73f379ffU, 0x6ae848beU, 0x41c51b7dU, 0x58de2a3cU,
      0xf0794f05U, 0xe9627e44U, 0xc24f2d87U, 0xdb541cc6U,
      0x94158a01U, 0x8d0ebb40U, 0xa623e883U, 0xbf38d9c2U,
      0x38a0c50dU, 0x21bbf44cU, 0x0a96a78fU, 0x138d96ceU,
      0x5ccc0009U, 0x45d73148U, 0x6efa628bU, 0x77e153caU,
      0xbabb5d54U, 0xa3a06c15U, 0x888d3fd6U, 0x91960e97U,
      0xded79850U, 0xc7cca911U, 0xece1fad2U, 0xf5facb93U,
      0x7262d75cU, 0x6b79e61dU, 0x4054b5deU, 0x594f849fU,
      0x160e1258U, 0x0f152319U, 0x243870daU, 0x3d23419bU,
      0x65fd6ba7U, 0x7ce65ae6U, 0x57cb0925U, 0x4ed03864U,
      0x0191aea3U, 0x188a9fe2U, 0x33a7cc21U, 0x2abcfd60U,
      0xad24e1afU, 0xb43fd0eeU, 0x9f12832dU, 0x8609b26cU,
      0xc94824abU, 0xd05315eaU, 0xfb7e4629U, 0xe2657768U,
      0x2f3f79f6U, 0x362448b7U, 0x1d091b74U, 0x04122a35U,
      0x4b53bcf2U, 0x52488db3U, 0x7965de70U, 0x607eef31U,
      0xe7e6f3feU, 0xfefdc2bfU, 0xd5d0917cU, 0xcccba03dU,
      0x838a36faU, 0x9a9107bbU, 0xb1bc5478U, 0xa8a76539U,
      0x3b83984bU, 0x2298a90aU, 0x09b5fac9U, 0x10aecb88U,
      0x5fef5d4fU, 0x46f46c0eU, 0x6dd93fcdU, 0x74c20e8cU,
      0xf35a1243U, 0xea412302U, 0xc16c70c1U, 0xd8774180U,
      0x9736d747U, 0x8e2de606U, 0xa500b5c5U, 0xbc1b8484U,
      0x71418a1aU, 0x685abb5bU, 0x4377e898U, 0x5a6cd9d9U,
      0x152d4f1eU, 0x0c367e5fU, 0x271b2d9cU, 0x3e001cddU,
      0xb9980012U, 0xa0833153U, 0x8bae6290U, 0x92b553d1U,
      0xddf4c516U, 0xc4eff457U, 0xefc2a794U, 0xf6d996d5U,
      0xae07bce9U, 0xb71c8da8U, 0x9c31de6bU, 0x852aef2aU,
      0xca6b79edU, 0xd37048acU, 0xf85d1b6fU, 0xe1462a2eU,
      0x66de36e1U, 0x7fc507a0U, 0x54e85463U, 0x4df36522U,
      0x02b2f3e5U, 0x1ba9c2a4U, 0x30849167U, 0x299fa026U,
      0xe4c5aeb8U, 0xfdde9ff9U, 0xd6f3cc3aU, 0xcfe8fd7bU,
      0x80a96bbcU, 0x99b25afdU, 0xb29f093eU, 0xab84387fU,
      0x2c1c24b0U, 0x350715f1U, 0x1e2a4632U, 0x07317773U,
      0x4870e1b4U, 0x516bd0f5U, 0x7a468336U, 0x635db277U,
      0xcbfad74eU, 0xd2e1e60fU, 0xf9ccb5ccU, 0xe0d7848dU,
      0xaf96124aU, 0xb68d230bU, 0x9da070c8U, 0x84bb4189U,
      0x03235d46U, 0x1a386c07U, 0x31153fc4U, 0x280e0e85U,
      0x674f9842U, 0x7e54a903U, 0x5579fac0U, 0x4c62cb81U,
      0x8138c51fU, 0x9823f45eU, 0xb30ea79dU, 0xaa1596dcU,
      0xe554001bU, 0xfc4f315aU, 0xd7626299U, 0xce7953d8U,
      0x49e14f17U, 0x50fa7e56U, 0x7bd72d95U, 0x62cc1cd4U,
      0x2d8d8a13U, 0x3496bb52U, 0x1fbbe891U, 0x06a0d9d0U,
      0x5e7ef3ecU, 0x4765c2adU, 0x6c48916eU, 0x7553a02fU,
      0x3a1236e8U, 0x230907a9U, 0x0824546aU, 0x113f652bU,
      0x96a779e4U, 0x8fbc48a5U, 0xa4911b66U, 0xbd8a2a27U,
      0xf2cbbce0U, 0xebd08da1U, 0xc0fdde62U, 0xd9e6ef23U,
      0x14bce1bdU, 0x0da7d0fcU, 0x268a833fU, 0x3f91b27eU,
      0x70d024b9U, 0x69cb15f8U, 0x42e6463bU, 0x5bfd777aU,
      0xdc656bb5U, 0xc57e5af4U, 0xee530937U, 0xf7483876U,
      0xb809aeb1U, 0xa1129ff0U, 0x8a3fcc33U, 0x9324fd72U,
    },
    {
      0x00000000U, 0x01c26a37U, 0x0384d46eU, 0x0246be59U,
      0x0709a8dcU, 0x06cbc2ebU, 0x048d7cb2U, 0x054f1685U,
      0x0e1351b8U, 0x0fd13b8fU, 0x0d9785d6U, 0x0c55efe1U,
      0x091af964U, 0x08d89353U, 0x0a9e2d0aU, 0x0b5c473dU,
      0x1c26a370U, 0x1de4c947U, 0x1fa2771eU, 0x1e601d29U,
      0x1b2f0bacU, 0x1aed619bU, 0x18abdfc2U, 0x1969b5f5U,
      0x1235f2c8U, 0x13f798ffU, 0x11b126a6U, 0x10734c91U,
      0x153c5a14U, 0x14fe3023U, 0x16b88e7aU, 0x177ae44dU,
      0x384d46e0U, 0x398f2cd7U, 0x3bc9928eU, 0x3a0bf8b9U,
      0x3f44ee3cU, 0x3e86840bU, 0x3cc03a52U, 0x3d025065U,
      0x365e1758U, 0x379c7d6fU, 0x35dac336U, 0x3418a901U,
      0x3157bf84U, 0x3095d5b3U, 0x32d36beaU, 0x331101ddU,
      0x246be590U, 0x25a98fa7U, 0x27ef31feU, 0x262d5bc9U,
      0x23624d4cU, 0x22a0277bU, 0x20e69922U, 0x2124f315U,
      0x2a78b428U, 0x2bbade1fU, 0x29fc6046U, 0x283e0a71U,
      0x2d711cf4U, 0x2cb376c3U, 0x2ef5c89aU, 0x2f37a2adU,
      0x709a8dc0U, 0x7158e7f7U, 0x731e59aeU, 0x72dc3399U,
      0x7793251cU, 0x76514f2bU, 0x7417f172U, 0x75d59b45U,
      0x7e89dc78U, 0x7f4bb64fU, 0x7d0d0816U, 0x7ccf6221U,
      0x798074a4U, 0x78421e93U, 0x7a04a0caU, 0x7bc6cafdU,
      0x6cbc2eb0U, 0x6d7e4487U, 0x6f38fadeU, 0x6efa90e9U,
      0x6bb5866cU, 0x6a77ec5bU, 0x68315202U, 0x69f33835U,
      0x62af7f08U, 0x636d153fU, 0x612bab66U, 0x60e9c151U,
      0x65a6d7d4U, 0x6464bde3U, 0x662203baU, 0x67e0698dU,
      0x48d7cb20U, 0x4915a117U, 0x4b531f4eU, 0x4a917579U,
      0x4fde63fcU, 0x4e1c09cbU, 0x4c5ab792U, 0x4d98dda5U,
      0x46c49a98U, 0x4706f0afU, 0x45404ef6U, 0x448224c1U,
      0x41cd3244U, 0x400f5873U, 0x4249e62aU, 0x438b8c1dU,
      0x54f16850U, 0x55330267U, 0x5775bc3eU, 0x56b7d609U,
      0x53f8c08cU, 0x523aaabbU, 0x507c14e2U, 0x51be7ed5U,
      0x5ae239e8U, 0x5b2053dfU, 0x5966ed86U, 0x58a487b1U,
      0x5deb9134U, 0x5c29fb03U, 0x5e6f455aU, 0x5fad2f6dU,
      0xe1351b80U, 0xe0f771b7U, 0xe2b1cfeeU, 0xe373a5d9U,
      0xe63cb35cU, 0xe7fed96bU, 0xe5b86732U, 0xe47a0d05U,
      0xef264a38U, 0xeee4200fU, 0xeca29e56U, 0xed60f461U,
      0xe82fe2e4U, 0xe9ed88d3U, 0xebab368aU, 0xea695cbdU,
      0xfd13b8f0U, 0xfcd1d2c7U, 0xfe976c9eU, 0xff5506a9U,
      0xfa1a102cU, 0xfbd87a1bU, 0xf99ec442U, 0xf85cae75U,
      0xf300e948U, 0xf2c2837fU, 0xf0843d26U, 0xf1465711U,
      0xf4094194U, 0xf5cb2ba3U, 0xf78d95faU, 0xf64fffcdU,
      0xd9785d60U, 0xd8ba3757U, 0xdafc890eU, 0xdb3ee339U,
      0xde71f5bcU, 0xdfb39f8bU, 0xddf521d2U, 0xdc374be5U,
      0xd76b0cd8U, 0xd6a966efU, 0xd4efd8b6U, 0xd52db281U,
      0xd062a404U, 0xd1a0ce33U, 0xd3e6706aU, 0xd2241a5dU,
      0xc55efe10U, 0xc49c9427U, 0xc6da2a7eU, 0xc7184049U,
      0xc25756ccU, 0xc3953cfbU, 0xc1d382a2U, 0xc011e895U,
      0xcb4dafa8U, 0xca8fc59fU, 0xc8c97bc6U, 0xc90b11f1U,
      0xcc440774U, 0xcd866d43U, 0xcfc0d31aU, 0xce02b92dU,
      0x91af9640U, 0x906dfc77U, 0x922b422eU, 0x93e92819U,
      0x96a63e9cU, 0x976454abU, 0x9522eaf2U, 0x94e080c5U,
      0x9fbcc7f8U, 0x9e7eadcfU, 0x9c381396U, 0x9dfa79a1U,
      0x98b56f24U, 0x99770513U, 0x9b31bb4aU, 0x9af3d17dU,
      0x8d893530U, 0x8c4b5f07U, 0x8e0de15eU, 0x8fcf8b69U,
      0x8a809decU, 0x8b42f7dbU, 0x89044982U, 0x88c623b5U,
      0x839a6488U, 0x82580ebfU, 0x801eb0e6U, 0x81dcdad1U,
      0x8493cc54U, 0x8551a663U, 0x8717183aU, 0x86d5720dU,
      0xa9e2d0a0U, 0xa820ba97U, 0xaa6604ceU, 0xaba46ef9U,
      0xaeeb787cU, 0xaf29124bU, 0xad6fac12U, 0xacadc625U,
      0xa7f18118U, 0xa633eb2fU, 0xa4755576U, 0xa5b73f41U,
      0xa0f829c4U, 0xa13a43f3U, 0xa37cfdaaU, 0xa2be979dU,
      0xb5c473d0U, 0xb40619e7U, 0xb640a7beU, 0xb782cd89U,
      0xb2cddb0cU, 0xb30fb13bU, 0xb1490f62U, 0xb08b6555U,
      0xbbd72268U, 0xba15485fU, 0xb853f606U, 0xb9919c31U,
      0xbcde8ab4U, 0xbd1ce083U, 0xbf5a5edaU, 0xbe9834edU,
    },
    {
      0x00000000U, 0xb8bc6765U, 0xaa09c88bU, 0x12b5afeeU,
      0x8f629757U, 0x37def032U, 0x256b5fdcU, 0x9dd738b9U,
      0xc5b428efU, 0x7d084f8aU, 0x6fbde064U, 0xd7018701U,
      0x4ad6bfb8U, 0xf26ad8ddU, 0xe0df7733U, 0x58631056U,
      0x5019579fU, 0xe8a530faU, 0xfa109f14U, 0x42acf871U,
      0xdf7bc0c8U, 0x67c7a7adU, 0x75720843U, 0xcdce6f26U,
      0x95ad7f70U, 0x2d111815U, 0x3fa4b7fbU, 0x8718d09eU,
      0x1acfe827U, 0xa2738f42U, 0xb0c620acU, 0x087a47c9U,
      0xa032af3eU, 0x188ec85bU, 0x0a3b67b5U, 0xb28700d0U,
      0x2f503869U, 0x97ec5f0cU, 0x8559f0e2U, 0x3de59787U,
      0x658687d1U, 0xdd3ae0b4U, 0xcf8f4f5aU, 0x7733283fU,
      0xeae41086U, 0x525877e3U, 0x40edd80dU, 0xf851bf68U,
      0xf02bf8a1U, 0x48979fc4U, 0x5a22302aU, 0xe29e574fU,
      0x7f496ff6U, 0xc7f50893U, 0xd540a77dU, 0x6dfcc018U,
      0x359fd04eU, 0x8d23b72bU, 0x9f9618c5U, 0x272a7fa0U,
      0xbafd4719U, 0x0241207cU, 0x10f48f92U, 0xa848e8f7U,
      0x9b14583dU, 0x23a83f58U, 0x311d90b6U, 0x89a1f7d3U,
      0x1476cf6aU, 0xaccaa80fU, 0xbe7f07e1U, 0x06c36084U,
      0x5ea070d2U, 0xe61c17b7U, 0xf4a9b859U, 0x4c15df3cU,
      0xd1c2e785U, 0x697e80e0U, 0x7bcb2f0eU, 0xc377486bU,
      0xcb0d0fa2U, 0x73b168c7U, 0x6104c729U, 0xd9b8a04cU,
      0x446f98f5U, 0xfcd3ff90U, 0xee66507eU, 0x56da371bU,
      0x0eb9274dU, 0xb6054028U, 0xa4b0efc6U, 0x1c0c88a3U,
      0x81dbb01aU, 0x3967d77fU, 0x2bd27891U, 0x936e1ff4U,
      0x3b26f703U, 0x839a9066U, 0x912f3f88U, 0x299358edU,
      0xb4446054U, 0x0cf80731U, 0x1e4da8dfU, 0xa6f1cfbaU,
      0xfe92dfecU, 0x462eb889U, 0x549b1767U, 0xec277002U,
      0x71f048bbU, 0xc94c2fdeU, 0xdbf98030U, 0x6345e755U,
      0x6b3fa09cU, 0xd383c7f9U, 0xc1366817U, 0x798a0f72U,
      0xe45d37cbU, 0x5ce150aeU, 0x4e54ff40U, 0xf6e89825U,
      0xae8b8873U, 0x1637ef16U, 0x048240f8U, 0xbc3e279dU,
      0x21e91f24U, 0x99557841U, 0x8be0d7afU, 0x335cb0caU,
      0xed59b63bU, 0x55e5d15eU, 0x47507eb0U, 0xffec19d5U,
      0x623b216cU, 0xda874609U, 0xc832e9e7U, 0x708e8e82U,
      0x28ed9ed4U, 0x9051f9b1U, 0x82e4565fU, 0x3a58313aU,
      0xa78f0983U, 0x1f336ee6U, 0x0d86c108U, 0xb53aa66dU,
      0xbd40e1a4U, 0x05fc86c1U, 0x1749292fU, 0xaff54e4aU,
      0x322276f3U, 0x8a9e1196U, 0x982bbe78U, 0x2097d91dU,
      0x78f4c94bU, 0xc048ae2eU, 0xd2fd01c0U, 0x6a4166a5U,
      0xf7965e1cU, 0x4f2a3979U, 0x5d9f9697U, 0xe523f1f2U,
      0x4d6b1905U, 0xf5d77e60U, 0xe762d18eU, 0x5fdeb6ebU,
      0xc2098e52U, 0x7ab5e937U, 0x680046d9U, 0xd0bc21bcU,
      0x88df31eaU, 0x3063568fU, 0x22d6f961U, 0x9a6a9e04U,
      0x07bda6bdU, 0xbf01c1d8U, 0xadb46e36U, 0x15080953U,
      0x1d724e9aU, 0xa5ce29ffU, 0xb77b8611U, 0x0fc7e174U,
      0x9210d9cdU, 0x2aacbea8U, 0x38191146U, 0x80a57623U,
      0xd8c66675U, 0x607a0110U, 0x72cfaefeU, 0xca73c99bU,
      0x57a4f122U, 0xef189647U, 0xfdad39a9U, 0x45115eccU,
      0x764dee06U, 0xcef18963U, 0xdc44268dU, 0x64f841e8U,
      0xf92f7951U, 0x41931e34U, 0x5326b1daU, 0xeb9ad6bfU,
      0xb3f9c6e9U, 0x0b45a18cU, 0x19f00e62U, 0xa14c6907U,
      0x3c9b51beU, 0x842736dbU, 0x96929935U, 0x2e2efe50U,
      0x2654b999U, 0x9ee8defcU, 0x8c5d7112U, 0x34e11677U,
      0xa9362eceU, 0x118a49abU, 0x033fe645U, 0xbb838120U,
      0xe3e09176U, 0x5b5cf613U, 0x49e959fdU, 0xf1553e98U,
      0x6c820621U, 0xd43e6144U, 0xc68bceaaU, 0x7e37a9cfU,
      0xd67f4138U, 0x6ec3265dU, 0x7c7689b3U, 0xc4caeed6U,
      0x591dd66fU, 0xe1a1b10aU, 0xf3141ee4U, 0x4ba87981U,
      0x13cb69d7U, 0xab770eb2U, 0xb9c2a15cU, 0x017ec639U,
      0x9ca9fe80U, 0x241599e5U, 0x36a0360bU, 0x8e1c516eU,
      0x866616a7U, 0x3eda71c2U, 0x2c6fde2cU, 0x94d3b949U,
      0x090481f0U, 0xb1b8e695U, 0xa30d497bU, 0x1bb12e1eU,
      0x43d23e48U, 0xfb6e592dU, 0xe9dbf6c3U, 0x516791a6U,
      0xccb0a91fU, 0x740cce7aU, 0x66b96194U, 0xde0506f1U,
    },
    {
      0x00000000U, 0x3d6029b0U, 0x7ac05360U, 0x47a07ad0U,
      0xf580a6c0U, 0xc8e08f70U, 0x8f40f5a0U, 0xb220dc10U,
      0x30704bc1U, 0x0d106271U, 0x4ab018a1U, 0x77d03111U,
      0xc5f0ed01U, 0xf890c4b1U, 0xbf30be61U, 0x825097d1U,
      0x60e09782U, 0x5d80be32U, 0x1a20c4e2U, 0x2740ed52U,
      0x95603142U, 0xa80018f2U, 0xefa06222U, 0xd2c04b92U,
      0x5090dc43U, 0x6df0f5f3U, 0x2a508f23U, 0x1730a693U,
      0xa5107a83U, 0x98705333U, 0xdfd029e3U, 0xe2b00053U,
      0xc1c12f04U, 0xfca106b4U, 0xbb017c64U, 0x866155d4U,
      0x344189c4U, 0x0921a074U, 0x4e81daa4U, 0x73e1f314U,
      0xf1b164c5U, 0xccd14d75U, 0x8b7137a5U, 0xb6111e15U,
      0x0431c205U, 0x3951ebb5U, 0x7ef19165U, 0x4391b8d5U,
      0xa121b886U, 0x9c419136U, 0xdbe1ebe6U, 0xe681c256U,
      0x54a11e46U, 0x69c137f6U, 0x2e614d26U, 0x13016496U,
      0x9151f347U, 0xac31daf7U, 0xeb91a027U, 0xd6f18997U,
      0x64d15587U, 0x59b17c37U, 0x1e1106e7U, 0x23712f57U,
      0x58f35849U, 0x659371f9U, 0x22330b29U, 0x1f532299U,
      0xad73fe89U, 0x9013d739U, 0xd7b3ade9U, 0xead38459U,
      0x68831388U, 0x55e33a38U, 0x124340e8U, 0x2f236958U,
      0x9d03b548U, 0xa0639cf8U, 0xe7c3e628U, 0xdaa3cf98U,
      0x3813cfcbU, 0x0573e67bU, 0x42d39cabU, 0x7fb3b51bU,
      0xcd93690bU, 0xf0f340bbU, 0xb7533a6bU, 0x8a3313dbU,
      0x0863840aU, 0x3503adbaU, 0x72a3d76aU, 0x4fc3fedaU,
      0xfde322caU, 0xc0830b7aU, 0x872371aaU, 0xba43581aU,
      0x9932774dU, 0xa4525efdU, 0xe3f2242dU, 0xde920d9dU,
      0x6cb2d18dU, 0x51d2f83dU, 0x167282edU, 0x2b12ab5dU,
      0xa9423c8cU, 0x9422153cU, 0xd3826fecU, 0xeee2465cU,
      0x5cc29a4cU, 0x61a2b3fcU, 0x2602c92cU, 0x1b62e09cU,
      0xf9d2e0cfU, 0xc4b2c97fU, 0x8312b3afU, 0xbe729a1fU,
      0x0c52460fU, 0x31326fbfU, 0x7692156fU, 0x4bf23cdfU,
      0xc9a2ab0eU, 0xf4c282beU, 0xb362f86eU, 0x8e02d1deU,
      0x3c220dceU, 0x0142247eU, 0x46e25eaeU, 0x7b82771eU,
      0xb1e6b092U, 0x8c869922U, 0xcb26e3f2U, 0xf646ca42U,
      0x44661652U, 0x79063fe2U, 0x3ea64532U, 0x03c66c82U,
      0x8196fb53U, 0xbcf6d2e3U, 0xfb56a833U, 0xc6368183U,
      0x74165d93U, 0x49767423U, 0x0ed60ef3U, 0x33b62743U,
      0xd1062710U, 0xec660ea0U, 0xabc67470U, 0x96a65dc0U,
      0x248681d0U, 0x19e6a860U, 0x5e46d2b0U, 0x6326fb00U,
      0xe1766cd1U, 0xdc164561U, 0x9bb63fb1U, 0xa6d61601U,
      0x14f6ca11U, 0x2996e3a1U, 0x6e369971U, 0x5356b0c1U,
      0x70279f96U, 0x4d47b626U, 0x0ae7ccf6U, 0x3787e546U,
      0x85a73956U, 0xb8c710e6U, 0xff676a36U, 0xc2074386U,
      0x4057d457U, 0x7d37fde7U, 0x3a978737U, 0x07f7ae87U,
      0xb5d77297U, 0x88b75b27U, 0xcf1721f7U, 0xf2770847U,
      0x10c70814U, 0x2da721a4U, 0x6a075b74U, 0x576772c4U,
      0xe547aed4U, 0xd8278764U, 0x9f87fdb4U, 0xa2e7d404U,
      0x20b743d5U, 0x1dd76a65U, 0x5a7710b5U, 0x67173905U,
      0xd537e515U, 0xe857cca5U, 0xaff7b675U, 0x92979fc5U,
      0xe915e8dbU, 0xd475c16bU, 0x93d5bbbbU, 0xaeb5920bU,
      0x1c954e1bU, 0x21f567abU, 0x66551d7bU, 0x5b3534cbU,
      0xd965a31aU, 0xe4058aaaU, 0xa3a5f07aU, 0x9ec5d9caU,
      0x2ce505daU, 0x11852c6aU, 0x562556baU, 0x6b457f0aU,
      0x89f57f59U, 0xb49556e9U, 0xf3352c39U, 0xce550589U,
      0x7c75d999U, 0x4115f029U, 0x06b58af9U, 0x3bd5a349U,
      0xb9853498U, 0x84e51d28U, 0xc34567f8U, 0xfe254e48U,
      0x4c059258U, 0x7165bbe8U, 0x36c5c138U, 0x0ba5e888U,
      0x28d4c7dfU, 0x15b4ee6fU, 0x521494bfU, 0x6f74bd0fU,
      0xdd54611fU, 0xe03448afU, 0xa794327fU, 0x9af41bcfU,
      0x18a48c1eU, 0x25c4a5aeU, 0x6264df7eU, 0x5f04f6ceU,
      0xed242adeU, 0xd044036eU, 0x97e479beU, 0xaa84500eU,
      0x4834505dU, 0x755479edU, 0x32f4033dU, 0x0f942a8dU,
      0xbdb4f69dU, 0x80d4df2dU, 0xc774a5fdU, 0xfa148c4dU,
      0x78441b9cU, 0x4524322cU, 0x028448fcU, 0x3fe4614cU,
      0x8dc4bd5cU, 0xb0a494ecU, 0xf704ee3cU, 0xca64c78cU,
    },
    {
      0x00000000U, 0xcb5cd3a5U, 0x4dc8a10bU, 0x869472aeU,
      0x9b914216U, 0x50cd91b3U, 0xd659e31dU, 0x1d0530b8U,
      0xec53826dU, 0x270f51c8U, 0xa19b2366U, 0x6ac7f0c3U,
      0x77c2c07bU, 0xbc9e13deU, 0x3a0a6170U, 0xf156b2d5U,
      0x03d6029bU, 0xc88ad13eU, 0x4e1ea390U, 0x85427035U,
      0x9847408dU, 0x531b9328U, 0xd58fe186U, 0x1ed33223U,
      0xef8580f6U, 0x24d95353U, 0xa24d21fdU, 0x6911f258U,
      0x7414c2e0U, 0xbf481145U, 0x39dc63ebU, 0xf280b04eU,
      0x07ac0536U, 0xccf0d693U, 0x4a64a43dU, 0x81387798U,
      0x9c3d4720U, 0x57619485U, 0xd1f5e62bU, 0x1aa9358eU,
      0xebff875bU, 0x20a354feU, 0xa6372650U, 0x6d6bf5f5U,
      0x706ec54dU, 0xbb3216e8U, 0x3da66446U, 0xf6fab7e3U,
      0x047a07adU, 0xcf26d408U, 0x49b2a6a6U, 0x82ee7503U,
      0x9feb45bbU, 0x54b7961eU, 0xd223e4b0U, 0x197f3715U,
      0xe82985c0U, 0x23755665U, 0xa5e124cbU, 0x6ebdf76eU,
      0x73b8c7d6U, 0xb8e41473U, 0x3e7066ddU, 0xf52cb578U,
      0x0f580a6cU, 0xc404d9c9U, 0x4290ab67U, 0x89cc78c2U,
      0x94c9487aU, 0x5f959bdfU, 0xd901e971U, 0x125d3ad4U,
      0xe30b8801U, 0x28575ba4U, 0xaec3290aU, 0x659ffaafU,
      0x789aca17U, 0xb3c619b2U, 0x35526b1cU, 0xfe0eb8b9U,
      0x0c8e08f7U, 0xc7d2db52U, 0x4146a9fcU, 0x8a1a7a59U,
      0x971f4ae1U, 0x5c439944U, 0xdad7ebeaU, 0x118b384fU,
      0xe0dd8a9aU, 0x2b81593fU, 0xad152b91U, 0x6649f834U,
      0x7b4cc88cU, 0xb0101b29U, 0x36846987U, 0xfdd8ba22U,
      0x08f40f5aU, 0xc3a8dcffU, 0x453cae51U, 0x8e607df4U,
      0x93654d4cU, 0x58399ee9U, 0xdeadec47U, 0x15f13fe2U,
      0xe4a78d37U, 0x2ffb5e92U, 0xa96f2c3cU, 0x6233ff99U,
      0x7f36cf21U, 0xb46a1c84U, 0x32fe6e2aU, 0xf9a2bd8fU,
      0x0b220dc1U, 0xc07ede64U, 0x46eaaccaU, 0x8db67f6fU,
      0x90b34fd7U, 0x5bef9c72U, 0xdd7beedcU, 0x16273d79U,
      0xe7718facU, 0x2c2d5c09U, 0xaab92ea7U, 0x61e5fd02U,
      0x7ce0cdbaU, 0xb7bc1e1fU, 0x31286cb1U, 0xfa74bf14U,
      0x1eb014d8U, 0xd5ecc77dU, 0x5378b5d3U, 0x98246676U,
      0x852156ceU, 0x4e7d856bU, 0xc8e9f7c5U, 0x03b52460U,
      0xf2e396b5U, 0x39bf4510U, 0xbf2b37beU, 0x7477e41bU,
      0x6972d4a3U, 0xa22e0706U, 0x24ba75a8U, 0xefe6a60dU,
      0x1d661643U, 0xd63ac5e6U, 0x50aeb748U, 0x9bf264edU,
      0x86f75455U, 0x4dab87f0U, 0xcb3ff55eU, 0x006326fbU,
      0xf135942eU, 0x3a69478bU, 0xbcfd3525U, 0x77a1e680U,
      0x6aa4d638U, 0xa1f8059dU, 0x276c7733U, 0xec30a496U,
      0x191c11eeU, 0xd240c24bU, 0x54d4b0e5U, 0x9f886340U,
      0x828d53f8U, 0x49d1805dU, 0xcf45f2f3U, 0x04192156U,
      0xf54f9383U, 0x3e134026U, 0xb8873288U, 0x73dbe12dU,
      0x6eded195U, 0xa5820230U, 0x2316709eU, 0xe84aa33bU,
      0x1aca1375U, 0xd196c0d0U, 0x5702b27eU, 0x9c5e61dbU,
      0x815b5163U, 0x4a0782c6U, 0xcc93f068U, 0x07cf23cdU,
      0xf6999118U, 0x3dc542bdU, 0xbb513013U, 0x700de3b6U,
      0x6d08d30eU, 0xa65400abU, 0x20c07205U, 0xeb9ca1a0U,
      0x11e81eb4U, 0xdab4cd11U, 0x5c20bfbfU, 0x977c6c1aU,
      0x8a795ca2U, 0x41258f07U, 0xc7b1fda9U, 0x0ced2e0cU,
      0xfdbb9cd9U, 0x36e74f7cU, 0xb0733dd2U, 0x7b2fee77U,
      0x662adecfU, 0xad760d6aU, 0x2be27fc4U, 0xe0beac61U,
      0x123e1c2fU, 0xd962cf8aU, 0x5ff6bd24U, 0x94aa6e81U,
      0x89af5e39U, 0x42f38d9cU, 0xc467ff32U, 0x0f3b2c97U,
      0xfe6d9e42U, 0x35314de7U, 0xb3a53f49U, 0x78f9ececU,
      0x65fcdc54U, 0xaea00ff1U, 0x28347d5fU, 0xe368aefaU,
      0x16441b82U, 0xdd18c827U, 0x5b8cba89U, 0x90d0692cU,
      0x8dd55994U, 0x46898a31U, 0xc01df89fU, 0x0b412b3aU,
      0xfa1799efU, 0x314b4a4aU, 0xb7df38e4U, 0x7c83eb41U,
      0x6186dbf9U, 0xaada085cU, 0x2c4e7af2U, 0xe712a957U,
      0x15921919U, 0xdececabcU, 0x585ab812U, 0x93066bb7U,
      0x8e035b0fU, 0x455f88aaU, 0xc3cbfa04U, 0x089729a1U,
      0xf9c19b74U, 0x329d48d1U, 0xb4093a7fU, 0x7f55e9daU,
      0x6250d962U, 0xa90c0ac7U, 0x2f987869U, 0xe4c4abccU,
    },
    {
      0x00000000U, 0xa6770bb4U, 0x979f1129U, 0x31e81a9dU,
      0xf44f2413U, 0x52382fa7U, 0x63d0353aU, 0xc5a73e8eU,
      0x33ef4e67U, 0x959845d3U, 0xa4705f4eU, 0x020754faU,
      0xc7a06a74U, 0x61d761c0U, 0x503f7b5dU, 0xf64870e9U,
      0x67de9cceU, 0xc1a9977aU, 0xf0418de7U, 0x56368653U,
      0x9391b8ddU, 0x35e6b369U, 0x040ea9f4U, 0xa279a240U,
      0x5431d2a9U, 0xf246d91dU, 0xc3aec380U, 0x65d9c834U,
      0xa07ef6baU, 0x0609fd0eU, 0x37e1e793U, 0x9196ec27U,
      0xcfbd399cU, 0x69ca3228U, 0x582228b5U, 0xfe552301U,
      0x3bf21d8fU, 0x9d85163bU, 0xac6d0ca6U, 0x0a1a0712U,
      0xfc5277fbU, 0x5a257c4fU, 0x6bcd66d2U, 0xcdba6d66U,
      0x081d53e8U, 0xae6a585cU, 0x9f8242c1U, 0x39f54975U,
      0xa863a552U, 0x0e14aee6U, 0x3ffcb47bU, 0x998bbfcfU,
      0x5c2c8141U, 0xfa5b8af5U, 0xcbb39068U, 0x6dc49bdcU,
      0x9b8ceb35U, 0x3dfbe081U, 0x0c13fa1cU, 0xaa64f1a8U,
      0x6fc3cf26U, 0xc9b4c492U, 0xf85cde0fU, 0x5e2bd5bbU,
      0x440b7579U, 0xe27c7ecdU, 0xd3946450U, 0x75e36fe4U,
      0xb044516aU, 0x16335adeU, 0x27db4043U, 0x81ac4bf7U,
      0x77e43b1eU, 0xd19330aaU, 0xe07b2a37U, 0x460c2183U,
      0x83ab1f0dU, 0x25dc14b9U, 0x14340e24U, 0xb2430590U,
      0x23d5e9b7U, 0x85a2e203U, 0xb44af89eU, 0x123df32aU,
      0xd79acda4U, 0x71edc610U, 0x4005dc8dU, 0xe672d739U,
      0x103aa7d0U, 0xb64dac64U, 0x87a5b6f9U, 0x21d2bd4dU,
      0xe47583c3U, 0x42028877U, 0x73ea92eaU, 0xd59d995eU,
      0x8bb64ce5U, 0x2dc14751U, 0x1c295dccU, 0xba5e5678U,
      0x7ff968f6U, 0xd98e6342U, 0xe86679dfU, 0x4e11726bU,
      0xb8590282U, 0x1e2e0936U, 0x2fc613abU, 0x89b1181fU,
      0x4c162691U, 0xea612d25U, 0xdb8937b8U, 0x7dfe3c0cU,
      0xec68d02bU, 0x4a1fdb9fU, 0x7bf7c102U, 0xdd80cab6U,
      0x1827f438U, 0xbe50ff8cU, 0x8fb8e511U, 0x29cfeea5U,
      0xdf879e4cU, 0x79f095f8U, 0x48188f65U, 0xee6f84d1U,
      0x2bc8ba5fU, 0x8dbfb1ebU, 0xbc57ab76U, 0x1a20a0c2U,
      0x8816eaf2U, 0x2e61e146U, 0x1f89fbdbU, 0xb9fef06fU,
      0x7c59cee1U, 0xda2ec555U, 0xebc6dfc8U, 0x4db1d47cU,
      0xbbf9a495U, 0x1d8eaf21U, 0x2c66b5bcU, 0x8a11be08U,
      0x4fb68086U, 0xe9c18b32U, 0xd82991afU, 0x7e5e9a1bU,
      0xefc8763cU, 0x49bf7d88U, 0x78576715U, 0xde206ca1U,
      0x1b87522fU, 0xbdf0599bU, 0x8c184306U, 0x2a6f48b2U,
      0xdc27385bU, 0x7a5033efU, 0x4bb82972U, 0xedcf22c6U,
      0x28681c48U, 0x8e1f17fcU, 0xbff70d61U, 0x198006d5U,
      0x47abd36eU, 0xe1dcd8daU, 0xd034c247U, 0x7643c9f3U,
      0xb3e4f77dU, 0x1593fcc9U, 0x247be654U, 0x820cede0U,
      0x74449d09U, 0xd23396bdU, 0xe3db8c20U, 0x45ac8794U,
      0x800bb91aU, 0x267cb2aeU, 0x1794a833U, 0xb1e3a387U,
      0x20754fa0U, 0x86024414U, 0xb7ea5e89U, 0x119d553dU,
      0xd43a6bb3U, 0x724d6007U, 0x43a57a9aU, 0xe5d2712eU,
      0x139a01c7U, 0xb5ed0a73U, 0x840510eeU, 0x22721b5aU,
      0xe7d525d4U, 0x41a22e60U, 0x704a34fdU, 0xd63d3f49U,
      0xcc1d9f8bU, 0x6a6a943fU, 0x5b828ea2U, 0xfdf58516U,
      0x3852bb98U, 0x9e25b02cU, 0xafcdaab1U, 0x09baa105U,
      0xfff2d1ecU, 0x5985da58U, 0x686dc0c5U, 0xce1acb71U,
      0x0bbdf5ffU, 0xadcafe4bU, 0x9c22e4d6U, 0x3a55ef62U,
      0xabc30345U, 0x0db408f1U, 0x3c5c126cU, 0x9a2b19d8U,
      0x5f8c2756U, 0xf9fb2ce2U, 0xc813367fU, 0x6e643dcbU,
      0x982c4d22U, 0x3e5b4696U, 0x0fb35c0bU, 0xa9c457bfU,
      0x6c636931U, 0xca146285U, 0xfbfc7818U, 0x5d8b73acU,
      0x03a0a617U, 0xa5d7ada3U, 0x943fb73eU, 0x3248bc8aU,
      0xf7ef8204U, 0x519889b0U, 0x6070932dU, 0xc6079899U,
      0x304fe870U, 0x9638e3c4U, 0xa7d0f959U, 0x01a7f2edU,
      0xc400cc63U, 0x6277c7d7U, 0x539fdd4aU, 0xf5e8d6feU,
      0x647e3ad9U, 0xc209316dU, 0xf3e12bf0U, 0x55962044U,
      0x90311ecaU, 0x3646157eU, 0x07ae0fe3U, 0xa1d90457U,
      0x579174beU, 0xf1e67f0aU, 0xc00e6597U, 0x66796e23U,
      0xa3de50adU, 0x05a95b19U, 0x34414184U, 0x92364a30U,
    },
    {
      0x00000000U, 0xccaa009eU, 0x4225077dU, 0x8e8f07e3U,
      0x844a0efaU, 0x48e00e64U, 0xc66f0987U, 0x0ac50919U,
      0xd3e51bb5U, 0x1f4f1b2bU, 0x91c01cc8U, 0x5d6a1c56U,
      0x57af154fU, 0x9b0515d1U, 0x158a1232U, 0xd92012acU,
      0x7cbb312bU, 0xb01131b5U, 0x3e9e3656U, 0xf23436c8U,
      0xf8f13fd1U, 0x345b3f4fU, 0xbad438acU, 0x767e3832U,
      0xaf5e2a9eU, 0x63f42a00U, 0xed7b2de3U, 0x21d12d7dU,
      0x2b142464U, 0xe7be24faU, 0x69312319U, 0xa59b2387U,
      0xf9766256U, 0x35dc62c8U, 0xbb53652bU, 0x77f965b5U,
      0x7d3c6cacU, 0xb1966c32U, 0x3f196bd1U, 0xf3b36b4fU,
      0x2a9379e3U, 0xe639797dU, 0x68b67e9eU, 0xa41c7e00U,
      0xaed97719U, 0x62737787U, 0xecfc7064U, 0x205670faU,
      0x85cd537dU, 0x496753e3U, 0xc7e85400U, 0x0b42549eU,
      0x01875d87U, 0xcd2d5d19U, 0x43a25afaU, 0x8f085a64U,
      0x562848c8U, 0x9a824856U, 0x140d4fb5U, 0xd8a74f2bU,
      0xd2624632U, 0x1ec846acU, 0x9047414fU, 0x5ced41d1U,
      0x299dc2edU, 0xe537c273U, 0x6bb8c590U, 0xa712c50eU,
      0xadd7cc17U, 0x617dcc89U, 0xeff2cb6aU, 0x2358cbf4U,
      0xfa78d958U, 0x36d2d9c6U, 0xb85dde25U, 0x74f7debbU,
      0x7e32d7a2U, 0xb298d73cU, 0x3c17d0dfU, 0xf0bdd041U,
      0x5526f3c6U, 0x998cf358U, 0x1703f4bbU, 0xdba9f425U,
      0xd16cfd3cU, 0x1dc6fda2U, 0x9349fa41U, 0x5fe3fadfU,
      0x86c3e873U, 0x4a69e8edU, 0xc4e6ef0eU, 0x084cef90U,
      0x0289e689U, 0xce23e617U, 0x40ace1f4U, 0x8c06e16aU,
      0xd0eba0bbU, 0x1c41a025U, 0x92cea7c6U, 0x5e64a758U,
      0x54a1ae41U, 0x980baedfU, 0x1684a93cU, 0xda2ea9a2U,
      0x030ebb0eU, 0xcfa4bb90U, 0x412bbc73U, 0x8d81bcedU,
      0x8744b5f4U, 0x4beeb56aU, 0xc561b289U, 0x09cbb217U,
      0xac509190U, 0x60fa910eU, 0xee7596edU, 0x22df9673U,
      0x281a9f6aU, 0xe4b09ff4U, 0x6a3f9817U, 0xa6959889U,
      0x7fb58a25U, 0xb31f8abbU, 0x3d908d58U, 0xf13a8dc6U,
      0xfbff84dfU, 0x37558441U, 0xb9da83a2U, 0x7570833cU,
      0x533b85daU, 0x9f918544U, 0x111e82a7U, 0xddb48239U,
      0xd7718b20U, 0x1bdb8bbeU, 0x95548c5dU, 0x59fe8cc3U,
      0x80de9e6fU, 0x4c749ef1U, 0xc2fb9912U, 0x0e51998cU,
      0x04949095U, 0xc83e900bU, 0x46b197e8U, 0x8a1b9776U,
      0x2f80b4f1U, 0xe32ab46fU, 0x6da5b38cU, 0xa10fb312U,
      0xabcaba0bU, 0x6760ba95U, 0xe9efbd76U, 0x2545bde8U,
      0xfc65af44U, 0x30cfafdaU, 0xbe40a839U, 0x72eaa8a7U,
      0x782fa1beU, 0xb485a120U, 0x3a0aa6c3U, 0xf6a0a65dU,
      0xaa4de78cU, 0x66e7e712U, 0xe868e0f1U, 0x24c2e06fU,
      0x2e07e976U, 0xe2ade9e8U, 0x6c22ee0bU, 0xa088ee95U,
      0x79a8fc39U, 0xb502fca7U, 0x3b8dfb44U, 0xf727fbdaU,
      0xfde2f2c3U, 0x3148f25dU, 0xbfc7f5beU, 0x736df520U,
      0xd6f6d6a7U, 0x1a5cd639U, 0x94d3d1daU, 0x5879d144U,
      0x52bcd85dU, 0x9e16d8c3U, 0x1099df20U, 0xdc33dfbeU,
      0x0513cd12U, 0xc9b9cd8cU, 0x4736ca6fU, 0x8b9ccaf1U,
      0x8159c3e8U, 0x4df3c376U, 0xc37cc495U, 0x0fd6c40bU,
      0x7aa64737U, 0xb60c47a9U, 0x3883404aU, 0xf42940d4U,
      0xfeec49cdU, 0x32464953U, 0xbcc94eb0U, 0x70634e2eU,
      0xa9435c82U, 0x65e95c1cU, 0xeb665bffU, 0x27cc5b61U,
      0x2d095278U, 0xe1a352e6U, 0x6f2c5505U, 0xa386559bU,
      0x061d761cU, 0xcab77682U, 0x44387161U, 0x889271ffU,
      0x825778e6U, 0x4efd7878U, 0xc0727f9bU, 0x0cd87f05U,
      0xd5f86da9U, 0x19526d37U, 0x97dd6ad4U, 0x5b776a4aU,
      0x51b26353U, 0x9d1863cdU, 0x1397642eU, 0xdf3d64b0U,
      0x83d02561U, 0x4f7a25ffU, 0xc1f5221cU, 0x0d5f2282U,
      0x079a2b9bU, 0xcb302b05U, 0x45bf2ce6U, 0x89152c78U,
      0x50353ed4U, 0x9c9f3e4aU, 0x121039a9U, 0xdeba3937U,
      0xd47f302eU, 0x18d530b0U, 0x965a3753U, 0x5af037cdU,
      0xff6b144aU, 0x33c114d4U, 0xbd4e1337U, 0x71e413a9U,
      0x7b211ab0U, 0xb78b1a2eU, 0x39041dcdU, 0xf5ae1d53U,
      0x2c8e0fffU, 0xe0240f61U, 0x6eab0882U, 0xa201081cU,
      0xa8c40105U, 0x646e019bU, 0xeae10678U, 0x264b06e6U,
    },
  };
  uint32_t crc = UINT32_MAX;

  while (size >= 8)
    {
      uint32_t word = (uint32_t) data[0] | ((uint32_t) data[1] << 8) |
                      ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);

      crc ^= word;
      crc = table[7][crc & 0xff] ^ table[6][(crc >> 8) & 0xff] ^
            table[5][(crc >> 16) & 0xff] ^ table[4][crc >> 24] ^
            table[3][data[4]] ^ table[2][data[5]] ^
            table[1][data[6]] ^ table[0][data[7]];
      data += 8;
      size -= 8;
    }
  for (size_t i = 0; i < size; i++)
    crc = (crc >> 8) ^ table[0][(crc ^ data[i]) & 0xff];
  return ~crc;
}

static uint8_t *
milan_pack_tagged_u32 (uint8_t *output,
                       uint8_t  tag,
                       uint32_t value)
{
  *output++ = tag;
  goodix_milan_template_write_u32 (output, value);
  return output + 4;
}

static int
milan_template_metadata_valid (const GoodixMilanTemplateMetadata *metadata,
                               size_t                              feature_count)
{
  if (!metadata || feature_count == 0 ||
      feature_count > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY ||
      metadata->maximum_features == 0 ||
      metadata->maximum_features > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY ||
      feature_count > metadata->maximum_features ||
      metadata->queue_state > 1 || metadata->graph_established > 1)
    return 0;
  if (metadata->graph_established != 0)
    return metadata->graph_reference_index >= 0 &&
           (size_t) metadata->graph_reference_index < feature_count;
  return metadata->graph_reference_index == -1 ||
         (metadata->graph_reference_index >= 0 &&
          (size_t) metadata->graph_reference_index < feature_count);
}

int
goodix_milan_template_pack (
  const uint8_t *const                 *feature_elements,
  const size_t                         *feature_element_sizes,
  size_t                                feature_count,
  const GoodixMilanTemplateRelation    *relations,
  size_t                                relation_count,
  const GoodixMilanTemplateMetadata    *metadata,
  const uint8_t                        *tail_state,
  size_t                                tail_state_size,
  uint8_t                              *packed,
  size_t                                packed_capacity,
  size_t                               *packed_size)
{
  static const uint8_t header_tags[MILAN_TEMPLATE_HEADER_FIELD_COUNT] = {
    MILAN_TEMPLATE_TAG_HEADER_FORMAT, MILAN_TEMPLATE_TAG_SENSOR_TYPE,
    MILAN_TEMPLATE_TAG_ROWS, MILAN_TEMPLATE_TAG_COLUMNS,
    MILAN_TEMPLATE_TAG_FEATURE_COUNT, MILAN_TEMPLATE_TAG_MAXIMUM_FEATURES,
    MILAN_TEMPLATE_TAG_REGISTRATION_COUNT,
    MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS,
    MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS_COPY,
    MILAN_TEMPLATE_TAG_GEOMETRY_PRIMARY,
    MILAN_TEMPLATE_TAG_GEOMETRY_SECONDARY, MILAN_TEMPLATE_TAG_QUEUE_STATE,
    MILAN_TEMPLATE_TAG_QUEUE_COUNTER,
  };
  size_t total_size = MILAN_TEMPLATE_FIXED_SIZE;
  uint8_t *output;

  if (!feature_elements || !feature_element_sizes || feature_count == 0 ||
      feature_count > UINT32_MAX || relation_count > UINT32_MAX ||
      (relation_count != 0 && !relations) ||
      !milan_template_metadata_valid (metadata, feature_count) || !tail_state ||
      tail_state_size < MILAN_TEMPLATE_TAIL_STATE_SIZE || !packed ||
      !packed_size ||
      relation_count > (SIZE_MAX - total_size) / MILAN_TEMPLATE_RELATION_SIZE)
    return -1;
  total_size += relation_count * MILAN_TEMPLATE_RELATION_SIZE;
  for (size_t i = 0; i < feature_count; i++)
    {
      if (!feature_elements[i] || feature_element_sizes[i] > UINT32_MAX ||
          feature_element_sizes[i] > SIZE_MAX - total_size)
        return -1;
      total_size += feature_element_sizes[i];
    }
  if (packed_capacity < total_size || total_size - 10 > UINT32_MAX)
    return -1;

  const uint32_t header_values[MILAN_TEMPLATE_HEADER_FIELD_COUNT] = {
    0x11f248ea, metadata->sensor_type,
    GOODIX_MILAN_EXTRACTION_CLASSIFICATION_ROWS,
    GOODIX_MILAN_EXTRACTION_CLASSIFICATION_COLUMNS, (uint32_t) feature_count,
    metadata->maximum_features, metadata->registration_count,
    metadata->maximum_records, metadata->maximum_records, 1, 1,
    metadata->queue_state, metadata->queue_transaction_counter,
  };
  packed[0] = MILAN_TEMPLATE_TAG_ENVELOPE;
  packed[5] = MILAN_TEMPLATE_TAG_PAYLOAD;
  output = packed + MILAN_TEMPLATE_ENVELOPE_SIZE;
  for (size_t i = 0; i < MILAN_TEMPLATE_HEADER_FIELD_COUNT; i++)
    output = milan_pack_tagged_u32 (output, header_tags[i], header_values[i]);
  for (size_t i = 0; i < feature_count; i++)
    {
      memcpy (output, feature_elements[i], feature_element_sizes[i]);
      output += feature_element_sizes[i];
    }
  for (size_t i = 0; i < relation_count; i++)
    {
      static const uint8_t relation_tags[8] = {
        MILAN_TEMPLATE_TAG_RELATION_INDEX,
        MILAN_TEMPLATE_TAG_RELATION_LEADING,
        MILAN_TEMPLATE_TAG_RELATION_VALUE_1,
        MILAN_TEMPLATE_TAG_RELATION_VALUE_2,
        MILAN_TEMPLATE_TAG_RELATION_VALUE_3,
        MILAN_TEMPLATE_TAG_RELATION_VALUE_4,
        MILAN_TEMPLATE_TAG_RELATION_VALUE_5,
        MILAN_TEMPLATE_TAG_RELATION_VALUE_6,
      };

      *output++ = MILAN_TEMPLATE_TAG_RELATION;
      goodix_milan_template_write_u32 (
        output, MILAN_TEMPLATE_RELATION_PAYLOAD_SIZE);
      output += 4;
      output = milan_pack_tagged_u32 (
        output, relation_tags[0], (uint32_t) relations[i].index);
      for (size_t value = 0; value < 7; value++)
        output = milan_pack_tagged_u32 (
          output, relation_tags[value + 1],
          (uint32_t) relations[i].values[value]);
    }
  *output++ = MILAN_TEMPLATE_TAG_GRAPH;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_GRAPH_PAYLOAD_SIZE);
  output += 4;
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_REFERENCE,
    (uint32_t) metadata->graph_reference_index);
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_COMPANION_FIRST,
    (uint32_t) metadata->graph_companion_f3);
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_COMPANION_SECOND,
    (uint32_t) metadata->graph_companion_f4);
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_ESTABLISHED,
    metadata->graph_established);
  *output++ = MILAN_TEMPLATE_TAG_TAIL;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_PAYLOAD_SIZE);
  output += 4;
  *output++ = MILAN_TEMPLATE_TAG_TAIL_RECORDS;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_RECORDS_SIZE);
  output += 4;
  memcpy (output, tail_state, MILAN_TEMPLATE_TAIL_RECORDS_SIZE);
  output += MILAN_TEMPLATE_TAIL_RECORDS_SIZE;
  uint32_t value;
  memcpy (&value, tail_state + 0xc8, sizeof(value));
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_TAIL_SCALAR, value);
  *output++ = MILAN_TEMPLATE_TAG_TAIL_VECTOR;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_VECTOR_SIZE);
  output += 4;
  memcpy (output, tail_state + 0xcc, MILAN_TEMPLATE_TAIL_VECTOR_SIZE);
  output += MILAN_TEMPLATE_TAIL_VECTOR_SIZE;
  *output++ = MILAN_TEMPLATE_TAG_TAIL_BITMAP;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_BITMAP_SIZE);
  output += 4;
  memcpy (output, tail_state + 0x10c, MILAN_TEMPLATE_TAIL_BITMAP_SIZE);
  output += MILAN_TEMPLATE_TAIL_BITMAP_SIZE;
  for (size_t i = 0; i < 4; i++)
    {
      memcpy (&value, tail_state + 0x50c + i * 4, sizeof(value));
      output = milan_pack_tagged_u32 (
        output, (uint8_t) (MILAN_TEMPLATE_TAG_TAIL_TRAILER_FIRST + i), value);
    }

  *packed_size = (size_t) (output - packed);
  if (*packed_size != total_size)
    return -1;
  uint32_t payload_size =
    (uint32_t) (total_size - MILAN_TEMPLATE_ENVELOPE_SIZE);
  goodix_milan_template_write_u32 (packed + 6, payload_size);
  goodix_milan_template_write_u32 (packed + 1,
                    milan_template_crc32 (
                      packed + MILAN_TEMPLATE_ENVELOPE_SIZE, payload_size));
  return 0;
}

int
goodix_milan_template_unpack (
  const uint8_t               *packed,
  size_t                       packed_size,
  GoodixMilanUnpackedTemplate *unpacked)
{
  static const uint8_t header_tags[MILAN_TEMPLATE_HEADER_FIELD_COUNT] = {
    MILAN_TEMPLATE_TAG_HEADER_FORMAT, MILAN_TEMPLATE_TAG_SENSOR_TYPE,
    MILAN_TEMPLATE_TAG_ROWS, MILAN_TEMPLATE_TAG_COLUMNS,
    MILAN_TEMPLATE_TAG_FEATURE_COUNT, MILAN_TEMPLATE_TAG_MAXIMUM_FEATURES,
    MILAN_TEMPLATE_TAG_REGISTRATION_COUNT,
    MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS,
    MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS_COPY,
    MILAN_TEMPLATE_TAG_GEOMETRY_PRIMARY,
    MILAN_TEMPLATE_TAG_GEOMETRY_SECONDARY, MILAN_TEMPLATE_TAG_QUEUE_STATE,
    MILAN_TEMPLATE_TAG_QUEUE_COUNTER,
  };
  static const uint8_t relation_tags[8] = {
    MILAN_TEMPLATE_TAG_RELATION_INDEX, MILAN_TEMPLATE_TAG_RELATION_LEADING,
    MILAN_TEMPLATE_TAG_RELATION_VALUE_1, MILAN_TEMPLATE_TAG_RELATION_VALUE_2,
    MILAN_TEMPLATE_TAG_RELATION_VALUE_3, MILAN_TEMPLATE_TAG_RELATION_VALUE_4,
    MILAN_TEMPLATE_TAG_RELATION_VALUE_5, MILAN_TEMPLATE_TAG_RELATION_VALUE_6,
  };
  uint32_t header_values[MILAN_TEMPLATE_HEADER_FIELD_COUNT];
  size_t cursor = MILAN_TEMPLATE_ENVELOPE_SIZE;

  if (!packed || !unpacked || packed_size < MILAN_TEMPLATE_FIXED_SIZE ||
      packed[0] != MILAN_TEMPLATE_TAG_ENVELOPE ||
      packed[5] != MILAN_TEMPLATE_TAG_PAYLOAD ||
      goodix_milan_template_read_u32 (packed + 6) !=
        packed_size - MILAN_TEMPLATE_ENVELOPE_SIZE ||
      goodix_milan_template_read_u32 (packed + 1) !=
        milan_template_crc32 (
          packed + MILAN_TEMPLATE_ENVELOPE_SIZE,
          packed_size - MILAN_TEMPLATE_ENVELOPE_SIZE))
    return -1;
  memset (unpacked, 0, sizeof(*unpacked));
  for (size_t i = 0; i < MILAN_TEMPLATE_HEADER_FIELD_COUNT; i++)
    {
      if (cursor + 5 > packed_size || packed[cursor] != header_tags[i])
        return -1;
      header_values[i] = goodix_milan_template_read_u32 (packed + cursor + 1);
      cursor += 5;
    }
  if (header_values[0] != 0x11f248ea ||
      (header_values[1] != 0 &&
       header_values[1] != GOODIX_MILAN_PRINT_SENSOR_TYPE) ||
      header_values[2] != GOODIX_MILAN_EXTRACTION_CLASSIFICATION_ROWS ||
      header_values[3] != GOODIX_MILAN_EXTRACTION_CLASSIFICATION_COLUMNS ||
      header_values[4] == 0 ||
      header_values[4] > GOODIX_MILAN_TEMPLATE_FEATURE_CAPACITY ||
      header_values[7] != header_values[8] || header_values[9] != 1 ||
       header_values[10] != 1)
    return -1;
  unpacked->feature_count = header_values[4];
  unpacked->metadata.sensor_type = header_values[1];
  unpacked->metadata.maximum_features = header_values[5];
  unpacked->metadata.registration_count = header_values[6];
  unpacked->metadata.maximum_records = header_values[7];
  unpacked->metadata.queue_state = header_values[11];
  unpacked->metadata.queue_transaction_counter = header_values[12];

  for (size_t i = 0; i < unpacked->feature_count; i++)
    {
      if (cursor + 5 > packed_size ||
          packed[cursor] != MILAN_FEATURE_TAG_ELEMENT)
        return -1;
      size_t element_size = 5 + goodix_milan_template_read_u32 (packed + cursor + 1);

      if (element_size > packed_size - cursor)
        return -1;
      unpacked->feature_elements[i] = packed + cursor;
      unpacked->feature_element_sizes[i] = element_size;
      cursor += element_size;
    }
  while (cursor < packed_size &&
         packed[cursor] == MILAN_TEMPLATE_TAG_RELATION)
    {
      if (unpacked->relation_count >= GOODIX_MILAN_TEMPLATE_RELATION_CAPACITY ||
          cursor + MILAN_TEMPLATE_RELATION_SIZE > packed_size ||
          goodix_milan_template_read_u32 (packed + cursor + 1) !=
            MILAN_TEMPLATE_RELATION_PAYLOAD_SIZE)
        return -1;
      GoodixMilanTemplateRelation *relation =
        &unpacked->relations[unpacked->relation_count++];
      size_t relation_cursor = cursor + 5;

      for (size_t i = 0; i < 8; i++)
        {
          if (packed[relation_cursor] != relation_tags[i])
            return -1;
          int32_t value = (int32_t) goodix_milan_template_read_u32 (
            packed + relation_cursor + 1);
          if (i == 0)
            relation->index = value;
          else
            relation->values[i - 1] = value;
          relation_cursor += 5;
        }
      cursor += MILAN_TEMPLATE_RELATION_SIZE;
    }
  if (cursor + MILAN_TEMPLATE_GRAPH_SIZE > packed_size ||
      packed[cursor] != MILAN_TEMPLATE_TAG_GRAPH ||
      goodix_milan_template_read_u32 (packed + cursor + 1) !=
        MILAN_TEMPLATE_GRAPH_PAYLOAD_SIZE)
    return -1;
  cursor += 5;
  for (size_t i = 0; i < 4; i++)
    {
      if (packed[cursor] != MILAN_TEMPLATE_TAG_GRAPH_REFERENCE + i)
        return -1;
      uint32_t value = goodix_milan_template_read_u32 (packed + cursor + 1);

      if (i == 0)
        unpacked->metadata.graph_reference_index = (int32_t) value;
      else if (i == 1)
        unpacked->metadata.graph_companion_f3 = (int32_t) value;
      else if (i == 2)
        unpacked->metadata.graph_companion_f4 = (int32_t) value;
      else
        unpacked->metadata.graph_established = value;
      cursor += 5;
    }
  if (!milan_template_metadata_valid (&unpacked->metadata,
                                      unpacked->feature_count) ||
      (unpacked->metadata.sensor_type == GOODIX_MILAN_PRINT_SENSOR_TYPE &&
       unpacked->metadata.graph_established == 0 &&
       unpacked->relation_count != 0))
    return -1;
  if (cursor + 5 > packed_size || packed[cursor] != MILAN_TEMPLATE_TAG_TAIL ||
      goodix_milan_template_read_u32 (packed + cursor + 1) !=
        MILAN_TEMPLATE_TAIL_PAYLOAD_SIZE)
    return -1;
  cursor += 5;
  if (cursor + MILAN_TEMPLATE_TAGGED_U32_SIZE +
        MILAN_TEMPLATE_TAIL_RECORDS_SIZE > packed_size ||
      packed[cursor] != MILAN_TEMPLATE_TAG_TAIL_RECORDS ||
      goodix_milan_template_read_u32 (packed + cursor + 1) !=
        MILAN_TEMPLATE_TAIL_RECORDS_SIZE)
    return -1;
  memcpy (unpacked->tail_state, packed + cursor + 5,
          MILAN_TEMPLATE_TAIL_RECORDS_SIZE);
  cursor += MILAN_TEMPLATE_TAGGED_U32_SIZE + MILAN_TEMPLATE_TAIL_RECORDS_SIZE;
  if (cursor + 5 > packed_size ||
      packed[cursor] != MILAN_TEMPLATE_TAG_TAIL_SCALAR)
    return -1;
  memcpy (unpacked->tail_state + 0xc8, packed + cursor + 1, 4);
  cursor += 5;
  if (cursor + MILAN_TEMPLATE_TAGGED_U32_SIZE +
        MILAN_TEMPLATE_TAIL_VECTOR_SIZE > packed_size ||
      packed[cursor] != MILAN_TEMPLATE_TAG_TAIL_VECTOR ||
      goodix_milan_template_read_u32 (packed + cursor + 1) !=
        MILAN_TEMPLATE_TAIL_VECTOR_SIZE)
    return -1;
  memcpy (unpacked->tail_state + 0xcc, packed + cursor + 5,
          MILAN_TEMPLATE_TAIL_VECTOR_SIZE);
  cursor += MILAN_TEMPLATE_TAGGED_U32_SIZE + MILAN_TEMPLATE_TAIL_VECTOR_SIZE;
  if (cursor + MILAN_TEMPLATE_TAGGED_U32_SIZE +
        MILAN_TEMPLATE_TAIL_BITMAP_SIZE > packed_size ||
      packed[cursor] != MILAN_TEMPLATE_TAG_TAIL_BITMAP ||
      goodix_milan_template_read_u32 (packed + cursor + 1) !=
        MILAN_TEMPLATE_TAIL_BITMAP_SIZE)
    return -1;
  memcpy (unpacked->tail_state + 0x10c, packed + cursor + 5,
          MILAN_TEMPLATE_TAIL_BITMAP_SIZE);
  cursor += MILAN_TEMPLATE_TAGGED_U32_SIZE + MILAN_TEMPLATE_TAIL_BITMAP_SIZE;
  for (size_t i = 0; i < 4; i++)
    {
      if (cursor + 5 > packed_size ||
          packed[cursor] != MILAN_TEMPLATE_TAG_TAIL_TRAILER_FIRST + i)
        return -1;
      memcpy (unpacked->tail_state + 0x50c + i * 4,
              packed + cursor + 1, 4);
      cursor += 5;
    }
  return cursor == packed_size ? 0 : -1;
}

int
goodix_milan_template_pack_one_feature (
  const uint8_t *feature_element,
  size_t         feature_element_size,
  const uint8_t *tail_state,
  size_t         tail_state_size,
  uint8_t       *packed,
  size_t         packed_capacity,
  size_t        *packed_size)
{
  static const uint8_t header_tags[MILAN_TEMPLATE_HEADER_FIELD_COUNT] = {
    MILAN_TEMPLATE_TAG_HEADER_FORMAT, MILAN_TEMPLATE_TAG_SENSOR_TYPE,
    MILAN_TEMPLATE_TAG_ROWS, MILAN_TEMPLATE_TAG_COLUMNS,
    MILAN_TEMPLATE_TAG_FEATURE_COUNT, MILAN_TEMPLATE_TAG_MAXIMUM_FEATURES,
    MILAN_TEMPLATE_TAG_REGISTRATION_COUNT,
    MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS,
    MILAN_TEMPLATE_TAG_MAXIMUM_RECORDS_COPY,
    MILAN_TEMPLATE_TAG_GEOMETRY_PRIMARY,
    MILAN_TEMPLATE_TAG_GEOMETRY_SECONDARY, MILAN_TEMPLATE_TAG_QUEUE_STATE,
    MILAN_TEMPLATE_TAG_QUEUE_COUNTER,
  };
  static const uint32_t header_values[MILAN_TEMPLATE_HEADER_FIELD_COUNT] = {
    0x11f248ea, GOODIX_MILAN_PRINT_SENSOR_TYPE,
    GOODIX_MILAN_EXTRACTION_CLASSIFICATION_ROWS,
    GOODIX_MILAN_EXTRACTION_CLASSIFICATION_COLUMNS,
    1, 1, 1, 150, 150, 1, 1, 0, 0,
  };
  const size_t fixed_size = MILAN_TEMPLATE_FIXED_SIZE;
  size_t total_size;
  uint8_t *output;

  if (!feature_element || !tail_state || !packed || !packed_size ||
      feature_element_size > UINT32_MAX ||
      tail_state_size < MILAN_TEMPLATE_TAIL_STATE_SIZE ||
      feature_element_size > SIZE_MAX - fixed_size)
    return -1;
  total_size = feature_element_size + fixed_size;
  if (packed_capacity < total_size)
    return -1;

  packed[0] = MILAN_TEMPLATE_TAG_ENVELOPE;
  packed[5] = MILAN_TEMPLATE_TAG_PAYLOAD;
  output = packed + MILAN_TEMPLATE_ENVELOPE_SIZE;
  for (size_t i = 0; i < MILAN_TEMPLATE_HEADER_FIELD_COUNT; i++)
    output = milan_pack_tagged_u32 (output, header_tags[i], header_values[i]);
  memcpy (output, feature_element, feature_element_size);
  output += feature_element_size;
  *output++ = MILAN_TEMPLATE_TAG_GRAPH;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_GRAPH_PAYLOAD_SIZE);
  output += 4;
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_REFERENCE, UINT32_MAX);
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_COMPANION_FIRST, UINT32_MAX);
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_COMPANION_SECOND, UINT32_MAX);
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_GRAPH_ESTABLISHED, 0);
  *output++ = MILAN_TEMPLATE_TAG_TAIL;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_PAYLOAD_SIZE);
  output += 4;
  *output++ = MILAN_TEMPLATE_TAG_TAIL_RECORDS;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_RECORDS_SIZE);
  output += 4;
  memcpy (output, tail_state, MILAN_TEMPLATE_TAIL_RECORDS_SIZE);
  output += MILAN_TEMPLATE_TAIL_RECORDS_SIZE;
  uint32_t value;
  memcpy (&value, tail_state + 0xc8, sizeof(value));
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_TAIL_SCALAR, value);
  *output++ = MILAN_TEMPLATE_TAG_TAIL_VECTOR;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_VECTOR_SIZE);
  output += 4;
  memcpy (output, tail_state + 0xcc, MILAN_TEMPLATE_TAIL_VECTOR_SIZE);
  output += MILAN_TEMPLATE_TAIL_VECTOR_SIZE;
  *output++ = MILAN_TEMPLATE_TAG_TAIL_BITMAP;
  goodix_milan_template_write_u32 (output, MILAN_TEMPLATE_TAIL_BITMAP_SIZE);
  output += 4;
  memcpy (output, tail_state + 0x10c, MILAN_TEMPLATE_TAIL_BITMAP_SIZE);
  output += MILAN_TEMPLATE_TAIL_BITMAP_SIZE;
  memcpy (&value, tail_state + 0x50c, sizeof(value));
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_TAIL_TRAILER_FIRST, value);
  memcpy (&value, tail_state + 0x510, sizeof(value));
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_TAIL_TRAILER_FIRST + 1, value);
  memcpy (&value, tail_state + 0x514, sizeof(value));
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_TAIL_TRAILER_FIRST + 2, value);
  memcpy (&value, tail_state + 0x518, sizeof(value));
  output = milan_pack_tagged_u32 (
    output, MILAN_TEMPLATE_TAG_TAIL_TRAILER_LAST, value);

  *packed_size = (size_t) (output - packed);
  if (*packed_size != total_size)
    return -1;
  uint32_t payload_size =
    (uint32_t) (total_size - MILAN_TEMPLATE_ENVELOPE_SIZE);
  goodix_milan_template_write_u32 (packed + 6, payload_size);
  goodix_milan_template_write_u32 (packed + 1,
                    milan_template_crc32 (
                      packed + MILAN_TEMPLATE_ENVELOPE_SIZE, payload_size));
  return 0;
}
