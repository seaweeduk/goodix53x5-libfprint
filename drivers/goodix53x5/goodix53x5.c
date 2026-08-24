/*
 * Goodix 53x5 driver for libfprint
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "driver-private.h"
#include "device/session.h"
#include "device/enroll.h"
#include "device/auth.h"
#include "device/persistence.h"

#include <string.h>
#include <openssl/crypto.h>

G_DEFINE_TYPE (FpiDeviceGoodix53x5, fpi_device_goodix53x5,
               FP_TYPE_DEVICE)

/* ========================================================================
 * FpDevice virtual methods
 * ======================================================================== */

static void
goodix_open (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  self->open_recovery_attempted = FALSE;
  self->open_usb_reset_required = FALSE;
  goodix_start_open_ssm (dev);
}

static void
goodix_close (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  GError *error = NULL;

  self->action_epoch++;
  if (self->cancel)
    g_cancellable_cancel (self->cancel);
  g_clear_object (&self->milan_task);
  g_clear_object (&self->cancel);
  goodix_clear_pending_result_report (self);
  g_clear_pointer (&self->otp_data, g_free);
  g_clear_pointer (&self->fw_version, g_free);
  g_clear_pointer (&self->rx.buf, g_free);
#ifdef GOODIX53X5_DEBUG
  g_clear_pointer (&self->captured_image, g_free);
#endif
  g_clear_pointer (&self->captured_raw_image, g_free);
  goodix_milan_generation_invalidate (&self->milan_generation);
  g_clear_pointer (&self->enroll_transaction,
                   goodix_milan_enrollment_transaction_free);
  goodix_milan_persistence_clear (dev);
  g_clear_error (&self->pending_enroll_error);
  OPENSSL_cleanse (self->psk, sizeof (self->psk));
  OPENSSL_cleanse (self->gtls.psk, sizeof (self->gtls.psk));
  self->psk_imported = FALSE;

  g_usb_device_release_interface (fpi_device_get_usb_device (dev),
                                  GOODIX_USB_INTERFACE, 0, &error);
  self->usb_interface_claimed = FALSE;

  fpi_device_close_complete (dev, error);
}

static void
goodix_enroll (FpDevice *dev)
{
  goodix_enroll_start (dev);
}

static void
goodix_verify (FpDevice *dev)
{
  goodix_auth_start (dev);
}

static void
goodix_identify (FpDevice *dev)
{
  goodix_auth_start (dev);
}

static void
goodix_suspend (FpDevice *dev)
{
  goodix_session_suspend (dev);
}

static void
goodix_resume (FpDevice *dev)
{
  goodix_session_resume (dev);
}

static void
goodix_cancel (FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  if (self->cancel)
    {
      self->action_epoch++;
      g_cancellable_cancel (self->cancel);
    }
}

/* ========================================================================
 * GObject boilerplate
 * ======================================================================== */

static void
fpi_device_goodix53x5_init (FpiDeviceGoodix53x5 *self)
{
#ifdef GOODIX53X5_DEBUG
  g_autofree gchar *capture_session_id = g_uuid_string_random ();

  g_strlcpy (self->debug_capture_session_id, capture_session_id,
             sizeof (self->debug_capture_session_id));
#endif
  memset (self->psk, 0, sizeof (self->psk));
  self->profile9_fdt.drift_anchor_empty = TRUE;
}

static const FpIdEntry goodix53x5_id_table[] = {
  { .vid = 0x27c6, .pid = 0x5335, },
  { .vid = 0x27c6, .pid = 0x5385, },
  { .vid = 0x27c6, .pid = 0x5395, },
  { .vid = 0, .pid = 0, .driver_data = 0 },
};

static void
fpi_device_goodix53x5_class_init (FpiDeviceGoodix53x5Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = "goodix53x5";
  dev_class->full_name = "Goodix HTK32 Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = goodix53x5_id_table;
  dev_class->nr_enroll_stages = GOODIX_ENROLL_SAMPLES;
  /* Native Milan has no equivalent time-based thermal cutoff. */
  dev_class->temp_hot_seconds = -1;
  dev_class->features = FP_DEVICE_FEATURE_VERIFY | FP_DEVICE_FEATURE_IDENTIFY;

  dev_class->open = goodix_open;
  dev_class->close = goodix_close;
  dev_class->enroll = goodix_enroll;
  dev_class->verify = goodix_verify;
  dev_class->identify = goodix_identify;
  dev_class->cancel = goodix_cancel;
  dev_class->suspend = goodix_suspend;
  dev_class->resume  = goodix_resume;
}
