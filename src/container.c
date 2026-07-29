/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <sys/file.h>

/* Container lifecycle lock
 *
 * Everything a container owns on disk is keyed by its name: pidfile, mount
 * point, cgroup, sidecars. So at most one actor may change a container's state
 * at a time, and that includes the monitor, not only commands. Whoever holds
 * <pids>/<name>.lock owns the transition, and decides what to do from the
 * state it finds while holding it.
 *
 * It is a flock(), so taking it is atomic and the kernel drops it when the
 * holder dies. There is no stale lock to detect and no PID to recycle.
 *
 * A second file, <name>.monitor, is held shared by a container's monitor for
 * as long as it lives. It answers one question for the pruners in pid.c: is
 * somebody still responsible for this container's leftovers? */

#define DS_EXT_MONITOR ".monitor"

static int g_held_locks[4] = {-1, -1, -1, -1};

/* A lock belongs to the process that took it. flock() locks live on the open
 * file description, which fork() shares with the child, so without this any
 * long-lived child (the monitor, a log relay) would keep its parent's lock
 * held long after the parent let go. */
static void drop_inherited_locks(void) {
  for (size_t i = 0; i < sizeof(g_held_locks) / sizeof(g_held_locks[0]); i++) {
    if (g_held_locks[i] >= 0)
      close(g_held_locks[i]);
    g_held_locks[i] = -1;
  }
}

static void track_lock(int fd, int held) {
  for (size_t i = 0; i < sizeof(g_held_locks) / sizeof(g_held_locks[0]); i++) {
    if (g_held_locks[i] == (held ? -1 : fd)) {
      g_held_locks[i] = held ? fd : -1;
      return;
    }
  }
}

/* Build <pids>/<safe_name><ext> with defensive truncation.
 * Precision: 2048 (pids_dir) + 256 (name) + ext < PATH_MAX (4096). */
static int get_lock_path(const char *name, const char *ext, char *buf,
                         size_t size) {
  if (!name || !buf || size == 0 || !validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));
  int r =
      snprintf(buf, size, "%.2048s/%.256s%s", get_pids_dir(), safe_name, ext);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* open + flock. Returns the fd, or -1 with errno set (EWOULDBLOCK when op has
 * LOCK_NB and somebody else holds it).
 *
 * We never unlink a lock file, but a monitor from a build before this lock
 * does: it deletes a lock file it considers stale. A lock on an unlinked file
 * guards nothing, so after locking check the path still names our file, and
 * start over if it does not. */
static int lock_file(const char *path, int op) {
  static int atfork_registered;
  if (!atfork_registered) {
    pthread_atfork(NULL, NULL, drop_inherited_locks);
    atfork_registered = 1;
  }

  for (int attempt = 0; attempt < 8; attempt++) {
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0)
      return -1;

    int r;
    do {
      r = flock(fd, op);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
      int err = errno;
      close(fd);
      errno = err;
      return -1;
    }

    struct stat held, named;
    if (fstat(fd, &held) == 0 && stat(path, &named) == 0 &&
        held.st_ino == named.st_ino && held.st_dev == named.st_dev) {
      track_lock(fd, 1);
      return fd;
    }
    close(fd);
  }
  errno = EAGAIN;
  return -1;
}

static void unlock_file(int fd) {
  track_lock(fd, 0);
  flock(fd, LOCK_UN);
  close(fd);
}

/* Take the lifecycle lock for a container. wait=0 fails at once with
 * errno == EWOULDBLOCK when another actor holds it, which is what a command
 * wants. wait=1 blocks until it is free, which is what the monitor wants: it
 * must not act on its container's exit until any command has finished.
 * Returns an fd for ds_container_unlock(), or -1. */
