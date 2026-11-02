/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

void print_ds_banner(void) {
  printf(C_CYAN C_BOLD "Welcome to " C_WHITE DS_PROJECT_NAME
                       " v" DS_VERSION C_CYAN " !" C_RESET "\r\n\r\n");
  fflush(stdout);
}
