/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <sys/personality.h>

/* Android detection */

int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  /* Priority 1: Check for recovery environment (e.g., TWRP) */
  if (access("/system/bin/recovery", F_OK) == 0) {
    cached_result = 0;
  }
  /* Priority 2: Check for core Android system markers */
  else if (access("/system/build.prop", F_OK) == 0 ||
           access("/system/bin/app_process", F_OK) == 0) {
    cached_result = 1;
  }
  /* Fallback: Not a standard Android environment */
  else {
    cached_result = 0;
  }

  return cached_result;
}

/* A 32-bit Android on a 64-bit kernel runs init as PER_LINUX32 and every
 * process inherits it, the container included, so a 64-bit rootfs sees uname
 * report armv8l and install scripts fetch 32-bit builds. Only the 64-bit
 * backend can be running on a 64-bit kernel, so it alone resets this. Call
 * while the host /system is still visible, is_android() looks there. */
void android_native_personality(void) {
#if UINTPTR_MAX > 0xffffffffu
  if (!is_android())
    return;
  int persona = personality(0xffffffff);
  if (persona != -1 && (persona & PER_MASK) == PER_LINUX32)
    personality((unsigned long)((persona & ~PER_MASK) | PER_LINUX));
#endif
}
