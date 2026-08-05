/*
 * Goodix 53x5 driver for libfprint - Milan match info internals
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include "milan/match/match.h"
#include "milan/match/rescue.h"

typedef struct
{
  int quality;
  int coverage;
  int32_t optional_c7;
} GoodixMilanExtractionMetadata;

typedef struct
{
  guint8 high_bitmap[286];
  guint8 enhanced_bitmap[286];
  guint8 low_bitmap[286];
} GoodixMilanFeatureBitmaps;

struct _GoodixMatchInfo
{
  GBytes *template;
  int     record_count;
  int     partition_count;
  GoodixMilanFeatureBitmaps feature_bitmaps;
  guint8  inline_mask[72];
  guint8  rescue_mask[GOODIX_MILAN_MATCH_RESCUE_MASK_SIZE];
  GoodixMilanAntifakeBlob antifake;
  GoodixMilanFeatureRecord *records;
  GoodixMilanExtractionMetadata extraction_metadata;
};

G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMilanExtractionMetadata, quality) == 0);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMilanExtractionMetadata, coverage) == 4);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMilanExtractionMetadata, optional_c7) == 8);
G_STATIC_ASSERT (sizeof(GoodixMilanExtractionMetadata) == 12);
G_STATIC_ASSERT (G_ALIGNOF (GoodixMilanExtractionMetadata) == 4);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMilanFeatureBitmaps, high_bitmap) == 0);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMilanFeatureBitmaps, enhanced_bitmap) == 286);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMilanFeatureBitmaps, low_bitmap) == 572);
G_STATIC_ASSERT (sizeof(GoodixMilanFeatureBitmaps) == 858);
G_STATIC_ASSERT (G_ALIGNOF (GoodixMilanFeatureBitmaps) == 1);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, template) == 0);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, record_count) == 8);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, partition_count) == 12);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, feature_bitmaps) == 16);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, feature_bitmaps.high_bitmap) == 16);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, feature_bitmaps.enhanced_bitmap) == 302);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, feature_bitmaps.low_bitmap) == 588);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, inline_mask) == 874);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, rescue_mask) == 946);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, antifake) == 1254);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, records) == 8104);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, extraction_metadata) == 8112);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, extraction_metadata.quality) == 8112);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, extraction_metadata.coverage) == 8116);
G_STATIC_ASSERT (G_STRUCT_OFFSET (GoodixMatchInfo, extraction_metadata.optional_c7) == 8120);
G_STATIC_ASSERT (sizeof(GoodixMatchInfo) == 8128);
G_STATIC_ASSERT (G_ALIGNOF (GoodixMatchInfo) == 8);
