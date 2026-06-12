/*
 * Droidspaces v6 - VirGL Server and Socket Manager
 *
 * Manages the virgl_test_server_android daemon lifecycle on Android
 * (spawning, logging, and stopping) and host-to-container socket bridging.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspace.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---- daemon child ----------------------------------------------------- */

struct virgl_args {
  char **extra;
  int extra_argc;
};

/* ready_fd: O_CLOEXEC write-end; EOF on execv success, byte on failure */
static void virgl_child_wrapper(int ready_fd, void *user_data) {
  struct virgl_args *args = (struct virgl_args *)user_data;

  /* Ignore hangups, keyboard interrupts, and broken pipes to make the server
   * process robust and persistent (except for SIGTERM which we use to stop it).
   */
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  /* Make VirGL server unkillable */
  ds_oom_protect();

  /* Enter droidspacesd domain -- best-effort, fallback on pre-reboot */
  ds_selinux_enter_domain();

  fprintf(stdout, "[VirGL] uid=%d starting server\n", (int)getuid());
  fflush(stdout);

  int total = 1 + args->extra_argc;
  char **argv = malloc((size_t)(total + 1) * sizeof(char *));
  if (!argv) {
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }
  argv[0] = TX11_VIRGL_BIN;
  for (int i = 0; i < args->extra_argc; i++)
    argv[1 + i] = args->extra[i];
  argv[total] = NULL;

  execv(argv[0], argv);
  perror("[VirGL] execv");
  free(argv);
  if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
  }
  _exit(1);
}

/* ---- spawn ------------------------------------------------------------ */

static pid_t spawn_virgl(const char *extra_flags) {
  /* Parse extra flags in the parent so rejection is visible on the terminal */
  char **extra = NULL;
  int extra_argc = 0;
  if (extra_flags && extra_flags[0]) {
    if (ds_split_flags(extra_flags, &extra, &extra_argc) < 0) {
      ds_warn(
          "VirGL: invalid virgl_extra_flags - launching without extra flags");
      extra = NULL;
      extra_argc = 0;
    }
  }

  struct virgl_args args = {
      .extra = extra,
      .extra_argc = extra_argc,
  };

  pid_t child = ds_spawn_daemon(virgl_child_wrapper, &args, "virgl.log",
                                "VirGL", "VirGL");

  ds_free_split_flags(extra, extra_argc);
  return child;
}

/* ---- public API ------------------------------------------------------- */

int ds_virgl_daemon_start(struct ds_config *cfg) {
  if (!cfg || !cfg->virgl || !is_android())
    return -1;
  if (getuid() != 0) {
    ds_error("VirGL: not running as root");
    return -1;
  }

  /* Check if binary exists */
  if (access(TX11_VIRGL_BIN, F_OK) != 0) {
    ds_warn("VirGL: server binary not found at %s - skipping start",
            TX11_VIRGL_BIN);
    return -1;
  }

  /* Reuse existing global server if still alive */
  pid_t existing = ds_daemon_read_pid("virgl.vpid");
  if (existing > 0) {
    ds_log("VirGL: server already running (PID %d)", (int)existing);
    cfg->virgl_pid = existing;
    return 1;
  }

  /* Clean up stale socket from a previous crashed run */
  unlink(TX11_VIRGL_SOCKET);

  ds_log("[VirGL] launching VirGL server (uid=%d)", (int)getuid());
  pid_t child = spawn_virgl(cfg->virgl_extra_flags);
  if (child > 0) {
    cfg->virgl_pid = child;
    ds_daemon_write_pid("virgl.vpid", child);
    return 0;
  }
  return -1;
}

void ds_virgl_daemon_stop(struct ds_config *cfg) {
  if (!cfg)
    return;
  ds_global_daemon_stop(check_virgl_needs, cfg->virgl_pid, &cfg->virgl_pid,
                        "virgl.vpid", TX11_VIRGL_SOCKET, "[VirGL]");
}

/* ---- socket bridge ---------------------------------------------------- */

int ds_setup_virgl_socket(struct ds_config *cfg) {
  if (!is_android() || !cfg->virgl)
    return 0;

  /* Post-pivot_root: host filesystem is accessible under /.old_root.
   * Use the same DS_TERMUX_TMP_OLDROOT prefix that X11 socket bridging uses,
   * otherwise stat() will always fail since the raw host path no longer
   * resolves inside the container's mount namespace. */
  char src[PATH_MAX];
  snprintf(src, sizeof(src), "%s/.virgl_test", DS_TERMUX_TMP_OLDROOT);

  struct stat st;
  if (stat(src, &st) != 0) {
    ds_warn("VirGL: socket not found at %s - skipping socket bridge", src);
    return 0;
  }

  uid_t uid = st.st_uid;

  if (ds_bind_mount_socket(src, DS_VIRGL_SOCKET, uid, "VirGL") < 0)
    return 0;

  ds_log("VirGL: socket bind-mounted into container");

  /* Set GALLIUM_DRIVER so mesa uses the virpipe backend for HW acceleration */
  setenv("GALLIUM_DRIVER", "virpipe", 1);

  return 0;
}
