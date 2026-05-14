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

static void cleanup_container_resources(struct ds_config *cfg, pid_t pid,
                                        int skip_unmount, int force_cleanup) {
  /* Flush filesystem buffers (skip if force cleanup - sync can hang on
   * zombie-held fs) */
  if (!force_cleanup)
    sync();

  if (is_android() && !skip_unmount) {
    if (count_running_containers(NULL, 0) == 0) {
      android_optimizations(0);
      cleanup_unified_tmpfs();
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
    if (cfg->pidfile[0])
      unlink(cfg->pidfile);
    if (global_pidfile[0] && strcmp(cfg->pidfile, global_pidfile) != 0)
      unlink(global_pidfile);

    /* Stale lock cleanup is handled by acquire_external_lock and
     * is_external_lock_active. Monitor only does resource cleanup
     * if no external lock is active. */
  }

  /* Network cleanup: remove host veth and iptables rules */
  if (cfg->net_mode == DS_NET_NAT) {
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
  print_ds_banner();
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

  /* 2a. Verify /sbin/init exists before any side effects (NAT, config save).
   * For rootfs.img mode the image is now mounted; for directory mode the
   * rootfs_path is already set.  Either way we have a valid host path. */
  {
    char init_path[PATH_MAX];
    char rootfs_norm[PATH_MAX];
    if (cfg->is_img_mount && cfg->img_mount_point[0])
      safe_strncpy(rootfs_norm, cfg->img_mount_point, sizeof(rootfs_norm));
    else
      safe_strncpy(rootfs_norm, cfg->rootfs_path, sizeof(rootfs_norm));
    size_t rlen = strlen(rootfs_norm);
    if (rlen > 0 && rootfs_norm[rlen - 1] == '/')
      rootfs_norm[rlen - 1] = '\0';

    snprintf(init_path, sizeof(init_path), "%.4080s/sbin/init", rootfs_norm);
    struct stat st;
    if (lstat(init_path, &st) != 0) {
      ds_error("Init binary not found: %s", init_path);
      ds_error(
          "Please ensure the rootfs path is correct and contains /sbin/init.");
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
  }

  /* 2b. Android Termux Bridge Preparation - only if flag is set */
  if (is_android() && cfg->termux_x11) {
    stop_termux_if_running();
    setup_unified_tmpfs();
  }

  /* 3. Early pre-flight for volatile mode (before any host changes) */
  if (check_volatile_mode(cfg) < 0) {
    goto cleanup;
  }

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

  cfg->tty_count = DS_MAX_TTYS;
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

  for (int i = 0; i < cfg->tty_count; i++) {
    if (ds_terminal_create(&cfg->ttys[i]) < 0)
      break;
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

  /* 8. Fork Monitor Process */
  pid_t monitor_pid = fork();
  if (monitor_pid < 0) {
    close(sync_pipe[0]);
    close(sync_pipe[1]);
    ds_error("fork failed: %s", strerror(errno));
    goto cleanup;
  }

  if (monitor_pid == 0) {
    /* MONITOR PROCESS */
    close(sync_pipe[0]);
    sync_pipe[0] = -1;
    if (setsid() < 0 && errno != EPERM) {
      /* Fatal only if it's not EPERM (which means already leader) */
      ds_error("setsid failed: %s", strerror(errno));
      _exit(EXIT_FAILURE);
    }

    /* Monitor Hardening
     * Ignore common termination signals to prevent Android's process manager
     * from ending the supervisor prematurely. Monitor must only die via
     * SIGKILL or successful container exit. */
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    prctl(PR_SET_NAME, "[ds-monitor]", 0, 0, 0);

    /* Unshare namespaces - Monitor enters new UTS, IPC, and optionally Cgroup
     * namespaces immediately. PID namespace is NOT unshared here because
     * unshare(CLONE_NEWPID) can only be called once per process. Instead,
     * each boot/reboot cycle forks an intermediate that creates a fresh
     * PID namespace. */
    int ns_flags = CLONE_NEWUTS | CLONE_NEWIPC;

    /* Adaptive Cgroup Namespace (introduced in Linux 4.6).
     *
     * CGROUP SELECTION: Only enable cgroupns when V2 is active.
     * If --force-cgroupv1 is set, we skip cgroupns so setup_cgroups()
     * has full rights to create named V1 hierarchies from the host context. */
    int cg_ns_ok = (access("/proc/self/ns/cgroup", F_OK) == 0) &&
                   (ds_cgroup_host_is_v2() && !cfg->force_cgroupv1);
    if (cg_ns_ok) {
      /* To get isolation from a cgroup namespace, we must be in a sub-cgroup
       * BEFORE we unshare. If we are in the root '/', the namespace root
       * will be the host's root, providing zero isolation.
       * We use a container-specific path to avoid conflicts. */
      if (access("/sys/fs/cgroup/cgroup.procs", F_OK) == 0) {
        char safe_name[256];
        sanitize_container_name(cfg->container_name, safe_name,
                                sizeof(safe_name));
        char cg_path[PATH_MAX];
        snprintf(cg_path, sizeof(cg_path), "/sys/fs/cgroup/droidspaces/%s",
                 safe_name);
        mkdir_p(cg_path, 0755);

        char cg_procs[PATH_MAX];
        safe_strncpy(cg_procs, cg_path, sizeof(cg_procs));
        strncat(cg_procs, "/cgroup.procs",
                sizeof(cg_procs) - strlen(cg_procs) - 1);
        FILE *f = fopen(cg_procs, "we");
        if (f) {
          fprintf(f, "%d\n", getpid());
          fclose(f);
        }
      }
      ns_flags |= CLONE_NEWCGROUP;
    } else {
      /* Legacy kernel without force flag - skip cgroupns, run in host
       * cgroupns with full rights so setup_cgroups() can create named
       * v1 hierarchies. */
    }

    if (unshare(ns_flags) < 0)
      ds_die("unshare failed: %s", strerror(errno));

    int stdio_redirected = 0;

    /* Reboot-aware boot loop
     * Each iteration forks an intermediate child that creates a fresh PID
     * namespace (unshare(CLONE_NEWPID)) and then forks the container init.
     *
     * Reboot detection uses EXIT CODES ONLY (no signal interception):
     *   1. Init calls reboot(2) → kernel kills init with SIGHUP
     *   2. Intermediate sees WTERMSIG(init)==SIGHUP via waitpid()
     *   3. Intermediate exits with DS_REBOOT_EXIT (249)
     *   4. Monitor sees WEXITSTATUS(mid)==249 → loop back
     *
     * This eliminates ghost containers because the Monitor never handles
     * SIGHUP - it only checks a deterministic exit code. */
  reboot_loop:;
    /* Close existing pipes from previous cycle to prevent FD leaks */
    if (cfg->net_ready_pipe[0] >= 0) {
      close(cfg->net_ready_pipe[0]);
      close(cfg->net_ready_pipe[1]);
      cfg->net_ready_pipe[0] = cfg->net_ready_pipe[1] = -1;
    }
    if (cfg->net_done_pipe[0] >= 0) {
      close(cfg->net_done_pipe[0]);
      close(cfg->net_done_pipe[1]);
      cfg->net_done_pipe[0] = cfg->net_done_pipe[1] = -1;
    }

    /* Networking pipes (created fresh for every boot cycle) */
    int mid_sync_pipe[2] = {-1, -1};
    if (cfg->net_mode != DS_NET_HOST) {
      if (pipe(cfg->net_ready_pipe) < 0 || pipe(cfg->net_done_pipe) < 0 ||
          pipe(mid_sync_pipe) < 0) {
        ds_error("Failed to create NAT sync pipes: %s", strerror(errno));
        _exit(EXIT_FAILURE);
      }

      /* Set FD_CLOEXEC on all new pipe ends */
      fcntl(cfg->net_ready_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl(cfg->net_ready_pipe[1], F_SETFD, FD_CLOEXEC);
      fcntl(cfg->net_done_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl(cfg->net_done_pipe[1], F_SETFD, FD_CLOEXEC);
      fcntl(mid_sync_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl(mid_sync_pipe[1], F_SETFD, FD_CLOEXEC);

      ds_log("[NET] Sync pipes created for net_mode=%d", cfg->net_mode);
    }

    /* First boot only: ensure no stale container with the same name is running
     */
    if (!cfg->reboot_cycle) {
      pid_t existing_pid = 0;
      if (is_container_running(cfg, &existing_pid)) {
        if (existing_pid != getpid()) {
          /*
           * Crucial Safety: Only kill the process if it's confirmed to be a
           * Droidspaces container. This prevents killing random processes that
           * might have recycled the PID after the container died without
           * cleanup.
           */
          if (is_valid_container_pid(existing_pid)) {
            ds_warn("Killing stale container with same name (PID %d)",
                    existing_pid);
            kill(existing_pid, SIGKILL);
            usleep(100000);
          }
        }
      }
    }

    /* Stdio handling for monitor in background mode (early redirection).
     * We must do this BEFORE forking the intermediate process, otherwise
     * the intermediate inherits the user's stdout/stderr (e.g. a pipe)
     * and holds it open indefinitely, causing CLI hangs in direct mode.
     * We only keep it open if we haven't reached the networking setup yet,
     * as setup_veth_host_side() might still need to print logs. */
    if (!cfg->foreground && !stdio_redirected) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 0);
        /* Note: we don't redirect 1 and 2 here yet because we want to see
         * networking setup logs. We'll do a full redirect after the fork. */
        close(devnull);
      }
    }

    pid_t mid_pid = fork();
    if (mid_pid < 0)
      _exit(EXIT_FAILURE);

    if (mid_pid == 0) {
      /* INTERMEDIATE PROCESS
       * Create a fresh PID namespace (and NET namespace for NAT/none modes)
       * for this boot cycle. */
      int clone_flags = CLONE_NEWPID;
      if (cfg->net_mode != DS_NET_HOST)
        clone_flags |= CLONE_NEWNET;

      if (unshare(clone_flags) < 0) {
        ds_error("unshare(PID|NET) failed: %s", strerror(errno));
        _exit(EXIT_FAILURE);
      }

      pid_t init_pid = fork();
      if (init_pid < 0)
        _exit(EXIT_FAILURE);

      if (init_pid == 0) {
        /* CONTAINER INIT (PID 1 inside namespace) */
        /* Close pipe ends the init process doesn't use */
        if (cfg->net_mode != DS_NET_HOST) {
          if (mid_sync_pipe[0] >= 0)
            close(mid_sync_pipe[0]);
          if (mid_sync_pipe[1] >= 0)
            close(mid_sync_pipe[1]);
        }
        close(sync_pipe[1]);
        _exit(internal_boot(cfg));
      }

      /* Intermediate: redirect stdio to /dev/null NOW (after forking init).
       * It only exists to wait for init and has no business talking to the
       * user's terminal or holding pipes open.
       *
       * BUG FIX: this redirect was previously placed BEFORE the fork(), which
       * caused init_pid to inherit /dev/null for fd 1 and fd 2. Every
       * ds_log() call inside internal_boot() writes to stdout, so all boot
       * logs were silently swallowed by /dev/null - visible only in the log
       * file (which uses direct file I/O, not stdout). Moving the redirect
       * here means only the intermediate itself goes silent; internal_boot()
       * retains the original terminal fds until it redirects to /dev/console
       * at its own step 24. */
      if (!cfg->foreground) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
          dup2(devnull, 0);
          dup2(devnull, 1);
          dup2(devnull, 2);
          close(devnull);
        }
      }

      /* Send init PID to monitor so it can target /proc/<pid>/ns/net */
      if (cfg->net_mode != DS_NET_HOST && mid_sync_pipe[1] >= 0) {
        if (write(mid_sync_pipe[1], &init_pid, sizeof(pid_t)) !=
            sizeof(pid_t)) {
          ds_warn(
              "[NET] Intermediate: failed to write init_pid to mid_sync_pipe");
        }
        close(mid_sync_pipe[1]);
        close(mid_sync_pipe[0]);
        mid_sync_pipe[0] = mid_sync_pipe[1] = -1;
      }

      /* Send init PID to parent via sync pipe (first boot only) */
      if (sync_pipe[1] >= 0) {
        if (write(sync_pipe[1], &init_pid, sizeof(pid_t)) != sizeof(pid_t)) {
          /* Reader will detect failure or handle empty/partial read */
        }
        close(sync_pipe[1]);
        sync_pipe[1] = -1;
      } else {
        /* Reboot cycle - update PID file directly so
         * 'droidspaces show/status' report the correct PID. */
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", init_pid);
        write_file_atomic(cfg->pidfile, pid_str);

        char global_pf[PATH_MAX];
        resolve_pidfile_from_name(cfg->container_name, global_pf,
                                  sizeof(global_pf));
        if (strcmp(cfg->pidfile, global_pf) != 0)
          write_file_atomic(global_pf, pid_str);
      }

      /* Wait for init to exit */
      int init_status;
      while (waitpid(init_pid, &init_status, 0) < 0 && errno == EINTR)
        ;

      /* Convert kernel signal to exit code:
       * SIGHUP from reboot(RESTART) → DS_REBOOT_EXIT (249)
       * Everything else → pass through as-is */
      if (WIFSIGNALED(init_status) && WTERMSIG(init_status) == SIGHUP) {
        _exit(DS_REBOOT_EXIT);
      }

      _exit(WIFEXITED(init_status) ? WEXITSTATUS(init_status) : EXIT_FAILURE);
    }

    /* MONITOR continues here */

    /* Close sync pipe write end (intermediate handles it) */
    if (sync_pipe[1] >= 0) {
      close(sync_pipe[1]);
      sync_pipe[1] = -1;
    }

    /* Monitor: NAT networking handshake
     *
     * Sequence (all non-blocking after pipes are ready):
     *   1. Read init_pid from mid_sync_pipe[0]
     *   2. Read "ready" byte from net_ready_pipe[0]  (init sent it)
     *   3. Call setup_veth_host_side → creates bridge/veth/rules
     *   4. Write ds_net_handshake to net_done_pipe[1] (init reads it)
     *
     * This handshake ensures the veth peer is moved into the container's
     * netns while the init process is alive and waiting, avoiding the race
     * where we try to open /proc/<pid>/ns/net before the process exists. */
    if (cfg->net_mode != DS_NET_HOST && mid_sync_pipe[0] >= 0) {
      close(mid_sync_pipe[1]); /* monitor is reader */

      pid_t netns_pid = -1;
      ssize_t nr = read(mid_sync_pipe[0], &netns_pid, sizeof(pid_t));
      close(mid_sync_pipe[0]);

      if (nr != sizeof(pid_t) || netns_pid <= 0) {
        ds_warn("[NET] Monitor: failed to read init_pid from mid_sync_pipe "
                "(nr=%zd pid=%d)",
                nr, (int)netns_pid);
      } else {
        ds_log("[NET] Monitor: received init_pid=%d, waiting for READY...",
               (int)netns_pid);
        cfg->container_pid = netns_pid;

        /* Close the ends we don't need */
        close(cfg->net_ready_pipe[1]); /* monitor reads, init writes */
        close(cfg->net_done_pipe[0]);  /* monitor writes, init reads  */

        char rdy;
        if (read(cfg->net_ready_pipe[0], &rdy, 1) < 0) {
          ds_warn("[NET] Monitor: failed to read READY signal: %s",
                  strerror(errno));
        } else {
          ds_log("[NET] Monitor: READY received from init (pid=%d)",
                 (int)netns_pid);
        }
        close(cfg->net_ready_pipe[0]);

        if (cfg->net_mode == DS_NET_NAT) {
          if (setup_veth_host_side(cfg, netns_pid) < 0) {
            ds_warn("[NET] Monitor: setup_veth_host_side failed - "
                    "container will have no internet");
          } else {
            /* Start the dynamic route monitor thread to handle WiFi/Mobile
             * switches */
            ds_net_start_route_monitor();
          }
        }

        /* Send handshake to init */
        struct ds_net_handshake hs;
        ds_net_derive_handshake(netns_pid, cfg, &hs);
        ds_log("[NET] Monitor: sending DONE: peer=%s ip=%s", hs.peer_name,
               hs.ip_str);
        if (write(cfg->net_done_pipe[1], &hs, sizeof(hs)) !=
            (ssize_t)sizeof(hs))
          ds_warn("[NET] Monitor: failed to write handshake to init");
        close(cfg->net_done_pipe[1]);
      }
    }

    /* Ensure monitor is not sitting inside any mount point */
    if (chdir("/") < 0) {
      ds_warn("Failed to chdir to /: %s", strerror(errno));
    }

    /* Stdio handling for monitor in background mode (first boot only) */
    if (!cfg->foreground && !stdio_redirected) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(devnull);
      }
      stdio_redirected = 1;
    }

    /* MONITOR waits for intermediate to complete */

    /* CRITICAL TIMING: Close sync pipe write end ONLY after intermediate
     * finishes. This ensures intermediate can write init PID to parent on first
     * boot. Closing too early causes parent's read() to return EOF, triggering
     * cleanup that deletes the PID file while container is still booting. See
     * commit 6f9f99a for details on the boot-at-boot race this prevents. */
    if (sync_pipe[1] >= 0) {
      close(sync_pipe[1]);
      sync_pipe[1] = -1;
    }

    int status;
    while (waitpid(mid_pid, &status, 0) < 0 && errno == EINTR)
      ;

    /* Log what monitor saw */
    if (WIFEXITED(status)) {
      int code = WEXITSTATUS(status);
      if (code == DS_REBOOT_EXIT) {
        write_monitor_debug_log(cfg->container_name,
                                "Detected internal REBOOT");
      } else {
        write_monitor_debug_log(cfg->container_name,
                                "Detected container SHUTDOWN (exit: %d)", code);
      }
    } else if (WIFSIGNALED(status)) {
      write_monitor_debug_log(cfg->container_name,
                              "Intermediate killed by signal: %d (%s)",
                              WTERMSIG(status), strsignal(WTERMSIG(status)));
    }

    /* Reboot detection (internal reboot) */
    if (WIFEXITED(status) && WEXITSTATUS(status) == DS_REBOOT_EXIT) {
      /* Check for external lock - if exists, abort reboot and let CLI handle it
       */
      if (is_external_lock_active(cfg->container_name)) {
        write_monitor_debug_log(
            cfg->container_name,
            "External command lock detected - aborting internal reboot");
        goto monitor_cleanup_and_exit;
      }

      if (cfg->foreground) {
        printf("\n" C_WHITE "Droidspaces v%s : Container " C_GREEN
               "%s" C_RESET C_WHITE " is now Rebooting...." C_RESET "\n",
               DS_VERSION, cfg->container_name);
        fflush(stdout);
      }

      /* Synchronize container_pid in Monitor */
      pid_t new_pid = -1;
      if (read_and_validate_pid(cfg->pidfile, &new_pid) == 0) {
        cfg->container_pid = new_pid;
      }

      /* Re-write the same UUID to sync file for the next boot cycle.
       * internal_boot reads this across the pivot_root boundary. */
      if (!cfg->volatile_mode && cfg->rootfs_path[0]) {
        char uuid_sync[PATH_MAX];
        snprintf(uuid_sync, sizeof(uuid_sync), "%.4060s/.droidspaces-uuid",
                 cfg->rootfs_path);
        write_file(uuid_sync, cfg->uuid);
      }

      /* Reload from workspace (canonical path the user edits) */
      {
        free_config_binds(cfg);
        /* Preserve env_vars across the reboot */
        struct ds_env_var *saved_vars = cfg->env_vars;
        int saved_count = cfg->env_var_count;
        int saved_cap = cfg->env_var_capacity;
        int old_force_cgv1 = cfg->force_cgroupv1;

        struct ds_config reboot_cfg = *cfg;
        if (ds_config_load_by_name(cfg->container_name, &reboot_cfg) == 0) {
          reboot_cfg.env_vars = saved_vars;
          reboot_cfg.env_var_count = saved_count;
          reboot_cfg.env_var_capacity = saved_cap;
          if (strcmp(cfg->dns_servers, reboot_cfg.dns_servers) != 0) {
            reboot_cfg.dns_server_content[0] = '\0';
            ds_get_dns_servers(reboot_cfg.dns_servers,
                               reboot_cfg.dns_server_content,
                               sizeof(reboot_cfg.dns_server_content));
          }
          /* Cgroup namespace is locked at monitor startup - can't change */
          if (reboot_cfg.force_cgroupv1 != old_force_cgv1) {
            printf("\n" C_BOLD C_YELLOW "force_cgroupv1 changed but "
                   "requires a full stop/start to take effect" C_RESET "\n");
            reboot_cfg.force_cgroupv1 = old_force_cgv1;
          }
          *cfg = reboot_cfg;
          /* Restore mount point for img-based containers */
          if (cfg->is_img_mount && cfg->img_mount_point[0]) {
            safe_strncpy(cfg->rootfs_path, cfg->img_mount_point,
                         sizeof(cfg->rootfs_path));
          }
        }
      }

      cfg->reboot_cycle = 1;
      if (cfg->foreground)
        ds_log_silent = 1;

      /* ds_dhcp_server_start() memsets g_dhcp under g_dhcp_lock on the next
       * cycle, racing the still-running dhcp_server_loop thread that reads from
       * the same memory.  The DHCP thread is intentionally joinable so stop()
       * can join before memset. */
      ds_dhcp_server_stop();

      goto reboot_loop;
    }

    /* Not a reboot - check if external command is handling cleanup */
    if (is_external_lock_active(cfg->container_name)) {
      write_monitor_debug_log(cfg->container_name,
                              "External command lock detected - yielding "
                              "cleanup to CLI");
      goto monitor_cleanup_and_exit;
    }

    /* Normal exit - monitor does cleanup */
    write_monitor_debug_log(cfg->container_name, "Monitor performing cleanup");

    /* Before cleaning up the container's cgroup subtree, move the
     * monitor process itself back to the root cgroup.  The monitor wrote its
     * own PID into /sys/fs/cgroup/droidspaces/<name>/ at start (for cgroup
     * namespace isolation).  If it is still in that cgroup when
     * ds_cgroup_cleanup_container() calls rmdir, the kernel sees a non-empty
     * cgroup and returns EBUSY - the directory is never removed.
     *
     * Writing our PID to the root cgroup.procs atomically migrates us out.
     * This is safe: the monitor is about to _exit() anyway. */
    {
      int root_fd = open("/sys/fs/cgroup/cgroup.procs", O_WRONLY | O_CLOEXEC);
      if (root_fd >= 0) {
        char pid_s[32];
        int len = snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
        if (write(root_fd, pid_s, len) < 0) {
        }
        close(root_fd);
      }
    }

    cleanup_container_resources(cfg, 0, 0, 0);

  monitor_cleanup_and_exit:
    /* Free dynamically allocated configuration members before exit */
    free_config_binds(cfg);
    free_config_env_vars(cfg);
    free_config_unknown_lines(cfg);
    _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
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
      free_config_binds(cfg);
      free_config_env_vars(cfg);
      free_config_unknown_lines(cfg);
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

  if (lock_acquired)
    release_external_lock(cfg->container_name);
  free_config_binds(cfg);
  free_config_env_vars(cfg);
  free_config_unknown_lines(cfg);

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
  for (int i = 0; i < cfg->tty_count; i++) {
    if (cfg->ttys[i].master >= 0) {
      close(cfg->ttys[i].master);
      cfg->ttys[i].master = -1;
    }
  }
  if (sync_pipe[0] >= 0)
    close(sync_pipe[0]);
  if (sync_pipe[1] >= 0)
    close(sync_pipe[1]);

  free_config_binds(cfg);
  free_config_env_vars(cfg);
  free_config_unknown_lines(cfg);
  return -1;
}

int stop_rootfs(struct ds_config *cfg, int skip_unmount) {
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

  /* 1. Detect init system and send the correct shutdown signal. */
  ds_init_type_t init_type = DS_INIT_UNKNOWN;
  const char *probe_root =
      cfg->img_mount_point[0] ? cfg->img_mount_point : cfg->rootfs_path;
  if (probe_root[0])
    init_type = detect_container_init(probe_root);

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
         DS_STOP_TIMEOUT);

  /* 2. Wait for exit */
  int stopped = 0;
  for (int i = 0; i < DS_STOP_TIMEOUT * 5; i++) {
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

  if (!cfg->foreground)
    ds_log("Container '%s' stopped.", cfg->container_name);

  /* Release lock ONLY if this is a final stop.
   * For restarts (skip_unmount=1), keep lock alive as handoff. */
  if (!skip_unmount) {
    release_external_lock(cfg->container_name);
  }

  return 0;
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
    ds_seccomp_apply_minimal(cfg->hw_access, cfg->privileged_mask);
    android_seccomp_setup(0, cfg->block_nested_ns &&
                                 !(cfg->privileged_mask & DS_PRIV_NOSEC));
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
    ds_seccomp_apply_minimal(cfg->hw_access, cfg->privileged_mask);
    android_seccomp_setup(0, cfg->block_nested_ns &&
                                 !(cfg->privileged_mask & DS_PRIV_NOSEC));
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
      show_containers();
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

      if (cfg->upstream_iface_count > 0) {
        printf("UPSTREAM_INTERFACES=");
        for (int i = 0; i < cfg->upstream_iface_count; i++) {
          printf("%s%s", cfg->upstream_ifaces[i],
                 (i < cfg->upstream_iface_count - 1) ? "," : "");
        }
        printf("\n");
      }
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

    if (is_android())
      printf("TERMUX_X11=%d\n", cfg->termux_x11);

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
    default:
      net = "host";
      break;
    }
    printf("  Networking: %s\n", net);
    feat_count++;

    /* 2. NAT Configuration (IP, Upstream, Ports) */
    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0]) {
        printf("  NAT IP: %s\n", ip);
        feat_count++;
      }

      if (cfg->upstream_iface_count > 0) {
        printf("  Upstream ifaces: ");
        for (int i = 0; i < cfg->upstream_iface_count; i++) {
          printf("%s%s", cfg->upstream_ifaces[i],
                 (i < cfg->upstream_iface_count - 1) ? ", " : "");
        }
        printf("\n");
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

    /* 8. SELinux Status */
    if (access("/sys/fs/selinux/enforce", R_OK) == 0) {
      int status = ds_get_selinux_status();
      if (status == 0) {
        printf("  " C_RED "SELinux:" C_RESET " Permissive\n");
      } else {
        printf("  SELinux: Enforcing\n");
      }
      feat_count++;
    }

    /* 9. Volatile Mode */
    if (cfg->volatile_mode) {
      printf("  Volatile mode: enabled\n");
      feat_count++;
    }

    /* 10. Cgroup v1 */
    if (cfg->force_cgroupv1) {
      printf("  " C_RED "Force Cgroup V1:" C_RESET " yes\n");
      feat_count++;
    }

    /* 11. Deadlock Shield (block_nested_ns) */
    if (cfg->block_nested_ns) {
      printf("  " C_RED "Deadlock Shield:" C_RESET " enabled\n");
      feat_count++;
    }

    /* 12. Privileged Mode */
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

    /* 13. Bind Mounts */
    if (cfg->bind_count > 0) {
      printf("  Bind mounts: %d active\n", cfg->bind_count);
      feat_count++;
    }

    /* 14. Environment Variables */
    if (cfg->env_var_count > 0) {
      printf("  Env variables: %d loaded\n", cfg->env_var_count);
      feat_count++;
    }

    if (feat_count == 0) {
      printf("  None\n");
    }
  }

  printf("\n");
  return 0;
}

int restart_rootfs(struct ds_config *cfg) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }
  ds_log("Restarting container %s...", cfg->container_name);
  if (stop_rootfs(cfg, 1) < 0) {
    return -1;
  }
  putchar('\n');
  return start_rootfs(cfg);
}
