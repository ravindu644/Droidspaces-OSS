/*
 * Droidspaces v5 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

int internal_boot(struct ds_config *cfg) {
  /* Defensive check: ensure configuration is valid */
  if (!cfg) {
    ds_error("internal_boot received NULL configuration.");
    return -1;
  }

  /* ── NAT child-side handshake ────────────────────────────────────────────
   *
   * This block runs BEFORE any mount operations, while /proc still points to
   * the HOST /proc.  That means /proc/<our_pid>/ns/net is accessible to the
   * monitor and it can move the veth peer into our netns.
   *
   * Protocol:
   *   1. Close pipe ends we don't use.
   *   2. Write "R" (READY) on net_ready_pipe[1] → monitor unblocks.
   *   3. Block on net_done_pipe[0] until monitor finishes veth setup.
   *   4. Call setup_veth_child_side_named() with the received handshake.
   *
   * For DS_NET_NONE: we still do the pipe exchange (monitor sends an empty
   * handshake) so loopback is still configured.  No veth is created.
   * ─────────────────────────────────────────────────────────────────────── */
  if (cfg->net_mode != DS_NET_HOST) {
    ds_log("[NET] Child: net_mode=%d — starting handshake with monitor",
           cfg->net_mode);

    /* We write to net_ready, read from net_done */
    if (cfg->net_ready_pipe[0] >= 0)
      close(cfg->net_ready_pipe[0]);
    if (cfg->net_done_pipe[1] >= 0)
      close(cfg->net_done_pipe[1]);

    /* Signal monitor: we are alive and in our new netns */
    char rdy = 'R';
    if (cfg->net_ready_pipe[1] >= 0) {
      if (write(cfg->net_ready_pipe[1], &rdy, 1) < 0)
        ds_warn("[NET] Child: write READY failed: %s", strerror(errno));
      close(cfg->net_ready_pipe[1]);
      cfg->net_ready_pipe[1] = -1;
    }

    /* Wait for monitor to complete host-side setup */
    struct ds_net_handshake hs;
    memset(&hs, 0, sizeof(hs));
    if (cfg->net_done_pipe[0] >= 0) {
      ssize_t nr = read(cfg->net_done_pipe[0], &hs, sizeof(hs));
      close(cfg->net_done_pipe[0]);
      cfg->net_done_pipe[0] = -1;
      if (nr != (ssize_t)sizeof(hs)) {
        ds_warn("[NET] Child: incomplete handshake (read %zd, expected %zu)",
                nr, sizeof(hs));
      } else {
        ds_log("[NET] Child: handshake received: peer=%s ip=%s", hs.peer_name,
               hs.ip_str);
      }
    }

    /* Configure our side of the veth (or just loopback for DS_NET_NONE) */
    if (cfg->net_mode == DS_NET_NAT) {
      setup_veth_child_side_named(cfg, hs.peer_name, hs.ip_str);
    } else {
      /* DS_NET_NONE: just bring up loopback */
      ds_nl_ctx_t *nlctx = ds_nl_open();
      if (nlctx) {
        ds_nl_link_up(nlctx, "lo");
        ds_nl_close(nlctx);
      }
    }

    ds_log("[NET] Child: handshake complete");
  }

  /* 0. Boot Guard: Ensure name is present and unique.
   * This is a critical security check to prevent anonymous or conflicting
   * containers from booting, even if the CLI checks were bypassed. */
  if (!cfg->container_name[0]) {
    ds_error("CRITICAL: Boot aborted — container name is empty.");
    return -1;
  }

  pid_t existing_pid = 0;
  if (is_container_running(cfg, &existing_pid)) {
    /* If we find ourselves in the pidfile, it's not a conflict, it's just us
     * being tracked early (which is fine). */
    if (existing_pid != getpid()) {
      ds_error(
          "CRITICAL: Boot aborted — name '%s' is already in use by PID %d.",
          cfg->container_name, existing_pid);
      return -1;
    }
  }

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

  /* Detect init system once — used for seccomp and cgroup setup */
  int is_systemd = is_systemd_rootfs(cfg->rootfs_path);

  /* Apply Android compatibility Seccomp filter to child processes.
   * On legacy kernels, this neutralizes broken sandboxing logic in systemd
   * that triggers VFS deadlocks in grab_super(). */
  if (is_android()) {
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

  /* 7. Pre-create standard directories in one loop to reduce syscalls */
  const char *dirs_to_create[] = {".old_root", "proc", "sys", "run", "tmp"};
  int dir_creation_failed = 0;
  for (size_t i = 0; i < sizeof(dirs_to_create) / sizeof(dirs_to_create[0]);
       i++) {
    if (mkdir(dirs_to_create[i], 0755) < 0 && errno != EEXIST) {
      ds_error("Failed to create '%s': %s", dirs_to_create[i], strerror(errno));
      /* .old_root is critical for pivot_root, track if it fails */
      if (strcmp(dirs_to_create[i], ".old_root") == 0) {
        dir_creation_failed = 1;
      }
    }
  }
  if (dir_creation_failed) {
    ds_error("Failed to create critical directory .old_root");
    return -1;
  }

  /* 8. Setup /dev (device nodes, devtmpfs) */
  if (setup_dev(".", cfg->hw_access) < 0) {
    ds_error("Failed to setup /dev.");
    return -1;
  }

  /* 9. Scan host GPU device GIDs (BEFORE pivot_root — need host /dev) */
  gid_t gpu_gids[DS_MAX_GPU_GROUPS];
  int gpu_gid_count = 0;
  if (!cfg->reboot_cycle) {
    if (cfg->hw_access) {
      ds_log("Setting up hardware access...");
      gpu_gid_count = scan_host_gpu_gids(gpu_gids, DS_MAX_GPU_GROUPS);
    } else {
      ds_log("Hardware access disabled: using isolated tmpfs...");
    }
  } else if (cfg->hw_access) {
    gpu_gid_count = scan_host_gpu_gids(gpu_gids, DS_MAX_GPU_GROUPS);
  }

  /* 10. Mount virtual filesystems (proc, sys) */
  if (domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount procfs: %s", strerror(errno));
    return -1;
  }

  /* 10b. Optional: Mount binfmt_misc if supported by the kernel (Android only).
   * This facilitates foreign architecture execution (e.g. via QEMU). */
  if (is_android() && is_binfmt_misc_supported()) {
    mkdir_p("proc/sys/fs/binfmt_misc", 0755);
    if (domount("binfmt_misc", "proc/sys/fs/binfmt_misc", "binfmt_misc", 0,
                NULL) < 0) {
      /* This is non-fatal; may fail if already mounted or restricted. */
      if (errno != EBUSY)
        ds_warn("Failed to mount binfmt_misc: %s", strerror(errno));
    }
  }

  /* Mount /sys */
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
  if (setup_cgroups(is_systemd, cfg->force_cgroupv1) < 0) {
    ds_error("Failed to setup container cgroups.");
    return -1;
  }

  /* 12. Mask the console discovery file to prevent resolution back to host */
  if (mount("/dev/null", "sys/class/tty/console/active", NULL, MS_BIND, NULL) <
      0) {
    /* File might not exist yet if sysfs is partially populated */
  }

  if (domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0) {
    ds_error("Failed to mount tmpfs at /run: %s", strerror(errno));
    return -1;
  }

  /* 12b. Setup /tmp */
  if (!is_android()) {
    /* Desktop Linux: mount fresh tmpfs for isolation */
    if (domount("tmpfs", "tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") <
        0) {
      ds_warn("Failed to mount tmpfs at /tmp: %s", strerror(errno));
    }
  } else {
    /* Android: /tmp will be handled by unified bridge after pivot_root */
    mkdir_p("tmp", 01777);
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

  /* 14. Write identity markers for PID discovery */
  mkdir("run/droidspaces", 0755);
  char marker[PATH_MAX];
  snprintf(marker, sizeof(marker), "run/droidspaces/%s", cfg->uuid);
  write_file(marker, ""); /* empty UUID marker */

  /* Save a normalized copy of the config inside /run for metadata recovery.
   * ds_config_save() resolves rootfs_path to absolute via realpath(),
   * preventing broken relative paths in the internal backup. */
  if (ds_config_save("run/droidspaces/container.config", cfg) < 0) {
    ds_warn("Boot: Failed to save internal configuration backup");
  }

  write_file("run/droidspaces/name", cfg->container_name);

  /* Save mount path for recovery — survives host-side .mount sidecar deletion
   */
  if (cfg->img_mount_point[0])
    write_file("run/droidspaces/mount", cfg->img_mount_point);

  /* Legacy compatibility: write version to the marker directory root */
  write_file("run/droidspaces/version", DS_VERSION);

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

  /* 20. Setup GPU groups and X11 socket (AFTER pivot_root) */
  setup_hardware_access(cfg, gpu_gids, gpu_gid_count);

  /* Log bind mounts and boot (after hw-access logs for clean ordering) */
  if (!cfg->reboot_cycle) {
    if (cfg->bind_count > 0)
      ds_log("Setting up %d custom bind mount(s)...", cfg->bind_count);
    ds_log("Booting '%s' (init: /sbin/init)...", cfg->container_name);
  }
  if (cfg->foreground) {
    printf(C_BOLD C_WHITE "\r\n(to exit from the foreground mode, press "
                          "CTRL+ALT+Q)\r\n" C_RESET);
    fflush(stdout);
  }
  printf("\r\n");
  fflush(stdout);

  /* 21. Cleanup .old_root */
  if (umount2("/.old_root", MNT_DETACH) < 0)
    ds_warn("Failed to unmount .old_root: %s", strerror(errno));
  else
    rmdir("/.old_root");

  /* 22. Set container identity for systemd/openrc */
  write_file(DS_SYSTEMD_CONTAINER_MARKER, "droidspaces");

  /* 23. Clear environment and set container defaults */
  ds_env_boot_setup(cfg);
  ds_env_save("/run/droidspaces.env", cfg);

  /* 23b. Integration with /etc/profile.d for universal sourcing */
  if (access("/etc/profile.d", F_OK) == 0) {
    const char *profile_link = "/etc/profile.d/droidspaces_env.sh";
    /* Always recreate — avoids TOCTOU and fixes stale symlinks after rootfs
     * swap */
    unlink(profile_link);
    if (symlink("/run/droidspaces.env", profile_link) < 0 && errno != EEXIST) {
      ds_warn("Failed to create profile.d symlink: %s", strerror(errno));
    }
  }

  /* 24. Redirect standard I/O to /dev/console */
  int console_fd = open("/dev/console", O_RDWR);
  if (console_fd >= 0) {
    if (ds_terminal_set_stdfds(console_fd) < 0) {
      ds_warn("Failed to redirect stdio to /dev/console");
      close(console_fd);
    } else {
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

      /* Sticky permissions again just in case systemd's TTYReset stripped them
       */
      fchmod(console_fd, 0620);
      if (fchown(console_fd, 0, DS_DEFAULT_TTY_GID) < 0) {
        ds_warn("Failed to chown console: %s", strerror(errno));
      }
      if (console_fd > 2)
        close(console_fd);
    }
  }

  /* 25. EXEC INIT */
  char *init_args[16];
  int argc = 0;
  init_args[argc++] = (char *)"/sbin/init";

  /* Tell systemd which cgroup hierarchy the container was actually set up
   * with.  We use statfs() on /sys/fs/cgroup (now the container root after
   * pivot_root) rather than guessing from kernel version.  setup_cgroups()
   * already decided the layout — we just reflect what it mounted:
   *   cgroup2fs  → unified (v2 only)  → unified_cgroup_hierarchy=1
   *   tmpfs      → legacy / hybrid    → unified_cgroup_hierarchy=0
   * This is exactly what LXC does via lxc.init.cmd. */
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif
  if (is_systemd) {
    struct statfs _cgsfs;
    if (statfs("/sys/fs/cgroup", &_cgsfs) == 0) {
      if ((unsigned long)_cgsfs.f_type == (unsigned long)CGROUP2_SUPER_MAGIC) {
        init_args[argc++] = (char *)"systemd.unified_cgroup_hierarchy=1";
      } else {
        /* tmpfs root → legacy or hybrid layout mounted by setup_cgroups */
        init_args[argc++] = (char *)"systemd.unified_cgroup_hierarchy=0";
        init_args[argc++] =
            (char *)"systemd.legacy_systemd_cgroup_controller=1";
      }
    }
    /* statfs failure → leave systemd to probe on its own */
  }

  init_args[argc] = NULL;

  if (execve("/sbin/init", init_args, environ) < 0) {
    ds_error("Failed to execute /sbin/init: %s", strerror(errno));
    ds_die("Container boot failed. Please ensure the rootfs path is correct "
           "and contains a valid /sbin/init binary.");
  }

  return -1;
}
