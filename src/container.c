/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * External Command Lock - CLI-only ownership
 *
 * The lock represents exactly ONE thing: an external CLI command is actively
 * managing this container. ONLY the CLI parent creates/removes locks.
 * The monitor is READ-ONLY for locks.
 * ---------------------------------------------------------------------------*/

/* Build lock path with defensive truncation.
 * Precision: 2048 (pids_dir) + 256 (name) + 5 (.lock) = 2309 < PATH_MAX (4096)
 * This prevents format-truncation warnings while ensuring paths never overflow.
 */
static int get_lock_path(const char *name, char *buf, size_t size) {
  if (!name || !buf || size == 0 || !validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));
  int r = snprintf(buf, size, "%.2048s/%.256s" DS_EXT_LOCK, get_pids_dir(),
                   safe_name);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* Create external command lock - ONLY called by CLI parent.
 * Returns: 0 on success, -1 if lock already held by a live process. */
static int acquire_external_lock(const char *name) {
  char lock_path[PATH_MAX];
  if (get_lock_path(name, lock_path, sizeof(lock_path)) < 0)
    return -1;

  /* Check if lock already exists */
  if (access(lock_path, F_OK) == 0) {
    /* Lock exists - verify if holder is still alive */
    char buf[32];
    if (read_file(lock_path, buf, sizeof(buf)) > 0) {
      pid_t holder = (pid_t)atoi(buf);
      if (holder > 0 && holder != getpid() && kill(holder, 0) == 0) {
        /* Lock holder is alive and NOT us - cannot acquire */
        ds_warn("Cannot acquire lock: held by process %d", holder);
        return -1;
      }
      /* Stale lock detected */
      if (holder > 0 && holder != getpid()) {
        ds_log("Removing stale lock (holder PID %d is dead)", holder);
      }
    }
    /* Remove stale lock */
    unlink(lock_path);
  }

  /* Write our PID to lock file */
  char pid_str[32];
  snprintf(pid_str, sizeof(pid_str), "%d", getpid());
  return write_file_atomic(lock_path, pid_str);
}

/* Release external command lock - ONLY called by CLI parent.
 * Verifies ownership before removing. */
static void release_external_lock(const char *name) {
  char lock_path[PATH_MAX];
  if (get_lock_path(name, lock_path, sizeof(lock_path)) < 0)
    return;

  /* Verify we own the lock before removing */
  char buf[32];
  if (read_file(lock_path, buf, sizeof(buf)) > 0) {
    pid_t holder = (pid_t)atoi(buf);
    if (holder == getpid()) {
      unlink(lock_path);
    } else if (holder > 0) {
      /* This should never happen but log it for debugging */
      ds_warn("Attempted to release lock owned by PID %d (we are %d)", holder,
              getpid());
    }
  }
}

/* ---------------------------------------------------------------------------
 * Configuration & Metadata Recovery
 * ---------------------------------------------------------------------------*/

/**
 * Enhanced config loader that performs a global /proc scan if host metadata
 * is missing.
 *
 * returns: 0 on success (config loaded/restored), -1 on fatal failure.
 */

void write_plain_env_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "re");
  if (!in)
    return;
  FILE *out = fopen(dst, "we");
  if (!out) {
    fclose(in);
    return;
  }
  char line[2048];
  while (fgets(line, sizeof(line), in)) {
    char *p = line;
    if (strncmp(p, "export ", 7) == 0)
      p += 7;
    fputs(p, out);
  }
  fclose(in);
  fclose(out);
}

/* Check if external command lock exists - called by monitor (READ ONLY).
 * Returns: 1 if lock exists and holder is alive, 0 otherwise. */
int is_external_lock_active(const char *name) {
  char lock_path[PATH_MAX];
  if (get_lock_path(name, lock_path, sizeof(lock_path)) < 0)
    return 0;

  if (access(lock_path, F_OK) != 0)
    return 0; /* No lock */

  /* Lock exists - verify holder is alive */
  char buf[32];
  if (read_file(lock_path, buf, sizeof(buf)) > 0) {
    pid_t holder = (pid_t)atoi(buf);
    if (holder > 0 && kill(holder, 0) == 0)
      return 1; /* Valid lock */

    /* Stale lock detected */
    write_monitor_debug_log(name, "Removing stale lock (holder PID %d is dead)",
                            holder);
  }

  /* Remove stale lock */
  unlink(lock_path);
  return 0;
}

/* ---------------------------------------------------------------------------
 * Cleanup
 * ---------------------------------------------------------------------------*/

/* Poll for a socket path to appear, bailing early if the server process dies.
 * Returns 0 on socket ready, -1 on server death or timeout. */
