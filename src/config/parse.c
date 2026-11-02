/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Bind Mount Sorting */

static int compare_bind_mounts(const void *a, const void *b) {
  const struct ds_bind_mount *ma = (const struct ds_bind_mount *)a;
  const struct ds_bind_mount *mb = (const struct ds_bind_mount *)b;
  return strcmp(ma->dest, mb->dest);
}

void sort_bind_mounts(struct ds_config *cfg) {
  if (!cfg || cfg->bind_count <= 1 || !cfg->binds)
    return;

  qsort(cfg->binds, cfg->bind_count, sizeof(struct ds_bind_mount),
        compare_bind_mounts);
}

int validate_bind_destination(const char *dest) {
  if (!dest || dest[0] != '/' || dest[1] == '\0')
    return 0;

  if (strlen(dest) >= PATH_MAX)
    return 0;

  /* "//" and "///" have no segment at all and land on the rootfs itself,
   * the same as "/", so a destination needs one real component. */
  int segments = 0;
  const char *p = dest;
  while (*p) {
    while (*p == '/')
      p++;
    const char *start = p;
    while (*p && *p != '/')
      p++;
    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;
    segments++;
    if ((len == 1 && start[0] == '.') ||
        (len == 2 && start[0] == '.' && start[1] == '.'))
      return 0;
    for (size_t i = 0; i < len; i++) {
      if (iscntrl((unsigned char)start[i]))
        return 0;
    }
  }

  return segments > 0;
}
