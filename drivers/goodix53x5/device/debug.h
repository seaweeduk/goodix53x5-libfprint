/*
 * Goodix 53x5 driver for libfprint - opt-in diagnostic instrumentation
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include "fpi-device.h"
#include "goodix53x5.h"

typedef struct _GoodixMilanRuntimeOutput GoodixMilanRuntimeOutput;

#ifdef GOODIX53X5_DEBUG

/* The diagnostic build recognizes:
 * GOODIX53X5_DUMP_DIR, GOODIX53X5_DUMP_PROBES, GOODIX53X5_LOG_TIMING, and
 * GOODIX53X5_LOG_DIAGNOSTICS. */
#define GOODIX53X5_DEBUG_ONLY(...) __VA_ARGS__

typedef struct
{
  FpiDeviceAction action;
  guint64         generation_use_index;
  gchar           setup_txon_sha256[65];
  gchar           live_raw_sha256[65];
} GoodixDebugRuntimeMetadata;

typedef struct
{
  gint64       action_started_us;
  gint64       open_started_us;
  gint64       open_phase_started_us;
  const gchar *open_last_state_name;
  gint64       finger_wait_started_us;
  gint64       finger_wait_phase_started_us;
  guint        finger_wait_false_event_retries;
  gint64       finger_up_started_us;
  gint64       finger_up_phase_started_us;
  gint64       ref_capture_started_us;
  gint64       ref_capture_phase_started_us;
  gint64       capture_started_us;
  gint64       capture_phase_started_us;
  gint64       deactivate_started_us;
  gint64       deactivate_phase_started_us;
} GoodixDebugTiming;

gboolean goodix_debug_dump_enabled (void);
void goodix_debug_dump_image (const gchar  *prefix,
                              const guint8 *img,
                              gsize         len);
void goodix_debug_dump_raw12 (const gchar   *prefix,
                              const guint16 *img,
                              gsize          len);
void goodix_debug_dump_pair (const gchar   *prefix,
                             const guint16 *raw_img,
                             const guint8  *img);
void goodix_debug_dump_probe (FpiDeviceAction action,
                              const gchar    *outcome,
                              const guint16  *raw_img,
                              const guint8   *img);

gboolean goodix_debug_timing_enabled (void);
void goodix_debug_timing_log (FpDevice    *dev,
                              const gchar *scope,
                              const gchar *event,
                              gint64       duration_us,
                              const gchar *detail);
void goodix_debug_timing_action_start (FpiDeviceGoodix53x5 *self,
                                       FpDevice            *dev,
                                       const gchar         *detail);
void goodix_debug_timing_action_done (FpiDeviceGoodix53x5 *self,
                                      FpDevice            *dev,
                                      const gchar         *detail);
void goodix_debug_timing_open_state (FpiDeviceGoodix53x5 *self,
                                     FpDevice            *dev,
                                     const gchar         *state_name,
                                     gint64               now_us);
void goodix_debug_timing_open_done (FpiDeviceGoodix53x5 *self,
                                    FpDevice            *dev,
                                    const gchar         *detail);

void goodix_debug_capture_runtime_metadata (
  GoodixDebugRuntimeMetadata *metadata,
  FpiDeviceAction             action,
  const guint16              *setup_tx_on,
  const guint16              *live_raw,
  guint64                     generation_use_index);
void goodix_debug_log_runtime_result (
  FpDevice                       *dev,
  guint                           stage,
  const GoodixDebugRuntimeMetadata *metadata,
  const GoodixMilanRuntimeOutput *output);

#else

#define GOODIX53X5_DEBUG_ONLY(...)

#define goodix_debug_dump_enabled() FALSE
#define goodix_debug_timing_enabled() FALSE
#define goodix_debug_dump_image(...) G_STMT_START { } G_STMT_END
#define goodix_debug_dump_raw12(...) G_STMT_START { } G_STMT_END
#define goodix_debug_dump_pair(...) G_STMT_START { } G_STMT_END
#define goodix_debug_dump_probe(...) G_STMT_START { } G_STMT_END
#define goodix_debug_timing_log(...) G_STMT_START { } G_STMT_END
#define goodix_debug_timing_action_start(...) G_STMT_START { } G_STMT_END
#define goodix_debug_timing_action_done(...) G_STMT_START { } G_STMT_END
#define goodix_debug_timing_open_state(...) G_STMT_START { } G_STMT_END
#define goodix_debug_timing_open_done(...) G_STMT_START { } G_STMT_END
#define goodix_debug_log_runtime_result(...) G_STMT_START { } G_STMT_END

#endif
