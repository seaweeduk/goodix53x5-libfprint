/*
 * Goodix 53x5 driver for libfprint - native Milan base generation
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <glib.h>

#include "milan/milan.h"

#define GOODIX_MILAN_PROFILE9_CHIP_FAMILY_MASK    0xffffff00u
#define GOODIX_MILAN_PROFILE9_CHIP_FAMILY_PREFIX  0x00220c00u
#define GOODIX_MILAN_VALIDATED_CHIP_ID             0x00220ca1u
#define GOODIX_MILAN_VALIDATED_SUBTYPE             12
#define GOODIX_MILAN_BASE_BORDER                   2
#define GOODIX_MILAN_BASE_MAD_LIMIT                200

typedef enum
{
  GOODIX_MILAN_BASE_STAGE_IDLE = 0,
  GOODIX_MILAN_BASE_STAGE_FDT_TX_ON_BEFORE,
  GOODIX_MILAN_BASE_STAGE_CAPTURE_TX_ON,
  GOODIX_MILAN_BASE_STAGE_FDT_TX_OFF,
  GOODIX_MILAN_BASE_STAGE_CAPTURE_TX_OFF,
  GOODIX_MILAN_BASE_STAGE_ADMIT_PAIR,
  GOODIX_MILAN_BASE_STAGE_FDT_TX_ON_AFTER,
  GOODIX_MILAN_BASE_STAGE_PUBLISH,
  GOODIX_MILAN_BASE_STAGE_REJECTED,
  GOODIX_MILAN_BASE_STAGE_CANCELLED,
  GOODIX_MILAN_BASE_STAGE_COUNT,
} GoodixMilanBaseStage;

typedef struct
{
  GoodixMilanBaseStage stage;
  guint16             *tx_on;
  guint16             *tx_off;
  gsize                tx_on_values;
  gsize                tx_off_values;
  guint64              mad;
  gboolean             admitted;
} GoodixMilanBaseAttempt;

typedef struct
{
  guint64                    generation_id;
  guint16                   *setup_tx_on;
  GoodixMilanPreprocessState state;
  gboolean                   admitted;
  gboolean                   identify_prelude_seen;
  guint                      identify_prelude_count;
  guint                      enrollment_stages;
  guint64                    use_count;
} GoodixMilanGeneration;

typedef enum
{
  GOODIX_MILAN_BASE_ERROR_INVALID_FRAME,
  GOODIX_MILAN_BASE_ERROR_INCOMPLETE,
  GOODIX_MILAN_BASE_ERROR_ID_EXHAUSTED,
} GoodixMilanBaseError;

#define GOODIX_MILAN_BASE_ERROR (goodix_milan_base_error_quark ())
GQuark goodix_milan_base_error_quark (void);

gboolean goodix_milan_runtime_subtype_for_chip (guint32  chip_id,
                                                 guint16 *subtype);

gboolean goodix_milan_base_pair_mad (const guint16 *tx_on,
                                     gsize          tx_on_values,
                                     const guint16 *tx_off,
                                     gsize          tx_off_values,
                                     guint64       *mad,
                                     GError       **error);

void goodix_milan_base_attempt_init (GoodixMilanBaseAttempt *attempt);
void goodix_milan_base_attempt_reset (GoodixMilanBaseAttempt *attempt);
void goodix_milan_base_attempt_cancel (GoodixMilanBaseAttempt *attempt);
void goodix_milan_base_attempt_take_frame (GoodixMilanBaseAttempt *attempt,
                                           gboolean                tx_on,
                                           guint16               **frame,
                                           gsize                   values);
gboolean goodix_milan_base_attempt_admit (GoodixMilanBaseAttempt *attempt,
                                          GError                **error);

gboolean goodix_milan_generation_allocate_id (guint64  *last_generation_id,
                                               guint64  *generation_id,
                                               GError  **error);
gboolean goodix_milan_base_attempt_publish (GoodixMilanBaseAttempt  *attempt,
                                            guint64                  generation_id,
                                            GoodixMilanGeneration **generation,
                                            guint16                **legacy_tx_off,
                                            GError                 **error);

void goodix_milan_generation_reset_preprocess (GoodixMilanGeneration *generation);
void goodix_milan_generation_free (GoodixMilanGeneration *generation);
void goodix_milan_generation_invalidate (GoodixMilanGeneration **generation);
guint64 goodix_milan_generation_note_use (GoodixMilanGeneration *generation);
void goodix_milan_generation_note_identify_prelude (GoodixMilanGeneration *generation);
void goodix_milan_generation_note_enrollment_stage (GoodixMilanGeneration *generation);

gboolean goodix_milan_replace_raw_frame (guint16 **owner,
                                         guint16 **frame,
                                         gsize     values,
                                         GError  **error);

typedef struct _FpiSsm FpiSsm;
typedef struct _FpDevice FpDevice;

void goodix_milan_base_start_ensure_subsm (FpiSsm   *parent_ssm,
                                           FpDevice *dev);
