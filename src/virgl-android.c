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

/* Read PID from the global virgl pidfile */
static pid_t virgl_read_pid(void) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/virgl.vpid", get_pids_dir());

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

/* Write PID to the global virgl pidfile */
static void virgl_write_pid(pid_t pid) {
  char path[PATH_MAX], buf[32];
  snprintf(path, sizeof(path), "%s/virgl.vpid", get_pids_dir());
  snprintf(buf, sizeof(buf), "%d", (int)pid);
  write_file_atomic(path, buf);
}

/* Remove the global virgl pidfile */
static void virgl_remove_pid(void) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/virgl.vpid", get_pids_dir());
  unlink(path);
}

/* ---- daemon child ----------------------------------------------------- */

/* ready_fd: O_CLOEXEC write-end; EOF on execv success, byte on failure */
static void __attribute__((noreturn)) virgl_child(int ready_fd) {
  /* Ignore hangups, keyboard interrupts, and broken pipes to make the server
   * process robust and persistent (except for SIGTERM which we use to stop it).
   */
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  /* Make VirGL server unkillable */
  FILE *oom_f = fopen("/proc/self/oom_score_adj", "w");
  if (oom_f) {
    fprintf(oom_f, "-1000\n");
    fclose(oom_f);
  }

  fprintf(stdout, "[VirGL] uid=%d starting server\n", (int)getuid());
  fflush(stdout);

  char *argv[] = {TX11_VIRGL_BIN, NULL};
  execv(argv[0], argv);
  perror("[VirGL] execv");
  if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
  }
  _exit(1);
}

/* ---- spawn + log relay ------------------------------------------------ */

static pid_t spawn_virgl(void) {
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    ds_warn("[VirGL] pipe: %s", strerror(errno));
    return -1;
  }

  /* ready pipe: EOF = execv succeeded (O_CLOEXEC), byte = execv failed */
  int readyfd[2];
  if (pipe2(readyfd, O_CLOEXEC) < 0) {
    ds_warn("[VirGL] pipe2: %s", strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  pid_t child = fork();
  if (child < 0) {
    ds_warn("[VirGL] fork: %s", strerror(errno));
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
    virgl_child(readyfd[1]);
  }

  close(pipefd[1]);
  close(readyfd[1]);

  /* EOF = exec succeeded; byte = exec failed */
  char rdy;
  if (read(readyfd[0], &rdy, 1) > 0) {
    ds_error("[VirGL] execv failed -- server did not start");
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
    snprintf(path, sizeof(path), "%s/virgl.log", get_logs_dir());
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
      dprintf(log_fd, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [VirGL] %s\n",
              tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
              tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, line);
    }
    fclose(ps);
    close(log_fd);
    _exit(0);
  }

  close(pipefd[0]);
  ds_log("VirGL: server pid=%d launched", (int)child);
  return child;
}

/* ---- public API ------------------------------------------------------- */

int ds_virgl_daemon_start(struct ds_config *cfg) {
  if (!cfg || !cfg->virgl || !is_android())
    return -1;
  if (getuid() != 0) {
    ds_error("[VirGL] not running as root");
    return -1;
  }

  /* Check if binary exists */
  if (access(TX11_VIRGL_BIN, F_OK) != 0) {
    ds_warn("[VirGL] server binary not found at %s - skipping start",
            TX11_VIRGL_BIN);
    return -1;
  }

  /* Reuse existing global server if still alive */
  pid_t existing = virgl_read_pid();
  if (existing > 0) {
    ds_log("VirGL: server already running (PID %d)", (int)existing);
    cfg->virgl_pid = existing;
    return 1;
  }

  /* Clean up stale socket from a previous crashed run */
  unlink(TX11_VIRGL_SOCKET);

  ds_log("[VirGL] launching VirGL server (uid=%d)", (int)getuid());
  pid_t child = spawn_virgl();
  if (child > 0) {
    cfg->virgl_pid = child;
    virgl_write_pid(child);
    return 0;
  }
  return -1;
}

void ds_virgl_daemon_stop(struct ds_config *cfg) {
  if (!cfg || !is_android())
    return;

  /* Keep the server alive if any other running container still needs VirGL */
  if (check_virgl_needs() == 1) {
    ds_log("[VirGL] keeping global VirGL server running for other active "
           "containers");
    return;
  }

  pid_t pid = cfg->virgl_pid > 0 ? cfg->virgl_pid : virgl_read_pid();
  if (pid > 0) {
    ds_log("[VirGL] terminating VirGL server (PID %d)...", (int)pid);
    kill(pid, SIGTERM);
    for (int i = 0; i < 10 && kill(pid, 0) == 0; i++)
      usleep(100000);
    if (kill(pid, 0) == 0) {
      kill(pid, SIGKILL);
      waitpid(pid, NULL, 0);
    }
    cfg->virgl_pid = 0;
  }

  virgl_remove_pid();
  unlink(TX11_VIRGL_SOCKET);
}

/* ---- socket bridge ---------------------------------------------------- */

static int bind_virgl_socket(const char *src, const char *dst, uid_t uid) {
  int fd = open(dst, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
  if (fd >= 0) {
    close(fd);
    if (chown(dst, uid, uid) < 0) { /* ignore */
    }
    chmod(dst, 0666);
  }
  if (mount(src, dst, NULL, MS_BIND, NULL) != 0) {
    ds_warn("[VirGL] failed to bind-mount socket: %s", strerror(errno));
    return -1;
  }
  return 0;
}

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
    ds_warn("[VirGL] socket not found at %s - skipping socket bridge", src);
    return 0;
  }

  uid_t uid = st.st_uid;

  if (bind_virgl_socket(src, DS_VIRGL_SOCKET, uid) < 0)
    return 0;

  ds_log("VirGL: socket bind-mounted into container (uid=%d)", (int)uid);

  /* Set GALLIUM_DRIVER so mesa uses the virpipe backend for HW acceleration */
  setenv("GALLIUM_DRIVER", "virpipe", 1);

  return 0;
}
