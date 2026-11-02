/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* A child user namespace may mount a fresh proc or sysfs only if some
 * instance of it in this mount namespace is "fully visible": the root of the
 * filesystem, no locked child mount covering a non-empty file, and not locked
 * read-only when the new mount is read-write (mnt_already_visible in
 * fs/namespace.c).  Every jail mask and every vproc bind disqualifies /proc
 * and /sys, so runc and bwrap fail with EPERM.  The kernel accepts any
 * instance anywhere, so give it one under /run/droidspaces, the same trick
 * as LXC's nesting.conf.
 *
 * Rules that must hold or the check fails again: never bind anything on top
 * of these two; no MS_NOATIME or MS_STRICTATIME, the atime mode must equal
 * what runc asks for (relatime); proc stays read-write because runc mounts
 * /proc rw.  sysfs can be read-only, Docker and Podman mount /sys ro and
 * bwrap does not mount it at all.
 *
 * The proc instance is unmasked, so container root can reach the host
 * sysctls through /run/droidspaces/proc.  That is the price of
 * --allow-sandboxing and the help text says so. */
static void mount_pristine_proc_sys(void) {
  mkdir("run/droidspaces/proc", 0755);
  mkdir("run/droidspaces/sys", 0755);
  if (domount("proc", "run/droidspaces/proc", "proc",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0)
    ds_warn("[SEC] pristine proc for sandboxing failed: %s", strerror(errno));
  if (domount("sysfs", "run/droidspaces/sys", "sysfs",
              MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0)
    ds_warn("[SEC] pristine sysfs for sandboxing failed: %s", strerror(errno));
}

int internal_boot(struct ds_config *cfg) {
  /* Defensive check: ensure configuration is valid */
  if (!cfg) {
    ds_error("internal_boot received NULL configuration.");
    return -1;
  }

  android_native_personality();
  ds_oom_unprotect();

  /* Pre-open the container log file before namespace isolation / pivot_root.
   * The FD survives mount namespace changes, ensuring all post-pivot logs
   * (X11 bridge, bind mounts, init exec) are captured in the host log. */
  ds_open_container_log(cfg);

  /* NAT child-side handshake
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
   */
  if (cfg->net_mode != DS_NET_HOST) {
    ds_log("[NET] Child: net_mode=%d - starting handshake with monitor",
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
      } else if (cfg->net_mode != DS_NET_NAT) {
        ds_log("[NET] Child: handshake received");
      } else {
        ds_log("[NET] Child: handshake received: peer=%s ip=%s", hs.peer_name,
               hs.ip_str);
      }
    }

    /* Configure our side of the veth (or just loopback for DS_NET_NONE) */
    if (cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_GATEWAY) {
      setup_veth_child_side_named(cfg, hs.peer_name, hs.ip_str);
    } else {
      /* none: just loopback. macvlan: the same, its eth0 is created and
       * brought up by the monitor, see ds_net_macvlan_wire() */
      ds_nl_ctx_t *nlctx = ds_nl_open();
      if (nlctx) {
        ds_nl_link_up(nlctx, "lo");
        ds_nl_close(nlctx);
      }
    }

    ds_log("[NET] Child: handshake complete");
  }

  /* 0. Boot Guard: a container needs a name. Whether the name is taken is
   * start's to check, under the lifecycle lock: from inside this new pid
   * namespace no host pid or monitor is visible. */
  if (!cfg->container_name[0]) {
    ds_error("CRITICAL: Boot aborted: container name is empty.");
    goto boot_fail;
  }

  /* 1. Isolated mount namespace */
  if (unshare(CLONE_NEWNS) < 0) {
    ds_error("Failed to unshare mount namespace: %s", strerror(errno));
    goto boot_fail;
  }

  /* 2. Make all mounts private to avoid leaking to host.
   * We ALWAYS start with MS_PRIVATE because MS_SHARED breaks pivot_root/MS_MOVE
   * fallbacks on some kernels (e.g. Android rootfs). We will switch to
   * MS_SHARED after the rootfs relocation if requested. */
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
    ds_error("Failed to make / private: %s", strerror(errno));
    goto boot_fail;
  }

  /* Init family was classified before fork in start_rootfs().
   * Boot-time setup only needs to know whether that family is systemd. */
  int is_systemd = (cfg->init_type == DS_INIT_SYSTEMD);

  /* 3. Setup volatile overlay INSIDE the container's mount namespace.
   * This MUST happen here (not in parent) so the overlay's connection to
   * its lowerdir (e.g. a loop-mounted image) survives mount privatization. */
  if (cfg->volatile_mode) {
    if (setup_volatile_overlay(cfg) < 0) {
      ds_error("Failed to setup volatile overlay.");
      goto boot_fail;
    }
  }

  /* 4. Bind mount rootfs to itself (required for pivot_root) */
  if (mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL) <
      0) {
    ds_error("Failed to bind mount rootfs: %s", strerror(errno));
    goto boot_fail;
  }

  /* 4b. Force the image rootfs read-write - the single unified point covering
   * fresh boot, restart, AND internal reboot.
   *
   * On shutdown/reboot OpenWrt remounts its root read-only, leaving our mount
   * point under /mnt/Droidspaces/<name> read-only too.  External and internal
   * reboots never unmount the rootfs.img (to keep restarts fast), so the next
   * boot reuses that still-read-only mount and pivot_root fails to create
   * .old_root ("Read-only file system") - the container never boots.
   *
   * Skipped for directory rootfs (a fresh self-bind in this new namespace, its
   * host backing fs already writable) and volatile mode (pivot_root targets the
   * read-write overlay, not the read-only lower image).  Safe: the guest synced
   * the fs before flipping it read-only.  A no-op when already read-write. */
  if (cfg->is_img_mount && !cfg->volatile_mode) {
    if (mount(NULL, cfg->rootfs_path, NULL,
              MS_REMOUNT | MS_NOATIME | MS_NODIRATIME, NULL) < 0)
      ds_warn("Failed to remount rootfs %s read-write: %s", cfg->rootfs_path,
              strerror(errno));
  }

  /* 5. Set working directory to rootfs (required before pivot_root) */
  if (chdir(cfg->rootfs_path) < 0) {
    ds_error("Failed to chdir to '%s': %s", cfg->rootfs_path, strerror(errno));
    goto boot_fail;
  }

  /* 6. Older builds handed the uuid across pivot_root in this file. cfg
   * carries it in memory, so only the leftover goes. */
  unlink(".droidspaces-uuid");

  /* 7. pivot_root parks the old root in .old_root, so that one must exist;
   * the rest are mountpoints a rootfs normally ships anyway. */
  if (mkdir(".old_root", 0755) < 0 && errno != EEXIST) {
    ds_error("Failed to create .old_root: %s", strerror(errno));
    goto boot_fail;
  }
  const char *dirs[] = {"proc", "sys", "run", "tmp"};
  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    if (mkdir(dirs[i], 0755) < 0 && errno != EEXIST)
      ds_error("Failed to create '%s': %s", dirs[i], strerror(errno));

  /* 8. Setup /dev (device nodes, devtmpfs) */
  if (setup_dev(".", cfg->hw_access, cfg->gpu_mode, cfg->allow_vts) < 0) {
    ds_error("Failed to setup /dev.");
    goto boot_fail;
  }

  /* 9. Log hardware access mode (BEFORE pivot_root) */
  if (!cfg->reboot_cycle) {
    if (cfg->hw_access)
      ds_log("Setting up hardware access...");
    else if (cfg->gpu_mode)
      ds_log("Setting up GPU-only access...");
    else
      ds_log("Hardware access disabled: using isolated tmpfs...");
  }

  /* 10. Mount virtual filesystems (proc, sys) */
  if (domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount procfs: %s", strerror(errno));
    goto boot_fail;
  }

  /* Mount /sys */
  if (domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount sysfs: %s", strerror(errno));
    goto boot_fail;
  }

  /* The far end of our veth gets RPS too, for traffic the host forwards in
   * from several CPUs at once (a Wi-Fi or modem driver spreading its receive
   * work). This sysfs belongs to our netns and is still writable. Only veth
   * modes: a macvlan frame is steered on the parent NIC, and a host netns
   * container would be writing to the phone's real interfaces. The LAN eth1
   * we add to a running gateway later is skipped, and needs nothing: all of
   * it comes from a client's host end, already steered. A cable some other
   * tool plugs in (VirtualAP's vaplan0) has to set its own. */
  if (cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_GATEWAY) {
    DIR *nd = opendir("sys/class/net");
    if (nd) {
      struct dirent *de;
      while ((de = readdir(nd)) != NULL)
        if (de->d_name[0] != '.' && strcmp(de->d_name, "lo") != 0)
          ds_net_steer_rx("sys", de->d_name);
      closedir(nd);
    }
  }

  /* 10. Pre-create the cgroup mountpoint while /sys is still RW.
   * This allows us to mount cgroups onto it later even after /sys is RO. */
  mkdir_p("sys/fs/cgroup", 0755);

  if (cfg->hw_access && cfg->foreground && is_systemd) {
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
  } else if (!cfg->hw_access) {
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

    /* Fix: Instead of mounting a fresh sysfs (which creates a recursive tree),
     * we bind-mount the existing net devices path from our own sysfs mount.
     * This keeps the symlink at /sys/class/net/eth0 valid while pinning the
     * path as an independent mount point that can survive isolation and
     * provide RW access if needed. */
    if (mount("sys/devices/virtual/net", "sys/devices/virtual/net", NULL,
              MS_BIND | MS_REC, NULL) < 0) {
      ds_warn("Failed to bind-mount network devices in isolated /sys "
              "(networking may be limited)");
    }
  }

  /* Remount /sys as RO for systemd's benefit, but ONLY if we are in
   * foreground mode + systemd (where we used pinned sub-mounts) or if
   * hw_access is disabled entirely. In background mode or non-systemd
   * hw_access mode, we leave /sys RW. */
  if (!cfg->hw_access || (cfg->foreground && is_systemd)) {
    if (mount(NULL, "sys", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0) {
      ds_warn("Failed to remount /sys as read-only: %s", strerror(errno));
    }
  }

  /* 11. Setup Cgroups AFTER locking down /sys.
   * Mounting onto a directory on a RO parent is allowed for root, and it
   * ensures the sub-mount (tmpfs) is RW and independent of the parent's RO. */
  if (setup_cgroups(is_systemd, cfg->force_cgroupv1) < 0) {
    ds_error("Failed to setup container cgroups.");
    goto boot_fail;
  }

  if (domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0) {
    ds_error("Failed to mount tmpfs at /run: %s", strerror(errno));
    goto boot_fail;
  }

  /* 13. Setup /tmp: always mount a fresh isolated tmpfs.
   * The X11 socket is bind-mounted into it later at /tmp/.X11-unix. */
  if (domount("tmpfs", "tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
    ds_warn("Failed to mount tmpfs at /tmp: %s", strerror(errno));

  /* 15. Android-specific storage */
  if (cfg->android_storage) {
    android_setup_storage(".");
  }

  /* 16. Custom bind mounts */
  if (!cfg->reboot_cycle && cfg->bind_count > 0)
    ds_log("Setting up %d custom bind mount(s)...", cfg->bind_count);
  setup_custom_binds(cfg, ".");

  /* 17. pivot_root with MS_MOVE+chroot fallback for ramfs/rootfs environments
   * (e.g. Android recovery) where pivot_root(2) always returns EINVAL because
   * the kernel refuses to pivot when new_root is on the same underlying fs as
   * the current root (ramfs has no backing device, self-bind doesn't help).
   * MS_MOVE atomically relocates the new root onto / and chroot(2) locks us
   * in - exactly what switch_root(8) does internally. */
  int used_ms_move = 0;
  if (is_ramfs("/")) {
    ds_log("Detected rootfs/ramfs root - automatically falling back to "
           "MS_MOVE+chroot");
    used_ms_move = 1;
    if (mount(".", "/", NULL, MS_MOVE, NULL) < 0) {
      ds_error("MS_MOVE fallback failed: %s", strerror(errno));
      goto boot_fail;
    }
    if (chroot(".") < 0) {
      ds_error("chroot(\".\") after MS_MOVE failed: %s", strerror(errno));
      goto boot_fail;
    }
  } else if (syscall(SYS_pivot_root, ".", ".old_root") < 0) {
    ds_error("pivot_root failed: %s", strerror(errno));
    goto boot_fail;
  }

  if (chdir("/") < 0) {
    ds_error("chdir(\"/\") after pivot_root failed: %s", strerror(errno));
    goto boot_fail;
  }

  /* 17b. Apply deferred mount propagation settings.
   * Switch to MS_SHARED only after relocation is complete. */
  if (cfg->privileged_mask & DS_PRIV_SHARED) {
    if (mount(NULL, "/", NULL, MS_REC | MS_SHARED, NULL) < 0) {
      ds_warn("[SEC] Failed to apply MS_SHARED propagation: %s",
              strerror(errno));
    } else {
      ds_log("[SEC] Root mount propagation set to SHARED.");
    }
  }

  /* 18. Setup devpts (must be after pivot_root for newinstance) */
  setup_devpts(cfg->hw_access);

  /* 18a. The console, from the devpts just mounted: a pty taken from the host
   * sat root-owned in the host's /dev/pts for the container's whole life
   * (#369). /dev/ptmx is the fallback for a kernel without newinstance
   * support, where setup_devpts left nothing of our own to allocate from.
   * Bound by fd, since by name /dev/pts/N could resolve into the wrong
   * instance on the setup_devpts fallbacks. The caller's terminal is still
   * our stdin here, so its size goes on before anything is printed. */
  struct ds_tty_info con = {.master = -1, .slave = -1};
  int console_slave = -1; /* step 24 takes it when the bind below fails */
  if (ds_openpty("/dev/pts/ptmx", &con.master, &con.slave, con.name) < 0 &&
      ds_openpty("/dev/ptmx", &con.master, &con.slave, con.name) < 0) {
    ds_warn("Failed to allocate console PTY: %s", strerror(errno));
  } else {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
      ioctl(con.master, TIOCSWINSZ, &ws);
    fchmod(con.slave, 0620);
    if (fchown(con.slave, 0, DS_DEFAULT_TTY_GID) < 0) {
      /* best-effort */
    }
    char src[64];
    snprintf(src, sizeof(src), "/proc/self/fd/%d", con.slave);
    if (mount(src, "/dev/console", NULL, MS_BIND, NULL) < 0 &&
        mount(con.name, "/dev/console", NULL, MS_BIND, NULL) < 0) {
      /* A pty from the host's instance cannot be bound in here (a cross
       * namespace bind is EINVAL before 5.2), so init takes the fd itself. */
      ds_warn("Failed to bind mount console '%s': %s", con.name,
              strerror(errno));
      console_slave = con.slave;
    }
    if (ds_console_send(cfg->console_sock[1], con.master, con.slave) < 0)
      ds_warn("Failed to hand the console to the monitor: %s", strerror(errno));
    close(con.master);
    if (console_slave < 0)
      close(con.slave);
  }

  /* Apply jail mask after pivot_root for correct path resolution */
  ds_apply_jail_mask(cfg->hw_access, cfg->privileged_mask,
                     cfg->sandboxing_allowed);

  /* 18b. Resource Visibility Virtualization
   * Always runs: uptime/loadavg are fundamental container features.
   * CPU/RAM spoofing is selectively enabled only when cgroup limits are set. */
  if (is_mountpoint("/proc")) {
    if (ds_virtualize_init(cfg) < 0)
      ds_warn(
          "[VIRT] Initialization failed, continuing without virtualization.");
  } else {
    ds_warn("[VIRT] /proc not mounted, skipping virtualization.");
  }

  /* 19. Configure rootfs networking (hostname, resolv.conf, etc) */
  fix_networking_rootfs(cfg);

  /* 20. Setup GPU groups and X11 socket (AFTER pivot_root) */
  setup_hardware_access(cfg);

  if (!cfg->reboot_cycle)
    ds_log("Booting '%s' (init: %s)...", cfg->container_name,
           cfg->custom_init[0] ? cfg->custom_init : DS_DEFAULT_INIT);

  /* 20b. The config this boot uses, for the guest to read: scripts inside
   * check net_mode= or enable_x11= here. It is information only, read-only,
   * and the host never reads it back. Root in here can rewrite it, so what
   * the container booted with comes from its monitor (container/command.c). */
  mkdir("run/droidspaces", 0755);
  if (cfg->sandboxing_allowed)
    mount_pristine_proc_sys();
  if (ds_config_save("run/droidspaces/container.config", cfg) < 0)
    ds_warn("Boot: Failed to save internal configuration backup");
  else
    chmod("run/droidspaces/container.config", 0444);

  /* Legacy compatibility: write version to the marker directory root */
  write_file("run/droidspaces/version", DS_VERSION);
  if (cfg->foreground) {
    printf(C_BOLD C_WHITE "\r\n(to exit from the foreground mode, press "
                          "CTRL+ALT+Q)\r\n" C_RESET);
    fflush(stdout);
  }
  printf("\r\n");
  fflush(stdout);

  /* 21. Cleanup .old_root (skip when MS_MOVE fallback was used - there is no
   * old root mountpoint to detach in that path). */
  if (!used_ms_move) {
    if (umount2("/.old_root", MNT_DETACH) < 0)
      ds_warn("Failed to unmount .old_root: %s", strerror(errno));
    else
      rmdir("/.old_root");
  } else {
    rmdir("/.old_root");
  }

  /* 22. Set container identity for systemd/openrc */
  write_file(DS_SYSTEMD_CONTAINER_MARKER, "droidspaces");

  /* 23. Clear environment and set container defaults */
  ds_env_boot_setup(cfg);
  ds_env_save("/run/droidspaces.env", cfg);

  /* 23b. Integration with /etc/profile.d for universal sourcing */
  if (access("/etc/profile.d", F_OK) == 0) {
    const char *profile_link = "/etc/profile.d/droidspaces_env.sh";
    /* Always recreate - avoids TOCTOU and fixes stale symlinks after rootfs
     * swap */
    unlink(profile_link);
    if (symlink("/run/droidspaces.env", profile_link) < 0 && errno != EEXIST) {
      ds_warn("Failed to create profile.d symlink: %s", strerror(errno));
    }
  }

  /* 23c. Apply security hardening (capabilities)
   * Apply security hardening (capabilities and seccomp)
   * This is done at the very end to ensure all setup tasks that might need
   * privileges (like chown/chmod or mknod) are finished. */
  /* Neutralize the KSU container-escape path BEFORE seccomp is applied: the
   * ioctl is delivered through the [ksu_driver] fd that the magic reboot()
   * installs, and the seccomp filter below denies that very magic reboot.
   * Best-effort, silent no-op on non-KSU kernels. */
  ds_ksu_neutralize_root_escape();
  ds_seccomp_apply_minimal(cfg->privileged_mask, cfg->sandboxing_allowed);
  ds_seccomp_keyring_shim(cfg->privileged_mask);
  ds_seccomp_mount_api_shim(cfg);

  ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask,
                                cfg->sandboxing_allowed);

  /* 24. Redirect standard I/O to /dev/console */
  int console_fd =
      console_slave >= 0 ? console_slave : open("/dev/console", O_RDWR);
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
        /* best-effort, ignore EPERM */
      }
      if (console_fd > 2)
        close(console_fd);
    }
  }

  /* 25. EXEC INIT */
  char *init_bin =
      cfg->custom_init[0] ? cfg->custom_init : (char *)DS_DEFAULT_INIT;
  char *init_args[16];
  int argc = 0;
  init_args[argc++] = init_bin;

  /* Tell systemd which cgroup hierarchy the container was actually set up
   * with.  We use statfs() on /sys/fs/cgroup (now the container root after
   * pivot_root) rather than guessing from kernel version.  setup_cgroups()
   * already decided the layout - we just reflect what it mounted:
   *   cgroup2fs  → unified (v2 only)  → unified_cgroup_hierarchy=1
   *   tmpfs      → legacy / hybrid    → unified_cgroup_hierarchy=0
   * This is exactly what LXC does via lxc.init.cmd. */
  if (is_systemd && !cfg->custom_init[0]) {
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

    /* libmount treats the shim's EPERM as fatal, see
     * ds_seccomp_mount_api_shim. */
    if (ds_mount_api_shim_wanted(cfg))
      init_args[argc++] = (char *)"systemd.setenv=LIBMOUNT_FORCE_MOUNT2=always";
  }

  init_args[argc] = NULL;

  if (execve(init_bin, init_args, environ) < 0) {
    ds_error("Failed to execute %s: %s", init_bin, strerror(errno));
    ds_die("Container boot failed. Please ensure the rootfs path is correct "
           "and contains a valid %s binary.",
           init_bin);
  }

boot_fail:
  ds_close_container_log();
  return -1;
}
