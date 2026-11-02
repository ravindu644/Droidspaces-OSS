/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "container/internal.h"

/* True once the container's init has exec'd the real init, false if it died
 * first or hung.
 *
 * fd is the read end of the sync pipe. Init holds the only write end left, and
 * it is close-on-exec, so the kernel closes it at the exact moment init stops
 * running our code. Nothing here guesses at how far the boot has got: every
 * line internal_boot() logs is out before this returns, and no later change to
 * the order of the boot steps can break that.
 *
 * End of file is also what a dead init looks like. Its executable settles it:
 * still ours means it never exec'd, gone means it died. The timeout only guards
 * against a boot that hangs, a healthy one never gets near it. */
static int wait_for_boot(int fd, pid_t pid) {
  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  int r;
  while ((r = poll(&pfd, 1, 30000)) < 0 && errno == EINTR)
    ;
  if (r <= 0)
    return 0;

  char path[PATH_MAX];
  struct stat self, init;
  snprintf(path, sizeof(path), "/proc/%d/exe", pid);
  if (stat("/proc/self/exe", &self) < 0 || stat(path, &init) < 0)
    return 0;
  return self.st_dev != init.st_dev || self.st_ino != init.st_ino;
}

/* The body of a start. The caller holds the lifecycle lock in *lock_fd.
 * reuse_mount is set by restart, whose stop left the image mounted. */
