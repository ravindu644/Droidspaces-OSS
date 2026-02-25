/*
 * Droidspaces v4 — High-performance Container Runtime
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
   * On legacy kernels, this neutralizes broken sandboxing logic in systemd
   * that triggers VFS deadlocks in grab_super(). */
  if (is_android()) {
    int is_systemd = is_systemd_rootfs(cfg->rootfs_path);
    android_seccomp_setup(is_systemd);
  }

  /* 3. Setup volatile overlay INSIDE the container's mount namespace.
   * This MUST happen here (not in parent) so the overlay's connection to
   * its lowerdir (e.g. a loop-mounted image) survives mount privatization. */
  if (cfg->volatile_mode) {
    if (setup_volatile_overlay(cfg) < 0) {
      ds_error("Failed to setup volatile overlay.");
      return -1;
    }
  }

  /* 4. Bind mount rootfs to itself (required for pivot_root) */
  if (mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL) <
      0) {
    ds_error("Failed to bind mount rootfs: %s", strerror(errno));
    return -1;
  }

  /* 5. Set working directory to rootfs (required before pivot_root) */
  if (chdir(cfg->rootfs_path) < 0) {
    ds_error("Failed to chdir to '%s': %s", cfg->rootfs_path, strerror(errno));
    return -1;
  }

  /* 6. Read UUID from sync file if not already provided (parity with v2) */
  if (cfg->uuid[0] == '\0') {
    read_file(".droidspaces-uuid", cfg->uuid, sizeof(cfg->uuid));
  }
  if (access(".droidspaces-uuid", F_OK) == 0) {
    if (unlink(".droidspaces-uuid") < 0) {
      /* This might fail if the rootfs is RO (image mount), but internal_boot
       * already skips writing it in that case. */
    }
  }

  /* 7. Prepare .old_root for pivot_root */
  if (mkdir(".old_root", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create .old_root directory: %s", strerror(errno));
    return -1;
  }

  /* 8. Setup /dev (device nodes, devtmpfs) */
  if (setup_dev(".", cfg->hw_access) < 0) {
    ds_error("Failed to setup /dev.");
    return -1;
  }

  /* 9. Mount virtual filesystems (proc, sys) */
  if (mkdir("proc", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create proc directory: %s", strerror(errno));
    return -1;
  }
  if (domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount procfs: %s", strerror(errno));
    return -1;
  }

  /* Mount /sys */
  if (mkdir("sys", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create sys directory: %s", strerror(errno));
    return -1;
  }
  if (domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount sysfs: %s", strerror(errno));
    return -1;
  }

  /* 10. Pre-create the cgroup mountpoint while /sys is still RW.
   * This allows us to mount cgroups onto it later even after /sys is RO. */
  mkdir_p("sys/fs/cgroup", 0755);

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

  if (mount(NULL, "sys", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0) {
    ds_warn("Failed to remount /sys as read-only: %s", strerror(errno));
  }

  /* 11. Setup Cgroups AFTER locking down /sys.
   * Mounting onto a directory on a RO parent is allowed for root, and it
   * ensures the sub-mount (tmpfs) is RW and independent of the parent's RO. */
  setup_cgroups();

  /* 12. Mask the console discovery file to prevent resolution back to host */
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

  /* 13. Bind-mount TTYs BEFORE pivot_root so we can still see /dev/pts/N
   * from host. We use relative paths to the current directory (rootfs). */
  if (mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL) < 0)
    ds_warn("Failed to bind mount console '%s': %s", cfg->console.name,
            strerror(errno));

  char tty_target[32];
  for (int i = 0; i < cfg->tty_count; i++) {
    snprintf(tty_target, sizeof(tty_target), "dev/tty%d", i + 1);
    if (mount(cfg->ttys[i].name, tty_target, NULL, MS_BIND, NULL) < 0)
      ds_warn("Failed to bind mount '%s': %s", tty_target, strerror(errno));
  }

  /* 14. Write UUID marker for PID discovery */
  char marker[PATH_MAX];
  snprintf(marker, sizeof(marker), "run/%s", cfg->uuid);
  write_file(marker, "init");
  write_file("run/droidspaces", DS_VERSION);

  /* 15. Android-specific storage */
  if (cfg->android_storage) {
    android_setup_storage(".");
  }

  /* 16. Custom bind mounts */
  setup_custom_binds(cfg, ".");

  /* 17. pivot_root */
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

  /* 18. Setup devpts (must be after pivot_root for newinstance) */
  setup_devpts(cfg->hw_access);

  /* 19. Configure rootfs networking (hostname, resolv.conf, etc) */
  fix_networking_rootfs(cfg);

  /* 20. Cleanup .old_root */
  if (umount2("/.old_root", MNT_DETACH) < 0)
    ds_warn("Failed to unmount .old_root: %s", strerror(errno));
  else
    rmdir("/.old_root");

  /* 21. Set container identity for systemd/openrc */
  write_file("/run/systemd/container", "droidspaces");

  /* 22. Clear environment and set container defaults */
  ds_env_boot_setup(cfg);

  /* 23. Redirect standard I/O to /dev/console */
  int console_fd = open("/dev/console", O_RDWR);
  if (console_fd >= 0) {
    ds_terminal_set_stdfds(console_fd);
    ds_terminal_make_controlling(console_fd);

    /* Set a sane default window size on the console PTY if none was set.
     * The parent's console_monitor_loop will overwrite this with the
     * real host terminal size via SIGWINCH, but we need a reasonable
     * default so early boot output (before the parent syncs) is
     * properly aligned. Without this, programs like sudo that query
     * the terminal size get {0,0} and produce misaligned output. */
    struct winsize ws;
    if (ioctl(console_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col == 0 &&
        ws.ws_row == 0) {
      ws.ws_row = 24;
      ws.ws_col = 80;
      ioctl(console_fd, TIOCSWINSZ, &ws);
    }

    /* Sticky permissions again just in case systemd's TTYReset stripped them */
    fchmod(console_fd, 0620);
    fchown(console_fd, 0, 5);
    if (console_fd > 2)
      close(console_fd);
  }

  /* 24. EXEC INIT */
  char *argv[] = {"/sbin/init", NULL};

  if (execve("/sbin/init", argv, environ) < 0) {
    ds_error("Failed to execute /sbin/init: %s", strerror(errno));
    ds_die("Container boot failed. Please ensure the rootfs path is correct "
           "and contains a valid /sbin/init binary.");
  }

  return -1;
}
