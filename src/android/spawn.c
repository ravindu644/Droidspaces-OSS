/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <time.h>

/* Daemon lifecycle helpers
 *
 * Shared building blocks for x11.c, virgl-android.c, pulseaudio-android.c,
 * monitor.c, and daemon.c.  Generic (not Android-specific). */

/* A daemon pidfile is "<pid> <starttime>\n<flags>". Pids/ survives a device
 * reboot, and by then the pid can belong to any process, which a stop would
 * SIGKILL. The starttime ties it to the process that was spawned. A file
 * written before the starttime existed has none, so *legacy is set and the
 * pid is trusted as it always was. The file lands in buf for the flags. */
static pid_t daemon_pidfile_load(const char *filename, char *buf, size_t size,
                                 int *legacy) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", get_pids_dir(), filename);
  if (read_file(path, buf, size) < 0)
    return -1;
  char *end;
  pid_t pid = (pid_t)strtol(buf, &end, 10);
  unsigned long long start = *end == ' ' ? strtoull(end + 1, NULL, 10) : 0;
  if (legacy)
    *legacy = start == 0;
  if (pid <= 1 || kill(pid, 0) != 0)
    return -1;
  return (start == 0 || proc_starttime(pid) == start) ? pid : -1;
}

/* Read a daemon PID from <pids_dir>/<filename>; returns pid or -1. */
pid_t ds_daemon_read_pid(const char *filename) {
  char buf[4096];
  return daemon_pidfile_load(filename, buf, sizeof(buf), NULL);
}

/* Write PID to <pids_dir>/<filename>, with the flags the server was spawned
 * with for ds_global_daemon_reuse() to compare. */
void ds_daemon_write_pid(const char *filename, pid_t pid, const char *flags) {
  char path[PATH_MAX], buf[4096];
  snprintf(path, sizeof(path), "%s/%s", get_pids_dir(), filename);
  snprintf(buf, sizeof(buf), "%d %llu\n%s", (int)pid, proc_starttime(pid),
           flags ? flags : "");
  write_file_atomic(path, buf);
}

/* Remove <pids_dir>/<filename>. */
void ds_daemon_remove_pid(const char *filename) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", get_pids_dir(), filename);
  unlink(path);
}

/* Resolve a cached pid or fall back to the pidfile. */
pid_t ds_resolve_daemon_pid(pid_t cached, const char *pidfile) {
  return cached > 0 ? cached : ds_daemon_read_pid(pidfile);
}

static void daemon_kill(pid_t pid, const char *pidfile, const char *sock_path,
                        const char *tag) {
  if (pid > 0) {
    ds_log("%s terminating (PID %d)...", tag, (int)pid);
    kill(pid, SIGTERM);
    for (int i = 0; i < 10 && kill(pid, 0) == 0; i++)
      usleep(100000);
    if (kill(pid, 0) == 0) {
      kill(pid, SIGKILL);
      waitpid(pid, NULL, 0);
    }
  }
  ds_daemon_remove_pid(pidfile);
  if (sock_path)
    unlink(sock_path);
}

/*
 * ds_global_daemon_stop - unified teardown for X11/VirGL/PulseAudio daemons.
 *
 * check_fn    : feature-needs checker (returns 1 if still needed)
 * cached_pid  : cfg->x11_pid / cfg->virgl_pid / cfg->pulse_pid
 * pid_out     : pointer to that field (zeroed on stop)
 * pidfile     : e.g. "x11.xpid"
 * sock_path   : socket to unlink after stop
 * tag         : log prefix e.g. "[X11]"
 */
void ds_global_daemon_stop(int (*check_fn)(void), pid_t cached_pid,
                           pid_t *pid_out, const char *pidfile,
                           const char *sock_path, const char *tag) {
  if (!is_android())
    return;

  if (check_fn() == 1) {
    ds_log("%s keeping global server running for other active containers", tag);
    return;
  }

  pid_t pid = ds_resolve_daemon_pid(cached_pid, pidfile);
  if (pid > 0 && pid_out)
    *pid_out = 0;
  daemon_kill(pid, pidfile, sock_path, tag);
}