int start_rootfs_locked(struct ds_config *cfg, int *lock_fd, int reuse_mount) {

  int has_side_effects = 0;
  int sync_pipe[2] = {-1, -1}; /* read by cleanup: set before any goto */
  pid_t tweaks_pid = -1;       /* the android_optimizations(1) helper */

  /* Console fds, filled in once init sends its pty up. Every caller hands us
   * a zeroed cfg, and 0 is stdin, which cleanup must never close. */
  cfg->console.master = cfg->console.slave = -1;
  cfg->console_sock[0] = cfg->console_sock[1] = -1;
  cfg->console_fwd[0] = cfg->console_fwd[1] = -1;

  /* Here and not with the other network checks in main.c: a restart reloads
   * its config after those ran, and this is the one both paths go through. */
  if (cfg->net_mode == DS_NET_MACVLAN && ds_net_macvlan_check(cfg) < 0)
    return -1;

  /* 0. Restart: pick the preserved mount back up. If it is gone after all,
   *    this is an ordinary start that mounts the image again. */
  if (reuse_mount) {
    /* Only an image container may adopt a mount under its name: a directory
     * one that inherits a leftover image mount would boot the wrong rootfs. */
    char existing_mount[PATH_MAX];
    if (cfg->rootfs_img_path[0] &&
        ds_img_mount_point(cfg->container_name, existing_mount,
                           sizeof(existing_mount)) == 0 &&
        is_mountpoint(existing_mount)) {
      safe_strncpy(cfg->rootfs_path, existing_mount, sizeof(cfg->rootfs_path));
      cfg->is_img_mount = 1;
      safe_strncpy(cfg->img_mount_point, cfg->rootfs_path,
                   sizeof(cfg->img_mount_point));
    } else {
      reuse_mount = 0;
    }
  }

  /* 1. Logo & Uniqueness Check */
  check_kernel_recommendation();

  /* 1b. Name Uniqueness Check
   * We no longer auto-generate or increment names. The name must be provided
   * by the user and it must be unique. We hold the lifecycle lock, so nobody
   * can start the same name between this check and our pidfile write. */
  {
    pid_t existing_pid = 0;
    if (is_container_running(cfg, &existing_pid)) {
      ds_error("Container name '%s' is already in use by PID %d.",
               cfg->container_name, existing_pid);
      goto cleanup;
    }
  }

  /* 2. Preparation */
  ensure_workspace();

  /* 0a. Resolve any symlinks in rootfs paths to canonical absolute paths.
   *     This prevents symlink-based attacks and ensures that all subsequent
   *     operations use the intended location. */
  if (cfg->rootfs_path[0]) {
    char *abs_path = ds_resolve_path_arg(cfg->rootfs_path);
    if (!abs_path || access(abs_path, F_OK) != 0) {
      ds_error("Failed to resolve rootfs path '%s': %s",
               abs_path ? abs_path : cfg->rootfs_path, strerror(errno));
      free(abs_path);
      goto cleanup;
    }
    safe_strncpy(cfg->rootfs_path, abs_path, sizeof(cfg->rootfs_path));
    free(abs_path);
  }
  if (cfg->rootfs_img_path[0]) {
    char *abs_path = ds_resolve_path_arg(cfg->rootfs_img_path);
    if (!abs_path || access(abs_path, F_OK) != 0) {
      ds_error("Failed to resolve rootfs image path '%s': %s",
               abs_path ? abs_path : cfg->rootfs_img_path, strerror(errno));
      free(abs_path);
      goto cleanup;
    }
    safe_strncpy(cfg->rootfs_img_path, abs_path, sizeof(cfg->rootfs_img_path));
    free(abs_path);
  }

  /* if foreground was requested but we have no interactive terminal (piped,
   * scripted, config foreground=1, etc.), flip the switch once here and warn
   * once. Covers both CLI and daemon paths. */
  if (cfg->foreground && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
    cfg->foreground = 0;
    ds_warn("No interactive terminal - foreground mode disabled, running in "
            "background.");
  }

  print_cgroup_status(cfg);

  /* If the user requested permissive mode, ensure it's applied.
   * ds_set_selinux_permissive() is a no-op if host is already permissive. */
  if (cfg->selinux_permissive) {
    ds_set_selinux_permissive(1);
  }

  if (cfg->android_storage && !is_android())
    ds_warn("--enable-android-storage is only supported on Android hosts. "
            "Skipping.");
  if (cfg->tx11_extra_flags && !is_android())
    ds_warn("--tx11-flags is only applicable on Android. Skipping.");
  if (cfg->virgl && !is_android())
    ds_warn("--virgl is only applicable on Android. Skipping.");
  if (cfg->virgl_extra_flags && !is_android())
    ds_warn("--virgl-flags is only applicable on Android. Skipping.");
  if (cfg->pulseaudio && !is_android())
    ds_warn("--pulse-audio is only applicable on Android. Skipping.");
  if (cfg->anland && !is_android())
    ds_warn("--anland is only applicable on Android. Skipping.");

  /* If no hostname specified, default to container name */
  if (cfg->hostname[0] == '\0') {
    safe_strncpy(cfg->hostname, cfg->container_name, sizeof(cfg->hostname));
  }

  has_side_effects = 1;

  /* 2. Mount rootfs image if provided (using the resolved name) */
  if (cfg->rootfs_img_path[0] && !reuse_mount) {
    if (mount_rootfs_img(cfg->rootfs_img_path, cfg->rootfs_path,
                         sizeof(cfg->rootfs_path), cfg->container_name) < 0) {
      goto cleanup;
    }
    cfg->is_img_mount = 1;
    safe_strncpy(cfg->img_mount_point, cfg->rootfs_path,
                 sizeof(cfg->img_mount_point));
  }

  /* 2a. Verify init binary exists before any side effects (NAT, config save).
   * For rootfs.img mode the image is now mounted; for directory mode the
   * rootfs_path is already set.  Either way we have a valid host path. */
  {
    char init_path[PATH_MAX * 2];
    char rootfs_norm[PATH_MAX];
    if (cfg->is_img_mount && cfg->img_mount_point[0])
      safe_strncpy(rootfs_norm, cfg->img_mount_point, sizeof(rootfs_norm));
    else
      safe_strncpy(rootfs_norm, cfg->rootfs_path, sizeof(rootfs_norm));
    size_t rlen = strlen(rootfs_norm);
    if (rlen > 0 && rootfs_norm[rlen - 1] == '/')
      rootfs_norm[rlen - 1] = '\0';

    const char *init_bin =
        cfg->custom_init[0] ? cfg->custom_init : DS_DEFAULT_INIT;
    snprintf(init_path, sizeof(init_path), "%.*s%s",
             (int)(sizeof(init_path) - strlen(init_bin) - 1), rootfs_norm,
             init_bin);
    struct stat st;
    if (lstat(init_path, &st) != 0) {
      ds_error("Init binary not found: %s", init_path);
      ds_error("Please ensure the rootfs path is correct and contains %s.",
               init_bin);
      if (cfg->is_img_mount)
        unmount_rootfs_img(cfg->img_mount_point, cfg->foreground);
      return -1;
    }
    /* Absolute symlinks resolve correctly inside the container after
     * pivot_root, so skip the X_OK check for symlinks. */
    if (!S_ISLNK(st.st_mode) && access(init_path, X_OK) != 0) {
      ds_error("Init binary is not executable: %s", init_path);
      ds_error("Ensure it has executable permissions.");
      if (cfg->is_img_mount)
        unmount_rootfs_img(cfg->img_mount_point, cfg->foreground);
      return -1;
    }

    /* Classify the container init family while the normalized host rootfs
     * path is already in scope. Detecting here avoids rebuilding the same
     * probe path later solely for shutdown metadata. */
    cfg->init_type = detect_container_init(rootfs_norm);
  }

  /* 2b. Android helper servers, before fork so their sockets exist when
   * they are bind-mounted post-pivot. */
  ds_android_helpers_sync(cfg);

  /* 3. Early pre-flight for volatile mode (before any host changes) */
  if (check_volatile_mode(cfg) < 0) {
    goto cleanup;
  }

  /* A container gets its UUID once, here, on its first start, and keeps it.
   * Nothing on the host finds a container by it any more: the uniqueness
   * check above asks the pidfile and the container's monitor. */
  if (cfg->uuid[0] == '\0')
    generate_uuid(cfg->uuid, sizeof(cfg->uuid));

  /* Resolve and lock in the container's static NAT IP before the first save.
   *
   * Rules (enforced inside ds_net_resolve_static_ip):
   *   1. If --nat-ip was given and passes validation + uniqueness -> keep it.
   *   2. If --nat-ip was given but fails either check -> warn + auto-assign.
   *   3. If static_nat_ip is already in config (previous boot) -> reuse it
   *      (uniqueness check skips self, so restarts are always idempotent).
   *   4. If none of the above -> derive from djb2(container_name), walk
   *      forward until a free slot is found.
   *
   * Doing this here (pre-save, pre-fork) means:
   *   - The IP is written to disk on the very first boot, even if the user
   *     never passed --nat-ip. Every subsequent boot loads it from config.
   *   - The monitor process inherits the fully resolved cfg struct so
   *     setup_veth_host_side() and the DHCP server see the same IP without
   *     any IPC needed.
   *
   * Only relevant for NAT mode -- host/none modes skip this cleanly. */
  if (cfg->net_mode == DS_NET_NAT)
    ds_net_resolve_static_ip(cfg);

  /* Persist UUID and resolved static_nat_ip (for NAT) to config immediately
   * so disk always matches the running container. CLI overrides (e.g. -f)
   * are already in cfg at this point since start_rootfs() is called after
   * argument parsing. */
  if (cfg->config_file[0]) {
    int was_new = !cfg->config_file_existed;
    if (ds_config_save(cfg->config_file, cfg) < 0) {
      ds_error("Failed to persist configuration to '%s': %s", cfg->config_file,
               strerror(errno));
      goto cleanup;
    }
    if (was_new) {
      ds_log("Configuration persisted to " C_BOLD "%s" C_RESET,
             cfg->config_file);
    }
  }

  /* Mirror to workspace so 'start -n <n>' works later without --conf */
  if (ds_config_save_by_name(cfg->container_name, cfg) < 0) {
    ds_warn("Failed to mirror configuration to workspace for '%s': %s",
            cfg->container_name, strerror(errno));
  }

  /* Parse environment file while host paths are reachable (before pivot_root)
   */
  parse_env_file_to_config(cfg);

  /* Pre-populate volatile_dir for monitor cleanup (actual overlay setup
   * happens inside internal_boot's isolated mount namespace) */
  if (cfg->volatile_mode) {
    snprintf(cfg->volatile_dir, sizeof(cfg->volatile_dir),
             "%s/" DS_VOLATILE_SUBDIR "/%s", get_workspace_dir(),
             cfg->container_name);
  }

  /* 4. Parent-side PTY allocation (LXC Model) */

  /* Firmware path - hw_access mode only.
   * By this point cfg->rootfs_path is fully resolved and the
   * image is mounted if applicable.  firmware_path_add() internally checks
   * that /lib/firmware exists in the rootfs before touching the sysfs node. */
  if (cfg->hw_access) {
    char fw_path[PATH_MAX + 16];
    snprintf(fw_path, sizeof(fw_path), "%s/lib/firmware", cfg->rootfs_path);
    firmware_path_add(fw_path);
  }

  /* 5. Resolve the PID file path early so the monitor inherits it */
  resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                            sizeof(cfg->pidfile));

  /* Three binder round trips into system_server. They used to run after the
   * host-side network setup, in series, with the whole start waiting on them.
   * Nothing between here and init's exec needs them, so a helper applies them
   * while the container boots and is reaped after wait_for_boot(), so they
   * are still in place by the time start returns. The guest may fork its
   * first services before the phantom process cap is lifted; the cap is
   * applied by a periodic trim of the app's cgroup, which the container's
   * processes have left by then, not at fork. Forked before the sync pipe
   * exists: holding its write end would hold back the EOF that marks init's
   * exec. */
  if (is_android()) {
    tweaks_pid = fork();
    if (tweaks_pid == 0) {
      android_optimizations(1);
      _exit(0);
    }
    if (tweaks_pid < 0)
      android_optimizations(1);
  }

  /* 6. Pipe for synchronization */
  if (pipe(sync_pipe) < 0) {
    ds_error("pipe failed: %s", strerror(errno));
    goto cleanup;
  }

  /* Set FD_CLOEXEC on both ends of sync_pipe */
  fcntl(sync_pipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(sync_pipe[1], F_SETFD, FD_CLOEXEC);

  /* 7. Configure host-side networking (NAT, ip_forward, DNS) BEFORE fork.
   * This eliminates the race condition where the child boots and reads
   * DNS before the parent has written it. */
  fix_networking_host(cfg);

  /* Record start time before fork so monitor and virtualize_update share it */
  clock_gettime(CLOCK_BOOTTIME, &cfg->start_time);

  /* The console is allocated by init from the container's own devpts, so the
   * host's /dev/pts never carries it, and comes back up this socketpair. */
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, cfg->console_sock) <
          0 ||
      (cfg->foreground && socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
                                     cfg->console_fwd) < 0)) {
    ds_error("socketpair failed: %s", strerror(errno));
    goto cleanup;
  }

  /* 8. Fork Monitor Process */
  pid_t monitor_pid = fork();
  if (monitor_pid < 0) {
    close(sync_pipe[0]);
    close(sync_pipe[1]);
    ds_error("fork failed: %s", strerror(errno));
    goto cleanup;
  }

  if (monitor_pid == 0) {
    /* The inherited copy of our lifecycle lock was closed by the fork handler
     * in the lock code: the monitor takes the lock itself when it needs it. */
    close(sync_pipe[0]);
    ds_monitor_run(cfg, sync_pipe[1]);
    /* ds_monitor_run never returns */
    _exit(EXIT_FAILURE);
  }

  /* PARENT PROCESS */
  close(sync_pipe[1]);
  close(cfg->console_sock[0]);
  close(cfg->console_sock[1]);
  cfg->console_sock[0] = cfg->console_sock[1] = -1;
  if (cfg->foreground) {
    close(cfg->console_fwd[1]);
    cfg->console_fwd[1] = -1;
  }

  /* Wait for Monitor to send child PID */
  if (read(sync_pipe[0], &cfg->container_pid, sizeof(pid_t)) != sizeof(pid_t)) {
    ds_error("Monitor failed to send container PID.");
    goto cleanup;
  }
  /* The read end stays open: wait_for_boot() watches it for init's exec. */

  ds_log("Container started with PID %d (Monitor: %d)", cfg->container_pid,
         monitor_pid);

  /* 9. Android: Remount /data with suid for directory-based containers.
   * This is required for sudo/su to work if the rootfs is on /data.
   * Skip on ramfs (recovery) as it's unnecessary and likely to fail. */
  if (is_android() && !cfg->rootfs_img_path[0] && !is_ramfs("/"))
    android_remount_data_suid();

  /* Log volatile mode */
  if (cfg->volatile_mode)
    ds_log("Entering volatile mode (OverlayFS)...");

  /* 10. Save PID file */
  char pid_str[32];
  snprintf(pid_str, sizeof(pid_str), "%d", cfg->container_pid);

  if (write_file_atomic(cfg->pidfile, pid_str) < 0)
    ds_error("Failed to write PID file: %s", cfg->pidfile);

  /* 11. Do not show the info or return until init has exec'd. A command that
   * returned earlier left init still logging to a terminal nobody was reading
   * any more, and its last lines, "Booting ..." among them, went missing. */
  int booted = wait_for_boot(sync_pipe[0], cfg->container_pid);
  close(sync_pipe[0]);
  sync_pipe[0] = -1;
  while (tweaks_pid > 0 && waitpid(tweaks_pid, NULL, 0) < 0 && errno == EINTR)
    ;
  tweaks_pid = -1;

  if (cfg->foreground) {
    /* We stay attached for the container's whole life, so let go of the lock
     * here, but not before the container is visible as running: a start that
     * raced in before that would not see it and would boot a second one. */
    ds_container_unlock(*lock_fd);
    *lock_fd = -1;

    int ret = console_monitor_loop(cfg->console_fwd[0], monitor_pid, cfg);
    close(cfg->console_fwd[0]);
    cfg->console_fwd[0] = -1;
    free_config_env_vars(cfg);
    return ret;
  } else {
    if (!booted) {
      ds_error("Container failed to boot correctly.");
      /* If pid is still alive, we might want to kill it, but monitor usually
       * handles this. Let's just return error so parent doesn't report
       * success.  Teardown (including ds_config_free) is owned by the single
       * cleanup: block below - freeing here would leave cleanup reading a
       * freed cfg and then double-free it. */
      goto cleanup;
    }

    show_info(cfg, 1);
    ds_log("Container '%s' is running in background.", cfg->container_name);
    if (is_android()) {
      ds_log("Use 'su -c \"%s --name='%s' enter\"' to connect.", cfg->prog_name,
             cfg->container_name);
    } else {
      ds_log("Use 'sudo %s --name='%s' enter' to connect.", cfg->prog_name,
             cfg->container_name);
    }
  }

  ds_config_free(cfg);

  return 0;