int wait_for_socket_or_death(pid_t pid, const char *path, int timeout_ms,
                             int interval_us) {
  int iters = timeout_ms * 1000 / interval_us;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (int i = 0; i < iters; i++) {
    if (access(path, F_OK) == 0) {
      clock_gettime(CLOCK_MONOTONIC, &t1);
      long ms =
          (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
      ds_log("[DEBUG] socket ready: %s (+%ldms)", path, ms);
      return 0;
    }
    int st;
    if (waitpid(pid, &st, WNOHANG) > 0) {
      ds_error("server pid=%d exited (status=%d) before socket appeared",
               (int)pid, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
      return -1;
    }
    usleep((unsigned int)interval_us);
  }
  ds_warn("timed out waiting for socket: %s", path);
  return -1;
}

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

  /* 2. Resolve global PID file path */
  char global_pidfile[PATH_MAX];
  resolve_pidfile_from_name(cfg->container_name, global_pidfile,
                            sizeof(global_pidfile));

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
  char mount_point[PATH_MAX] = "";
  if (read_mount_path(cfg->pidfile, mount_point, sizeof(mount_point)) <= 0) {
    /* Fallback: use cfg->img_mount_point if .mount sidecar is gone */
    if (cfg->img_mount_point[0]) {
      safe_strncpy(mount_point, cfg->img_mount_point, sizeof(mount_point));
    }
  }

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

  /* 5. Remove tracking info and unlink PID files.
   * For restart (skip_unmount), preserve the .mount sidecar and pidfiles
   * so start_rootfs() can detect the existing mount and reuse it. */
  if (!skip_unmount) {
    remove_mount_path(cfg->pidfile);
    remove_init_type(cfg->pidfile);
    if (cfg->pidfile[0])
      unlink(cfg->pidfile);
    if (global_pidfile[0] && strcmp(cfg->pidfile, global_pidfile) != 0)
      unlink(global_pidfile);

    /* Stale lock cleanup is handled by acquire_external_lock and
     * is_external_lock_active. Monitor only does resource cleanup
     * if no external lock is active. */
  }

  /* Network cleanup: remove host veth and owned network state */
  if (cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_GATEWAY) {
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

/* ---------------------------------------------------------------------------
 * Introspection
 * ---------------------------------------------------------------------------*/

int is_valid_container_pid(pid_t pid) {
  char path[PATH_MAX];

  /* Primary marker: /run/droidspaces must exist inside the container.
   * This is the one authoritative marker written by droidspaces on boot.
   * We do NOT require /run/systemd/container - Alpine/runit/openrc never
   * write that file, causing scan to be blind to non-systemd distros. */
  if (build_proc_root_path(pid, DS_DROIDSPACES_MARKER, path, sizeof(path)) < 0)
    return 0;
  if (access(path, F_OK) != 0)
    return 0;

  /* Secondary check: process must be the init (PID 1) of its namespace.
   * This is more robust than checking cmdline for "init" which distros
   * like Void Linux (runit) or Alpine may not provide. */
  if (!is_container_init(pid))
    return 0;

  return 1;
}

/* ---------------------------------------------------------------------------
 * Start
 * ---------------------------------------------------------------------------*/

int start_rootfs(struct ds_config *cfg) {

  int has_side_effects = 0;
  int lock_acquired = 0;

  /* 0. Early restart detection: check for external lock from previous stop
   *    command to detect a preserved mount for reuse. */
  if (cfg->container_name[0]) {
    char lock_path[PATH_MAX];
    if (get_lock_path(cfg->container_name, lock_path, sizeof(lock_path)) == 0 &&
        access(lock_path, F_OK) == 0) {
      /* This looks like a restart handoff - take ownership of the lock */
      if (acquire_external_lock(cfg->container_name) == 0) {
        lock_acquired = 1;

        /* Try to reuse existing mount */
        if (cfg->pidfile[0] == '\0')
          resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                                    sizeof(cfg->pidfile));

        char existing_mount[PATH_MAX];
        if (cfg->pidfile[0] &&
            read_mount_path(cfg->pidfile, existing_mount,
                            sizeof(existing_mount)) > 0 &&
            is_mountpoint(existing_mount)) {
          safe_strncpy(cfg->rootfs_path, existing_mount,
                       sizeof(cfg->rootfs_path));
          cfg->is_img_mount = 1;
          safe_strncpy(cfg->img_mount_point, cfg->rootfs_path,
                       sizeof(cfg->img_mount_point));
        } else {
          /* Mount not active - remove invalid lock */
          release_external_lock(cfg->container_name);
          lock_acquired = 0;
        }
      }
    }
  }

  /* 1. Logo & Uniqueness Check */
  check_kernel_recommendation();

  /* 1b. Name Uniqueness Check
   * We no longer auto-generate or increment names. The name must be provided
   * by the user and it must be unique. */
  if (!lock_acquired) {
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
  if (cfg->termux_x11 && !is_android())
    ds_warn("--termux-x11 is only applicable on Android. Skipping.");
  if (cfg->tx11_extra_flags && !is_android())
    ds_warn("--tx11-flags is only applicable on Android. Skipping.");
  if (cfg->virgl && !is_android())
    ds_warn("--virgl is only applicable on Android. Skipping.");
  if (cfg->virgl_extra_flags && !is_android())
    ds_warn("--virgl-flags is only applicable on Android. Skipping.");
  if (cfg->pulseaudio && !is_android())
    ds_warn("--pulse-audio is only applicable on Android. Skipping.");

  /* If no hostname specified, default to container name */
  if (cfg->hostname[0] == '\0') {
    safe_strncpy(cfg->hostname, cfg->container_name, sizeof(cfg->hostname));
  }

  has_side_effects = 1;

  /* 2. Mount rootfs image if provided (using the resolved name) */
  if (cfg->rootfs_img_path[0] && !lock_acquired) {
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

  /* 2b. Android: start Termux-X11, VirGL, and PulseAudio servers before fork
   * so the sockets exist when bind-mounted later */
  if (is_android() && cfg->termux_x11) {
    if (ds_x11_daemon_start(cfg) == 0)
      wait_for_socket_or_death(
          cfg->x11_pid, TX11_SOCK_DIR "/" TX11_DISPLAY_SOCK, 5000, 50000);
  }

  if (is_android() && cfg->virgl) {
    if (ds_virgl_daemon_start(cfg) == 0)
      wait_for_socket_or_death(cfg->virgl_pid, TX11_VIRGL_SOCKET, 2000, 20000);
  }

  if (is_android() && cfg->pulseaudio) {
    ds_pulse_daemon_start(cfg);
  }

  /* 3. Early pre-flight for volatile mode (before any host changes) */
  if (check_volatile_mode(cfg) < 0) {
    goto cleanup;
  }

  {
    char active_uuids[DS_MAX_CONTAINERS][DS_UUID_LEN + 1];
    int uuid_count = collect_active_uuids(active_uuids, DS_MAX_CONTAINERS);
    int need_new = (cfg->uuid[0] == '\0');
    if (!need_new) {
      for (int _i = 0; _i < uuid_count; _i++) {
        if (strcmp(cfg->uuid, active_uuids[_i]) == 0) {
          need_new = 1;
          break;
        }
      }
    }
    if (need_new)
      generate_uuid(cfg->uuid, sizeof(cfg->uuid));
  }

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
  if (cfg->env_file[0] != '\0') {
    free_config_env_vars(cfg);
    int _prev = ds_log_silent;
    ds_log_silent = 1;
    parse_env_file_to_config(cfg->env_file, cfg);
    ds_log_silent = _prev;
  }

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

  ds_fix_host_ptys();

  if (ds_terminal_create(&cfg->console) < 0) {
    ds_error("Failed to allocate console PTY");
    goto cleanup;
  }

  /* Propagate the host terminal's window size to the console PTY master
   * so the slave (which becomes /dev/console) has correct dimensions
   * from the very start of boot. This prevents misaligned output during
   * the window between PTY creation and the console_monitor_loop startup.
   * Without this, 'sudo poweroff' output is misaligned for the first
   * ~10 lines because sudo resets/queries the terminal size and finds
   * a {0,0} winsize on the PTY slave. */
  if (isatty(STDIN_FILENO)) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
      ioctl(cfg->console.master, TIOCSWINSZ, &ws);
  }

  /* 5. Resolve target PID file names early so monitor inherits them */
  char global_pidfile[PATH_MAX];
  resolve_pidfile_from_name(cfg->container_name, global_pidfile,
                            sizeof(global_pidfile));

  /* If no pidfile specified, or we want to use the global one */
  if (!cfg->pidfile[0]) {
    safe_strncpy(cfg->pidfile, global_pidfile, sizeof(cfg->pidfile));
  }

  /* 6. Pipe for synchronization */
  int sync_pipe[2];
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
  android_optimizations(1);

  /* Record start time before fork so monitor and virtualize_update share it */
  clock_gettime(CLOCK_BOOTTIME, &cfg->start_time);

  /* 8. Fork Monitor Process */
  pid_t monitor_pid = fork();
  if (monitor_pid < 0) {
    close(sync_pipe[0]);
    close(sync_pipe[1]);
    ds_error("fork failed: %s", strerror(errno));
    goto cleanup;
  }

  if (monitor_pid == 0) {
    close(sync_pipe[0]);
    ds_monitor_run(cfg, sync_pipe[1]);
    /* ds_monitor_run never returns */
    _exit(EXIT_FAILURE);
  }

  /* PARENT PROCESS */
  close(sync_pipe[1]);

  /* Wait for Monitor to send child PID */
  if (read(sync_pipe[0], &cfg->container_pid, sizeof(pid_t)) != sizeof(pid_t)) {
    ds_error("Monitor failed to send container PID.");
    if (lock_acquired)
      release_external_lock(cfg->container_name);
    goto cleanup;
  }
  close(sync_pipe[0]);
  sync_pipe[0] = -1;

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

  /* Always save to global Pids directory (for --name lookups) */
  if (write_file_atomic(global_pidfile, pid_str) < 0) {
    ds_error("Failed to write PID file: %s", global_pidfile);
  }

  /* Also save to user-specified --pidfile if different */
  if (cfg->pidfile[0] && strcmp(cfg->pidfile, global_pidfile) != 0) {
    if (write_file_atomic(cfg->pidfile, pid_str) < 0) {
      ds_error("Failed to write PID file: %s", cfg->pidfile);
    }
  }

  if (cfg->is_img_mount)
    save_mount_path(cfg->pidfile, cfg->img_mount_point);

  /* Also save init type */
  save_init_type(cfg->pidfile, cfg->init_type);

  /* 11. Foreground or background finish */
  if (cfg->foreground) {

    if (lock_acquired) {
      release_external_lock(cfg->container_name);
      lock_acquired = 0;
    }

    int ret = console_monitor_loop(cfg->console.master, monitor_pid, cfg);
    free_config_env_vars(cfg);
    return ret;
  } else {
    /* Wait for container to finish pivot_root before showing info.
     * The boot sequence writes /run/droidspaces after pivot_root,
     * so we poll for it via /proc/<pid>/root/run/droidspaces. */
    char marker[PATH_MAX];
    snprintf(marker, sizeof(marker), "/proc/%d/root/run/droidspaces",
             cfg->container_pid);
    int booted = 0;
    for (int i = 0; i < 50; i++) { /* 5 seconds max */
      if (access(marker, F_OK) == 0) {
        booted = 1;
        break;
      }
      /* If the container PID is already dead, stop polling */
      if (kill(cfg->container_pid, 0) < 0 && errno == ESRCH)
        break;
      usleep(100000); /* 100ms */
    }

    if (!booted) {
      ds_error("Container failed to boot correctly.");
      /* If pid is still alive, we might want to kill it, but monitor usually
       * handles this. Let's just return error so parent doesn't report
       * success.
       */
      ds_config_free(cfg);
      goto cleanup;
    }

    show_info(cfg, 1);
    ds_socketd_record_core_event("start", cfg->container_name, cfg->uuid);
    ds_log("Container '%s' is running in background.", cfg->container_name);
    if (is_android()) {
      ds_log("Use 'su -c \"%s --name='%s' enter\"' to connect.", cfg->prog_name,
             cfg->container_name);
    } else {
      ds_log("Use 'sudo %s --name='%s' enter' to connect.", cfg->prog_name,
             cfg->container_name);
    }
  }

  if (lock_acquired)
    release_external_lock(cfg->container_name);
  ds_config_free(cfg);

  return 0;

cleanup:
  /* Centralized host-side cleanup IF we are returning error.
   * This ensures image mounts and tracking files are reverted on fatal boot
   * errors. Only execute if we successfully crossed the point of creating
   * effects. */
  if (has_side_effects) {
    cleanup_container_resources(cfg, cfg->container_pid, 0, 1 /* force */);
  }
  if (lock_acquired)
    release_external_lock(cfg->container_name);

  if (cfg->console.master >= 0) {
    close(cfg->console.master);
    cfg->console.master = -1;
  }
  if (sync_pipe[0] >= 0)
    close(sync_pipe[0]);
  if (sync_pipe[1] >= 0)
    close(sync_pipe[1]);

  ds_config_free(cfg);
  return -1;
}

int stop_rootfs_with_timeout(struct ds_config *cfg, int skip_unmount,
                             int timeout_seconds) {
  if (timeout_seconds < 0)
    timeout_seconds = DS_STOP_TIMEOUT;

  /* Acquire external command lock FIRST */
  if (acquire_external_lock(cfg->container_name) != 0) {
    ds_error("Cannot stop '%s': another command is managing this container",
             cfg->container_name);
    ds_error("Wait for the other operation to complete, or use 'droidspaces "
             "show' to check status");
    return -1;
  }

  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    release_external_lock(cfg->container_name);
    return -1;
  }

  ds_log("Stopping container '%s' (PID %d)...", cfg->container_name, pid);

  /* Safe Metadata Capture: Read the mount path from the tracking file (.mount)
   * into memory before we start the shutdown wait loop. This ensures we have
   * the correct host path even if the tracking files are deleted by the monitor
   * or another process during the timeout. */
  if (cfg->img_mount_point[0] == '\0') {
    read_mount_path(cfg->pidfile, cfg->img_mount_point,
                    sizeof(cfg->img_mount_point));
  }

  /* 1. Send shutdown signal. */
  if (cfg->custom_init[0]) {
    kill(pid, SIGKILL);
  } else {
    /* Detect init system and send the correct shutdown signal. */
    ds_init_type_t init_type = DS_INIT_UNKNOWN;
    const char *probe_root =
        cfg->img_mount_point[0] ? cfg->img_mount_point : cfg->rootfs_path;
    if (__builtin_expect((read_init_type(cfg->pidfile, &init_type) != 0 ||
                          init_type == DS_INIT_UNKNOWN),
                         0)) {
      /* Fallback for containers launched before .init sidecars existed,
       * or if runtime metadata was lost / non-informative. */
      if (__builtin_expect(probe_root[0], '/'))
        init_type = detect_container_init(probe_root);
    }

    switch (init_type) {
    case DS_INIT_PROCD:
    case DS_INIT_S6:
    case DS_INIT_BUSYBOX:
      kill(pid, SIGUSR2);
      break;
    case DS_INIT_RUNIT:
      kill(pid, SIGCONT);
      break;
    case DS_INIT_SYSTEMD:
      kill(pid, DS_SIG_STOP); /* SIGRTMIN+3 */
      break;
    case DS_INIT_SYSVINIT: {
      /* sysvinit ignores all signals for shutdown -- it only listens on the
       * initctl FIFO. Write a telinit-compatible init_request struct directly
       * into the container's /run/initctl via /proc/<pid>/root. */
      char initctl[PATH_MAX];
      snprintf(initctl, sizeof(initctl), "/proc/%d/root/run/initctl", pid);

      /* struct layout from initreq.h: magic(4) cmd(4) runlevel(4) sleeptime(4)
       * data(368) = 384 bytes total. */
      struct {
        int magic;
        int cmd;
        int runlevel;
        int sleeptime;
        char data[368];
      } req = {
          .magic = 0x03091969, /* INIT_MAGIC */
          .cmd = 1,            /* INIT_CMD_RUNLVL */
          .runlevel = '0',     /* poweroff */
          .sleeptime = 3,
      };

      int fd = open(initctl, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) {
        /* Fallback: try /dev/initctl (historical path, used by Slackware) */
        snprintf(initctl, sizeof(initctl), "/proc/%d/root/dev/initctl", pid);
        fd = open(initctl, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
      }

      if (fd >= 0) {
        if (write(fd, &req, sizeof(req)) != (ssize_t)sizeof(req))
          ds_warn("sysvinit: short write to initctl, falling back to SIGPWR");
        close(fd);
      } else {
        ds_warn("sysvinit: cannot open initctl (tried /run and /dev), falling "
                "back to SIGPWR");
        kill(pid, SIGPWR);
      }
      break;
    }
    case DS_INIT_OPENRC:
      kill(pid, SIGPWR);
      break;
    default: /* unknown */
      kill(pid, SIGTERM);
      break;
    }

    ds_log("Waiting for graceful shutdown (this may take up to %d seconds)...",
           timeout_seconds);
  }

  /* 2. Wait for exit */
  int stopped = 0;
  for (int i = 0; i < timeout_seconds * 5; i++) {
    if (kill(pid, 0) < 0) {
      if (errno == ESRCH) {
        stopped = 1;
        break;
      }
    }
    usleep(DS_RETRY_DELAY_US);
  }

  /* 3. Force kill if still running */
  int unkillable = 0;
  if (!stopped) {
    ds_warn("Graceful stop timed out, sending SIGKILL...");
    kill(pid, SIGKILL);

    /*
     * Wait up to 5 seconds for the kernel to clean up the process.
     * We don't use blocking waitpid() because we aren't the parent,
     * and we want a timeout to prevent hanging on unkillable PIDs.
     */
    int killed = 0;
    for (int j = 0; j < 25; j++) { /* 5 seconds total */
      if (kill(pid, 0) < 0 && errno == ESRCH) {
        killed = 1;
        break;
      }
      usleep(200000); /* 200ms */
    }

    if (!killed) {
      unkillable = 1;
      ds_error("Container PID %d is in an unkillable state!", pid);
      ds_warn("This often happens on old Android kernels due to zombie "
              "processes.\nPlease restart your device to clear it.");
      ds_warn("Proceeding with best-effort host cleanup (no sync)...");
    }
  }

  /* 4. Firmware cleanup (hw_access mode only).
   * Skip when unkillable - accessing zombie-held rootfs can hang. */
  if (cfg->img_mount_point[0] && !unkillable && cfg->hw_access) {
    char fw_path[PATH_MAX + 16];
    snprintf(fw_path, sizeof(fw_path), "%s/lib/firmware", cfg->img_mount_point);
    firmware_path_remove(fw_path);
  }

  /* 5. Complete resource cleanup. */
  cleanup_container_resources(cfg, pid, skip_unmount, unkillable);
  ds_socketd_record_core_event("die", cfg->container_name, cfg->uuid);

  if (!cfg->foreground)
    ds_log("Container '%s' stopped.", cfg->container_name);

  /* Release lock ONLY if this is a final stop.
   * For restarts (skip_unmount=1), keep lock alive as handoff. */
  if (!skip_unmount) {
    release_external_lock(cfg->container_name);
  }

  return 0;
}

int stop_rootfs(struct ds_config *cfg, int skip_unmount) {
  return stop_rootfs_with_timeout(cfg, skip_unmount, DS_STOP_TIMEOUT);
}

/* ---------------------------------------------------------------------------
 * Namespace Entry (shared for enter and run)
 * ---------------------------------------------------------------------------*/

int enter_namespace(pid_t pid, struct ds_config *cfg) {
  /* Verify process is still alive before trying to enter namespaces */
  if (kill(pid, 0) < 0) {
    ds_error("Container PID %d is no longer alive.", pid);
    return -1;
  }

  const char *ns_names[] = {"mnt", "uts", "ipc", "pid", "cgroup", "net"};
  int ns_fds[6];
  char path[PATH_MAX];

  /* 1. Open all namespace descriptors first (CRITICAL: before any setns) */
  for (int i = 0; i < 6; i++) {
    snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, ns_names[i]);
    ns_fds[i] = open(path, O_RDONLY);
    if (ns_fds[i] < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("Failed to open mount namespace at %s: %s", path,
                 strerror(errno));
        /* Cleanup previous fds */
        for (int j = 0; j < i; j++)
          close(ns_fds[j]);
        return -1;
      }
      if (errno != ENOENT && i != 5) {
        ds_warn("Optional namespace %s (%s) is missing: %s", ns_names[i], path,
                strerror(errno));
      }
    }
  }

  /* 2. Enter namespaces */
  for (int i = 0; i < 6; i++) {
    if (ns_fds[i] < 0)
      continue;

    /* Skip entering the 'net' namespace (index 5) if host networking is enabled
     */
    if (i == 5 && cfg && cfg->net_mode == DS_NET_HOST) {
      close(ns_fds[i]);
      continue;
    }

    if (setns(ns_fds[i], 0) < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("setns(mnt) failed: %s", strerror(errno));
        for (int j = i; j < 6; j++)
          if (ns_fds[j] >= 0)
            close(ns_fds[j]);
        return -1;
      }
      if (i != 5) {
        ds_warn("setns(%s) failed (ignored): %s", ns_names[i], strerror(errno));
      }
    }
    close(ns_fds[i]);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Enter / Run
 * ---------------------------------------------------------------------------*/

int enter_rootfs(struct ds_config *cfg, const char *user) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }

  /* Parse environment file while host paths are reachable */
  if (cfg->env_file[0] != '\0') {
    free_config_env_vars(cfg);
    int prev_silent = ds_log_silent;
    ds_log_silent = 1;
    parse_env_file_to_config(cfg->env_file, cfg);
    ds_log_silent = prev_silent;
  }

  /* PTY allocation is deferred until after entering the container namespaces.
   * This ensures the slave PTY is part of the container's private devpts
   * instance. */
  struct ds_tty_info tty;
  memset(&tty, 0, sizeof(tty));
  tty.master = tty.slave = -1;

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    close(tty.master);
    close(tty.slave);
    free_config_env_vars(cfg);
    return -1;
  }

  /* Parent will receive master FD from child after namespace entry */
  close(tty.slave);
  tty.slave = -1;
  close(tty.master);
  tty.master = -1;

  pid_t child = fork();
  if (child < 0) {
    close(sv[0]);
    close(sv[1]);
    close(tty.master);
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    close(sv[0]);

    /* In this refactored flow, the child allocates the PTY itself after setns.
     */
    tty.slave = -1;

    /* cgroup attach before entering namespaces */
    ds_log_silent = 1;
    ds_cgroup_attach(pid);
    ds_log_silent = 0;

    if (enter_namespace(pid, cfg) < 0)
      _exit(EXIT_FAILURE);

    /* Now that we are in the container's mount namespace, allocate a PTY.
     * This ensures its path (e.g. /dev/pts/0) exists and is resolvable. */
    if (ds_terminal_create(&tty) < 0)
      _exit(EXIT_FAILURE);

    /* Send master FD back to parent (host monitor) */
    if (ds_send_fd(sv[1], tty.master) < 0)
      _exit(EXIT_FAILURE);
    close(tty.master);
    tty.master = -1;

    /* Apply identical security hardening as internal_boot().
     * Seccomp filters and capability bounding set drops are per-process and
     * inherited only via fork/exec from PID 1 - entering processes arrive via
     * setns() and are NOT children of init, so they inherit nothing. */
    ds_log_silent = 1;
    ds_seccomp_apply_minimal(cfg->privileged_mask);
    android_seccomp_setup(
        0, cfg->block_nested_ns && !(cfg->privileged_mask & DS_PRIV_NOSEC),
        cfg->privileged_mask);
    ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask);
    ds_log_silent = 0;

    /* ---------------------------------------------------------------
     * LXC-STYLE SESSION SETUP - intermediate becomes session leader
     * ---------------------------------------------------------------
     * Ubuntu 24.04+ login (util-linux) calls vhangup() as part of its
     * "secure login" sequence: hang up the old session, reopen the
     * terminal fresh, then setsid()+TIOCSCTTY to own it.
     *
     * vhangup() sends SIGHUP to the SESSION LEADER of the controlling
     * terminal.  In our OLD design the grandchild (bash) was the session
     * leader, so it received SIGHUP, killed login's process group, then
     * killed itself - the terminal collapsed.
     *
     * THE FIX (matches lxc-attach behaviour):
     *   • The INTERMEDIATE does setsid() + TIOCSCTTY here (not the shell).
     *   • The intermediate ignores SIGHUP.
     *   • The grandchild (bash) is a child of the intermediate's session
     *     and is therefore NOT the session leader - it never receives
     *     the SIGHUP that vhangup() generates.
     *   • login's vhangup() → SIGHUP → intermediate → ignored → bash lives.
     *   • login then does setsid() + open(/dev/pts/N without O_NOCTTY) to
     *     auto-acquire the terminal as the new session leader.
     *   • After login exits the terminal is released and bash resumes.
     *
     * The slave fd is intentionally kept open in the intermediate for the
     * duration of the session (the LXC "peer fd").  This prevents the
     * slave from entering a destroyed state during the vhangup/reopen
     * window and keeps a stable reference count on the pts entry.
     */
    if (setsid() < 0)
      _exit(EXIT_FAILURE);
    if (ioctl(tty.slave, TIOCSCTTY, 0) < 0)
      _exit(EXIT_FAILURE);
    signal(SIGHUP, SIG_IGN);

    close(sv[1]);

    /* Must fork again to actually be in the new PID namespace */
    pid_t shell_pid = fork();
    if (shell_pid < 0)
      _exit(EXIT_FAILURE);
    if (shell_pid == 0) {
      /* The controlling terminal and session leader were established in
       * the intermediate (parent of this fork) - do NOT call setsid()
       * or TIOCSCTTY here.  This process (bash) is a child member of
       * the intermediate's session and inherits pts/1 as its ctty.
       * Being a non-session-leader is deliberate: when the user runs
       * 'login' inside bash, login's vhangup() sends SIGHUP only to
       * the session leader (the intermediate, which ignores it), so
       * bash is unaffected and its prompt returns after login exits. */
      if (ds_terminal_set_stdfds(tty.slave) < 0)
        _exit(EXIT_FAILURE);

      if (tty.slave > STDERR_FILENO)
        close(tty.slave);

      if (chdir("/") < 0)
        _exit(EXIT_FAILURE);

      /* Apply fixed and user-defined environment */
      ds_env_boot_setup(cfg);
      load_etc_environment();

      extern char **environ;

      /* Primary path: proper login via su -l <user>.
       * This gives the correct home directory, shell, and login environment
       * from the container's /etc/passwd.  user is always non-NULL here
       * (main.c defaults to "root" when no argument is given). */
      char *shell_argv[] = {"su", "-l", (char *)(uintptr_t)user, NULL};
      execve("/bin/su", shell_argv, environ);
      execve("/usr/bin/su", shell_argv, environ);
      execve("/run/wrappers/bin/su", shell_argv, environ);

      /* Fallback: su not available - look up the shell from /etc/passwd */
      char user_shell[PATH_MAX] = {0};
      if (get_user_shell(user, user_shell, sizeof(user_shell)) == 0) {
        if (access(user_shell, X_OK) == 0) {
          const char *sh_name = strrchr(user_shell, '/');
          sh_name = sh_name ? sh_name + 1 : user_shell;
          char *sh_argv[] = {(char *)(uintptr_t)sh_name, "-l", NULL};
          execve(user_shell, sh_argv, environ);
        }
      }

      /* Last resort: try shells in priority order */
      const char *shells[] = {"/bin/bash", "/bin/ash", "/bin/sh", NULL};
      for (int i = 0; shells[i]; i++) {
        if (access(shells[i], X_OK) == 0) {
          const char *sh_name = strrchr(shells[i], '/');
          sh_name = sh_name ? sh_name + 1 : shells[i];
          char *sh_argv[] = {(char *)(uintptr_t)sh_name, "-l", NULL};
          execve(shells[i], sh_argv, environ);
        }
      }

      ds_error("Failed to find any usable shell");
      _exit(EXIT_FAILURE);
    }
    /* Intermediate: intentionally keep tty.slave open as the peer fd.
     * This holds a stable reference on the pts slave entry for the entire
     * session, preventing it from being destroyed during the brief
     * vhangup()/reopen window when the user runs 'login'.
     * The fd is released automatically when we _exit below. */
    waitpid(shell_pid, NULL, 0);
    _exit(EXIT_SUCCESS);
  }

  close(sv[1]);

  /* Receive native PTY master from child */
  int master_fd = ds_recv_fd(sv[0]);
  close(sv[0]);

  if (master_fd < 0) {
    ds_error("Failed to receive PTY master from child");
    waitpid(child, NULL, 0);
    ds_cgroup_detach(child, cfg->container_name);
    return -1;
  }

  /* Synchronize window size BEFORE starting setup to avoid race with child
   * exec. This ensures htop/nano see the correct size immediately upon startup.
   */
  if (isatty(STDIN_FILENO)) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
      ioctl(master_fd, TIOCSWINSZ, &ws);
  }

  /* Parent: setup host terminal and proxy I/O */
  struct termios old_tios;
  int has_tty = (ds_setup_tios(STDIN_FILENO, &old_tios) == 0);

  ds_terminal_proxy(master_fd);

  if (has_tty) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tios);
  }

  close(master_fd);
  waitpid(child, NULL, 0);
  ds_cgroup_detach(child, cfg->container_name);
  free_config_env_vars(cfg);
  return 0;
}

