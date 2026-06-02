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

/* SELinux domains to try, newest first */
static const char *const untrusted_domains[] = {
    "u:r:untrusted_app",    "u:r:untrusted_app_32", "u:r:untrusted_app_30",
    "u:r:untrusted_app_29", "u:r:untrusted_app_27", "u:r:untrusted_app_25",
};

/* ---- helpers ---------------------------------------------------------- */

/*
 * Extract MLS categories from a full SELinux context string.
 * e.g. "u:object_r:app_data_file:s0:c78,c257,c512,c768"
 *                                    ^ returned pointer
 */
static const char *extract_mls(const char *ctx) {
  int colons = 0;
  for (const char *p = ctx; *p; p++)
    if (*p == ':' && ++colons == 3)
      return p + 1;
  return NULL;
}

/*
 * Resolve Termux UID and verify that termux-x11 is installed.
 * Returns the UID on success, -1 on failure.
 */
static int resolve_termux_uid(void) {
  FILE *f = fopen(TX11_PACKAGES, "r");
  if (!f) {
    ds_warn("[X11] cannot open packages.list (%s)", TX11_PACKAGES);
    return -1;
  }

  char line[1024];
  int uid = -1, x11_ok = 0;

  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "com.termux ", 11) == 0)
      uid = atoi(line + 11);
    if (strncmp(line, "com.termux.x11 ", 15) == 0)
      x11_ok = 1;
  }
  fclose(f);

  if (uid < 0 || !x11_ok) {
    ds_warn("[X11] termux or termux-x11 package missing");
    return -1;
  }
  if (access(TX11_LOADER, F_OK) != 0) {
    ds_warn("[X11] loader.apk not found");
    return -1;
  }
  return uid;
}

/* Read PID from the global x11 pidfile; returns pid on success, -1 otherwise */
static pid_t x11_read_pid(void) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/x11.xpid", get_pids_dir());

  char buf[32] = {0};
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n <= 0)
    return -1;
  pid_t pid = (pid_t)atoi(buf);
  return (pid > 1 && kill(pid, 0) == 0) ? pid : -1;
}

/* Write PID to the global x11 pidfile */
static void x11_write_pid(pid_t pid) {
  char path[PATH_MAX], buf[32];
  snprintf(path, sizeof(path), "%s/x11.xpid", get_pids_dir());
  snprintf(buf, sizeof(buf), "%d", (int)pid);
  write_file_atomic(path, buf);
}

/* Remove the global x11 pidfile */
static void x11_remove_pid(void) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/x11.xpid", get_pids_dir());
  unlink(path);
}

/* ---- xserver child ---------------------------------------------------- */

/*
 * Set up the forked child and exec app_process as the Termux-X11 server.
 * ready_fd: write-end of a pipe; closed by execv (O_CLOEXEC) on success,
 * written + closed on failure so the parent knows exec failed.
 * This function never returns on success.
 */
static void __attribute__((noreturn))
xserver_child(int uid, const char *display, int ready_fd) {
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
  FILE *oom_f = fopen("/proc/self/oom_score_adj", "w");
  if (oom_f) {
    fprintf(oom_f, "-1000\n");
    fclose(oom_f);
  }

  char ctx[256];
  if (get_selinux_context(TX11_DATA_DIR, ctx, sizeof(ctx)) < 0 &&
      get_selinux_context(TX11_DATA_ALT, ctx, sizeof(ctx)) < 0) {
    fprintf(stderr, "[X11] cannot read Termux SELinux context\n");
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }

  const char *mls = extract_mls(ctx);
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
  if (chown(TX11_SOCK_DIR, (uid_t)uid, (gid_t)uid) < 0) {
    /* ignore */
  }

  /* Drop privileges */
  gid_t groups[] = {(gid_t)uid, 1003, 1004, 1011, 1015, 1028, 3003, 9997};
  if (setgroups(sizeof(groups) / sizeof(groups[0]), groups) < 0 ||
      setresgid(uid, uid, uid) < 0 || setresuid(uid, uid, uid) < 0) {
    perror("[X11] privilege drop failed");
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }

  /* SELinux dyntransition into untrusted_app */
  char target[256] = "";
  int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
  if (fd >= 0) {
    for (size_t i = 0;
         i < sizeof(untrusted_domains) / sizeof(untrusted_domains[0]); i++) {
      snprintf(target, sizeof(target), "%s:%s", untrusted_domains[i], mls);
      if (write(fd, target, strlen(target) + 1) > 0)
        break;
    }
    close(fd);
  }

  /* Clear stale exec context */
  fd = open("/proc/self/attr/exec", O_WRONLY | O_CLOEXEC);
  if (fd >= 0) {
    if (write(fd, "\0", 1) < 0) {
      /* ignore */
    }
    close(fd);
  }

  fprintf(stdout, "[X11] ctx=%s uid=%d display=%s\n", target, (int)getuid(),
          display);
  fflush(stdout);

  char nice[256];
  snprintf(nice, sizeof(nice), "--nice-name=termux-x11 com.termux.x11 %s",
           display);
  char *argv[] = {
      "/system/bin/app_process", "-Xnoimage-dex2oat",        "/",  nice,
      "com.termux.x11.Loader",   (char *)(uintptr_t)display, NULL,
  };
  execv(argv[0], argv);
  perror("[X11] execv");
  if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
  }
  _exit(1);
}