int ds_container_lock(const char *name, int wait) {
  char path[PATH_MAX];
  if (get_lock_path(name, DS_EXT_LOCK, path, sizeof(path)) < 0) {
    errno = EINVAL;
    return -1;
  }
  ensure_workspace();

  int fd = lock_file(path, LOCK_EX | (wait ? 0 : LOCK_NB));
  if (fd < 0)
    return -1;

  /* Monitors started by a build older than this lock cannot see a flock().
   * They read "pid boot-id" from this file and yield to a live holder, so
   * keep writing it. Drop this once no such monitor can still be running. */
  char boot_id[64] = "", stamp[96];
  read_file("/proc/sys/kernel/random/boot_id", boot_id, sizeof(boot_id));
  int len = snprintf(stamp, sizeof(stamp), "%d %s", getpid(), boot_id);
  if (ftruncate(fd, 0) < 0 || pwrite(fd, stamp, (size_t)len, 0) < 0)
    ds_log("[DEBUG] could not stamp %s: %s", path, strerror(errno));
  return fd;
}

void ds_container_unlock(int fd) {
  if (fd < 0)
    return;
  if (ftruncate(fd, 0) < 0) {
    /* Only the compatibility stamp; the lock itself is released below. */
  }
  unlock_file(fd);
}

/* Called once by a monitor, which then holds it until it exits. */
void ds_container_claim_supervision(const char *name) {
  char path[PATH_MAX];
  if (get_lock_path(name, DS_EXT_MONITOR, path, sizeof(path)) == 0)
    lock_file(path, LOCK_SH);
}

/* For the pruners: take the lifecycle lock only if nobody is in the middle of
 * a transition AND no monitor is alive for this name. A live monitor cleans
 * up after its own container, and deleting its pidfile first would make it
 * conclude a command had already done so. Returns an fd for
 * ds_container_unlock(), or -1, in which case leave the container alone.
 * Anything we cannot determine counts as "not ours to touch". */
int ds_container_lock_orphan(const char *name) {
  char path[PATH_MAX];
  if (get_lock_path(name, DS_EXT_MONITOR, path, sizeof(path)) < 0)
    return -1;

  int fd = ds_container_lock(name, 0);
  if (fd < 0)
    return -1;

  int probe = lock_file(path, LOCK_EX | LOCK_NB);
  if (probe < 0) {
    ds_container_unlock(fd);
    return -1;
  }
  unlock_file(probe);
  return fd;
}

/* Configuration & Metadata Recovery */

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

/* Cleanup */

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
  }

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

/* Introspection */

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

