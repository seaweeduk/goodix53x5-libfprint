/*
 * Goodix 53x5 driver for libfprint — Device session (open, GTLS, reinit)
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

#pragma once

#include "driver-private.h"

/**
 * Start the full open/initialization SSM as the top-level task SSM and
 * complete the libfprint open action when it finishes.
 */
void goodix_start_open_ssm (FpDevice *dev);

typedef void (*GoodixGtlsRestartDone) (FpDevice *dev,
                                       GError   *error,
                                       gpointer  data);

/* How long a secure-channel handshake waits for each reply from the sensor. */
#define GOODIX_CHANNEL_REPLY_TIMEOUT_MS 2000

/* Run the native three-attempt restart with the retained selected PSK.
 * The caller joins the asynchronous completion before resuming scan work. */
void goodix_start_gtls_restart (FpDevice             *dev,
                                GoodixGtlsRestartDone done,
                                gpointer              data);

/**
 * If the hardware session is invalid, release any stale interface claim and
 * run the full open-time initialization SSM as a sub-SSM of @ssm.
 *
 * Returns TRUE if a reinit sub-SSM was started (caller returns and the
 * parent advances when it completes), FALSE if no reinit was needed.
 */
gboolean goodix_maybe_start_reinit_subsm (FpiSsm   *ssm,
                                          FpDevice *dev);

/**
 * TRUE for errors that indicate the USB device/claim is likely stale or
 * gone. Setting needs_reinit on these makes the next action attempt
 * self-heal with a full reinitialization.
 */
gboolean goodix_error_indicates_stale_device (const GError *error);

/**
 * Handle system sleep/wake while the device is open, including ACTION_NONE.
 * Suspend cancels/joins the selected hardware owner and CPU consumer, sends
 * sleep/EC-off, then joins reception while retaining calibration/reference/FDT
 * state. Resume reclaims USB and re-keys GTLS, with one cold reconstruction on
 * failure, then restarts packet-driven servicing before completing.
 */
void goodix_session_suspend (FpDevice *dev);
void goodix_session_resume (FpDevice *dev);

/* Join every hardware owner, release the interface and complete the close. */
void goodix_session_close (FpDevice *dev);

/* Background and power owners use session_cancel even during foreground
 * admission. Only foreground/open callers use libfprint's action token. */
GCancellable *goodix_session_io_cancellable (FpDevice *dev);

/* Shared steps of a family's bring-up (GoodixFamily.open_new). */

/* Returns NULL to accept @firmware, which is NULL when every read failed, or
 * the error that fails open. */
typedef GError *(*GoodixFirmwareCheck) (const char *firmware);

/* Wakes the sensor and reads its firmware version, up to @reads times with
 * the native 100 ms sleep after each failure, then asks @check. */
void goodix_open_probe (FpiSsm *ssm, FpDevice *dev, guint reads,
                        GoodixFirmwareCheck check);
/* Resets the sensor and reads its chip ID, up to six read cycles; the first
 * reset asks for the IRQ status reply where @initial_reset_requests_irq. */
void goodix_open_identify_chip (FpiSsm *ssm, FpDevice *dev,
                                gboolean initial_reset_requests_irq);
/* Verifies and parses the OTP reply the previous state read. */
gboolean goodix_open_parse_otp (FpDevice *dev, GError **error);
/* Runs the open handshake attempts of the family's channel. */
void goodix_open_channel (FpiSsm *ssm, FpDevice *dev);

/* Selects the PSK for this open: the imported key in the state file, or the
 * default all-zero PSK when there is none. */
gboolean goodix_load_psk (FpiDeviceGoodix53x5 *self, GError **error);
/* Sets @matches to whether the PSK hash the sensor returned in preset @slot
 * is the hash of the PSK this driver holds. Fails when the reply does not parse. */
gboolean goodix_psk_hash_reply_matches (FpDevice *dev, guint32 slot,
                                        gboolean *matches, GError **error);
/* Checks that the PSK hash the sensor returned in preset @slot is the hash of
 * the PSK this driver holds. Fails when the reply does not parse or the hash
 * differs, which a sensor the driver never provisions cannot recover from. */
gboolean goodix_require_current_psk (FpDevice *dev, guint32 slot, GError **error);

/* Every hardware owner reports here when it stops; the session then starts
 * the pending action, the requested reader join, or idle maintenance. */
void goodix_session_settle (FpDevice *dev);
/* The idle service or a detached deactivation tail finished. A terminal error
 * disables maintenance until the next action reconstructs the session. */
void goodix_session_service_done (FpDevice *dev, GError *error);
/* The coordinator hands a finished action back and keeps running its
 * deactivation tail as background maintenance. */
void goodix_session_detach_action (FpDevice *dev);
void goodix_session_quiesce (FpDevice *dev,
                            void (*joined) (FpDevice *, gpointer),
                            gpointer data);
void goodix_session_start_action (FpDevice *dev);
/* Complete an unadmitted request without taking ownership of maintenance I/O. */
gboolean goodix_session_cancel_pending_action (FpDevice *dev);
/* Deliver the action's outcome, then settle the next hardware owner. */
void goodix_session_action_done (FpDevice *dev, GError *error,
                                void (*complete) (FpDevice *, GError *));