/* ---- spawn + log relay ------------------------------------------------ */

/*
 * Fork the xserver child and a log-relay grandchild writing to Logs/x11.log.
 * Returns the xserver PID, or -1 on error.
 */
static pid_t spawn_xserver(int uid, const char *display) {
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    ds_warn("[X11] pipe: %s", strerror(errno));
    return -1;
  }

  /* ready pipe: EOF = execv succeeded (O_CLOEXEC), byte = execv failed */
  int readyfd[2];
  if (pipe2(readyfd, O_CLOEXEC) < 0) {
    ds_warn("[X11] pipe2: %s", strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  pid_t child = fork();
  if (child < 0) {
    ds_warn("[X11] fork: %s", strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    close(readyfd[0]);
    close(readyfd[1]);
    return -1;
  }
  if (child == 0) {
    close(pipefd[0]);
    close(readyfd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    xserver_child(uid, display, readyfd[1]);
    /* unreachable; xserver_child never returns */
  }

  close(pipefd[1]);
  close(readyfd[1]);

  /* EOF = exec succeeded; byte = exec failed */
  char rdy;
  if (read(readyfd[0], &rdy, 1) > 0) {
    ds_error("[X11] execv failed -- xserver did not start");
    waitpid(child, NULL, 0);
    close(readyfd[0]);
    close(pipefd[0]);
    return -1;
  }
  close(readyfd[0]);

  pid_t relay = fork();
  if (relay == 0) {
    /* Ignore hangups, keyboard interrupts, broken pipes, and SIGTERM.
     * The relay should only exit when the child process closes the pipe. */
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    /* Make log relay unkillable */
    FILE *oom_f = fopen("/proc/self/oom_score_adj", "w");
    if (oom_f) {
      fprintf(oom_f, "-1000\n");
      fclose(oom_f);
    }

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/x11.log", get_logs_dir());
    rotate_log(path, 2 * 1024 * 1024);
    int log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0) {
      close(pipefd[0]);
      _exit(0);
    }

    FILE *ps = fdopen(pipefd[0], "r");
    if (!ps) {
      close(log_fd);
      close(pipefd[0]);
      _exit(0);
    }

    char line[2048];
    while (fgets(line, sizeof(line), ps)) {
      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
      if (len == 0)
        continue;
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      struct tm tm;
      localtime_r(&ts.tv_sec, &tm);
      dprintf(log_fd, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [X11] %s\n",
              tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
              tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, line);
    }
    fclose(ps);
    close(log_fd);
    _exit(0);
  }

  close(pipefd[0]);
  ds_log("Termux:X11: xserver pid=%d launched", (int)child);
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
  pid_t existing = x11_read_pid();
  if (existing > 0) {
    ds_log("Termux:X11: xserver already running (PID %d)", (int)existing);
    cfg->x11_pid = existing;
    return 1;
  }

  int uid = resolve_termux_uid();
  if (uid < 0)
    return -1;

  ds_log("[X11] launching Termux-X11 server (uid=%d)", uid);
  pid_t child = spawn_xserver(uid, TX11_DISPLAY_STR);
  if (child > 0) {
    cfg->x11_pid = child;
    x11_write_pid(child);
    return 0;
  }
  return -1;
}

void ds_x11_daemon_stop(struct ds_config *cfg) {
  if (!cfg || !is_android())
    return;

  /* Keep the server alive if any other running container still needs X11 */
  if (check_x11_needs() == 1) {
    ds_log(
        "[X11] keeping global X11 server running for other active containers");
    return;
  }

  pid_t pid = cfg->x11_pid > 0 ? cfg->x11_pid : x11_read_pid();
  if (pid > 0) {
    ds_log("[X11] terminating Termux-X11 server (PID %d)...", (int)pid);
    kill(pid, SIGTERM);
    for (int i = 0; i < 10 && kill(pid, 0) == 0; i++)
      usleep(100000);
    if (kill(pid, 0) == 0) {
      kill(pid, SIGKILL);
      waitpid(pid, NULL, 0);
    }
    cfg->x11_pid = 0;
  }

  x11_remove_pid();
  unlink(TX11_SOCK_DIR "/" TX11_DISPLAY_SOCK);
}

/* ---- socket bridge ---------------------------------------------------- */

/*
 * Bind-mount an X0 socket file from src into the container at dst.
 * Creates the target file node if absent.
 */
static int bind_x0(const char *src, const char *dst, uid_t uid) {
  int fd = open(dst, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
  if (fd >= 0) {
    close(fd);
    if (chown(dst, uid, uid) < 0) {
      /* ignore */
    }
    chmod(dst, 0666);
  }
  if (mount(src, dst, NULL, MS_BIND, NULL) != 0) {
    ds_warn("[X11] failed to bind-mount X0 socket: %s", strerror(errno));
    return -1;
  }
  return 0;
}

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
    if (bind_x0(src, dst, 0) < 0)
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
    ds_warn("[X11] .X11-unix not found at %s - skipping socket bridge",
            src_dir);
    return 0;
  }
  if (access(src, F_OK) != 0) {
    ds_warn("[X11] " TX11_DISPLAY_SOCK
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
  if (bind_x0(src, dst, termux_uid) < 0)
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
