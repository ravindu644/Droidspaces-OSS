/*
 * Droidspaces v6 - X11 Server and Socket Manager
 *
 * Manages the Termux-X11 server lifecycle on Android (spawning, logging, and
 * stopping) and unifies host-to-container X11 socket bridging for both Android
 * and Linux Desktop.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspace.h"
#include <fcntl.h>
#include <grp.h>
#include <sys/wait.h>

/* ---- helpers ---------------------------------------------------------- */

/*
 * Resolve Termux UID and verify that termux-x11 is installed.
 * Returns the UID on success, -1 on failure.
 */
static int resolve_termux_uid(void) {
  int uid = ds_resolve_termux_uid();
  if (uid < 0)
    return -1;

  if (grep_file(TX11_PACKAGES, "com.termux.x11 ") != 1) {
    ds_warn("Termux:X11: termux or termux-x11 package missing");
    return -1;
  }
  if (access(TX11_LOADER, F_OK) != 0) {
    ds_warn(
        "Termux:X11: loader.apk not found. Is termux-x11 package installed?");
    return -1;
  }
  return uid;
}

/* ---- xserver child ---------------------------------------------------- */

struct xserver_args {
  int uid;
  const char *display;
  char **extra;
  int extra_argc;
};

/*
 * Set up the forked child and exec app_process as the Termux-X11 server.
 * ready_fd: write-end of a pipe; closed by execv (O_CLOEXEC) on success,
 * written + closed on failure so the parent knows exec failed.
 * This function never returns on success.
 */
static void xserver_child_wrapper(int ready_fd, void *user_data) {
  struct xserver_args *args = (struct xserver_args *)user_data;

  /* Ignore hangups, keyboard interrupts, and broken pipes to make the server
   * process robust and persistent (except for SIGTERM which we use to stop it).
   */
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  /* Make Termux-X11 server unkillable.
   * This must be done while we are still running as root (before dropping
   * privileges). */
  ds_oom_protect();

  char ctx[256];
  if (get_selinux_context(TX11_DATA_DIR, ctx, sizeof(ctx)) < 0 &&
      get_selinux_context(TX11_DATA_ALT, ctx, sizeof(ctx)) < 0) {
    fprintf(stderr, "[X11] cannot read Termux SELinux context\n");
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }

  const char *mls = ds_extract_mls(ctx);
  if (!mls) {
    fprintf(stderr, "[X11] malformed SELinux context: %s\n", ctx);
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }

  /* Prepare environment */
  const char *ldp = getenv("LD_LIBRARY_PATH");
  const char *ldpr = getenv("LD_PRELOAD");
  const char *cp = getenv("CLASSPATH");
  setenv("XSTARTUP_LD_LIBRARY_PATH", ldp ? ldp : "", 1);
  setenv("XSTARTUP_LD_PRELOAD", ldpr ? ldpr : "", 1);
  setenv("XSTARTUP_CLASSPATH", cp ? cp : "", 1);
  unsetenv("LD_LIBRARY_PATH");
  unsetenv("LD_PRELOAD");
  setenv("CLASSPATH", TX11_LOADER, 1);
  setenv("TMPDIR", TX11_PREFIX "/tmp", 1);
  setenv("XKB_CONFIG_ROOT", TX11_PREFIX "/share/X11/xkb", 1);
  setenv("HOME", TX11_HOME, 1);

  /* Socket dir -- created as root before we drop privs */
  mkdir_p(TX11_SOCK_DIR, 01777);
  if (chown(TX11_SOCK_DIR, (uid_t)args->uid, (gid_t)args->uid) < 0) {
    /* ignore */
  }

  /* Drop privileges */
  if (ds_drop_privileges(args->uid) < 0) {
    perror("[X11] privilege drop failed");
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }

  /* SELinux dyntransition into untrusted_app_27 (Termux domain) */
  ds_selinux_dyntransition(mls);

  fprintf(stdout, "[X11] uid=%d display=%s\n", (int)getuid(), args->display);
  fflush(stdout);

  char nice[256];
  snprintf(nice, sizeof(nice), "--nice-name=termux-x11 com.termux.x11 %s",
           args->display);

  /* Base argv (7 slots: app_process … display + NULL) */
  char *base_argv[] = {
      "/system/bin/app_process",
      "-Xnoimage-dex2oat",
      "/",
      nice,
      "com.termux.x11.Loader",
      (char *)(uintptr_t)args->display,
      NULL,
  };
  int base_argc = 6; /* not counting NULL */

  /* Build final argv */
  int total = base_argc + args->extra_argc;
  char **argv = malloc((size_t)(total + 1) * sizeof(char *));
  if (!argv) {
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }
  for (int i = 0; i < base_argc; i++)
    argv[i] = base_argv[i];
  for (int i = 0; i < args->extra_argc; i++)
    argv[base_argc + i] = args->extra[i];
  argv[total] = NULL;

  execv(argv[0], argv);
  perror("[X11] execv");
  free(argv);
  if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
  }
  _exit(1);
}

/* ---- spawn ------------------------------------------------------------ */