int run_in_rootfs(struct ds_config *cfg, int argc, char **argv,
                  const char *as_user) {
  (void)argc;
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }

  /* Removed verbose status log to allow raw output stream */

  /* Parse environment file while host paths are reachable */
  if (cfg->env_file[0] != '\0') {
    free_config_env_vars(cfg);
    int prev_silent = ds_log_silent;
    ds_log_silent = 1;
    parse_env_file_to_config(cfg->env_file, cfg);
    ds_log_silent = prev_silent;
  }

  pid_t child = fork();
  if (child < 0) {
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    /* Mirror enter_rootfs: attach to the container's cgroup subtree before
     * crossing into its namespaces, so the command is properly accounted
     * under systemd's hierarchy instead of leaking to the cgroup root. */
    ds_log_silent = 1;
    ds_cgroup_attach(pid);
    ds_log_silent = 0;

    if (enter_namespace(pid, cfg) < 0)
      _exit(EXIT_FAILURE);

    /* Apply identical security hardening as internal_boot() and enter_rootfs().
     * Same reasoning: run processes are not children of container PID 1. */
    ds_log_silent = 1;
    ds_seccomp_apply_minimal(cfg->privileged_mask);
    android_seccomp_setup(
        0, cfg->block_nested_ns && !(cfg->privileged_mask & DS_PRIV_NOSEC),
        cfg->privileged_mask);
    ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask);
    ds_log_silent = 0;

    pid_t cmd_pid = fork();
    if (cmd_pid < 0)
      _exit(EXIT_FAILURE);
    if (cmd_pid == 0) {
      if (chdir("/") < 0)
        _exit(EXIT_FAILURE);

      /* Setup environment */
      ds_env_boot_setup(cfg);
      load_etc_environment();

      /* Append NixOS binary path so Nix-managed tools (e.g. ip, hostname)
       * are available without requiring the caller to prefix PATH manually. */
      {
        const char *cur_path = getenv("PATH");
        if (cur_path) {
          char nix_path[4096];
          snprintf(nix_path, sizeof(nix_path), "%s:/run/current-system/sw/bin",
                   cur_path);
          setenv("PATH", nix_path, 1);
        } else {
          setenv("PATH", "/run/current-system/sw/bin", 1);
        }
      }

      /* Run the command directly as an alien process (instant results) */
      if (as_user != NULL) {
        /* Build the command string for su -c.
         * If argv[0] has no spaces and argv[1] is NULL, pass it directly.
         * Otherwise join all args into a single shell string. */
        char cmd_buf[4096];
        if (argv[1] == NULL) {
          safe_strncpy(cmd_buf, argv[0], sizeof(cmd_buf));
        } else {
          size_t off = 0;
          for (int k = 0; argv[k] && off < sizeof(cmd_buf) - 1; k++) {
            if (k > 0 && off < sizeof(cmd_buf) - 2)
              cmd_buf[off++] = ' ';
            size_t al = strlen(argv[k]);
            if (off + al >= sizeof(cmd_buf) - 1)
              al = sizeof(cmd_buf) - 1 - off;
            memcpy(cmd_buf + off, argv[k], al);
            off += al;
          }
          cmd_buf[off] = '\0';
        }
        char *su_argv[] = {"su", "-",     (char *)(uintptr_t)as_user,
                           "-c", cmd_buf, NULL};
        execvp("/bin/su", su_argv);
        execvp("/usr/bin/su", su_argv);
        execvp("/run/wrappers/bin/su", su_argv);
        ds_error("Failed to exec su for user '%s': %s", as_user,
                 strerror(errno));
        _exit(EXIT_FAILURE);
      }

      if (argv[1] == NULL && strchr(argv[0], ' ') != NULL) {
        char *shell_argv[] = {"/bin/sh", "-c", argv[0], NULL};
        execvp("/bin/sh", shell_argv);
      } else {
        execvp(argv[0], argv);
      }

      ds_error("Failed to execute command: %s", strerror(errno));
      _exit(EXIT_FAILURE);
    }

    int status;
    waitpid(cmd_pid, &status, 0);
    _exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
  }

  int status;
  waitpid(child, &status, 0);
  ds_cgroup_detach(child, cfg->container_name);
  free_config_env_vars(cfg);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---------------------------------------------------------------------------
 * Other operations
 * ---------------------------------------------------------------------------*/

