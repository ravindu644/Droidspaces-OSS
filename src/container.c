/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * External Command Lock — CLI-only ownership
 *
 * The lock represents exactly ONE thing: an external CLI command is actively
 * managing this container. ONLY the CLI parent creates/removes locks.
 * The monitor is READ-ONLY for locks.
 * ---------------------------------------------------------------------------*/

/* Build lock path with defensive truncation.
 * Precision: 2048 (pids_dir) + 256 (name) + 5 (.lock) = 2309 < PATH_MAX (4096)
 * This prevents format-truncation warnings while ensuring paths never overflow.
 */
static void get_lock_path(const char *name, char *buf, size_t size) {
  snprintf(buf, size, "%.2048s/%.256s" DS_EXT_LOCK, get_pids_dir(), name);
}

/* Create external command lock — ONLY called by CLI parent.
 * Returns: 0 on success, -1 if lock already held by a live process. */
static int acquire_external_lock(const char *name) {
  char lock_path[PATH_MAX];
  get_lock_path(name, lock_path, sizeof(lock_path));

  /* Check if lock already exists */
  if (access(lock_path, F_OK) == 0) {
    /* Lock exists — verify if holder is still alive */
    char buf[32];
    if (read_file(lock_path, buf, sizeof(buf)) > 0) {
      pid_t holder = (pid_t)atoi(buf);
      if (holder > 0 && holder != getpid() && kill(holder, 0) == 0) {
        /* Lock holder is alive and NOT us — cannot acquire */
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

/* Release external command lock — ONLY called by CLI parent.
 * Verifies ownership before removing. */
static void release_external_lock(const char *name) {
  char lock_path[PATH_MAX];
  get_lock_path(name, lock_path, sizeof(lock_path));

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

/* Mirrors ContainerManager.sanitizeContainerName() in the Android app.
 * Replaces spaces with dashes so directory names are consistent. */
static void sanitize_container_name(const char *name, char *out, size_t size) {
  size_t i;
  for (i = 0; i < size - 1 && name[i] != '\0'; i++)
    out[i] = (name[i] == ' ') ? '-' : name[i];
  out[i] = '\0';
}

int load_config_with_recovery(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;

  ensure_workspace();

  /* If config was already explicitly loaded (e.g. via --conf=), skip
   * reload entirely — CLI overrides are already applied on top of it.
   * Just ensure pidfile is resolved if missing. */
  if (cfg->config_file_existed && cfg->config_file[0] != '\0') {
    if (cfg->pidfile[0] == '\0' && cfg->container_name[0] != '\0')
      resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                                sizeof(cfg->pidfile));
    return 0;
  }

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  /* Step 2: Try to resolve host-side config path if not already set */
  if (cfg->config_file[0] == '\0') {
    snprintf(cfg->config_file, sizeof(cfg->config_file),
             "%s/Containers/%s/container.config", get_workspace_dir(),
             safe_name);
  }

  /* Step 3: Try to load from host workspace */
  if (ds_config_load(cfg->config_file, cfg) == 0 && cfg->config_file_existed) {
    if (cfg->container_name[0] && strcmp(cfg->container_name, name) != 0) {
      /* name mismatch — fall through */
    } else {
      if (!cfg->container_name[0])
        safe_strncpy(cfg->container_name, name, sizeof(cfg->container_name));
      return 0;
    }
  }

  /* Step 3.5: Workspace config missing, but the pidfile may point us directly
   * to a live PID — avoids full /proc scan for containers started with an
   * external --conf= path where no workspace config was ever written. */
  char direct_pidfile[PATH_MAX];
  resolve_pidfile_from_name(safe_name, direct_pidfile, sizeof(direct_pidfile));

  pid_t direct_pid = 0;
  if (read_and_validate_pid(direct_pidfile, &direct_pid) == 0 &&
      direct_pid > 0) {
    char internal_config[PATH_MAX];
    build_proc_root_path(direct_pid, "/run/droidspaces/container.config",
                         internal_config, sizeof(internal_config));

    if (ds_config_load(internal_config, cfg) == 0) {
      ds_log("Recovering metadata for '%s' from internal backup (PID %d).",
             name, direct_pid);
      char container_dir[PATH_MAX];
      snprintf(container_dir, sizeof(container_dir), "%s/Containers/%s",
               get_workspace_dir(), safe_name);
      mkdir_p(container_dir, 0755);
      ds_config_save(cfg->config_file, cfg);
    } else {
      /* No internal backup (pre-marker container) — populate minimal cfg */
      ds_log("No internal backup for '%s' — using minimal config (PID %d).",
             name, direct_pid);
      safe_strncpy(cfg->container_name, name, sizeof(cfg->container_name));
      safe_strncpy(cfg->pidfile, direct_pidfile, sizeof(cfg->pidfile));
    }
    return 0;
  }

  /* Step 4: No workspace config and no pidfile — full /proc scan last resort */
  ds_log("Metadata for '" C_BOLD "%s" C_RESET "' is missing. Scanning /proc...",
         name);
  pid_t pid = find_container_by_name(name);
  if (pid <= 0) {
    ds_error("Container '%s' not found on system.", name);
    return -1;
  }

  char internal_config[PATH_MAX];
  build_proc_root_path(pid, "/run/droidspaces/container.config",
                       internal_config, sizeof(internal_config));

  if (ds_config_load(internal_config, cfg) < 0) {
    ds_error("Failed to load internal backup config for '%s'.", name);
    return -1;
  }

  if (strcmp(cfg->container_name, name) != 0) {
    ds_error(
        "Discovery mismatch: expected '%s', found '%s'. Aborting recovery.",
        name, cfg->container_name);
    return -1;
  }

  ds_log("Found running container '" C_BOLD "%s" C_RESET "' (PID %d).", name,
         pid);
  ds_log("Restoring host-side metadata...");

  char container_dir[PATH_MAX];
  snprintf(container_dir, sizeof(container_dir), "%s/Containers/%s",
           get_workspace_dir(), safe_name);
  mkdir_p(container_dir, 0755);
  ds_config_save(cfg->config_file, cfg);

  resolve_pidfile_from_name(name, cfg->pidfile, sizeof(cfg->pidfile));
  char pid_str[32];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);
  write_file(cfg->pidfile, pid_str);

  ds_log("Metadata restored successfully. Continuing...");
  return 0;
}

/* Check if external command lock exists — called by monitor (READ ONLY).
 * Returns: 1 if lock exists and holder is alive, 0 otherwise. */
int is_external_lock_active(const char *name) {
  char lock_path[PATH_MAX];
  get_lock_path(name, lock_path, sizeof(lock_path));

  if (access(lock_path, F_OK) != 0)
    return 0; /* No lock */

  /* Lock exists — verify holder is alive */
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
  /* Flush filesystem buffers (skip if force cleanup — sync can hang on
   * zombie-held fs) */
  if (!force_cleanup)
    sync();

  if (is_android() && !skip_unmount && count_running_containers(NULL, 0) == 0) {
    android_optimizations(0);
    cleanup_unified_tmpfs();
  }

  /* 1. Cleanup firmware path (skip when force — accessing zombie rootfs hangs)
   */
  if (!force_cleanup) {
    if (cfg->rootfs_path[0]) {
      firmware_path_remove_rootfs(cfg->rootfs_path);
    } else if (pid > 0) {
      char rootfs[PATH_MAX];
      char root_link[PATH_MAX];
      snprintf(root_link, sizeof(root_link), "/proc/%d/root", pid);
      ssize_t rlen = readlink(root_link, rootfs, sizeof(rootfs) - 1);
      if (rlen > 0) {
        rootfs[rlen] = '\0'; /* readlink does NOT null-terminate */
        firmware_path_remove_rootfs(rootfs);
      }
    }
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
}

int is_valid_container_pid(pid_t pid) {
  char path[PATH_MAX];
  char buf[256];

  /* Primary marker: /run/droidspaces must exist inside the container.
   * This is the one authoritative marker written by droidspaces on boot.
   * We do NOT require /run/systemd/container — Alpine/runit/openrc never
   * write that file, causing scan to be blind to non-systemd distros. */
  if (build_proc_root_path(pid, DS_DROIDSPACES_MARKER, path, sizeof(path)) < 0)
    return 0;
  if (access(path, F_OK) != 0)
    return 0;

  /* Secondary check: cmdline must contain "init" (any init system).
   * Accepts: /sbin/init, /bin/init, /usr/bin/runit-init, /bin/openrc-init */
  snprintf(path, sizeof(path), DS_PROC_CMDLINE_FMT, pid);
  if (read_file(path, buf, sizeof(buf)) < 0)
    return 0;
  if (!strstr(buf, "init"))
    return 0;

  return 1;
}

/* ---------------------------------------------------------------------------
 * Introspection
 * ---------------------------------------------------------------------------*/

int check_status(struct ds_config *cfg, pid_t *pid_out) {
  if (auto_resolve_pidfile(cfg) < 0) {
    ds_error("Could not resolve PID file. Use --name or --pidfile.");
    return -1;
  }

  pid_t pid = 0;
  if (is_container_running(cfg, &pid)) {
    if (pid_out)
      *pid_out = pid;
    return 0;
  }

  ds_error("Container '%s' is not running or invalid.", cfg->container_name);
  return -1;
}

/* ---------------------------------------------------------------------------
 * Start
 * ---------------------------------------------------------------------------*/

int start_rootfs(struct ds_config *cfg) {
  /* 0. Early restart detection: check for external lock from previous stop
   *    command to detect a preserved mount for reuse. */
  int lock_acquired = 0;
  if (cfg->container_name[0] && cfg->rootfs_img_path[0]) {
    char lock_path[PATH_MAX];
    get_lock_path(cfg->container_name, lock_path, sizeof(lock_path));

    if (access(lock_path, F_OK) == 0) {
      /* This looks like a restart handoff — take ownership of the lock */
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
          ds_log("Reusing existing mount at %s (restart)", existing_mount);
          safe_strncpy(cfg->rootfs_path, existing_mount,
                       sizeof(cfg->rootfs_path));
          cfg->is_img_mount = 1;
          safe_strncpy(cfg->img_mount_point, cfg->rootfs_path,
                       sizeof(cfg->img_mount_point));
        } else {
          /* Mount not active — remove invalid lock */
          release_external_lock(cfg->container_name);
          lock_acquired = 0;
        }
      }
    }
  }

  /* 1. Preparation */
  ensure_workspace();

  if (cfg->selinux_permissive)
    android_set_selinux_permissive();
  if (cfg->android_storage && !is_android())
    ds_warn("--enable-android-storage is only supported on Android hosts. "
            "Skipping.");
  if (cfg->termux_x11 && !is_android())
    ds_warn("--termux-x11 is only applicable on Android. Skipping.");

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

  /* If no hostname specified, default to container name */
  if (cfg->hostname[0] == '\0') {
    safe_strncpy(cfg->hostname, cfg->container_name, sizeof(cfg->hostname));
  }

  /* 2. Mount rootfs image if provided (using the resolved name) */
  if (cfg->rootfs_img_path[0] && !lock_acquired) {
    if (mount_rootfs_img(cfg->rootfs_img_path, cfg->rootfs_path,
                         sizeof(cfg->rootfs_path), cfg->volatile_mode,
                         cfg->container_name) < 0) {
      goto cleanup;
    }
    cfg->is_img_mount = 1;
    safe_strncpy(cfg->img_mount_point, cfg->rootfs_path,
                 sizeof(cfg->img_mount_point));
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

  /* Persist the new UUID to config immediately so disk always matches
   * the running container. CLI overrides (e.g. -f) are already in cfg
   * at this point since start_rootfs is called after argument parsing. */
  if (cfg->config_file[0])
    ds_config_save(cfg->config_file, cfg);

  /* Parse environment file while host paths are reachable (before pivot_root)
   */
  if (cfg->env_file[0] != '\0') {
    parse_env_file_to_config(cfg->env_file, cfg);
  }

  /* Pre-populate volatile_dir for monitor cleanup (actual overlay setup
   * happens inside internal_boot's isolated mount namespace) */
  if (cfg->volatile_mode) {
    snprintf(cfg->volatile_dir, sizeof(cfg->volatile_dir),
             "%s/" DS_VOLATILE_SUBDIR "/%s", get_workspace_dir(),
             cfg->container_name);
  }

  /* 4. Parent-side PTY allocation (LXC Model) */
  /* CRITICAL: Before forking, verify /sbin/init exists in the rootfs */
  char init_path[PATH_MAX];
  char rootfs_norm[PATH_MAX];
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
    goto cleanup;
  }

  /*
   * Robust Check: If it's a symlink, we MUST assume it's valid.
   * Absolute symlinks (e.g. /sbin/init -> /lib/systemd/systemd) will appear
   * "broken" from the host's perspective, but will resolve correctly inside
   * the container after pivot_root.
   */
  if (!S_ISLNK(st.st_mode) && access(init_path, X_OK) != 0) {
    ds_error("Init binary is not executable: %s", init_path);
    ds_error("Ensure it has executable permissions.");
    if (cfg->is_img_mount)
      unmount_rootfs_img(cfg->img_mount_point, cfg->foreground);
    return -1;
  }

  cfg->tty_count = DS_MAX_TTYS;
  if (ds_terminal_create(&cfg->console) < 0)
    ds_die("Failed to allocate console PTY");

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
  if (pipe(sync_pipe) < 0)
    ds_die("pipe failed: %s", strerror(errno));

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
    ds_die("fork failed: %s", strerror(errno));
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

    /* ── Monitor Hardening ──
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

    /* Adaptive Cgroup Namespace (introduced in Linux 4.6) */
    if (access("/proc/self/ns/cgroup", F_OK) == 0) {
      /* To get isolation from a cgroup namespace, we must be in a sub-cgroup
       * BEFORE we unshare. If we are in the root '/', the namespace root will
       * be the host's root, providing zero isolation.
       * We use a container-specific path to avoid conflicts. */
      if (access("/sys/fs/cgroup/cgroup.procs", F_OK) == 0) {
        char cg_path[PATH_MAX];
        snprintf(cg_path, sizeof(cg_path), "/sys/fs/cgroup/droidspaces/%s",
                 cfg->container_name);
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
    }

    if (unshare(ns_flags) < 0)
      ds_die("unshare failed: %s", strerror(errno));

    int stdio_redirected = 0;

    /* ── Reboot-aware boot loop ──
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
     * SIGHUP — it only checks a deterministic exit code. */
  reboot_loop:;
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

    pid_t mid_pid = fork();
    if (mid_pid < 0)
      _exit(EXIT_FAILURE);

    if (mid_pid == 0) {
      /* ── INTERMEDIATE PROCESS ──
       * Create a fresh PID namespace for this boot cycle. */
      if (unshare(CLONE_NEWPID) < 0) {
        ds_error("unshare(CLONE_NEWPID) failed: %s", strerror(errno));
        _exit(EXIT_FAILURE);
      }

      pid_t init_pid = fork();
      if (init_pid < 0)
        _exit(EXIT_FAILURE);

      if (init_pid == 0) {
        /* CONTAINER INIT (PID 1 inside namespace) */
        close(sync_pipe[1]);
        _exit(internal_boot(cfg));
      }

      /* Send init PID to parent via sync pipe (first boot only) */
      if (sync_pipe[1] >= 0) {
        if (write(sync_pipe[1], &init_pid, sizeof(pid_t)) != sizeof(pid_t)) {
          /* Reader will detect failure or handle empty/partial read */
        }
        close(sync_pipe[1]);
        sync_pipe[1] = -1;
      } else {
        /* Reboot cycle — update PID file directly so
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

    /* ── MONITOR continues here ── */

    /* Close sync pipe write end (intermediate handles it) */
    if (sync_pipe[1] >= 0) {
      close(sync_pipe[1]);
      sync_pipe[1] = -1;
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

    /* ── Reboot detection (internal reboot) ── */
    if (WIFEXITED(status) && WEXITSTATUS(status) == DS_REBOOT_EXIT) {
      /* Check for external lock — if exists, abort reboot and let CLI handle it
       */
      if (is_external_lock_active(cfg->container_name)) {
        write_monitor_debug_log(
            cfg->container_name,
            "External command lock detected — aborting internal reboot");
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

      /* Reload configuration from disk if available (merge strategy) */
      if (cfg->config_file[0]) {
        free_config_binds(cfg);
        free_config_env_vars(cfg);
        struct ds_config reboot_cfg = *cfg;
        if (ds_config_load(cfg->config_file, &reboot_cfg) == 0) {
          if (strcmp(cfg->dns_servers, reboot_cfg.dns_servers) != 0) {
            reboot_cfg.dns_server_content[0] = '\0';
            ds_get_dns_servers(reboot_cfg.dns_servers,
                               reboot_cfg.dns_server_content,
                               sizeof(reboot_cfg.dns_server_content));
          }
          *cfg = reboot_cfg;
        }
      }

      cfg->reboot_cycle = 1;
      if (cfg->foreground)
        ds_log_silent = 1;
      goto reboot_loop;
    }

    /* Not a reboot — check if external command is handling cleanup */
    if (is_external_lock_active(cfg->container_name)) {
      write_monitor_debug_log(cfg->container_name,
                              "External command lock detected — yielding "
                              "cleanup to CLI");
      goto monitor_cleanup_and_exit;
    }

    /* Normal exit — monitor does cleanup */
    write_monitor_debug_log(cfg->container_name, "Monitor performing cleanup");
    cleanup_container_resources(cfg, 0, 0, 0);

  monitor_cleanup_and_exit:
    /* Free dynamically allocated configuration members before exit */
    free_config_binds(cfg);
    free_config_env_vars(cfg);
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
   * This is required for sudo/su to work if the rootfs is on /data. */
  if (is_android() && !cfg->rootfs_img_path[0])
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

    int ret =
        console_monitor_loop(cfg->console.master, monitor_pid, cfg->pidfile);
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
      goto cleanup;
    }

    show_info(cfg, 1);
    if (lock_acquired)
      release_external_lock(cfg->container_name);
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

  return 0;

cleanup:
  /* Centralized host-side cleanup IF we are returning error.
   * This ensures image mounts and tracking files are reverted on fatal boot
   * errors. */
  cleanup_container_resources(cfg, cfg->container_pid, 0, 1 /* force */);
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

  pid_t pid;
  if (check_status(cfg, &pid) < 0) {
    release_external_lock(cfg->container_name);
    return -1; /* Container not running — signal failure to caller */
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

  /* 1. Try graceful shutdown with a "signal bucket" to support multiple init
   * systems:
   * - SIGRTMIN+3: Standard systemd poweroff signal in containers.
   * - SIGTERM: Universal signal for graceful termination (Alpine/OpenRC reacts
   * to this).
   * - SIGPWR: Universal power failure signal (often used by LXC/SysVinit for
   * shutdown).
   */
  kill(pid, DS_SIG_STOP);
  kill(pid, SIGTERM);
  kill(pid, SIGPWR);
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

  /* 4. Firmware cleanup.
   * Skip when unkillable — accessing zombie-held rootfs can hang. */
  if (cfg->img_mount_point[0] && !unkillable)
    firmware_path_remove_rootfs(cfg->img_mount_point);

  /* 5. Complete resource cleanup. */
  cleanup_container_resources(cfg, 0, skip_unmount, unkillable);

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

int enter_namespace(pid_t pid) {
  /* Verify process is still alive before trying to enter namespaces */
  if (kill(pid, 0) < 0) {
    ds_error("Container PID %d is no longer alive.", pid);
    return -1;
  }

  const char *ns_names[] = {"mnt", "uts", "ipc", "pid", "cgroup"};
  int ns_fds[5];
  char path[PATH_MAX];

  /* 1. Open all namespace descriptors first (CRITICAL: before any setns) */
  for (int i = 0; i < 5; i++) {
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
      if (errno != ENOENT) {
        ds_warn("Optional namespace %s (%s) is missing: %s", ns_names[i], path,
                strerror(errno));
      }
    }
  }

  /* 2. Enter namespaces */
  for (int i = 0; i < 5; i++) {
    if (ns_fds[i] < 0)
      continue;

    if (setns(ns_fds[i], 0) < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("setns(mnt) failed: %s", strerror(errno));
        for (int j = i; j < 5; j++)
          if (ns_fds[j] >= 0)
            close(ns_fds[j]);
        return -1;
      }
      ds_warn("setns(%s) failed (ignored): %s", ns_names[i], strerror(errno));
    }
    close(ns_fds[i]);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Enter / Run
 * ---------------------------------------------------------------------------*/

int enter_rootfs(struct ds_config *cfg, const char *user) {
  pid_t pid;
  if (check_status(cfg, &pid) < 0)
    return -1;

  /* Parse environment file while host paths are reachable */
  if (cfg->env_file[0] != '\0') {
    parse_env_file_to_config(cfg->env_file, cfg);
  }

  ds_log("Entering container '%s' as %s...", cfg->container_name,
         user ? user : "root");

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    free_config_env_vars(cfg);
    return -1;
  }

  pid_t child = fork();
  if (child < 0) {
    close(sv[0]);
    close(sv[1]);
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    close(sv[0]);

    /* CRITICAL: Physically attach process to the container's cgroup on the
     * host. This ensures the process is inside the container's hierarchy
     * subtree, which is required for D-Bus/logind inside to move it into
     * session scopes.
     */
    ds_cgroup_attach(pid);

    if (enter_namespace(pid) < 0)
      _exit(EXIT_FAILURE);

    /* Allocate TTY INSIDE the container namespaces */
    struct ds_tty_info tty;
    if (ds_terminal_create(&tty) < 0)
      _exit(EXIT_FAILURE);

    /* Send master FD back to parent */
    if (ds_send_fd(sv[1], tty.master) < 0)
      _exit(EXIT_FAILURE);

    close(tty.master);
    close(sv[1]);

    /* Must fork again to actually be in the new PID namespace */
    pid_t shell_pid = fork();
    if (shell_pid < 0)
      _exit(EXIT_FAILURE);
    if (shell_pid == 0) {
      /* Establish controlling terminal in the FINAL child process.
       * This is critical: setsid() + TIOCSCTTY must happen in the
       * process that will exec the shell, so that programs like
       * 'login' can properly re-acquire the controlling terminal
       * via their own setsid(). If we did this in the intermediate
       * parent, login's setsid() would detach from the ctty but
       * could never re-acquire it (the intermediate still owns it),
       * causing a hang. This matches how LXC does it in
       * lxc_terminal_prepare_login(). */
      if (ds_terminal_make_controlling(tty.slave) < 0)
        _exit(EXIT_FAILURE);

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

      if (user && user[0]) {
        char *shell_argv[] = {"su", "-l", (char *)(uintptr_t)user, NULL};
        execve("/bin/su", shell_argv, environ);
        execve("/usr/bin/su", shell_argv, environ);
      }

      /* Try shells in order */
      const char *shells[] = {"/bin/bash", "/bin/ash", "/bin/sh", NULL};
      for (int i = 0; shells[i]; i++) {
        if (access(shells[i], X_OK) == 0) {
          const char *sh_name = strrchr(shells[i], '/');
          sh_name = sh_name ? sh_name + 1 : shells[i];
          char *shell_argv[] = {(char *)(uintptr_t)sh_name, "-l", NULL};
          execve(shells[i], shell_argv, environ);
        }
      }

      ds_error("Failed to find any usable shell");
      _exit(EXIT_FAILURE);
    }
    /* Intermediate: close slave fd we no longer need, wait for shell */
    close(tty.slave);
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
  free_config_env_vars(cfg);
  return 0;
}

int run_in_rootfs(struct ds_config *cfg, int argc, char **argv) {
  (void)argc;
  pid_t pid;
  if (check_status(cfg, &pid) < 0)
    return -1;

  /* Removed verbose status log to allow raw output stream */

  /* Parse environment file while host paths are reachable */
  if (cfg->env_file[0] != '\0') {
    parse_env_file_to_config(cfg->env_file, cfg);
  }

  pid_t child = fork();
  if (child < 0) {
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    if (enter_namespace(pid) < 0)
      _exit(EXIT_FAILURE);

    pid_t cmd_pid = fork();
    if (cmd_pid < 0)
      _exit(EXIT_FAILURE);
    if (cmd_pid == 0) {
      if (chdir("/") < 0)
        _exit(EXIT_FAILURE);

      /* Setup environment */
      ds_env_boot_setup(cfg);
      load_etc_environment();

      /* If single argument with spaces, run via /bin/sh -c */
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
  /* Host info */
  const char *host = is_android() ? "Android" : "Linux";
  const char *arch = get_architecture();
  printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);

  /* Case 1: No container name specified */
  if (cfg->container_name[0] == '\0') {
    char first_name[256];
    int count = count_running_containers(first_name, sizeof(first_name));

    if (count == 0) {
      printf("\n" C_YELLOW "Container:" C_RESET " No containers running.\n\n");
      return 0;
    }

    if (count == 1) {
      /* Auto-resolve to the only running container */
      safe_strncpy(cfg->container_name, first_name,
                   sizeof(cfg->container_name));
      resolve_pidfile_from_name(first_name, cfg->pidfile, sizeof(cfg->pidfile));
    } else {
      /* Multiple containers running, show list */
      printf("\n" C_YELLOW "Multiple containers running:" C_RESET "\n");
      show_containers();
      printf("\nUse '" C_GREEN "--name <NAME> info" C_RESET
             "' for detailed information.\n\n");
      return 0;
    }
  }

  /* Case 2: Specific name specified or auto-resolved */
  if (cfg->pidfile[0] == '\0' && cfg->container_name[0] != '\0') {
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
  }

  pid_t pid = 0;
  if (trust_cfg_pid && cfg->container_pid > 0) {
    /* Trust the PID we just got from the sync pipe.
     * We assume it's running because parent waited for boot marker. */
    pid = cfg->container_pid;
  } else {
    /* For other calls (e.g., info command), read and validate from pidfile. */
    is_container_running(cfg, &pid);
  }

  printf("\n" C_GREEN "Container:" C_RESET " %s (%s)\n", cfg->container_name,
         pid > 0 ? "RUNNING" : "STOPPED");

  if (pid > 0) {
    printf("  PID: %d\n", pid);

    char pretty[256];
    char osr_path[PATH_MAX];
    if (build_proc_root_path(pid, "/etc/os-release", osr_path,
                             sizeof(osr_path)) == 0) {
      get_os_pretty(osr_path, pretty, sizeof(pretty));
      if (pretty[0])
        printf("  OS: %s\n", pretty);
    }

    printf("\n" C_GREEN "Features:" C_RESET "\n");

    /* SELinux */
    if (access("/sys/fs/selinux/enforce", R_OK) == 0) {
      const char *sel =
          android_get_selinux_status() == 0 ? "Permissive" : "Enforcing";
      printf("  SELinux: %s\n", sel);
    }

    /* IPv6 */
    printf("  IPv6: %s\n",
           detect_ipv6_in_container(pid) ? "enabled" : "disabled");

    /* Android storage */
    printf("  Android storage: %s\n",
           detect_android_storage_in_container(pid) ? "enabled" : "disabled");

    /* HW access */
    int hw = detect_hw_access_in_container(pid);
    if (hw)
      printf("  " C_RED "HW access:" C_RESET " enabled\n");
    else
      printf("  HW access: disabled\n");
  } else {
    /* Best effort: read os-release from rootfs path */
    if (cfg->rootfs_path[0]) {
      char osr_path[PATH_MAX];
      snprintf(osr_path, sizeof(osr_path), "%.4070s/etc/os-release",
               cfg->rootfs_path);
      char pretty[256];
      get_os_pretty(osr_path, pretty, sizeof(pretty));
      if (pretty[0])
        printf("  Rootfs OS: %s\n", pretty);
    }
  }
  printf("\n");

  return 0;
}

int restart_rootfs(struct ds_config *cfg) {
  ds_log("Restarting container %s...", cfg->container_name);
  stop_rootfs(cfg, 1); /* skip unmount to keep rootfs.img attached */
  return start_rootfs(cfg);
}