/* Start */

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
static int start_rootfs_locked(struct ds_config *cfg, int *lock_fd,
                               int reuse_mount) {

  int has_side_effects = 0;
  int sync_pipe[2] = {-1, -1}; /* read by cleanup: set before any goto */
  pid_t tweaks_pid = -1;       /* the android_optimizations(1) helper */

  /* Here and not with the other network checks in main.c: a restart reloads
   * its config after those ran, and this is the one both paths go through. */
  if (cfg->net_mode == DS_NET_MACVLAN && ds_net_macvlan_check(cfg) < 0)
    return -1;

  /* 0. Restart: pick the preserved mount back up. If it is gone after all,
   *    this is an ordinary start that mounts the image again. */
  if (reuse_mount) {
    if (cfg->pidfile[0] == '\0')
      resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                                sizeof(cfg->pidfile));

    char existing_mount[PATH_MAX];
    if (cfg->pidfile[0] &&
        read_mount_path(cfg->pidfile, existing_mount, sizeof(existing_mount)) >
            0 &&
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

  /* 2b. Android: start Termux-X11, VirGL, and PulseAudio servers before fork
   * so the sockets exist when bind-mounted later */
  if (is_android() && cfg->x11) {
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

  /* anland display daemon: generate the per-container host socket and start the
   * broker before fork so the socket exists when bind-mounted post-pivot. The
   * generated cfg->anland_sock is recorded in the Pids dir (not
   * container.config) by ds_anland_daemon_start. */
  if (is_android() && cfg->anland) {
    ds_anland_daemon_start(cfg);
  }

  /* 3. Early pre-flight for volatile mode (before any host changes) */
  if (check_volatile_mode(cfg) < 0) {
    goto cleanup;
  }

  /* The uniqueness check above reports the name as in use when a running
   * container carries this UUID (its deep scan is by UUID marker), so a UUID
   * is only ever minted here, never replaced. */
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

    int ret = console_monitor_loop(cfg->console.master, monitor_pid, cfg);
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

static int lifecycle_lock_or_complain(struct ds_config *cfg, const char *verb) {
  int fd = ds_container_lock(cfg->container_name, 0);
  if (fd >= 0)
    return fd;

  if (errno == EWOULDBLOCK) {
    ds_error("Cannot %s '%s': another operation is in progress on this "
             "container",
             verb, cfg->container_name);
    ds_error("Wait for it to complete, or use 'droidspaces show' to check "
             "status");
  } else {
    ds_error("Cannot %s '%s': failed to take the container lock: %s", verb,
             cfg->container_name, strerror(errno));
  }
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

/* Load the config a running container actually booted with, from the
 * snapshot it keeps in its own /run. Returns 0 and fills *out, which the
 * caller releases with ds_config_free(), or -1 if there is no readable
 * snapshot. */
static int load_booted_config(const struct ds_config *cfg, pid_t pid,
                              struct ds_config *out) {
  char run_path[PATH_MAX];
  if (build_proc_root_path(pid, "/run/droidspaces/container.config", run_path,
                           sizeof(run_path)) != 0)
    return -1;

  memset(out, 0, sizeof(*out));
  out->net_ready_pipe[0] = out->net_ready_pipe[1] = -1;
  out->net_done_pipe[0] = out->net_done_pipe[1] = -1;
  out->net_mode = DS_NET_NAT; /* zero is host, see main() */
  safe_strncpy(out->container_name, cfg->container_name,
               sizeof(out->container_name));
  safe_strncpy(out->prog_name, cfg->prog_name, sizeof(out->prog_name));

  if (ds_config_load(run_path, out) < 0) {
    ds_config_free(out);
    return -1;
  }

  /* The pidfile is how the caller found this instance, and it may have come
   * from --pidfile, which no config file records. */
  safe_strncpy(out->pidfile, cfg->pidfile, sizeof(out->pidfile));
  return 0;
}

/* The body of a stop. The caller holds the lifecycle lock. */
static int stop_rootfs_locked(struct ds_config *caller_cfg, int skip_unmount,
                              int timeout_seconds) {
  if (timeout_seconds < 0)
    timeout_seconds = DS_STOP_TIMEOUT;

  pid_t pid = 0;
  if (!is_container_running(caller_cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.",
             caller_cfg->container_name);
    return -1;
  }

  /* Tear down what is running, not what the caller is holding. The caller's
   * config describes what the container should be next: on a restart it is
   * the edited config file or the snapshot with new flags applied on top. If
   * that changed the network mode, cleaning up with it runs the wrong
   * teardown, e.g. the gateway cleanup for a container wired as NAT, and
   * leaves its veth, port forwards and shared rules behind. The instance's
   * own snapshot is the only description of what it was booted as. Without a
   * readable one (a container from before snapshots existed) the caller's
   * config is the best we have. */
  struct ds_config booted;
  int have_booted = (load_booted_config(caller_cfg, pid, &booted) == 0);
  struct ds_config *cfg = have_booted ? &booted : caller_cfg;

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

      int fd = open(initctl, O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
      if (fd < 0) {
        /* Fallback: try /dev/initctl (historical path, used by Slackware) */
        snprintf(initctl, sizeof(initctl), "/proc/%d/root/dev/initctl", pid);
        fd = open(initctl, O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
      }

      /* Only write into an actual FIFO.  The container fully controls
       * /run/initctl inside its own root: O_NOFOLLOW rejects a symlink at the
       * final component, and the S_ISFIFO check rejects a regular file the
       * container may have planted to capture the host-written init_request. */
      struct stat ictl_st;
      if (fd >= 0 && fstat(fd, &ictl_st) == 0 && S_ISFIFO(ictl_st.st_mode)) {
        if (write(fd, &req, sizeof(req)) != (ssize_t)sizeof(req))
          ds_warn("sysvinit: short write to initctl, falling back to SIGPWR");
        close(fd);
      } else {
        if (fd >= 0)
          close(fd);
        ds_warn("sysvinit: cannot open initctl FIFO (tried /run and /dev), "
                "falling back to SIGPWR");
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

  /* 2. Wait for exit. We are not init's parent, so this is a poll; 20 ms
   * keeps the overshoot small and fifty kill(2)s a second cost nothing. */
  const int poll_us = 20000;
  int stopped = 0;
  for (int i = 0; i < timeout_seconds * (1000000 / poll_us); i++) {
    if (kill(pid, 0) < 0 && errno == ESRCH) {
      stopped = 1;
      break;
    }
    usleep(poll_us);
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

  if (have_booted)
    ds_config_free(&booted);
  return 0;
}

int stop_rootfs_with_timeout(struct ds_config *cfg, int skip_unmount,
                             int timeout_seconds) {
  int lock_fd = lifecycle_lock_or_complain(cfg, "stop");
  if (lock_fd < 0)
    return -1;

  int ret = stop_rootfs_locked(cfg, skip_unmount, timeout_seconds);
  ds_container_unlock(lock_fd);
  return ret;
}

int stop_rootfs(struct ds_config *cfg, int skip_unmount) {
  return stop_rootfs_with_timeout(cfg, skip_unmount, DS_STOP_TIMEOUT);
}

/* Namespace Entry (shared for enter and run) */

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
    ns_fds[i] = open(path, O_RDONLY | O_CLOEXEC);
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

/* Enter / Run */

/* Shell convention, the same one lxc-attach follows: 128 plus the signal when
 * the child was killed, so `run -- sh -c 'kill -9 $$'` reports 137, not 1. */
static int exit_status(int st) {
  if (WIFEXITED(st))
    return WEXITSTATUS(st);
  if (WIFSIGNALED(st))
    return 128 + WTERMSIG(st);
  return EXIT_FAILURE;
}

/* What a shell reports after a failed exec: 127 when the binary is missing,
 * 126 when it is there but cannot be run. Read errno before logging. */
static int exec_exit_code(void) { return errno == ENOENT ? 127 : 126; }

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
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
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

    /* cgroup attach before entering namespaces */
    ds_log_silent = 1;
    ds_cgroup_attach(cfg->container_name);
    ds_log_silent = 0;
    ds_virtualize_join(pid);

    if (enter_namespace(pid, cfg) < 0)
      _exit(EXIT_FAILURE);

    /* Now that we are in the container's mount namespace, allocate a PTY.
     * This ensures its path (e.g. /dev/pts/0) exists and is resolvable. */
    if (ds_terminal_create(&tty) < 0)
      _exit(EXIT_FAILURE);

    /* Size the pty before the shell exists, as lxc-attach does on its ptx, so
     * nothing ever reads a 0x0 window. The caller's stdin is still ours here;
     * only the grandchild swaps it for the slave. */
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
      ioctl(tty.master, TIOCSWINSZ, &ws);

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
    /* Neutralize the KSU container-escape path BEFORE seccomp is applied:
     * the ioctl needs the [ksu_driver] fd that the magic reboot() installs,
     * and the seccomp filter below denies that very magic reboot. */
    ds_ksu_neutralize_root_escape();
    ds_seccomp_apply_minimal(cfg->privileged_mask, cfg->sandboxing_allowed);
    android_seccomp_setup(cfg->privileged_mask);
    ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask,
                                  cfg->sandboxing_allowed);
    ds_log_silent = 0;

    /* The intermediate, not the shell, owns the session and the terminal.
     * util-linux login (Ubuntu 24.04+) calls vhangup(), which SIGHUPs the
     * session leader: with the shell as leader that killed bash and took the
     * whole session with it. Here the leader ignores SIGHUP and the shell is a
     * plain member, so login's hangup passes through and the prompt returns
     * after logout. lxc-attach does the opposite, its attached shell calls
     * setsid() itself, which is why login ends an lxc-attach session. */
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
      /* Not the session leader on purpose, see above: no setsid, no
       * TIOCSCTTY, the slave is already our controlling terminal. */
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
      _exit(127);
    }
    /* Keep tty.slave open until the shell exits: it holds the pts alive
     * across login's vhangup()/reopen window. */
    int st;
    waitpid(shell_pid, &st, 0);
    _exit(exit_status(st));
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

  /* Parent: setup host terminal and proxy I/O */
  struct termios old_tios;
  int has_tty = (ds_setup_tios(STDIN_FILENO, &old_tios) == 0);

  ds_terminal_proxy(master_fd);

  if (has_tty) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tios);
  }

  close(master_fd);
  int st;
  waitpid(child, &st, 0);
  free_config_env_vars(cfg);
  return exit_status(st);
}

/* Append arg to buf as a single-quoted shell word, space-separated from any
 * prior content unless first.  Embedded single quotes use the '\'' idiom.
 * Writes at most size-1 chars and always NUL-terminates; returns the new
 * offset.  Preserves argv word boundaries across su -c's shell. */
static size_t shell_quote_append(char *buf, size_t off, size_t size,
                                 const char *arg, int first) {
  if (!first && off < size - 1)
    buf[off++] = ' ';
  if (off < size - 1)
    buf[off++] = '\'';
  for (const char *p = arg; *p; p++) {
    if (*p == '\'') {
      const char *esc = "'\\''"; /* close quote, escaped quote, reopen quote */
      for (const char *e = esc; *e && off < size - 1; e++)
        buf[off++] = *e;
    } else if (off < size - 1) {
      buf[off++] = *p;
    }
  }
  if (off < size - 1)
    buf[off++] = '\'';
  buf[off] = '\0';
  return off;
}

int run_in_rootfs(struct ds_config *cfg, char **argv, const char *as_user) {
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

  /* The terminal delivers ^C and ^\ to the whole foreground group, the
   * command included. Only the command should die of them; the two processes
   * that relay its exit status stay, the same as lxc-attach (its issue #313).
   * A command that traps them itself cannot be interrupted from the terminal
   * any more, also as in lxc-attach. The grandchild restores the defaults. */
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);

  pid_t child = fork();
  if (child < 0) {
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    /* Mirror enter_rootfs: attach to the container's cgroup subtree before
     * crossing into its namespaces, so the command is accounted to the
     * container instead of leaking to the host's cgroup root. */
    ds_log_silent = 1;
    ds_cgroup_attach(cfg->container_name);
    ds_log_silent = 0;
    ds_virtualize_join(pid);

    if (enter_namespace(pid, cfg) < 0)
      _exit(EXIT_FAILURE);

    /* Apply identical security hardening as internal_boot() and enter_rootfs().
     * Same reasoning: run processes are not children of container PID 1. */
    ds_log_silent = 1;
    /* Neutralize the KSU container-escape path BEFORE seccomp is applied:
     * the ioctl needs the [ksu_driver] fd that the magic reboot() installs,
     * and the seccomp filter below denies that very magic reboot. */
    ds_ksu_neutralize_root_escape();
    ds_seccomp_apply_minimal(cfg->privileged_mask, cfg->sandboxing_allowed);
    android_seccomp_setup(cfg->privileged_mask);
    ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask,
                                  cfg->sandboxing_allowed);
    ds_log_silent = 0;

    pid_t cmd_pid = fork();
    if (cmd_pid < 0)
      _exit(EXIT_FAILURE);
    if (cmd_pid == 0) {
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

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
          /* Single argument is treated as a shell command string as-is, so a
           * caller can pass e.g. `-- "ls -la | grep foo"`. */
          safe_strncpy(cmd_buf, argv[0], sizeof(cmd_buf));
        } else {
          /* Multiple args: shell-quote each so word boundaries (spaces, globs,
           * metacharacters) survive su's shell -- e.g. `-- rm "a b"` stays two
           * arguments instead of being resplit into three. */
          size_t off = 0;
          for (int k = 0; argv[k]; k++)
            off = shell_quote_append(cmd_buf, off, sizeof(cmd_buf), argv[k],
                                     k == 0);
        }
        char *su_argv[] = {"su", "-",     (char *)(uintptr_t)as_user,
                           "-c", cmd_buf, NULL};
        execvp("/bin/su", su_argv);
        execvp("/usr/bin/su", su_argv);
        execvp("/run/wrappers/bin/su", su_argv);
        int code = exec_exit_code();
        ds_error("Failed to exec su for user '%s': %s", as_user,
                 strerror(errno));
        _exit(code);
      }

      if (argv[1] == NULL && strchr(argv[0], ' ') != NULL) {
        char *shell_argv[] = {"/bin/sh", "-c", argv[0], NULL};
        execvp("/bin/sh", shell_argv);
      } else {
        execvp(argv[0], argv);
      }

      int code = exec_exit_code();
      ds_error("Failed to execute command: %s", strerror(errno));
      _exit(code);
    }

    int status;
    waitpid(cmd_pid, &status, 0);
    _exit(exit_status(status));
  }

  int status;
  waitpid(child, &status, 0);
  free_config_env_vars(cfg);
  return exit_status(status);
}