static const char *get_architecture(void) {
  static struct utsname uts;
  if (uname(&uts) != 0)
    return "unknown";

  if (strcmp(uts.machine, "x86_64") == 0)
    return "x86_64";
  if (strcmp(uts.machine, "aarch64") == 0 || strcmp(uts.machine, "arm64") == 0)
    return "aarch64";
  if (strncmp(uts.machine, "arm", 3) == 0)
    return "arm";
  if (strcmp(uts.machine, "i686") == 0 || strcmp(uts.machine, "i386") == 0)
    return "x86";
  return uts.machine;
}

static void parse_pretty_name(FILE *fp, char *buf, size_t size) {
  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
      char *val = line + 12;
      size_t len = strlen(val);
      while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '"'))
        val[--len] = '\0';
      if (val[0] == '"') {
        val++;
        len--;
      }
      if (len >= size)
        len = size - 1;
      snprintf(buf, size, "%.*s", (int)len, val);
      return;
    }
  }
}

static void get_os_pretty(const char *osrelease_path, char *buf, size_t size) {
  if (!buf || size == 0)
    return;
  buf[0] = '\0';

  FILE *fp = fopen(osrelease_path, "r");
  if (!fp)
    return;

  parse_pretty_name(fp, buf, size);
  fclose(fp);
}