/* A global server's flags are fixed when it spawns, and a restart keeps the
 * server alive for the next boot, so reusing it blindly meant edited extra
 * flags never reached it. Reuse it only while its flags match this config's.
 * A mismatched server is replaced unless another running container shares
 * it. A legacy pidfile records neither flags nor identity, so that server is
 * reused as before and the next full stop moves it to the new format.
 * Returns the pid to reuse, or -1 when the caller should spawn. */
pid_t ds_global_daemon_reuse(int (*check_fn)(void), const char *pidfile,
                             const char *flags, const char *sock_path,
                             const char *tag) {
  char buf[4096];
  int legacy;
  pid_t pid = daemon_pidfile_load(pidfile, buf, sizeof(buf), &legacy);
  if (pid <= 0)
    return -1;

  const char *nl = strchr(buf, '\n');
  if (legacy || strcmp(nl ? nl + 1 : "", flags ? flags : "") == 0)
    return pid;

  if (check_fn() == 1) {
    ds_warn("%s shared with another running container, new extra flags apply "
            "once it stops",
            tag);
    return pid;
  }
  ds_log("%s extra flags changed, restarting the server", tag);
  daemon_kill(pid, pidfile, sock_path, tag);
  return -1;
}

void ds_daemon_child_preamble(void) {
  /* Ignore hangups, keyboard interrupts, and broken pipes so the helper
   * survives terminal disconnects (SIGTERM remains the shutdown signal). */
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  /* Protect from the OOM killer while still root, before any privilege drop. */
  ds_oom_protect();
}

/*
 * Fork a log-relay child that reads from pipe_read_fd and writes timestamped
 * lines to <logs_dir>/<log_file> with [tag] prefix.  The relay ignores all
 * signals, calls ds_oom_protect(), and exits when the pipe reaches EOF.
 * pipe_read_fd is closed in the parent after this returns.
 */
void ds_spawn_log_relay(int pipe_read_fd, const char *log_file,
                        const char *tag) {
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
    ds_oom_protect();

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", get_logs_dir(), log_file);
    rotate_log(path, 2 * 1024 * 1024);
    int log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0) {
      close(pipe_read_fd);
      _exit(0);
    }

    FILE *ps = fdopen(pipe_read_fd, "r");
    if (!ps) {
      close(log_fd);
      close(pipe_read_fd);
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
      dprintf(log_fd, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] %s\n",
              tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
              tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, tag, line);
    }
    fclose(ps);
    close(log_fd);
    _exit(0);
  }

  close(pipe_read_fd);
}

/*
 * Fork a daemon child, redirect stdout/stderr through a pipe, verify exec
 * via a ready-fd, then spawn a log relay.
 *
 * child_fn  : called in the child process (must exec or _exit)
 * user_data : passed through to child_fn
 * log_file  : filename for log relay (e.g. "x11.log")
 * tag       : log tag (e.g. "X11")
 * label     : human-readable label for error messages (e.g. "Termux:X11")
 *
 * Returns child PID on success, -1 on error.
 */
pid_t ds_spawn_daemon(ds_child_fn child_fn, void *user_data,
                      const char *log_file, const char *tag,
                      const char *label) {
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    ds_warn("[%s] pipe: %s", tag, strerror(errno));
    return -1;
  }

  /* ready pipe: EOF = execv succeeded (O_CLOEXEC), byte = execv failed */
  int readyfd[2];
  if (pipe2(readyfd, O_CLOEXEC) < 0) {
    ds_warn("[%s] pipe2: %s", tag, strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  pid_t child = fork();
  if (child < 0) {
    ds_warn("[%s] fork: %s", tag, strerror(errno));
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
    child_fn(readyfd[1], user_data);
    /* unreachable if child_fn calls execv/_exit */
    _exit(1);
  }

  close(pipefd[1]);
  close(readyfd[1]);

  /* EOF = exec succeeded; byte = exec failed */
  char rdy;
  if (read(readyfd[0], &rdy, 1) > 0) {
    ds_error("%s: execv failed -- daemon did not start", label);
    waitpid(child, NULL, 0);
    close(readyfd[0]);
    close(pipefd[0]);
    return -1;
  }
  close(readyfd[0]);

  /* Spawn the log relay (takes ownership of pipefd[0]) */
  ds_spawn_log_relay(pipefd[0], log_file, tag);

  ds_log("%s: daemon pid=%d launched", label, (int)child);
  return child;
}
