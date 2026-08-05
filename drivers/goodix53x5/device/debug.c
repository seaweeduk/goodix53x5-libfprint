/*
 * Goodix 53x5 driver for libfprint - opt-in diagnostic instrumentation
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef GOODIX53X5_DEBUG
#error "device/debug.c is only valid in a goodix53x5_debug build"
#endif

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "driver-private.h"
#include "device/debug.h"
#include "milan/runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib/gstdio.h>

typedef enum
{
  GOODIX_DUMP_PROBES_NONE,
  GOODIX_DUMP_PROBES_FAILED,
  GOODIX_DUMP_PROBES_ALL,
} GoodixDumpProbesMode;

static const gchar *
goodix_debug_dump_dir (void)
{
  const gchar *dump_dir = g_getenv ("GOODIX53X5_DUMP_DIR");

  return dump_dir != NULL && dump_dir[0] != '\0' ? dump_dir : NULL;
}

gboolean
goodix_debug_dump_enabled (void)
{
  return goodix_debug_dump_dir () != NULL;
}

gboolean
goodix_debug_dump_txon_ref_enabled (void)
{
  const gchar *value = g_getenv ("GOODIX53X5_DUMP_TXON_REF");

  return goodix_debug_dump_enabled () && value != NULL && value[0] != '\0' &&
         g_strcmp0 (value, "0") != 0;
}

static gboolean
goodix_debug_ensure_dump_dir (const gchar *dump_dir)
{
  if (g_mkdir_with_parents (dump_dir, 0700) != 0)
    {
      fp_warn ("Could not create dump dir %s: %s", dump_dir,
               g_strerror (errno));
      return FALSE;
    }

  (void) g_chmod (dump_dir, 0700);
  return TRUE;
}

static void
goodix_debug_write_dump (const gchar      *prefix,
                          guint32           crc,
                          const GByteArray *buf,
                          const gchar      *extension)
{
  const gchar *dump_dir = goodix_debug_dump_dir ();
  g_autofree gchar *path = NULL;
  gint fd = -1;
  gint64 stamp = g_get_real_time ();

  for (guint attempt = 0; attempt < 1000 && fd < 0; attempt++)
    {
      g_free (path);
      path = g_strdup_printf ("%s/%s-%" G_GINT64_FORMAT "-%08x.%s",
                              dump_dir, prefix, stamp + attempt, crc, extension);
      fd = g_open (path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
      if (fd < 0 && errno != EEXIST)
        break;
    }
  if (fd < 0 || write (fd, buf->data, buf->len) != (ssize_t) buf->len ||
      fsync (fd) != 0 || close (fd) != 0)
    {
      gint saved_errno = errno;

      if (fd >= 0)
        close (fd);
      fp_warn ("Failed to publish %s: %s", path, g_strerror (saved_errno));
      return;
    }

  fp_dbg ("Dumped %s (%u bytes)", path, (unsigned) buf->len);
}

static void
goodix_debug_write_gbytes (const gchar *prefix,
                           GBytes      *source,
                           const gchar *extension)
{
  gconstpointer data;
  gsize size;
  g_autoptr(GByteArray) bytes = NULL;

  if (!source)
    return;
  data = g_bytes_get_data (source, &size);
  bytes = g_byte_array_sized_new (size);
  g_byte_array_append (bytes, data, size);
  goodix_debug_write_dump (
    prefix, goodix_crypto_crc32_mpeg2 (data, size), bytes, extension);
}

static gchar *
goodix_debug_raw12_sha256 (const guint16 *image)
{
  g_autoptr(GChecksum) checksum = NULL;

  if (!image)
    return NULL;
  checksum = g_checksum_new (G_CHECKSUM_SHA256);
  for (gsize i = 0; i < GOODIX_SENSOR_PIXELS; i++)
    {
      const guint16 value = image[i] & 0x0fff;
      const guint8 bytes[2] = { value >> 8, value & 0xff };

      g_checksum_update (checksum, bytes, sizeof (bytes));
    }
  return g_strdup (g_checksum_get_string (checksum));
}

void
goodix_debug_dump_image (const gchar  *prefix,
                         const guint8 *img,
                         gsize         len)
{
  const gchar *dump_dir = goodix_debug_dump_dir ();
  g_autofree gchar *header = NULL;
  g_autoptr(GByteArray) buf = NULL;

  if (dump_dir == NULL || img == NULL || len != GOODIX_SENSOR_PIXELS ||
      !goodix_debug_ensure_dump_dir (dump_dir))
    return;

  header = g_strdup_printf ("P5\n%d %d\n255\n", GOODIX_SENSOR_WIDTH,
                            GOODIX_SENSOR_HEIGHT);
  buf = g_byte_array_new ();
  g_byte_array_append (buf, (const guint8 *) header, strlen (header));
  g_byte_array_append (buf, img, len);
  goodix_debug_write_dump (
    prefix, goodix_crypto_crc32_mpeg2 (img, len), buf, "pgm");
}

void
goodix_debug_dump_raw12 (const gchar   *prefix,
                         const guint16 *img,
                         gsize          len)
{
  const gchar *dump_dir = goodix_debug_dump_dir ();
  g_autofree gchar *header = NULL;
  g_autoptr(GByteArray) buf = NULL;
  gsize header_len;

  if (dump_dir == NULL || img == NULL || len != GOODIX_SENSOR_PIXELS ||
      !goodix_debug_ensure_dump_dir (dump_dir))
    return;

  header = g_strdup_printf ("P5\n%d %d\n4095\n", GOODIX_SENSOR_WIDTH,
                            GOODIX_SENSOR_HEIGHT);
  header_len = strlen (header);
  buf = g_byte_array_new ();
  g_byte_array_append (buf, (const guint8 *) header, header_len);
  for (gsize i = 0; i < len; i++)
    {
      guint16 value = img[i] & 0x0fff;
      guint8 bytes[2] = { value >> 8, value & 0xff };

      g_byte_array_append (buf, bytes, sizeof (bytes));
    }
  goodix_debug_write_dump (
    prefix,
    goodix_crypto_crc32_mpeg2 (buf->data + header_len, len * 2), buf, "pgm");
}

void
goodix_debug_dump_pair (const gchar   *prefix,
                        const guint16 *raw_img,
                        const guint8  *img)
{
  g_autofree gchar *raw_prefix = NULL;

  if (!goodix_debug_dump_enabled ())
    return;
  raw_prefix = g_strdup_printf ("raw12-%s", prefix);
  goodix_debug_dump_raw12 (raw_prefix, raw_img, GOODIX_SENSOR_PIXELS);
  goodix_debug_dump_image (prefix, img, GOODIX_SENSOR_PIXELS);
}

static GoodixDumpProbesMode
goodix_debug_dump_probes_mode (void)
{
  const gchar *value = g_getenv ("GOODIX53X5_DUMP_PROBES");

  if (value == NULL || value[0] == '\0' ||
      g_ascii_strcasecmp (value, "failed") == 0)
    return GOODIX_DUMP_PROBES_FAILED;
  if (g_ascii_strcasecmp (value, "all") == 0)
    return GOODIX_DUMP_PROBES_ALL;
  if (g_ascii_strcasecmp (value, "none") == 0)
    return GOODIX_DUMP_PROBES_NONE;
  fp_warn ("Ignoring invalid GOODIX53X5_DUMP_PROBES=%s (using failed)", value);
  return GOODIX_DUMP_PROBES_FAILED;
}

void
goodix_debug_dump_probe (FpiDeviceAction action,
                         const gchar    *outcome,
                         const guint16  *raw_img,
                         const guint8   *img)
{
  GoodixDumpProbesMode mode;
  gboolean should_dump;
  g_autofree gchar *prefix = NULL;

  if (!goodix_debug_dump_enabled ())
    return;
  mode = goodix_debug_dump_probes_mode ();
  should_dump = mode == GOODIX_DUMP_PROBES_ALL ||
                (mode == GOODIX_DUMP_PROBES_FAILED &&
                 (g_strcmp0 (outcome, "fail") == 0 ||
                  g_strcmp0 (outcome, "miss") == 0 ||
                  g_strcmp0 (outcome, "weak") == 0));
  if (!should_dump)
    return;
  prefix = g_strdup_printf ("%s-%s",
                            action == FPI_DEVICE_ACTION_IDENTIFY
                              ? "identify" : "verify",
                            outcome);
  goodix_debug_dump_pair (prefix, raw_img, img);
}

gboolean
goodix_debug_timing_enabled (void)
{
  const gchar *value = g_getenv ("GOODIX53X5_LOG_TIMING");

  return value != NULL && value[0] != '\0' && g_strcmp0 (value, "0") != 0;
}

static const gchar *
goodix_debug_action_name (FpiDeviceAction action)
{
  switch (action)
    {
    case FPI_DEVICE_ACTION_OPEN: return "open";
    case FPI_DEVICE_ACTION_CLOSE: return "close";
    case FPI_DEVICE_ACTION_ENROLL: return "enroll";
    case FPI_DEVICE_ACTION_VERIFY: return "verify";
    case FPI_DEVICE_ACTION_IDENTIFY: return "identify";
    case FPI_DEVICE_ACTION_NONE: return "none";
    case FPI_DEVICE_ACTION_PROBE:
    case FPI_DEVICE_ACTION_CAPTURE:
    case FPI_DEVICE_ACTION_LIST:
    case FPI_DEVICE_ACTION_DELETE:
    case FPI_DEVICE_ACTION_CLEAR_STORAGE:
      return "other";
    }
  g_assert_not_reached ();
}

void
goodix_debug_timing_log (FpDevice    *dev,
                         const gchar *scope,
                         const gchar *event,
                         gint64       duration_us,
                         const gchar *detail)
{
  if (!goodix_debug_timing_enabled ())
    return;
  fp_info ("timing[%s] %s %s: %.2f ms%s%s",
           goodix_debug_action_name (fpi_device_get_current_action (dev)),
           scope, event, duration_us / 1000.0,
           detail != NULL ? " " : "", detail != NULL ? detail : "");
}

static void
goodix_debug_timing_reset_phases (GoodixDebugTiming *timing)
{
  timing->open_started_us = 0;
  timing->open_phase_started_us = 0;
  timing->open_last_state_name = NULL;
  timing->finger_wait_started_us = 0;
  timing->finger_wait_phase_started_us = 0;
  timing->finger_wait_false_event_retries = 0;
  timing->finger_up_started_us = 0;
  timing->finger_up_phase_started_us = 0;
  timing->ref_capture_started_us = 0;
  timing->ref_capture_phase_started_us = 0;
  timing->capture_started_us = 0;
  timing->capture_phase_started_us = 0;
  timing->deactivate_started_us = 0;
  timing->deactivate_phase_started_us = 0;
}

void
goodix_debug_timing_action_start (FpiDeviceGoodix53x5 *self,
                                  FpDevice            *dev,
                                  const gchar         *detail)
{
  if (!goodix_debug_timing_enabled ())
    return;
  self->debug_timing.action_started_us = g_get_monotonic_time ();
  goodix_debug_timing_reset_phases (&self->debug_timing);
  goodix_debug_timing_log (dev, "action", "start", 0, detail);
}

void
goodix_debug_timing_action_done (FpiDeviceGoodix53x5 *self,
                                 FpDevice            *dev,
                                 const gchar         *detail)
{
  if (!goodix_debug_timing_enabled ())
    return;
  if (self->debug_timing.action_started_us != 0)
    goodix_debug_timing_log (
      dev, "action", "done",
      g_get_monotonic_time () - self->debug_timing.action_started_us, detail);
  self->debug_timing.action_started_us = 0;
  goodix_debug_timing_reset_phases (&self->debug_timing);
}

void
goodix_debug_timing_open_state (FpiDeviceGoodix53x5 *self,
                                FpDevice            *dev,
                                const gchar         *state_name,
                                gint64               now_us)
{
  GoodixDebugTiming *timing = &self->debug_timing;

  if (!goodix_debug_timing_enabled ())
    return;
  if (timing->open_started_us == 0)
    {
      timing->open_started_us = now_us;
      timing->open_phase_started_us = now_us;
      goodix_debug_timing_log (dev, "open", "start", 0, NULL);
    }
  else
    {
      goodix_debug_timing_log (
        dev, "open",
        timing->open_last_state_name != NULL ? timing->open_last_state_name
                                             : "unknown",
        now_us - timing->open_phase_started_us, NULL);
      timing->open_phase_started_us = now_us;
    }
  timing->open_last_state_name = state_name;
}

void
goodix_debug_timing_open_done (FpiDeviceGoodix53x5 *self,
                               FpDevice            *dev,
                               const gchar         *detail)
{
  GoodixDebugTiming *timing = &self->debug_timing;
  gint64 now_us;

  if (!goodix_debug_timing_enabled () || timing->open_started_us == 0)
    return;
  now_us = g_get_monotonic_time ();
  goodix_debug_timing_log (
    dev, "open",
    timing->open_last_state_name != NULL ? timing->open_last_state_name
                                         : "unknown",
    now_us - timing->open_phase_started_us, NULL);
  goodix_debug_timing_log (dev, "open", "done",
                           now_us - timing->open_started_us, detail);
  timing->open_started_us = 0;
  timing->open_phase_started_us = 0;
  timing->open_last_state_name = NULL;
}

void
goodix_debug_log_runtime_result (FpDevice                       *dev,
                                 guint                           stage,
                                 const GoodixMilanRuntimeOutput *output)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  const gchar *value = g_getenv ("GOODIX53X5_LOG_DIAGNOSTICS");
  g_autofree gchar *probe_sha256 = NULL;
  g_autofree gchar *candidate_sha256 = NULL;
  g_autofree gchar *live_raw_sha256 = NULL;
  g_autofree gchar *processed_image_sha256 = NULL;
  g_autofree gchar *setup_txon_sha256 = NULL;
  g_autofree gchar *prefix = NULL;
  g_autoptr(GString) record = NULL;
  g_autoptr(GByteArray) bytes = NULL;
  guint64 generation_use_index = 0;
  gchar *record_data;
  gsize record_len;
  gboolean dump_templates;

  if (value == NULL || value[0] == '\0' || g_strcmp0 (value, "0") == 0 ||
      output == NULL)
    return;
  value = g_getenv ("GOODIX53X5_DUMP_TEMPLATES");
  dump_templates = value != NULL && value[0] != '\0' &&
                   g_strcmp0 (value, "0") != 0;
  if (output->probe_template)
    probe_sha256 = g_compute_checksum_for_bytes (
      G_CHECKSUM_SHA256, output->probe_template);
  if (output->final_candidate)
    candidate_sha256 = g_compute_checksum_for_bytes (
      G_CHECKSUM_SHA256, output->final_candidate);
  live_raw_sha256 = goodix_debug_raw12_sha256 (self->captured_raw_image);
  if (output->processed_image)
    processed_image_sha256 = g_compute_checksum_for_bytes (
      G_CHECKSUM_SHA256, output->processed_image);
  if (self->milan_generation &&
      self->milan_generation->generation_id == output->generation_id)
    {
      generation_use_index = self->milan_generation->use_count;
      setup_txon_sha256 = goodix_debug_raw12_sha256 (
        self->milan_generation->setup_tx_on);
    }
  fp_info ("diagnostic[%s] epoch=%" G_GUINT64_FORMAT
           " generation=%" G_GUINT64_FORMAT
           " stage=%u purpose=%u status=%u quality=%d coverage=%d "
           "records=%u partitions=%u/%u score=%d winner=%u/%" G_GSIZE_FORMAT
           " study=%u gallery=%u/%u/%u cancel=%u/%" G_GSIZE_FORMAT
           "error=%u:%d learning_error=%u:%d",
           goodix_debug_action_name (fpi_device_get_current_action (dev)),
           output->action_epoch, output->generation_id, stage,
           (guint) output->purpose, (guint) output->status,
           output->quality, output->coverage,
           output->probe_record_count, output->probe_partition0_count,
           output->probe_partition1_count, output->score, output->winner_index,
           output->winner_position,
           (guint) output->study_action, output->valid_gallery_count,
           output->invalid_gallery_count, output->evaluated_gallery_count,
           (guint) output->cancellation.cancelled_at,
           output->cancellation.cancelled_gallery_position,
           output->error ? output->error->domain : 0,
           output->error ? output->error->code : 0,
           output->learning_error ? output->learning_error->domain : 0,
           output->learning_error ? output->learning_error->code : 0);
  record = g_string_new (NULL);
  g_string_append_printf (
    record,
    "{\"action\":\"%s\",\"action_epoch_u64\":%" G_GUINT64_FORMAT
    ",\"candidate_sha256\":%s%s%s,\"coverage_i32\":%d,"
    "\"dac_high_u16\":%u,\"dac_low_u16\":%u,"
    "\"evaluated_gallery_u32\":%u,\"gallery\":[",
    goodix_debug_action_name (fpi_device_get_current_action (dev)),
    output->action_epoch, candidate_sha256 ? "\"" : "null",
    candidate_sha256 ? candidate_sha256 : "", candidate_sha256 ? "\"" : "",
    output->coverage, output->dac_high, output->dac_low,
    output->evaluated_gallery_count);
  for (guint i = 0; i < output->gallery_results->len; i++)
    {
      GoodixMilanRuntimeGalleryResult *result =
        g_ptr_array_index (output->gallery_results, i);

      if (i != 0)
        g_string_append_c (record, ',');
      g_string_append_printf (
        record,
        "{\"accepted\":%s,\"after_match_sha256\":%s%s%s,"
        "\"evaluated\":%s,\"gallery_index_u32\":%u,"
        "\"gallery_position_u64\":%" G_GSIZE_FORMAT
        ",\"input_template_sha256\":%s%s%s,"
        "\"matched_feature_u64\":%" G_GSIZE_FORMAT
        ",\"queue_counter_u32\":%u,\"queue_eligible_i32\":%d,"
        "\"queue_occupied_after_u64\":%" G_GSIZE_FORMAT
        ",\"queue_occupied_before_u64\":%" G_GSIZE_FORMAT
        ",\"queue_state_u32\":%u,\"score_i32\":%d,\"valid\":%s}",
        result->accepted ? "true" : "false",
        result->after_match_sha256[0] ? "\"" : "null",
        result->after_match_sha256,
        result->after_match_sha256[0] ? "\"" : "",
        result->evaluated ? "true" : "false", result->gallery_index,
        result->gallery_position,
        result->input_template_sha256[0] ? "\"" : "null",
        result->input_template_sha256,
        result->input_template_sha256[0] ? "\"" : "",
        result->match_result.matched_feature_index,
        result->queue_counter_before_match,
        result->match_result.study_control.queue_candidate_eligible,
        result->queue_occupied_after_match,
        result->queue_occupied_before_match,
        result->queue_state_before_match, result->score,
        result->valid ? "true" : "false");
    }
  g_string_append_printf (
    record,
    "],\"generation_id_u64\":%" G_GUINT64_FORMAT
    ",\"generation_use_index_u64\":%" G_GUINT64_FORMAT
    ",\"live_raw_sha256\":%s%s%s,"
    "\"partition0_count_u32\":%u,\"partition1_count_u32\":%u,"
    "\"probe_record_count_u32\":%u,\"probe_sha256\":%s%s%s,"
    "\"processed_image_sha256\":%s%s%s,"
    "\"profile_u16\":%u,\"purpose_u32\":%u,\"quality_i32\":%d,"
    "\"schema\":\"goodix53x5-runtime-debug/v1\",\"score_i32\":%d,"
    "\"sensor_subtype_u16\":%u,\"setup_txon_sha256\":%s%s%s,"
    "\"stage_u32\":%u,\"status_u32\":%u,"
    "\"study_action_u32\":%u,\"tcode_u16\":%u,"
    "\"winner_index_u32\":%u,\"winner_position_u64\":%" G_GSIZE_FORMAT "}\n",
    output->generation_id,
    generation_use_index,
    live_raw_sha256 ? "\"" : "null",
    live_raw_sha256 ? live_raw_sha256 : "", live_raw_sha256 ? "\"" : "",
    output->probe_partition0_count,
    output->probe_partition1_count, output->probe_record_count,
    probe_sha256 ? "\"" : "null",
    probe_sha256 ? probe_sha256 : "", probe_sha256 ? "\"" : "",
    processed_image_sha256 ? "\"" : "null",
    processed_image_sha256 ? processed_image_sha256 : "",
    processed_image_sha256 ? "\"" : "",
    output->profile, (guint) output->purpose, output->quality, output->score,
    output->sensor_subtype,
    setup_txon_sha256 ? "\"" : "null",
    setup_txon_sha256 ? setup_txon_sha256 : "",
    setup_txon_sha256 ? "\"" : "",
    stage, (guint) output->status,
    (guint) output->study_action, output->tcode, output->winner_index,
    output->winner_position);
  prefix = g_strdup_printf (
    "runtime-%s-%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "-%u",
    goodix_debug_action_name (fpi_device_get_current_action (dev)),
    output->action_epoch, output->generation_id, stage);
  record_len = record->len;
  record_data = g_string_free (g_steal_pointer (&record), FALSE);
  bytes = g_byte_array_new_take ((guint8 *) record_data, record_len);
  goodix_debug_write_dump (
    prefix, goodix_crypto_crc32_mpeg2 (bytes->data, bytes->len), bytes, "json");
  if (dump_templates)
    {
      g_autofree gchar *template_prefix = NULL;

      template_prefix = g_strdup_printf (
        "template-probe-%s-%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "-%u",
        goodix_debug_action_name (fpi_device_get_current_action (dev)),
        output->action_epoch, output->generation_id, stage);
      goodix_debug_write_gbytes (template_prefix, output->probe_template, "bin");
      g_clear_pointer (&template_prefix, g_free);
      template_prefix = g_strdup_printf (
        "template-final-%s-%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "-%u",
        goodix_debug_action_name (fpi_device_get_current_action (dev)),
        output->action_epoch, output->generation_id, stage);
      goodix_debug_write_gbytes (template_prefix, output->final_candidate, "bin");
      for (guint i = 0; i < output->gallery_results->len; i++)
        {
          GoodixMilanRuntimeGalleryResult *result =
            g_ptr_array_index (output->gallery_results, i);

          g_clear_pointer (&template_prefix, g_free);
          template_prefix = g_strdup_printf (
            "template-input-%s-%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT
            "-%u-%" G_GSIZE_FORMAT,
            goodix_debug_action_name (fpi_device_get_current_action (dev)),
            output->action_epoch, output->generation_id, stage,
            result->gallery_position);
          goodix_debug_write_gbytes (
            template_prefix, result->input_template, "bin");
          g_clear_pointer (&template_prefix, g_free);
          template_prefix = g_strdup_printf (
            "template-after-match-%s-%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT
            "-%u-%" G_GSIZE_FORMAT,
            goodix_debug_action_name (fpi_device_get_current_action (dev)),
            output->action_epoch, output->generation_id, stage,
            result->gallery_position);
          goodix_debug_write_gbytes (
            template_prefix, result->after_match_template, "bin");
        }
    }
}
