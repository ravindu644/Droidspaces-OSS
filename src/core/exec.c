/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Safe Command Execution (fork + execvp) */

static int internal_run(char *const argv[], int quiet) {
  pid_t pid = fork();
  if (pid < 0)
    return -1;

  if (pid == 0) {
    if (quiet) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(devnull);
      }
    }
    execvp(argv[0], argv);
    _exit(127); /* exec failed */
  }

  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;

  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return -1;
}

int run_command(char *const argv[]) { return internal_run(argv, 0); }
int run_command_quiet(char *const argv[]) { return internal_run(argv, 1); }

/* run_command_log: runs argv, captures stderr and emits it via ds_log so
 * iptables error messages are visible in the droidspaces log on failure. */
int run_command_log(char *const argv[]) {
  int pipefd[2];
  if (pipe(pipefd) < 0)
    return internal_run(argv, 0);

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    execvp(argv[0], argv);
    _exit(127);
  }

  close(pipefd[1]);

  char buf[512];
  FILE *f = fdopen(pipefd[0], "r");
  if (f) {
    while (fgets(buf, sizeof(buf), f)) {
      size_t l = strlen(buf);
      while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
        buf[--l] = '\0';
      if (l > 0)
        ds_log("[IPT] %s", buf);
    }
    fclose(f);
  } else {
    close(pipefd[0]);
  }

  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* pthread_create for a helper thread that must never receive signals.
 *
 * A thread inherits its creator's signal mask, and a process-directed signal
 * goes to any thread that has it unblocked. The monitor waits for its
 * container through a signalfd on SIGCHLD, so a helper thread created with
 * SIGCHLD unblocked can take that signal and throw it away, and the monitor
 * then only notices the container died when its 500 ms poll times out.
 * Blocking everything around the create call gives the new thread a full mask
 * from its first instruction and leaves the caller's mask as it was. */
int ds_thread_create(pthread_t *tid, void *(*fn)(void *), void *arg) {
  sigset_t all, old;
  sigfillset(&all);
  pthread_sigmask(SIG_BLOCK, &all, &old);
  int ret = pthread_create(tid, NULL, fn, arg);
  pthread_sigmask(SIG_SETMASK, &old, NULL);
  return ret;
}

static void set_oom_score_adj(const char *val) {
  FILE *f = fopen("/proc/self/oom_score_adj", "w");
  if (f) {
    fputs(val, f);
    fclose(f);
  }
}

/* The runtime itself, daemon and monitor, must outlive any OOM kill.
 * Best-effort, no error return. */
void ds_oom_protect(void) { set_oom_score_adj("-1000\n"); }

/* Container processes are forked from a protected parent and would inherit
 * -1000, leaving a memory limit nothing to kill but failed allocations, and a
 * global OOM free to pass over the container and take Android instead.
 * Raising the value needs no privilege. */
void ds_oom_unprotect(void) { set_oom_score_adj("0\n"); }