/*
 * Fork the xserver child and a log-relay grandchild writing to Logs/x11.log.
 * Returns the xserver PID, or -1 on error.
 */
static pid_t spawn_xserver(int uid, const char *display,
                           const char *extra_flags) {
  /* Parse extra flags in the parent so rejection is visible on the terminal */
  char **extra = NULL;
  int extra_argc = 0;
  if (extra_flags && extra_flags[0]) {
    if (ds_split_flags(extra_flags, &extra, &extra_argc) < 0) {
      ds_warn("Termux:X11: invalid tx11_extra_flags - launching without extra "
              "flags");
      extra = NULL;
      extra_argc = 0;
    }
  }

  struct xserver_args args = {
      .uid = uid,
      .display = display,
      .extra = extra,
      .extra_argc = extra_argc,
  };

  pid_t child = ds_spawn_daemon(xserver_child_wrapper, &args, "x11.log", "X11",
                                "Termux:X11");

  ds_free_split_flags(extra, extra_argc);
  return child;
}

/* ---- public API ------------------------------------------------------- */

int ds_x11_daemon_start(struct ds_config *cfg) {
  if (!cfg || !cfg->termux_x11 || !is_android())
    return -1;
  if (getuid() != 0) {
    ds_error("[X11] not running as root");
    return -1;
  }

  /* Reuse existing global server if still alive */
  pid_t existing = ds_daemon_read_pid("x11.xpid");
  if (existing > 0) {
    ds_log("Termux:X11: xserver already running (PID %d)", (int)existing);
    cfg->x11_pid = existing;
    return 1;
  }

  int uid = resolve_termux_uid();
  if (uid < 0)
    return -1;

  ds_log("[X11] launching Termux-X11 server (uid=%d)", uid);
  pid_t child = spawn_xserver(uid, TX11_DISPLAY_STR, cfg->tx11_extra_flags);
  if (child > 0) {
    cfg->x11_pid = child;
    ds_daemon_write_pid("x11.xpid", child);
    return 0;
  }
  return -1;
}

void ds_x11_daemon_stop(struct ds_config *cfg) {
  if (!cfg)
    return;
  ds_global_daemon_stop(check_x11_needs, cfg->x11_pid, &cfg->x11_pid,
                        "x11.xpid", TX11_SOCK_DIR "/" TX11_DISPLAY_SOCK,
                        "[X11]");
}

/* ---- socket bridge ---------------------------------------------------- */

int ds_setup_x11_socket(struct ds_config *cfg) {
  if (!is_android()) {
    /* Desktop Linux path */
    char src[PATH_MAX], dst[PATH_MAX];
    snprintf(src, sizeof(src), "%s/X0", DS_X11_PATH_DESKTOP);
    if (access(src, F_OK) != 0) {
      ds_warn("X11 support skipped: no host X11 socket detected at %s", src);
      return 0;
    }
    mkdir_p(DS_X11_CONTAINER_DIR, 01777);
    snprintf(dst, sizeof(dst), "%s/X0", DS_X11_CONTAINER_DIR);
    if (ds_bind_mount_socket(src, dst, 0, "X11") < 0)
      return -1;
    ds_log("X11: Bridged host X11 socket (X0) with container");
    return 0;
  }

  /* Android path */
  if (!cfg->termux_x11)
    return 0;

  char src_dir[PATH_MAX], src[PATH_MAX], dst[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/.X11-unix", DS_TERMUX_TMP_OLDROOT);
  snprintf(src, sizeof(src), "%s/.X11-unix/" TX11_DISPLAY_SOCK,
           DS_TERMUX_TMP_OLDROOT);

  struct stat st;
  if (stat(src_dir, &st) != 0) {
    ds_warn("Termux:X11: .X11-unix not found at %s - skipping socket bridge",
            src_dir);
    return 0;
  }
  if (access(src, F_OK) != 0) {
    ds_warn("Termux:X11: " TX11_DISPLAY_SOCK
            " socket not found at %s - skipping socket bridge",
            src);
    return 0;
  }

  uid_t termux_uid = st.st_uid;
  mkdir_p(DS_X11_CONTAINER_DIR, 01777);
  if (chown(DS_X11_CONTAINER_DIR, termux_uid, termux_uid) < 0) {
    /* ignore */
  }
  chmod(DS_X11_CONTAINER_DIR, 01777);

  snprintf(dst, sizeof(dst), "%s/" TX11_DISPLAY_SOCK, DS_X11_CONTAINER_DIR);
  if (ds_bind_mount_socket(src, dst, termux_uid, "X11") < 0)
    return 0;

  ds_log("[X11] " TX11_DISPLAY_SOCK
         " socket bind-mounted into container (uid=%d)",
         (int)termux_uid);
  ds_log("Termux:X11: display is " TX11_DISPLAY_STR " (X%d bind mount)",
         TX11_DISPLAY_NUM);

  /* Inject DISPLAY into the container environment so all processes see it */
  setenv("DISPLAY", TX11_DISPLAY_STR, 1);

  return 0;
}
