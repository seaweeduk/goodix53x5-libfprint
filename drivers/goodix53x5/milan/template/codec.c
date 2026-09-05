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
  /* Eight reflected CRC steps for each byte, polynomial 0xedb88320. */
  static const uint32_t table[256] = {
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
  };
  uint32_t crc = UINT32_MAX;

  for (size_t i = 0; i < size; i++)
    crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xff];
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
