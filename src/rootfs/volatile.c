/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

int check_volatile_mode(struct ds_config *cfg) {
  if (!cfg->volatile_mode)
    return 0;

  if (grep_file("/proc/filesystems", "overlay") != 1) {
    ds_error("OverlayFS is not supported by your kernel. Volatile mode cannot "
             "be used.");
    return -1;
  }

  /* Pre-flight: reject f2fs lowerdir - known Android kernel limitation */
  struct statfs sfs;
  if (statfs(cfg->rootfs_path, &sfs) == 0 && sfs.f_type == 0xF2F52010) {
    ds_error("Volatile mode cannot be used: Your rootfs is on f2fs, which is "
             "not supported as an OverlayFS lower layer on most Android "
             "kernels.");
    ds_error("Tip: Use a rootfs image (-i) instead of a directory (-r) "
             "for volatile mode on f2fs partitions.");
    return -1;
  }

  return 0;
}

int setup_volatile_overlay(struct ds_config *cfg) {
  /* 1. Create temporary workspace in Droidspaces/Volatile/<name> */
  char base[PATH_MAX];
  snprintf(base, sizeof(base), "%s/" DS_VOLATILE_SUBDIR "/%s",
           get_workspace_dir(), cfg->container_name);
  if (mkdir_p(base, 0755) < 0) {
    ds_error("Failed to create volatile workspace: %s", base);
    return -1;
  }
  safe_strncpy(cfg->volatile_dir, base, sizeof(cfg->volatile_dir));

  /* 2. Mount tmpfs as the backing store for upper/work */
  if (domount("none", base, "tmpfs", 0, "size=50%,mode=755") < 0)
    return -1;

  /* 3. Create subdirectories */
  char upper[PATH_MAX + 32], work[PATH_MAX + 32], merged[PATH_MAX + 32];
  snprintf(upper, sizeof(upper), "%s/upper", base);
  snprintf(work, sizeof(work), "%s/work", base);
  snprintf(merged, sizeof(merged), "%s/merged", base);
  mkdir(upper, 0755);
  mkdir(work, 0755);
  mkdir(merged, 0755);

  /* 4. Perform Overlay mount */
  char opts[32768];
  int n;

  if (is_android()) {
    n = snprintf(opts, sizeof(opts),
                 "lowerdir=%s,upperdir=%s/upper,workdir=%s/work,context=\"%s\"",
                 cfg->rootfs_path, base, base, DS_ANDROID_TMPFS_CONTEXT);
  } else {
    n = snprintf(opts, sizeof(opts),
                 "lowerdir=%s,upperdir=%s/upper,workdir=%s/work",
                 cfg->rootfs_path, base, base);
  }

  if (n < 0 || (size_t)n >= sizeof(opts)) {
    ds_error("OverlayFS options too long");
    cleanup_volatile_overlay(cfg);
    return -1;
  }

  if (domount("overlay", merged, "overlay", 0, opts) < 0) {
    ds_error("OverlayFS mount failed. Your kernel might not support it.");
    /* Cleanup: unmount tmpfs first, then remove workspace */
    umount2(base, MNT_DETACH);
    ds_error("OverlayFS mount failed: %s", strerror(errno));
    cleanup_volatile_overlay(cfg);
    return -1;
  }

  /* 9. Update cfg->rootfs_path to the merged view */
  safe_strncpy(cfg->rootfs_path, merged, sizeof(cfg->rootfs_path));

  return 0;
}

/**
 * is_mount_in_namespace() - Check if `path` is mounted in OUR namespace.
 *
 * Reads /proc/self/mountinfo and searches for an exact match of `path`
 * in the mount-point column (field 5, 0-indexed: 4).
 *
 * Unlike is_mountpoint() (which uses stat-based device ID comparison),
 * this checks the kernel's mount table directly. This is critical for
 * overlay mounts that may share the same device as the lowerdir.
 *
 * Returns 1 if mounted, 0 if not.
 */
static int is_mount_in_namespace(const char *path) {
  FILE *f = fopen("/proc/self/mountinfo", "r");
  if (!f)
    return 0;

  char io_buf[65536];
  setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

  char line[4096];
  size_t path_len = strlen(path);

  while (fgets(line, sizeof(line), f)) {
    /* mountinfo format: id parent_id major:minor root mount_point ... */
    /* We need field 5 (mount_point), skip first 4 fields */
    const char *p = line;
    for (int skip = 0; skip < 4 && *p; skip++) {
      while (*p && *p != ' ')
        p++;
      while (*p == ' ')
        p++;
    }
    /* p now points at the mount_point field */
    if (strncmp(p, path, path_len) == 0 &&
        (p[path_len] == ' ' || p[path_len] == '\n' || p[path_len] == '\0')) {
      fclose(f);
      return 1;
    }
  }
  fclose(f);
  return 0;
}

/**
 * cleanup_volatile_overlay() - Simplified OverlayFS cleanup.
 *
 * The overlay is mounted INSIDE the container's mount namespace (boot.c).
 * When the container dies, the kernel tears down the namespace and the
 * mounts vanish automatically.
 *
 * We simply check if the mount is visible in our namespace (host); if so,
 * we try to unmount it normally before deleting the workspace directory.
 */
int cleanup_volatile_overlay(struct ds_config *cfg) {
  if (cfg->volatile_dir[0] == '\0')
    return 0;

  char merged[PATH_MAX + 32];
  snprintf(merged, sizeof(merged), "%s/merged", cfg->volatile_dir);

  /* Skip logging for clean exits - nothing prints after 'Powering off.' */

  /* 1. Fast path: check if mounts already vanished (normal case) */
  if (!is_mount_in_namespace(merged) &&
      !is_mount_in_namespace(cfg->volatile_dir)) {
    goto done;
  }

  /* 2. Slow path: unmount visible mounts (e.g. stop-rootfs on live container)
   */
  sync();
  umount(merged);
  umount(cfg->volatile_dir);

done:
  /* settle time for kernel to release backing store info */
  usleep(DS_RETRY_DELAY_US / 2);
  int r = remove_recursive(cfg->volatile_dir);
  cfg->volatile_dir[0] = '\0';
  return r;
}
