/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Kernel firmware search path management
 *
 * Android kernels patch firmware_class.c to support a comma-separated list
 * of custom search paths in the single 256-byte fw_path_para buffer
 * (e.g. "/vendor/firmware,/efs/wifi").  Writing a newline to the sysfs node
 * pops the first entry but always preserves the tail - so the last path can
 * never be fully cleared.  We therefore never attempt a full clear; removal
 * is best-effort and skipped when it would leave an empty string.
 *
 * Only called when --hw-access is active AND /lib/firmware exists in the
 * rootfs - both conditions are enforced at every call site.
 * Not supported on desktop Linux - both functions are no-ops there. */

/* Android kernel fw_path_para is 256 bytes including the NUL terminator. */
#define FW_PATH_BUF_SIZE 256

/*
 * Token-aware removal: walk the comma-separated list and rebuild it without
 * the matching entry.  Matches on exact token boundaries (not substrings) to
 * avoid accidentally removing "/mnt/Droidspaces/Void" when removing
 * "/mnt/Droidspaces/Void2".
 * Returns the length of the rebuilt string (0 = only entry, do not write).
 */
static int fw_remove_token(const char *buf, const char *token, char *out,
                           size_t out_size) {
  size_t token_len = strlen(token);
  const char *p = buf;
  int first = 1;
  out[0] = '\0';

  while (*p) {
    const char *comma = strchr(p, ',');
    size_t seg_len = comma ? (size_t)(comma - p) : strlen(p);

    if (!(seg_len == token_len && memcmp(p, token, token_len) == 0)) {
      /* Not our token - keep it */
      if (!first)
        strncat(out, ",", out_size - strlen(out) - 1);
      strncat(out, p,
              (seg_len < out_size - strlen(out) - 1)
                  ? seg_len
                  : out_size - strlen(out) - 1);
      first = 0;
    }

    if (!comma)
      break;
    p = comma + 1;
  }

  return (int)strlen(out);
}

void firmware_path_add(const char *fw_path) {
  /* Firmware path manipulation is an Android-kernel-specific feature.
   * Desktop Linux firmware_class does not support this sysfs node in the
   * same way - skip entirely on non-Android hosts. */
  if (!is_android())
    return;

  /* Bail silently if /lib/firmware is absent in the rootfs. */
  struct stat st;
  if (stat(fw_path, &st) < 0)
    return;

  /* Read the current comma-separated path list.
   * read_file() already strips trailing newlines. */
  char current[FW_PATH_BUF_SIZE] = {0};
  read_file(DS_FW_PATH_FILE, current, sizeof(current));

  /* Idempotent - don't add if already present as an exact token. */
  size_t fw_len = strlen(fw_path);
  const char *p = current;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t seg_len = comma ? (size_t)(comma - p) : strlen(p);
    if (seg_len == fw_len && memcmp(p, fw_path, fw_len) == 0)
      return; /* already there */
    if (!comma)
      break;
    p = comma + 1;
  }

  /* Build "fw_path,existing" - prepend so container firmware wins over OEM
   * defaults.  Guard against the 255-char string limit of fw_path_para.
   * Pre-validate lengths so the compiler can confirm no truncation occurs. */
  char new_path[FW_PATH_BUF_SIZE] = {0};
  if (current[0]) {
    size_t needed =
        strlen(fw_path) + 1 /* comma */ + strlen(current) + 1 /* NUL */;
    if (needed > sizeof(new_path)) {
      ds_warn("[FW] firmware path too long to prepend '%s' - skipping",
              fw_path);
      return;
    }
    /* Lengths validated - safe to build without truncation. */
    safe_strncpy(new_path, fw_path, sizeof(new_path));
    strncat(new_path, ",", sizeof(new_path) - strlen(new_path) - 1);
    strncat(new_path, current, sizeof(new_path) - strlen(new_path) - 1);
  } else {
    safe_strncpy(new_path, fw_path, sizeof(new_path));
  }

  ds_log("[FW] Adding firmware path: %s", fw_path);
  write_file(DS_FW_PATH_FILE, new_path);
}

void firmware_path_remove(const char *fw_path) {
  if (!is_android())
    return;

  /* Read current list - read_file() strips trailing newlines. */
  char current[FW_PATH_BUF_SIZE] = {0};
  if (read_file(DS_FW_PATH_FILE, current, sizeof(current)) < 0)
    return;

  char new_path[FW_PATH_BUF_SIZE] = {0};
  int new_len = fw_remove_token(current, fw_path, new_path, sizeof(new_path));

  if (new_len == 0) {
    /* Our path was the only entry.  The Android kernel never allows a full
     * clear - writing empty would be a no-op anyway - so just leave it. */
    ds_log("[FW] Skipping firmware path removal (last entry): %s", fw_path);
    return;
  }

  ds_log("[FW] Removing firmware path: %s", fw_path);
  write_file(DS_FW_PATH_FILE, new_path);
}