/* Other operations */

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
    } else if (cfg->format_output) {
      return show_containers(cfg);
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

  long long lim_mem, lim_quota, lim_period, lim_pids;
  ds_cgroup_get_limits(cfg->container_name, &lim_mem, &lim_quota, &lim_period,
                       &lim_pids);

  /* Success - print Host and detailed Container info */
  if (cfg->format_output) {
    struct ds_status st = {0};
    safe_strncpy(st.name, cfg->container_name, sizeof(st.name));
    st.pid = pid;
    safe_strncpy(st.hostname, cfg->hostname, sizeof(st.hostname));
    ds_anland_load_sock(cfg);
    safe_strncpy(st.anland_sock, cfg->anland_sock, sizeof(st.anland_sock));
    long ram_total = ds_collect_status(&st, 1);

    int first = 1;
    printf("{");
    ds_json_str("host_platform", is_android() ? "Android" : "Linux", &first);
    ds_json_str("host_arch", get_architecture(), &first);
    ds_json_status(&st, &first);
    ds_json_int("ram_total_kb", ram_total, &first);

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
    case DS_NET_MACVLAN:
      net = "macvlan";
      break;
    default:
      net = "host";
      break;
    }
    ds_json_str("networking_mode", net, &first);

    char buf[1024];
    size_t pos;
    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0])
        ds_json_str("nat_ip", ip, &first);

      if (cfg->upstream_iface_count > 0) {
        pos = 0;
        for (int i = 0; i < cfg->upstream_iface_count && pos < sizeof(buf); i++)
          pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                                  i ? "," : "", cfg->upstream_ifaces[i]);
        ds_json_str("upstream_interfaces", buf, &first);
      }
    } else if (cfg->net_mode == DS_NET_GATEWAY) {
      ds_json_str("gateway_container", cfg->gateway_container, &first);
      ds_json_str("gateway_net", cfg->gateway_net[0] ? cfg->gateway_net : "lan",
                  &first);
      if (cfg->gateway_bridge[0])
        ds_json_str("gateway_bridge", cfg->gateway_bridge, &first);
      ds_json_str("gateway_iface",
                  cfg->gateway_lan_ifname[0] ? cfg->gateway_lan_ifname : "eth1",
                  &first);
    } else if (cfg->net_mode == DS_NET_MACVLAN) {
      ds_json_str("macvlan_parent", cfg->macvlan_parent, &first);
      ds_json_str("macvlan_mode", ds_macvlan_mode_name(cfg->macvlan_mode),
                  &first);
    }

    ds_json_int("disable_ipv6", cfg->disable_ipv6, &first);
    if (is_android())
      ds_json_int("android_storage", cfg->android_storage, &first);

    ds_json_str("hw_access",
                cfg->hw_access ? "full" : (cfg->gpu_mode ? "GPU" : "none"),
                &first);

    ds_json_int("x11", cfg->x11, &first);
    if (is_android()) {
      if (cfg->tx11_extra_flags)
        ds_json_str("tx11_flags", cfg->tx11_extra_flags, &first);
      ds_json_int("virgl", cfg->virgl, &first);
      if (cfg->virgl_extra_flags)
        ds_json_str("virgl_flags", cfg->virgl_extra_flags, &first);
      ds_json_int("pulseaudio", cfg->pulseaudio, &first);
      ds_json_int("anland", cfg->anland, &first);
    }

    if (access("/sys/fs/selinux/enforce", R_OK) == 0)
      ds_json_str("selinux",
                  ds_get_selinux_status() == 0 ? "Permissive" : "Enforcing",
                  &first);

    ds_json_int("volatile_mode", cfg->volatile_mode, &first);
    ds_json_int("force_cgroup_v1", cfg->force_cgroupv1, &first);
    /* 0 is unlimited */
    ds_json_int("memory_limit", lim_mem, &first);
    ds_json_int("cpu_quota", lim_quota, &first);
    ds_json_int("cpu_period", lim_period, &first);
    ds_json_int("pids_limit", lim_pids, &first);
    ds_json_int("sandboxing_allowed", cfg->sandboxing_allowed, &first);
    ds_json_int("vts_allowed", cfg->allow_vts, &first);
    ds_json_int("foreground_mode", cfg->foreground, &first);
    ds_json_str("dns_servers", cfg->dns_servers, &first);

    pos = 0;
    for (int i = 0; i < cfg->port_forward_count && pos < sizeof(buf); i++) {
      struct ds_port_forward *pf = &cfg->port_forwards[i];
      if (pf->host_port_end == 0)
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%d:%d/%s",
                                i ? "," : "", pf->host_port, pf->container_port,
                                pf->proto);
      else
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                "%s%d-%d:%d-%d/%s", i ? "," : "", pf->host_port,
                                pf->host_port_end, pf->container_port,
                                pf->container_port_end, pf->proto);
    }
    ds_json_str("port_forwards", cfg->port_forward_count ? buf : "", &first);

    if (cfg->privileged_mask == DS_PRIV_FULL) {
      ds_json_str("privileged_mode", "full", &first);
    } else if (cfg->privileged_mask > 0) {
      pos = 0;
      const struct {
        int bit;
        const char *name;
      } bits[] = {{DS_PRIV_NOMASK, "nomask"},
                  {DS_PRIV_NOCAPS, "nocaps"},
                  {DS_PRIV_NOSEC, "noseccomp"},
                  {DS_PRIV_SHARED, "shared"}};
      for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++)
        if (cfg->privileged_mask & bits[i].bit)
          pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                                  pos ? "," : "", bits[i].name);
      ds_json_str("privileged_mode", buf, &first);
    }

    ds_json_int("bind_mount_count", cfg->bind_count, &first);
    ds_json_int("env_var_count", cfg->env_var_count, &first);
    printf("}\n");
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
    case DS_NET_MACVLAN:
      net = "macvlan";
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

    if (cfg->net_mode == DS_NET_MACVLAN) {
      printf("  Macvlan: %s (%s)\n", cfg->macvlan_parent,
             ds_macvlan_mode_name(cfg->macvlan_mode));
      feat_count++;
    }

    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0]) {
        printf("  NAT IP: %s\n", ip);
        feat_count++;
      }

      if (cfg->upstream_iface_count > 0) {
        printf("  Upstream (pinned): ");
        for (int i = 0; i < cfg->upstream_iface_count; i++)
          printf("%s%s", cfg->upstream_ifaces[i],
                 (i < cfg->upstream_iface_count - 1) ? ", " : "");
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

    /* 7. X11 */
    if (cfg->x11) {
      printf("  X11: enabled\n");
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

    /* 9b. Anland */
    if (is_android() && cfg->anland) {
      printf("  Anland: enabled\n");
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

    /* 14. Sandboxing (user namespaces) */
    if (cfg->sandboxing_allowed) {
      printf("  " C_RED "Sandboxing:" C_RESET " enabled\n");
      feat_count++;
    }

    /* 14b. Host virtual terminals */
    if (cfg->allow_vts) {
      printf("  " C_RED "Host VTs:" C_RESET " tty1-6 unmasked\n");
      feat_count++;
    }

    /* 15. Privileged Mode */
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
      }
      printf("\n");
      feat_count++;
    }

    /* 16. Bind Mounts */
    if (cfg->bind_count > 0) {
      printf("  Bind mounts: %d active\n", cfg->bind_count);
      feat_count++;
    }

    /* 17. Custom Init */
    if (cfg->custom_init[0]) {
      printf("  " C_RED "Custom Init:" C_RESET " %s\n", cfg->custom_init);
      feat_count++;
    }

    /* 18. Environment Variables */
    if (cfg->env_var_count > 0) {
      printf("  Env variables: %d loaded\n", cfg->env_var_count);
      feat_count++;
    }

    if (feat_count == 0) {
      printf("  None\n");
    }
  }

  /* Resource limits in force. Live usage only for the info command: right
   * after a start or restart there is nothing meaningful to report yet. */
  if (lim_mem || lim_quota || lim_pids) {
    long long mu = -1, cache = 0, cu = -1, pu = -1;
    if (!trust_cfg_pid)
      ds_cgroup_get_usage(cfg->container_name, &mu, &cache, &cu, &pu);
    /* Used as free(1) counts it: without the reclaimable file cache */
    if (mu >= cache)
      mu -= cache;
    printf("\n" C_GREEN "Resources:" C_RESET "\n");

    if (lim_mem) {
      char used[32], lim[32];
      ds_format_size(lim_mem, lim, sizeof(lim));
      if (mu >= 0) {
        ds_format_size(mu, used, sizeof(used));
        printf("  Memory : %s / %s\n", used, lim);
      } else {
        printf("  Memory : %s\n", lim);
      }
    }
    if (lim_quota) {
      double cores = (double)lim_quota / (double)lim_period;
      printf("  CPU    : %.2f cores", cores);
      long uptime = cu >= 0 ? ds_get_container_uptime(pid) : 0;
      /* Average use of the allowed cores since boot. cu is in usec. */
      if (uptime > 0)
        printf(" (Avg usage: %.1f%%)",
               ((double)cu / 1e6 / (double)uptime) / cores * 100.0);
      printf("\n");
    }
    if (lim_pids) {
      printf("  PIDs   : %lld", lim_pids);
      if (pu >= 0)
        printf(" (current: %lld)", pu);
      printf("\n");
    }
  }

  printf("\n");
  return 0;
}

