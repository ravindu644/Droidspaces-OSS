/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

int internal_boot(struct ds_config *cfg) {
  /* 1. Isolated mount namespace */
  if (unshare(CLONE_NEWNS) < 0) {
    ds_error("Failed to unshare mount namespace: %s", strerror(errno));
    return -1;
  }

  /* 2. Make all mounts private to avoid leaking to host */
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
    ds_error("Failed to make / private: %s", strerror(errno));
    return -1;
  }

  /* Apply Android compatibility Seccomp filter to child processes.
   * This neutralizes broken sandboxing/keyring logic in systemd. */
  if (is_android()) {
    android_seccomp_setup();
  }

  /* 2.5 Setup volatile overlay INSIDE the container's mount namespace.
   * This MUST happen here (not in parent) so the overlay's connection to
   * its lowerdir (e.g. a loop-mounted image) survives mount privatization. */
  /* 3. Volatile Overlay (Ephemereal mode) */
  if (cfg->volatile_mode) {
    if (setup_volatile_overlay(cfg) < 0) {
      ds_error("Failed to setup volatile overlay.");
      return -1;
    }
  }

  /* 3. Bind mount rootfs to itself (required for pivot_root) */
  if (mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL) <
      0) {
    ds_error("Failed to bind mount rootfs: %s", strerror(errno));
    return -1;
  }

  /* 5. Set working directory to rootfs */
  if (chdir(cfg->rootfs_path) < 0) {
    ds_error("Failed to chdir to '%s': %s", cfg->rootfs_path, strerror(errno));
    return -1;
  }

  /* 4.1 Read UUID from sync file if not already provided (parity with v2) */
  if (cfg->uuid[0] == '\0') {
    read_file(".droidspaces-uuid", cfg->uuid, sizeof(cfg->uuid));
  }
  if (access(".droidspaces-uuid", F_OK) == 0) {
    if (unlink(".droidspaces-uuid") < 0) {
      /* This might fail if the rootfs is RO (image mount), but internal_boot
       * already skips writing it in that case. */
    }
  }

  /* 5. Prepare .old_root for pivot_root */
  if (mkdir(".old_root", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create .old_root directory: %s", strerror(errno));
    return -1;
  }

  /* 8. Setup /dev */
  if (setup_dev(".", cfg->hw_access) < 0) {
    ds_error("Failed to setup /dev.");
    return -1;
  }

  /* 6. Mount virtual filesystems */
  if (mkdir("proc", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create proc directory: %s", strerror(errno));
    return -1;
  }
  if (domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount procfs: %s", strerror(errno));
    return -1;
  }

  /* Mount /sys
   * CRITICAL: Without --hw-access, mount sysfs as read-only to prevent
   * systemd-udevd from triggering hardware events on the host.
   * However, remount /sys/devices/virtual/net as read-write for networking
   * (needed for Docker, WireGuard, Tailscale, SSH, etc.).
   * With --hw-access, allow read-write access for full hardware control. */
  if (mkdir("sys", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create sys directory: %s", strerror(errno));
    return -1;
  }
  if (domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount sysfs: %s", strerror(errno));
    return -1;
  }

  if (cfg->hw_access) {
    /* DYNAMIC HARDWARE HOLES: Instead of hardcoding, we iterate through
     * everything in /sys and 'pin' subdirectories as independent RW mounts.
     * This ensures 100% hardware visibility (devices, bus, class, block, etc)
     * even after we remount the top-level /sys as RO for systemd's benefit. */
    DIR *d = opendir("sys");
    if (d) {
      struct dirent *de;
      while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
          continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "sys/%s", de->d_name);

        struct stat st;
        if (stat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
          if (mount(subpath, subpath, NULL, MS_BIND | MS_REC, NULL) < 0) {
            /* Ignore errors for files or pseudo-dirs that can't be mounted */
          }
        }
      }
      closedir(d);
    }
  } else {
    /* Hardware isolation: network only mixed mode */
    if (mkdir("sys/devices", 0755) < 0 && errno != EEXIST) {
      ds_warn("Failed to create sys/devices directory: %s", strerror(errno));
    }
    if (mkdir("sys/devices/virtual", 0755) < 0 && errno != EEXIST) {
      ds_warn("Failed to create sys/devices/virtual directory: %s",
              strerror(errno));
    }
    if (mkdir("sys/devices/virtual/net", 0755) < 0 && errno != EEXIST) {
      ds_warn("Failed to create sys/devices/virtual/net directory: %s",
              strerror(errno));
    }

    if (domount("sysfs", "sys/devices/virtual/net", "sysfs",
                MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
      ds_warn("Failed to mount sysfs at sys/devices/virtual/net "
              "(networking may be limited)");
    }
  }

  /* CRITICAL: Remount entire /sys as read-only. This is the official systemd
   * indicator that it is running in a container. Without this, systemd 258+
   * tries to 'resolve' /dev/console to a host TTY, leading to the getty Loop.
   * Because we self-bind mounted subdirectories above, they remain RW. */
  if (mount(NULL, "sys", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0) {
    ds_warn("Failed to remount /sys as read-only: %s", strerror(errno));
  }

  /* Mask the console discovery file to prevent resolution back to host */
  if (mount("/dev/null", "sys/class/tty/console/active", NULL, MS_BIND, NULL) <
      0) {
    /* File might not exist yet if sysfs is partially populated */
  }

  if (mkdir("run", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create run directory: %s", strerror(errno));
    return -1;
  }
  if (domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0) {
    ds_error("Failed to mount tmpfs at /run: %s", strerror(errno));
    return -1;
  }

  /* 7. Bind-mount TTYs BEFORE pivot_root so we can still see /dev/pts/N from
   * host */
  /* We use relative paths to the current directory (rootfs) */
  if (mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL) < 0)
    ds_warn("Failed to bind mount console '%s': %s", cfg->console.name,
            strerror(errno));

  char tty_target[32];
  for (int i = 0; i < cfg->tty_count; i++) {
    snprintf(tty_target, sizeof(tty_target), "dev/tty%d", i + 1);
    if (mount(cfg->ttys[i].name, tty_target, NULL, MS_BIND, NULL) < 0)
      ds_warn("Failed to bind mount '%s': %s", tty_target, strerror(errno));
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

  /* 11. Custom bind mounts */
  setup_custom_binds(cfg, ".");

  /* 14. pivot_root */
  if (syscall(SYS_pivot_root, ".", ".old_root") < 0) {
    ds_error("pivot_root failed: %s", strerror(errno));
    /* pivot_root might fail if we are on ramfs.
     * We don't die here because we might want to try fallback or
     * at least log it properly. But in this implementation, it's critical. */
    return -1;
  }

  if (chdir("/") < 0) {
    ds_error("chdir(\"/\") after pivot_root failed: %s", strerror(errno));
    return -1;
  }

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
    /* Sticky permissions again just in case systemd's TTYReset stripped them */
    fchmod(console_fd, 0620);
    fchown(console_fd, 0, 5);
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
