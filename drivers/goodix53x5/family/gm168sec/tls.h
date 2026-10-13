/*
 * Goodix 53x5 driver for libfprint — TLS-PSK for wrapped sensors
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <glib.h>

/* A TLS record's header: content type, version, and its big-endian length. */
#define GOODIX_TLS_RECORD_HEADER 5

/* The host (server) end of a wrapped sensor's TLS 1.2-PSK session: the one
 * place its records are produced and authenticated; transport only moves them. */
typedef struct _GoodixTls GoodixTls;

GoodixTls *goodix_tls_new (const guint8 *psk,
                           gsize         psk_len,
                           GError      **error);
void       goodix_tls_free (GoodixTls *tls);

/* Advances the handshake with the sensor's records in @in, which may be
 * empty; @out receives the records to send back, or NULL. Fails, and
 * leaves the session unusable, when the sensor's records are rejected or
 * the handshake takes more than ten calls. */
gboolean   goodix_tls_handshake (GoodixTls    *tls,
                                 const guint8 *in,
                                 gsize         in_len,
                                 GBytes      **out,
                                 GError      **error);
gboolean   goodix_tls_established (GoodixTls *tls);
gboolean   goodix_tls_handshake_pending (GoodixTls *tls);

/* Returns the plaintext of application-data records, or NULL when one does
 * not authenticate or the last is incomplete; either leaves the session
 * unusable. */
guint8    *goodix_tls_decrypt (GoodixTls    *tls,
                               const guint8 *records,
                               gsize         records_len,
                               gsize        *out_len);
