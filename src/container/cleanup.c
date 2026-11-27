/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

void cleanup_container_resources(struct ds_config *cfg, pid_t pid,
                                 int skip_unmount, int force_cleanup) {
  /* Flush filesystem buffers (skip if force cleanup - sync can hang on
   * zombie-held fs) */
  if (!force_cleanup)
    sync();

  if (is_android() && !skip_unmount) {
    ds_x11_daemon_stop(cfg);
    ds_virgl_daemon_stop(cfg);
    ds_pulse_daemon_stop(cfg);
    ds_anland_daemon_stop(cfg);
    if (count_running_containers(NULL, 0) == 0) {
      android_optimizations(0);
    }
    /* SELinux: Restore enforcing mode if no other permissive containers are
     * running, but only if at least one permissive container is installed. */
    int selinux_needs = check_selinux_permissive_needs();
    if (selinux_needs == 0) {
      ds_set_selinux_permissive(0);
    }
  }

  /* 1. Cleanup firmware path (hw_access mode only; skip on force-cleanup
   * since accessing a zombie-held rootfs can hang).
   * Use cfg->rootfs_path directly - it is already fully resolved and valid for
   * both dir-based and img-based modes at this point. */
  if (!force_cleanup && cfg->hw_access && cfg->rootfs_path[0]) {
    char fw_path[PATH_MAX + 16];
    snprintf(fw_path, sizeof(fw_path), "%s/lib/firmware", cfg->rootfs_path);
    firmware_path_remove(fw_path);
  }

  /* 3. Handle Volatile Overlay Cleanup (upper/work/merged)
   * This MUST happen before unmounting the lower rootfs image.
   * When force_cleanup, use detach+force unmount to avoid hangs. */
  if (cfg->volatile_mode) {
    if (force_cleanup) {
      /* Force path: skip sync, just detach everything */
      char merged[PATH_MAX + 32];
      snprintf(merged, sizeof(merged), "%s/merged", cfg->volatile_dir);
      umount2(merged, MNT_DETACH | MNT_FORCE);
      umount2(cfg->volatile_dir, MNT_DETACH | MNT_FORCE);
      /* Best-effort directory removal */
      remove_recursive(cfg->volatile_dir);
      cfg->volatile_dir[0] = '\0';
    } else {
      cleanup_volatile_overlay(cfg);
    }
  }

  /* 4. Handle rootfs image unmount */
  const char *mount_point = cfg->img_mount_point;
  if (mount_point[0] && !skip_unmount) {
    if (force_cleanup) {
      /* Force path: detach+force unmount, no sync, no retry loops */
      umount2(mount_point, MNT_DETACH | MNT_FORCE);
      rmdir(mount_point); /* best-effort */
    } else {
      /* Explicitly call unmount wrapper. It handles its own logging. */
      unmount_rootfs_img(mount_point, cfg->foreground);
    }
  }

  /* 5. Unlink PID files. A restart (skip_unmount) keeps them: the monitor
   * reads the dead PID in them as "nobody has claimed this exit yet". */
  if (!skip_unmount)
    unlink(cfg->pidfile);

  /* Network cleanup: remove host veth and owned network state. Any isolated
   * mode can be someone's gateway (an OpenWrt in --net=none, say), and its
   * LAN cables must go with it, so this is not limited to NAT and gateway. */
  if (cfg->net_mode != DS_NET_HOST) {
    ds_net_cleanup(cfg, pid > 0 ? pid : cfg->container_pid);
  }

  /* Cgroup subtree cleanup: remove /sys/fs/cgroup/droidspaces/<name>/.
   * All container processes are dead by now so every leaf is empty and
   * the bottom-up rmdir walk always succeeds.  Skipped on restart
   * (skip_unmount=1) so the monitor's cgroup context stays intact for
   * the next boot cycle. */
  if (!skip_unmount) {
    ds_cgroup_cleanup_container(cfg->container_name);
  }
}
