/*
 * Goodix 53x5 driver for libfprint - Milan enrollment template internals
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include "goodix53x5-match.h"

#define GOODIX_MILAN_TEMPLATE_MAGIC      "G53M"
#define GOODIX_MILAN_TEMPLATE_MAGIC_LEN  4
#define GOODIX_MILAN_TEMPLATE_HEADER_LEN \
  (GOODIX_MILAN_TEMPLATE_MAGIC_LEN + sizeof (guint16))
#define GOODIX_MILAN_TEMPLATE_MAX_LEN    (1024 * 1024)

GBytes *goodix_match_wrap_template (const guint8 *template,
                                    gsize         template_len);

const guint8 *goodix_match_unwrap_template (
  const guint8              *template,
  gsize                      template_len,
  gsize                     *template_payload_len,
  GoodixSigfmTemplateStatus *status);
