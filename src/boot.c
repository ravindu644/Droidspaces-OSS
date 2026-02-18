/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

int internal_boot(struct ds_config *cfg) {

  /* 1. Isolated mount namespace */
  if (unshare(CLONE_NEWNS) < 0)
    ds_die("Failed to unshare mount namespace: %s", strerror(errno));

  /* 2. Make root filesystem private to prevent mount leakage to host */
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
    ds_die("Failed to remount / as private: %s", strerror(errno));

  /* 2. Bind mount rootfs to itself (required for pivot_root) */
  if (mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL) <
      0)
    ds_die("Failed to bind mount rootfs: %s", strerror(errno));

  /* 3. Change directory to rootfs */
  if (chdir(cfg->rootfs_path) < 0)
    ds_die("Failed to chdir to rootfs: %s", strerror(errno));

  /* 3.1 Read UUID from sync file if not already provided (parity with v2) */
  if (cfg->uuid[0] == '\0') {
    read_file(".droidspaces-uuid", cfg->uuid, sizeof(cfg->uuid));
  }
  unlink(".droidspaces-uuid");

  /* 4. Prepare .old_root for pivot_root */
  mkdir(".old_root", 0755);

  /* 5. Setup /dev (tmpfs or devtmpfs) */
  if (setup_dev(".", cfg->hw_access) < 0)
    ds_die("Failed to setup /dev");

  /* 6. Mount virtual filesystems */
  mkdir("proc", 0755);
  domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);

  /* Mount /sys
   * CRITICAL: Without --hw-access, mount sysfs as read-only to prevent
   * systemd-udevd from triggering hardware events on the host.
   * However, remount /sys/devices/virtual/net as read-write for networking
   * (needed for Docker, WireGuard, Tailscale, SSH, etc.).
   * With --hw-access, allow read-write access for full hardware control. */
  mkdir("sys", 0755);
  if (cfg->hw_access) {
    /* Full hardware access: mount sysfs read-write */
    domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
  } else {
    /* Hardware isolation: mixed mode
     * Step 1: Mount sysfs read-write initially */
    domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);

    /* Step 2: Create directory structure and mount new sysfs instance at
     * /sys/devices/virtual/net (read-write). This must be done BEFORE
     * remounting the parent /sys as read-only. */
    mkdir("sys/devices", 0755);
    mkdir("sys/devices/virtual", 0755);
    mkdir("sys/devices/virtual/net", 0755);

    if (domount("sysfs", "sys/devices/virtual/net", "sysfs",
                MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
      ds_warn("Failed to mount sysfs at sys/devices/virtual/net "
              "(networking may be limited)");
    }

    /* Step 3: Remount entire /sys as read-only (prevents hardware
     * interference). The subdirectory mount at /sys/devices/virtual/net remains
     * read-write as a separate mount point. */
    if (mount(NULL, "sys", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0) {
      ds_warn("Failed to remount /sys as read-only: %s", strerror(errno));
    }
  }

  mkdir("run", 0755);
  domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755");

  /* 7. Bind-mount TTYs BEFORE pivot_root so we can still see /dev/pts/N from
   * host */
  /* We use relative paths to the current directory (rootfs) */
  if (mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL) < 0)
    ds_warn("Failed to bind mount console %s: %s", cfg->console.name,
            strerror(errno));

  char tty_target[32];
  for (int i = 0; i < cfg->tty_count; i++) {
    snprintf(tty_target, sizeof(tty_target), "dev/tty%d", i + 1);
    if (mount(cfg->ttys[i].name, tty_target, NULL, MS_BIND, NULL) < 0)
      ds_warn("Failed to bind mount %s: %s", tty_target, strerror(errno));
  }

  /* 8. Write UUID marker for PID discovery */
  char marker[PATH_MAX];
  snprintf(marker, sizeof(marker), "run/%s", cfg->uuid);
  write_file(marker, "init");
  write_file("run/droidspaces", DS_VERSION);

  /* 9. Setup Cgroups */
  setup_cgroups();

  /* 10. Android-specific storage */
  if (cfg->android_storage) {
    android_setup_storage(".");
  }

  /* 12. PIVOT_ROOT */
  if (syscall(SYS_pivot_root, ".", ".old_root") < 0)
    ds_die("pivot_root failed: %s", strerror(errno));

  /* 13. Switch to new root */
  if (chdir("/") < 0)
    ds_die("chdir / failed: %s", strerror(errno));

  /* 14. Setup devpts (must be after pivot_root for newinstance) */
  setup_devpts(cfg->hw_access);

  /* 15. Configure rootfs networking (hostname, resolv.conf, etc) */
  fix_networking_rootfs(cfg);

  /* 16. Cleanup .old_root */
  if (umount2("/.old_root", MNT_DETACH) < 0)
    ds_warn("Failed to unmount .old_root: %s", strerror(errno));
  else
    rmdir("/.old_root");

  /* 17. Set container identity for systemd/openrc */
  write_file("/run/systemd/container", "droidspaces");

  /* 18. Clear environment and set container defaults */
  ds_env_boot_setup(cfg);

  /* 19. Redirect standard I/O to /dev/console */
  int console_fd = open("/dev/console", O_RDWR);
  if (console_fd >= 0) {
    ds_terminal_set_stdfds(console_fd);
    ds_terminal_make_controlling(console_fd);
    if (console_fd > 2)
      close(console_fd);
  }

  /* 21. EXEC INIT */
  char *argv[] = {"/sbin/init", NULL};

  if (execve("/sbin/init", argv, environ) < 0) {
    ds_error("Failed to execute /sbin/init: %s", strerror(errno));
    ds_die("Container boot failed. Please ensure the rootfs path is correct "
           "and contains a valid /sbin/init binary.");
  }

  return -1;
}
