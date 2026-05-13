/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Android detection
 * ---------------------------------------------------------------------------*/

int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  if (getenv("ANDROID_ROOT") || access("/system/bin/app_process", F_OK) == 0 ||
      access("/system/build.prop", F_OK) == 0)
    cached_result = 1;
  else
    cached_result = 0;

  return cached_result;
}

/* ---------------------------------------------------------------------------
 * Android optimizations
 * ---------------------------------------------------------------------------*/

void android_optimizations(int enable) {
  if (!is_android())
    return;

  if (enable) {
    ds_log("Applying Android system optimizations...");
    char *args1[] = {"cmd",
                     "device_config",
                     "put",
                     "activity_manager",
                     "max_phantom_processes",
                     "2147483647",
                     NULL};
    run_command_quiet(args1);
    char *args2[] = {"cmd", "device_config", "set_sync_disabled_for_tests",
                     "persistent", NULL};
    run_command_quiet(args2);
    char *args3[] = {"dumpsys", "deviceidle", "disable", NULL};
    run_command_quiet(args3);
  } else {
    char *args1[] = {"cmd",
                     "device_config",
                     "put",
                     "activity_manager",
                     "max_phantom_processes",
                     "32",
                     NULL};
    run_command_quiet(args1);
    char *args2[] = {"cmd", "device_config", "set_sync_disabled_for_tests",
                     "none", NULL};
    run_command_quiet(args2);
    char *args3[] = {"dumpsys", "deviceidle", "enable", NULL};
    run_command_quiet(args3);
  }
}

/* ---------------------------------------------------------------------------
 * Data partition remount (for suid support)
 * ---------------------------------------------------------------------------*/

void android_remount_data_suid(void) {
  if (!is_android())
    return;

  ds_log("Ensuring /data is mounted with suid support...");
  /* On some Android versions, /data is mounted nosuid. We need suid for
   * sudo/su/ping within the container if it's stored on /data. */
  char *args[] = {"mount", "-o", "remount,suid", "/data", NULL};
  if (run_command_quiet(args) != 0) {
    ds_warn(
        "Failed to remount /data with suid support. su/sudo might not work.");
  }
}

/* ---------------------------------------------------------------------------
 * Storage
 * ---------------------------------------------------------------------------*/

int android_setup_storage(const char *rootfs_path) {
  if (!is_android()) {
    return 0;
  }

  if (!rootfs_path) {
    ds_warn("android_setup_storage called with NULL rootfs_path");
    return -1;
  }

  const char *storage_src = "/storage/emulated/0";
  struct stat st;

  if (stat(storage_src, &st) < 0 || !S_ISDIR(st.st_mode) ||
      access(storage_src, R_OK) < 0) {
    ds_warn("Android storage not found or not readable at %s", storage_src);
    return -1;
  }

  /* Create target directories inside rootfs: storage/, storage/emulated/,
   * storage/emulated/0 */
  char path[PATH_MAX];
  int ret;

  ret = snprintf(path, sizeof(path), "%s/storage", rootfs_path);
  if (ret < 0 || (size_t)ret >= sizeof(path))
    return -1;
  if (mkdir(path, 0755) < 0 && errno != EEXIST)
    return -1;

  ret = snprintf(path, sizeof(path), "%s/storage/emulated", rootfs_path);
  if (ret < 0 || (size_t)ret >= sizeof(path))
    return -1;
  if (mkdir(path, 0755) < 0 && errno != EEXIST)
    return -1;

  ret = snprintf(path, sizeof(path), "%s/storage/emulated/0", rootfs_path);
  if (ret < 0 || (size_t)ret >= sizeof(path))
    return -1;
  if (mkdir(path, 0755) < 0 && errno != EEXIST)
    return -1;

  ds_log("Mounting Android internal storage to /storage/emulated/0...");
  if (mount(storage_src, path, NULL, MS_BIND | MS_REC, NULL) < 0) {
    ds_warn("Failed to bind-mount Android storage %s -> %s: %s", storage_src,
            path, strerror(errno));
    return -1;
  }

  return 0;
}