/* The body of a restart. One lock covers the stop and the start, so nothing
 * else can act on the container in the gap where the old instance is gone and
 * the new one is not up yet. */
static int restart_rootfs_locked(struct ds_config *cfg, int *lock_fd,
                                 int timeout_seconds, int argc, char **argv) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }
  ds_log("Restarting container %s...", cfg->container_name);
  if (stop_rootfs_locked(cfg, 1, timeout_seconds) < 0) {
    return -1;
  }
  /* The stop above tore down using the booted snapshot (loaded while the
   * container was alive). It is gone now, so reloading by name returns the
   * workspace copy - reload it so host-side container.config edits made while
   * it ran take effect on this restart, then re-apply the CLI overrides
   * (--reset included) so every flag behaves exactly as it would on a fresh
   * start. An explicit --conf never read the booted snapshot in the first
   * place: cfg already holds that file plus the overrides, so skip the
   * reload entirely. start_rootfs re-derives the preserved mount from the
   * on-disk .mount sidecar, so losing the snapshot paths is fine. */
  if (!cfg->config_file_specified) {
    /* Clean slate first: the load only overlays keys present in the file, so
     * without the reset every conditionally-written key would keep its value
     * from the booted snapshot, and port/upstream lists would union across
     * the two loads instead of being replaced. */
    ds_config_reset_defaults(cfg);
    ds_config_load_by_name(cfg->container_name, cfg);
    if (argv && ds_apply_cli_overrides(argc, argv, cfg, 0) != 0)
      return -1;
  }
  putchar('\n');
  print_ds_banner();
  return start_rootfs_locked(cfg, lock_fd, 1);
}

int restart_rootfs_with_timeout(struct ds_config *cfg, int timeout_seconds,
                                int argc, char **argv) {
  int lock_fd = lifecycle_lock_or_complain(cfg, "restart");
  if (lock_fd < 0)
    return -1;

  int ret = restart_rootfs_locked(cfg, &lock_fd, timeout_seconds, argc, argv);
  ds_container_unlock(lock_fd);
  return ret;
}

int restart_rootfs(struct ds_config *cfg, int argc, char **argv) {
  return restart_rootfs_with_timeout(cfg, DS_STOP_TIMEOUT, argc, argv);
}
