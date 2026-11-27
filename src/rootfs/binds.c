/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

int setup_custom_binds(struct ds_config *cfg, const char *rootfs) {
  if (cfg->bind_count == 0 || !cfg->binds)
    return 0;

  /* Ensure mounts are processed in alphabetical order of destination
   * so parent directories are always mounted before children. */
  sort_bind_mounts(cfg);

  for (int i = 0; i < cfg->bind_count; i++) {
    /* Handing the container the whole host filesystem defeats isolation.
     * realpath() so "//", "/.." and "/x/.." cannot dodge the check. */
    char real_src[PATH_MAX];
    if (realpath(cfg->binds[i].src, real_src) && strcmp(real_src, "/") == 0) {
      ds_error("Security Violation: Bind source / (host root) is not allowed, "
               "skipping %s!",
               cfg->binds[i].dest);
      continue;
    }

    char tgt[PATH_MAX * 2];
    int n = snprintf(tgt, sizeof(tgt), "%s%s", rootfs, cfg->binds[i].dest);
    if (n < 0 || (size_t)n >= sizeof(tgt)) {
      ds_warn("Bind mount target path too long, skipping: %s",
              cfg->binds[i].dest);
      continue;
    }

    /* Ensure parent directory exists */
    char parent[PATH_MAX];
    safe_strncpy(parent, tgt, sizeof(parent));
    char *slash = strrchr(parent, '/');
    if (slash) {
      *slash = '\0';
      mkdir_p(parent, 0755);
    }

    /* Perform bind mount (handles source/target symlink checks securely) */
    if (bind_mount(cfg->binds[i].src, tgt) < 0) {
      ds_warn("Failed to bind mount %s on %s: %s (skipping)", cfg->binds[i].src,
              cfg->binds[i].dest, strerror(errno));
      continue;
    }

    /* Verify isolation: Ensure we didn't accidentally mount over a host path
     * if the container rootfs had a complex malicious structure. */
    if (!is_subpath(rootfs, tgt)) {
      ds_error("Security Violation: Bind destination %s escapes rootfs %s!",
               tgt, rootfs);
      umount2(tgt, MNT_DETACH);
      continue;
    }

    /* Remount RO if requested (bind always lands RW first) */
    if (cfg->binds[i].ro) {
      if (mount(NULL, tgt, NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0)
        ds_warn("Failed to remount %s read-only: %s", tgt, strerror(errno));
    }
  }

  return 0;
}
