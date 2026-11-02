/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Mirrors ContainerManager.sanitizeContainerName() in the Android app.
 * Replaces spaces with dashes so directory names are consistent. */
void sanitize_container_name(const char *name, char *out, size_t size) {
  /* Guard size == 0: 'size - 1' is size_t, so it would wrap to SIZE_MAX and
   * turn the copy into an unbounded write.  (safe_strncpy/read_file guard this
   * the same way.) */
  if (!out || size == 0)
    return;
  if (!name) {
    out[0] = '\0';
    return;
  }
  size_t i;
  for (i = 0; i < size - 1 && name[i] != '\0'; i++)
    out[i] = (name[i] == ' ') ? '-' : name[i];
  out[i] = '\0';
}

int validate_container_name(const char *name) {
  if (!name || !name[0])
    return 0;

  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 0;

  size_t len = strlen(name);
  if (len >= 256)
    return 0;

  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' '))
      return 0;
  }

  return 1;
}

int reject_container_name(const char *name) {
  if (!validate_container_name(name)) {
    ds_error("Invalid container name '%s'. Use only letters, numbers, "
             "'.', '_', '-' and spaces.",
             name);
    return -1;
  }
  return 0;
}
