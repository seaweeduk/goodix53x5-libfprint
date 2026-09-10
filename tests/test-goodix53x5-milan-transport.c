/*
 * Goodix profile-9 transport/coordinator scheduling contracts.
 * Copyright (C) 2026 goodix53x5-libfprint contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Keep the private production boundaries in one translation unit. The USB/time
 * fixture is separately compiled; these files own the existing case families. */
#include "test-goodix53x5-milan-transport-owners.h"
#include "test-goodix53x5-milan-transport-cases.h"
#include "test-goodix53x5-milan-transport-commands.c"
#include "test-goodix53x5-milan-transport-capture.c"
#include "test-goodix53x5-milan-transport-lifecycle.c"

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  register_command_tests ();
  register_capture_tests ();
  register_lifecycle_tests ();
  return g_test_run ();
}
