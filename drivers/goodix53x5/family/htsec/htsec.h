/*
 * Goodix 53x5 driver for libfprint — the HTSEC family (5335/5385/5395)
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include "driver-private.h"

/* Accepts any firmware version, or none. */
GError *goodix_htsec_check_firmware (const char *firmware);
/* Returns a new SSM running the 53x5 bring-up. */
FpiSsm *goodix_htsec_open_new (FpDevice *dev);

/* Returns a new SSM for one GTLS handshake attempt. */
FpiSsm *goodix_gtls_attempt_new (FpDevice *dev, GoodixChannelReason reason);
/* Decrypts a GTLS image payload into raw12 bytes in @out. */
GoodixFrameStatus goodix_decrypt_frame_gtls (FpiDeviceGoodix53x5 *self,
                                             const guint8 *payload, gsize len,
                                             guint8 **out, gsize *out_len);
/* Counts an undecodable frame; TRUE at every second one. */
gboolean goodix_gtls_image_error (FpiDeviceGoodix53x5 *self);
/* Wipes the GTLS session's copy of the PSK. */
void     goodix_gtls_discard (FpiDeviceGoodix53x5 *self);