int show_info(struct ds_config *cfg, int trust_cfg_pid) {
  /* Case 1: No container name specified - try auto-resolution or listing */
  if (cfg->container_name[0] == '\0') {
    char first_name[256];
    int count = count_running_containers(first_name, sizeof(first_name));

    if (count == 0) {
      const char *host = is_android() ? "Android" : "Linux";
      const char *arch = get_architecture();
      printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);
      printf("\n" C_YELLOW "Container:" C_RESET " No containers running.\n\n");
      return 0;
    }

    if (count == 1) {
      /* Auto-resolve to the only running container */
      safe_strncpy(cfg->container_name, first_name,
                   sizeof(cfg->container_name));
      resolve_pidfile_from_name(first_name, cfg->pidfile, sizeof(cfg->pidfile));
    } else {
      /* Multiple containers running, show Host info and list */
      const char *host = is_android() ? "Android" : "Linux";
      const char *arch = get_architecture();
      printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);
      printf("\n" C_YELLOW "Multiple containers running:" C_RESET "\n");
      show_containers(cfg);
      printf("\nUse '" C_GREEN "--name <NAME> info" C_RESET
             "' for detailed information.\n\n");
      return 0;
    }
  }

  /* Now we have a container name. Ensure its config is loaded from the source
   * of truth (container.config) so we show accurate feature info without
   * expensive live probing. */
  if (!trust_cfg_pid) {
    ds_config_load_by_name(cfg->container_name, cfg);
  }

  /* Case 2: Ensure pidfile resolved. */
  if (cfg->pidfile[0] == '\0' && cfg->container_name[0] != '\0') {
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
  }

  /* Case 3: Validate running status */
  pid_t pid = 0;
  if (trust_cfg_pid && cfg->container_pid > 0) {
    /* Trust the PID we just got from the sync pipe.
     * We assume it's running because parent waited for boot marker. */
    pid = cfg->container_pid;
  } else {
    /* For other calls (e.g., info command), read and validate from pidfile. */
    is_container_running(cfg, &pid);
  }

  if (pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }

  /* Success - print Host and detailed Container info */
  if (cfg->format_output) {
    const char *host = is_android() ? "Android" : "Linux";
    const char *arch = get_architecture();
    printf("HOST_PLATFORM=%s\n", host);
    printf("HOST_ARCH=%s\n", arch);
    printf("CONTAINER_NAME=%s\n", cfg->container_name);
    printf("CONTAINER_PID=%d\n", pid);

    char pretty[256];
    char osr_path[PATH_MAX];
    if (build_proc_root_path(pid, "/etc/os-release", osr_path,
                             sizeof(osr_path)) == 0) {
      get_os_pretty(osr_path, pretty, sizeof(pretty));
      if (pretty[0])
        printf("CONTAINER_OS=%s\n", pretty);
    }

    if (cfg->hostname[0])
      printf("CONTAINER_HOSTNAME=%s\n", cfg->hostname);

    if (!trust_cfg_pid) {
      long uptime_sec = ds_get_container_uptime(pid);
      if (uptime_sec >= 0) {
        char uptime_str[128];
        ds_format_uptime(uptime_sec, uptime_str, sizeof(uptime_str));
        printf("CONTAINER_UPTIME=%s\n", uptime_str);
        printf("CONTAINER_UPTIME_SEC=%ld\n", uptime_sec);
      }
    }

    const char *net;
    switch (cfg->net_mode) {
    case DS_NET_NAT:
      net = "NAT";
      break;
    case DS_NET_NONE:
      net = "none";
      break;
    case DS_NET_GATEWAY:
      net = "gateway";
      break;
    default:
      net = "host";
      break;
    }
    printf("NETWORKING_MODE=%s\n", net);

    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0])
        printf("NAT_IP=%s\n", ip);
    } else if (cfg->net_mode == DS_NET_GATEWAY) {
      printf("GATEWAY_CONTAINER=%s\n", cfg->gateway_container);
      printf("GATEWAY_NET=%s\n",
             cfg->gateway_net[0] ? cfg->gateway_net : "lan");
      if (cfg->gateway_bridge[0])
        printf("GATEWAY_BRIDGE=%s\n", cfg->gateway_bridge);
      printf("GATEWAY_IFACE=%s\n",
             cfg->gateway_lan_ifname[0] ? cfg->gateway_lan_ifname : "eth1");
    }

    printf("DISABLE_IPV6=%d\n", cfg->disable_ipv6);
    if (is_android())
      printf("ANDROID_STORAGE=%d\n", cfg->android_storage);

    if (cfg->hw_access)
      printf("HW_ACCESS=full\n");
    else if (cfg->gpu_mode)
      printf("HW_ACCESS=GPU\n");
    else
      printf("HW_ACCESS=none\n");

    if (is_android()) {
      printf("TERMUX_X11=%d\n", cfg->termux_x11);
      if (cfg->tx11_extra_flags)
        printf("TX11_FLAGS=%s\n", cfg->tx11_extra_flags);
    }
    if (is_android()) {
      printf("VIRGL=%d\n", cfg->virgl);
      if (cfg->virgl_extra_flags)
        printf("VIRGL_FLAGS=%s\n", cfg->virgl_extra_flags);
    }
    if (is_android()) {
      printf("PULSEAUDIO=%d\n", cfg->pulseaudio);
    }

    if (access("/sys/fs/selinux/enforce", R_OK) == 0) {
      printf("SELINUX=%s\n",
             ds_get_selinux_status() == 0 ? "Permissive" : "Enforcing");
    }

    printf("VOLATILE_MODE=%d\n", cfg->volatile_mode);
    printf("FORCE_CGROUP_V1=%d\n", cfg->force_cgroupv1);
    printf("DEADLOCK_SHIELD=%d\n", cfg->block_nested_ns);
    printf("FOREGROUND_MODE=%d\n", cfg->foreground);

    printf("DNS_SERVERS=%s\n", cfg->dns_servers[0] ? cfg->dns_servers : "");

    printf("PORT_FORWARDS=");
    for (int i = 0; i < cfg->port_forward_count; i++) {
      struct ds_port_forward *pf = &cfg->port_forwards[i];
      if (pf->host_port_end == 0) {
        printf("%d:%d/%s", pf->host_port, pf->container_port, pf->proto);
      } else {
        printf("%d-%d:%d-%d/%s", pf->host_port, pf->host_port_end,
               pf->container_port, pf->container_port_end, pf->proto);
      }
      if (i < cfg->port_forward_count - 1)
        printf(",");
    }
    printf("\n");

    if (cfg->privileged_mask > 0) {
      printf("PRIVILEGED_MODE=");
      if (cfg->privileged_mask == DS_PRIV_FULL) {
        printf("full");
      } else {
        int first = 1;
        if (cfg->privileged_mask & DS_PRIV_NOMASK) {
          printf("%snomask", first ? "" : ",");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_NOCAPS) {
          printf("%snocaps", first ? "" : ",");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_NOSEC) {
          printf("%snoseccomp", first ? "" : ",");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_SHARED) {
          printf("%sshared", first ? "" : ",");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_UNFILTERED) {
          printf("%sunfiltered-dev", first ? "" : ",");
          first = 0;
        }
      }
      printf("\n");
    }

    printf("BIND_MOUNT_COUNT=%d\n", cfg->bind_count);
    printf("ENV_VAR_COUNT=%d\n", cfg->env_var_count);
    show_container_usage(cfg);
  } else {
    /* Human-readable output */
    const char *host = is_android() ? "Android" : "Linux";
    const char *arch = get_architecture();
    printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);

    printf("\n" C_GREEN "Container:" C_RESET " %s (RUNNING)\n",
           cfg->container_name);
    printf("  PID: %d\n", pid);

    char pretty[256];
    char osr_path[PATH_MAX];
    if (build_proc_root_path(pid, "/etc/os-release", osr_path,
                             sizeof(osr_path)) == 0) {
      get_os_pretty(osr_path, pretty, sizeof(pretty));
      if (pretty[0])
        printf("  OS: %s\n", pretty);
    }

    if (cfg->hostname[0])
      printf("  Hostname: %s\n", cfg->hostname);

    /* Uptime (only if called from info command) */
    if (!trust_cfg_pid) {
      long uptime_sec = ds_get_container_uptime(pid);
      if (uptime_sec >= 0) {
        char uptime_str[128];
        ds_format_uptime(uptime_sec, uptime_str, sizeof(uptime_str));
        printf("  Uptime: %s\n", uptime_str);
      }
    }

    printf("\n" C_GREEN "Features:" C_RESET "\n");
    int feat_count = 0;

    /* 1. Networking Mode */
    const char *net;
    switch (cfg->net_mode) {
    case DS_NET_NAT:
      net = "NAT";
      break;
    case DS_NET_NONE:
      net = "none";
      break;
    case DS_NET_GATEWAY:
      net = "gateway";
      break;
    default:
      net = "host";
      break;
    }
    printf("  Networking: %s\n", net);
    feat_count++;

    /* 2. NAT/Gateway Configuration */
    if (cfg->net_mode == DS_NET_GATEWAY) {
      printf("  Gateway: %s (%s)\n", cfg->gateway_container,
             cfg->gateway_net[0] ? cfg->gateway_net : "lan");
      feat_count++;
    }

    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0]) {
        printf("  NAT IP: %s\n", ip);
        feat_count++;
      }

      if (cfg->port_forward_count > 0) {
        printf("  Port forwards: ");
        for (int i = 0; i < cfg->port_forward_count; i++) {
          struct ds_port_forward *pf = &cfg->port_forwards[i];
          if (pf->host_port_end == 0) {
            printf("%d:%d", pf->host_port, pf->container_port);
          } else {
            printf("%d-%d:%d-%d", pf->host_port, pf->host_port_end,
                   pf->container_port, pf->container_port_end);
          }
          printf("/%s%s", pf->proto,
                 (i < cfg->port_forward_count - 1) ? ", " : "");
        }
        printf("\n");
        feat_count++;
      }
    }

    /* 3. DNS */
    if (cfg->dns_servers[0]) {
      printf("  DNS Servers: %s\n", cfg->dns_servers);
      feat_count++;
    }

    /* 4. IPv6 */
    if (cfg->disable_ipv6) {
      printf("  Disable IPv6: yes\n");
      feat_count++;
    }

    /* 5. Android Storage */
    if (is_android() && cfg->android_storage) {
      printf("  Android storage: enabled\n");
      feat_count++;
    }

    /* 6. HW/GPU Access */
    if (cfg->hw_access) {
      printf("  " C_RED "HW access:" C_RESET " full\n");
      feat_count++;
    } else if (cfg->gpu_mode) {
      printf("  HW access: GPU\n");
      feat_count++;
    }

    /* 7. Termux-X11 */
    if (is_android() && cfg->termux_x11) {
      printf("  Termux-X11: enabled\n");
      feat_count++;
    }

    /* 8. VirGL */
    if (is_android() && cfg->virgl) {
      printf("  VirGL: enabled\n");
      feat_count++;
    }

    /* 9. PulseAudio */
    if (is_android() && cfg->pulseaudio) {
      printf("  PulseAudio: enabled\n");
      feat_count++;
    }

    /* 10. SELinux Status */
    if (access("/sys/fs/selinux/enforce", R_OK) == 0) {
      int status = ds_get_selinux_status();
      if (status == 0) {
        printf("  " C_RED "SELinux:" C_RESET " Permissive\n");
      } else {
        printf("  SELinux: Enforcing\n");
      }
      feat_count++;
    }

    /* 11. Volatile Mode */
    if (cfg->volatile_mode) {
      printf("  Volatile mode: enabled\n");
      feat_count++;
    }

    /* 12. Cgroup v1 */
    if (cfg->force_cgroupv1) {
      printf("  " C_RED "Force Cgroup V1:" C_RESET " yes\n");
      feat_count++;
    }

    /* 13. Deadlock Shield (block_nested_ns) */
    if (cfg->block_nested_ns) {
      printf("  " C_RED "Deadlock Shield:" C_RESET " enabled\n");
      feat_count++;
    }

    /* 14. Privileged Mode */
    if (cfg->privileged_mask > 0) {
      printf("  " C_RED "Privileged mode:" C_RESET " ");
      if (cfg->privileged_mask == DS_PRIV_FULL) {
        printf("full");
      } else {
        int first = 1;
        if (cfg->privileged_mask & DS_PRIV_NOMASK) {
          printf("%snomask", first ? "" : ", ");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_NOCAPS) {
          printf("%snocaps", first ? "" : ", ");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_NOSEC) {
          printf("%snoseccomp", first ? "" : ", ");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_SHARED) {
          printf("%sshared", first ? "" : ", ");
          first = 0;
        }
        if (cfg->privileged_mask & DS_PRIV_UNFILTERED) {
          printf("%sunfiltered-dev", first ? "" : ", ");
          first = 0;
        }
      }
      printf("\n");
      feat_count++;
    }

    /* 15. Bind Mounts */
    if (cfg->bind_count > 0) {
      printf("  Bind mounts: %d active\n", cfg->bind_count);
      feat_count++;
    }

    /* 16. Custom Init */
    if (cfg->custom_init[0]) {
      printf("  " C_RED "Custom Init:" C_RESET " %s\n", cfg->custom_init);
      feat_count++;
    }

    /* 17. Environment Variables */
    if (cfg->env_var_count > 0) {
      printf("  Env variables: %d loaded\n", cfg->env_var_count);
      feat_count++;
    }

    if (feat_count == 0) {
      printf("  None\n");
    }
  }

  /* Resource limits & live usage. Only show if Cgroup V2 is active,
   * since we skip resource management entirely on V1. We also skip this
   * when called during the boot sequence (!trust_cfg_pid). */
  if (!trust_cfg_pid &&
      (cfg->memory_limit || cfg->cpu_quota || cfg->pids_limit) &&
      !cfg->force_cgroupv1 && ds_cgroup_host_is_v2()) {
    long long mu = -1, cu = -1, pu = -1;
    ds_cgroup_get_usage(cfg, &mu, &cu, &pu);
    printf("\n" C_GREEN "Resources:" C_RESET "\n");

    if (cfg->memory_limit) {
      char used[32] = "?", lim[32];
      if (mu >= 0)
        ds_format_size(mu, used, sizeof(used));
      ds_format_size(cfg->memory_limit, lim, sizeof(lim));
      printf("  Memory : %s / %s\n", used, lim);
    }
    if (cfg->cpu_quota) {
      long long period = cfg->cpu_period > 0 ? cfg->cpu_period : 100000;
      double cores = (double)cfg->cpu_quota / period;
      printf("  CPU    : %.2f cores", cores);
      if (cu >= 0) {
        long uptime = ds_get_container_uptime(pid);
        if (uptime > 0) {
          /* Average usage as percentage of total capacity (all allocated
           * cores). cu is in usec, uptime in sec. */
          double usage_sec = (double)cu / 1e6;
          double avg_util = (usage_sec / (double)uptime) / cores * 100.0;
          printf(" (Avg usage: %.1f%%)", avg_util);
        } else {
          printf(" (used: %.3fs)", (double)cu / 1e6);
        }
      }
      printf("\n");
    }
    if (cfg->pids_limit) {
      printf("  PIDs   : limit %lld", cfg->pids_limit);
      if (pu >= 0)
        printf(" (current: %lld)", pu);
      printf("\n");
    }
  }

  printf("\n");
  return 0;
}

int restart_rootfs_with_timeout(struct ds_config *cfg, int timeout_seconds) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }
  ds_log("Restarting container %s...", cfg->container_name);
  if (stop_rootfs_with_timeout(cfg, 1, timeout_seconds) < 0) {
    return -1;
  }
  /* The stop above tore down using the booted snapshot (loaded while the
   * container was alive). It is gone now, so reloading by name returns the
   * workspace copy - reload it so host-side container.config edits made while
   * it ran take effect on this restart. start_rootfs re-derives the preserved
   * mount from the on-disk .mount sidecar, so losing the snapshot paths is
   * fine. */
  free_config_binds(cfg);
  ds_config_load_by_name(cfg->container_name, cfg);
  putchar('\n');
  print_ds_banner();
  return start_rootfs(cfg);
}

int restart_rootfs(struct ds_config *cfg) {
  return restart_rootfs_with_timeout(cfg, DS_STOP_TIMEOUT);
}