cleanup:
  /* The helper must be done before cleanup's android_optimizations(0), or its
   * last command would land after the reset. */
  while (tweaks_pid > 0 && waitpid(tweaks_pid, NULL, 0) < 0 && errno == EINTR)
    ;

  /* Centralized host-side cleanup IF we are returning error.
   * This ensures image mounts and tracking files are reverted on fatal boot
   * errors. Only execute if we successfully crossed the point of creating
   * effects. */
  if (has_side_effects) {
    cleanup_container_resources(cfg, cfg->container_pid, 0, 1 /* force */);
  }
  for (int i = 0; i < 2; i++) {
    if (cfg->console_sock[i] >= 0)
      close(cfg->console_sock[i]);
    if (cfg->console_fwd[i] >= 0)
      close(cfg->console_fwd[i]);
    cfg->console_sock[i] = cfg->console_fwd[i] = -1;
  }
  if (sync_pipe[0] >= 0)
    close(sync_pipe[0]);
  if (sync_pipe[1] >= 0)
    close(sync_pipe[1]);

  ds_config_free(cfg);
  return -1;
}

int start_rootfs(struct ds_config *cfg) {
  /* Kept on the stack: start_rootfs_locked() frees cfg on success. */
  int lock_fd = lifecycle_lock_or_complain(cfg, "start");
  if (lock_fd < 0)
    return -1;

  int ret = start_rootfs_locked(cfg, &lock_fd, 0);
  ds_container_unlock(lock_fd);
  return ret;
}
