/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/* Load the config the running instance pid actually booted with, from its
 * monitor, never from the copy in the guest's own /run, which root in the
 * container can rewrite. Returns 0 and fills *out, which the caller releases
 * with ds_config_free(), or -1 if no monitor answers for that very pid: one
 * whose init just died still answers for half a second with its own. */
static int load_booted_config(const struct ds_config *cfg, pid_t pid,
                              struct ds_config *out) {
  ds_config_init(out);
  safe_strncpy(out->container_name, cfg->container_name,
               sizeof(out->container_name));
  safe_strncpy(out->prog_name, cfg->prog_name, sizeof(out->prog_name));

  pid_t booted_pid = 0;
  if (ds_cmd_get_config(cfg->container_name, out, &booted_pid) < 0 ||
      booted_pid != pid) {
    ds_config_free(out);
    return -1;
  }

  /* The pidfile is how the caller found this instance, and it may have come
   * from --pidfile, which no config file records. */
  safe_strncpy(out->pidfile, cfg->pidfile, sizeof(out->pidfile));
  return 0;
}

/* Set the mode of a runit flag file through an fd, so a FIFO or device the
 * guest planted at the path is refused instead of blocked on or followed. */
static int runit_flag(const char *path, int flags, mode_t mode) {
  int fd = open(path, flags | O_WRONLY | O_NONBLOCK | O_CLOEXEC, 0);
  struct stat st;
  int ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
           fchmod(fd, mode) == 0;
  if (fd >= 0)
    close(fd);
  return ok ? 0 : -1;
}

/* runit shuts down on SIGCONT only while /etc/runit/stopit carries the owner
 * execute bit, so this does what runit-init 0 does before it signals: create
 * the flag, set the bit, clear the reboot flag so stage 3 halts. Void and
 * Debian ship stopit as an absolute symlink into /run, which through
 * /proc/<pid>/root would resolve against the host, so a child steps into the
 * container's mount namespace first. */
static void runit_request_halt(pid_t pid, struct ds_config *cfg) {
  pid_t child = fork();
  if (child == 0) {
    if (enter_namespace(pid, cfg) < 0 ||
        runit_flag("/etc/runit/stopit", O_CREAT, 0100) < 0)
      _exit(1);
    runit_flag("/etc/runit/reboot", 0, 0);
    _exit(0);
  }
  int st;
  if (child < 0 || waitpid(child, &st, 0) < 0 || !WIFEXITED(st) ||
      WEXITSTATUS(st))
    ds_warn("runit: could not set /etc/runit/stopit, SIGCONT may be ignored");
}

/* The body of a stop. The caller holds the lifecycle lock. */
int stop_rootfs_locked(struct ds_config *caller_cfg, int skip_unmount,
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
   * the edited config file or the booted one with new flags applied on top. If
   * that changed the network mode, cleaning up with it runs the wrong
   * teardown, e.g. the gateway cleanup for a container wired as NAT, and
   * leaves its veth, port forwards and shared rules behind. What its monitor
   * holds is the only description of what it was booted as. Without an
   * answer (a monitor from an older build, or one already tearing down) the
   * caller's config is the best we have. */
  struct ds_config booted;
  int have_booted = (load_booted_config(caller_cfg, pid, &booted) == 0);
  struct ds_config *cfg = have_booted ? &booted : caller_cfg;

  ds_log("Stopping container '%s' (PID %d)...", cfg->container_name, pid);

  /* The booted config names the image file, not where it is mounted. The mount
   * point is a function of the name, so look there. A directory container
   * does not claim a leftover image mount under its name: the firmware path
   * and unmount below would then act on a mount it never owned. An image
   * deleted under a running container reloads as a directory and leaves its
   * mount for the next scan or start to clear. */
  if (cfg->img_mount_point[0] == '\0' && cfg->rootfs_img_path[0]) {
    char expected[PATH_MAX];
    if (ds_img_mount_point(cfg->container_name, expected, sizeof(expected)) ==
            0 &&
        is_mountpoint(expected))
      safe_strncpy(cfg->img_mount_point, expected,
                   sizeof(cfg->img_mount_point));
  }

  /* 1. Send shutdown signal. */
  if (cfg->custom_init[0]) {
    kill(pid, SIGKILL);
  } else {
    /* Detect the init system through the live container's root rather than a
     * host path: that is right for a volatile overlay, a --pidfile-only
     * invocation and a container whose monitor did not answer alike. */
    ds_init_type_t init_type = DS_INIT_UNKNOWN;
    char probe_root[PATH_MAX];
    if (build_proc_root_path(pid, "", probe_root, sizeof(probe_root)) == 0)
      init_type = detect_container_init(probe_root);

    if (init_type == DS_INIT_SYSVINIT) {
      /* sysvinit ignores all signals for shutdown -- it only listens on the
       * initctl FIFO. Write a telinit-compatible init_request struct directly
       * into the container's /run/initctl via /proc/<pid>/root. */

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

      /* Fallback: /dev/initctl, the historical path Slackware uses, unless
       * the container's /dev is the host's devtmpfs (hw_access), where it
       * would be the host's own. A symlink at any step could aim us at the
       * host's initctl just as well. */
      int fd = ds_open_in_root(pid, "/run/initctl", O_WRONLY | O_NONBLOCK);
      if (fd < 0 && !cfg->hw_access)
        fd = ds_open_in_root(pid, "/dev/initctl", O_WRONLY | O_NONBLOCK);

      /* Only write into an actual FIFO.  The container fully controls
       * /run/initctl inside its own root: ds_open_in_root rejects a symlink
       * at any step, and the S_ISFIFO check rejects a regular file the
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
    } else {
      /* Each family's own shutdown request; SIGTERM when we cannot tell. */
      int sig = SIGTERM;
      if (init_type == DS_INIT_PROCD || init_type == DS_INIT_S6 ||
          init_type == DS_INIT_BUSYBOX)
        sig = SIGUSR2;
      else if (init_type == DS_INIT_RUNIT) {
        runit_request_halt(pid, cfg);
        sig = SIGCONT;
      } else if (init_type == DS_INIT_SYSTEMD)
        sig = DS_SIG_STOP; /* SIGRTMIN+3 */
      else if (init_type == DS_INIT_OPENRC)
        sig = SIGPWR;
      kill(pid, sig);
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

  /* The monitor's route monitor thread puts the shared NAT rules back when it
   * finds them missing, and only the monitor can join it. Wait for that before
   * pulling the rules, or they come back and nobody removes them: the monitor
   * concedes the cleanup to us once it sees the pidfile gone. */
  if (!unkillable)
    ds_container_wait_helpers(cfg->container_name, 5000);

  /* 5. Complete resource cleanup. */
  cleanup_container_resources(cfg, pid, skip_unmount, unkillable);
  ds_container_prune_supervision(cfg->container_name);

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
