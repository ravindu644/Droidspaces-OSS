/*
 * Droidspaces v7 daemon & client mode
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * magisk and apatch run root processes under u:r:magisk:s0 with seccomp
 * filters that block namespace syscalls. grapheneos does similar hardening.
 *
 * this daemon runs from service.sh to bypass those restrictions. the app
 * talks to it via libsu using a dumb client that only does basic socket
 * i/o, avoiding the seccomp traps entirely.
 *
 * we use an abstract socket (@droidspaces) so there's no filesystem mess
 * to clean up when we crash or get killed.
 *
 * the wire protocol is just simple typed frames.
 */

#include "droidspaces.h"
#include <arpa/inet.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "daemon/internal.h"

static FILE *g_daemon_log_fp = NULL;
static char g_daemon_log_path[PATH_MAX] = "";

/* Rotate droidspacesd.log mid-run when it exceeds the size limit.
 * Renames current log to .old, reopens fresh file, and redirects
 * stdout/stderr (background mode) or g_daemon_log_fp (foreground). */
static void rotate_daemon_log_if_needed(void) {
  if (g_daemon_log_path[0] == '\0')
    return;

  struct stat st;
  if (stat(g_daemon_log_path, &st) < 0 || st.st_size < 4 * 1024 * 1024)
    return;

  char old_path[sizeof(g_daemon_log_path) + 32];
  snprintf(old_path, sizeof(old_path), "%s.old", g_daemon_log_path);
  rename(g_daemon_log_path, old_path);

  if (g_daemon_log_fp) {
    /* foreground: reopen tee file */
    fclose(g_daemon_log_fp);
    g_daemon_log_fp = fopen(g_daemon_log_path, "ae");
  } else {
    /* background: reopen and re-dup2 stdout/stderr */
    int lfd = open(g_daemon_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                   0644);
    if (lfd >= 0) {
      dup2(lfd, STDOUT_FILENO);
      dup2(lfd, STDERR_FILENO);
      if (lfd > STDERR_FILENO)
        close(lfd);
    }
  }
}

/*
 * Tee helper: writes a plain (no ANSI) line to g_daemon_log_fp.
 * Used only in foreground mode; background mode relies on dup2.
 */

void daemon_log_tee(const char *prefix, const char *fmt, ...) {
  if (!g_daemon_log_fp)
    return;
  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  fprintf(g_daemon_log_fp, "[%s] %s\n", prefix, msg);
  fflush(g_daemon_log_fp);
}

/* handle incoming client connections */

static void handle_conn(int conn) {
  ds_req_t req;
  if (recv_req(conn, &req) < 0) {
    ds_send_frame(conn, MSG_ERR, "daemon: bad request\n", 20);
    send_exit(conn, 1);
    close(conn);
    _exit(1);
  }

  /*
   * Everything after the command word belongs to that command, run's command
   * line and enter's user included, so only the word itself can start a
   * daemon or client recursion. It is found the way the client found it,
   * with the option table, so a flag without a value (--stdin run daemon)
   * cannot shift it. DS_NO_PROXY=1 is the primary guard (set before reexec);
   * this is defence-in-depth.
   */
  char **av = make_exec_argv(&req);
  const char *cmd = av ? ds_command_word(req.argc + 1, av) : NULL;
  free(av);
  if (cmd && (strcmp(cmd, "daemon") == 0 || strcmp(cmd, "client") == 0)) {
    ds_send_frame(conn, MSG_ERR, "daemon: recursive call refused\n", 31);
    send_exit(conn, 1);
    free_req(&req);
    close(conn);
    _exit(1);
  }

  /* log the request */
  {
    char cmdline[512];
    size_t off = 0;
    for (int i = 0; i < req.argc && off < sizeof(cmdline) - 1; i++) {
      int n = snprintf(cmdline + off, sizeof(cmdline) - off, "%s%s",
                       i > 0 ? " " : "", req.argv[i]);
      if (n > 0)
        off += (size_t)n;
    }
    ds_log("Client connected. Mode: %s",
           (req.flags & REQ_FLAG_PTY) ? "PTY" : "PIPE");
    ds_log("Executing command: %s", cmdline);
  }

  handle_session(conn, &req);

  ds_log("Session finished. Client disconnected.");
  free_req(&req);
  close(conn);
  _exit(0);
}

/* main daemon loop */

/* daemonize the process: detach from terminal and protect from OOM killer */
static void daemonize(int foreground) {
  if (is_android()) {
    setenv(
        "PATH",
        "/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:"
        "/system_ext/bin:/system/bin:/system/xbin:/odm/bin:/vendor/bin:/vendor/"
        "xbin",
        1);
  } else {
    setenv("PATH",
           "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
  }

  if (!foreground) {
    pid_t pid = fork();
    if (pid < 0)
      exit(1);
    if (pid > 0)
      exit(0); /* parent exits */

    if (setsid() < 0)
      exit(1);

    /* ignore signals that might kill us on terminal closure */
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0)
      exit(1);
    if (pid > 0)
      exit(0); /* second parent exits */
  }

  umask(0);
  if (chdir("/") < 0) { /* ignore */
  }

  /* Scrub dynamic-linker and field-splitting env vars before serving requests
   * and re-exec'ing droidspaces as root, so a value inherited from the launcher
   * cannot influence the privileged process (defense-in-depth; container init
   * additionally receives a clearenv() in ds_env_boot_setup). */
  unsetenv("LD_PRELOAD");
  unsetenv("LD_LIBRARY_PATH");
  unsetenv("LD_AUDIT");
  unsetenv("IFS");

  if (!foreground) {
    /* redirect standard streams */
    int dn = open("/dev/null", O_RDONLY);
    if (dn >= 0) {
      dup2(dn, STDIN_FILENO);
      if (dn > STDERR_FILENO)
        close(dn);
    }
  }

  {
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s/droidspacesd.log", get_logs_dir());
    safe_strncpy(g_daemon_log_path, log_path, sizeof(g_daemon_log_path));
    rotate_log(log_path, 2 * 1024 * 1024);

    if (!foreground) {
      /* Background: dup2 stdout/stderr to log file; g_daemon_log_fp stays
       * NULL so the tee macros don't double-write. */
      int lfd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
      if (lfd >= 0) {
        dup2(lfd, STDOUT_FILENO);
        dup2(lfd, STDERR_FILENO);
        if (lfd > STDERR_FILENO)
          close(lfd);
      }
    } else {
      /* Foreground: keep terminal live; open log file for explicit tee.
       * The daemon_log_tee() helper writes every message here alongside
       * the terminal output produced by ds_log_internal(). */
      g_daemon_log_fp = fopen(log_path, "ae");
    }
  }

  /* Make daemon unkillable */
  ds_oom_protect();
}

/* The app sends SIGUSR2 after a live binary swap. New sessions pick the new
 * binary up on their own, so the signal only must not kill the daemon. Not
 * SIG_IGN: that survives execve and would reach every command the daemon
 * runs on a client's behalf. */
static void sigusr2_handler(int sig) { (void)sig; }

/*
 * ds_selinux_transition - re-exec into the droidspacesd SELinux domain.
 *
 * When launched from Magisk/KernelSU/APatch post-fs-data, the process runs
 * under the root manager's domain (e.g. u:r:magisk:s0) rather than our own.
 * This function writes our target context to /proc/self/attr/exec and re-execs
 * the same binary with the same argv, causing the kernel to transition into
 * u:r:droidspacesd:s0 at the exec boundary - exactly what runcon/setexeccon
 * does internally, with zero libselinux dependency.
 *
 * On the second entry (after re-exec) the current context already matches,
 * so the strcmp short-circuits and we continue normally. The function is also
 * a no-op on non-Android platforms and when SELinux is not mounted/enforcing.
 */
#define DS_SELINUX_CTX "u:r:droidspacesd:s0"

static void ds_selinux_transition(char **argv) {
  if (!is_android())
    return;

  /* Read our current SELinux context */
  char cur[256] = {0};
  int fd = open("/proc/self/attr/current", O_RDONLY);
  if (fd < 0)
    return; /* SELinux not mounted */
  ssize_t n = read(fd, cur, sizeof(cur) - 1);
  close(fd);
  if (n <= 0)
    return;
  cur[n] = '\0';
  /* Trim trailing newline that the kernel appends */
  char *nl = strchr(cur, '\n');
  if (nl)
    *nl = '\0';

  /* Already in the right domain - clear any stale exec context and return */
  if (strcmp(cur, DS_SELINUX_CTX) == 0) {
    fd = open("/proc/self/attr/exec", O_WRONLY);
    if (fd >= 0) {
      if (write(fd, "\0", 1) < 0) {
        /* ignore */
      }
      close(fd);
    }
    return;
  }

  /* Set the exec context - the transition fires on the next execv() */
  fd = open("/proc/self/attr/exec", O_WRONLY);
  if (fd < 0)
    return; /* No permission or SELinux disabled - continue in current domain */

  n = write(fd, DS_SELINUX_CTX, strlen(DS_SELINUX_CTX));
  close(fd);
  if (n < 0)
    return;

  /* Re-exec ourselves into the new domain */
  char self[PATH_MAX];
  n = readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (n <= 0)
    return;
  self[n] = '\0';

  execv(self, argv);
  /*
   * execv() failed - most likely the binary lives on a noexec mount
   * (e.g. /data on some vendor kernels). Fall back to setcon() which
   * switches the current process context in-place without a re-exec.
   * This is less clean than the exec-based transition (open fds are not
   * re-validated by the kernel) but is safe here because we haven't
   * opened any sensitive descriptors yet - we are still at the very top
   * of ds_daemon_run(), before daemonize() or any socket work.
   *
   * If setcon() also fails (e.g. the policy doesn't allow dyntransition),
   * we clear the stale exec context and continue in the current domain.
   * Since droidspacesd is typepermissive, this is still functional.
   */
  {
    int sfd = open("/proc/self/attr/current", O_WRONLY);
    if (sfd >= 0) {
      if (write(sfd, DS_SELINUX_CTX, strlen(DS_SELINUX_CTX)) < 0) {
        /* ignore */
      }
      close(sfd);
    }
  }
  /* Clear the stale exec context regardless of whether setcon worked */
  fd = open("/proc/self/attr/exec", O_WRONLY);
  if (fd >= 0) {
    if (write(fd, "\0", 1) < 0) {
      /* ignore */
    }
    close(fd);
  }
}

#undef DS_SELINUX_CTX

int ds_daemon_run(int foreground, char **argv) {
  ensure_workspace();

  if (ds_daemon_probe()) {
    ds_error("Daemon is already running (@%s)", DS_SOCK_NAME);
    return 1;
  }

  /* Transition into our SELinux domain before daemonizing, so the daemon
   * and all its children run under u:r:droidspacesd:s0 regardless of how
   * we were launched (init, Magisk, KernelSU, APatch, shell, etc.) */
  ds_selinux_transition(argv);

  daemonize(foreground);

  /*
   * Pin our absolute path NOW, once, in the final daemon process.
   * /proc/self/exe is still clean here. After any atomic mv swap that
   * replaces our binary, g_self_path still holds the same string but
   * it now resolves to the new file on disk. Every fork()+reexec() from
   * this point on will automatically exec the updated binary.
   */
  {
    ssize_t n =
        readlink("/proc/self/exe", g_self_path, sizeof(g_self_path) - 1);
    if (n > 0)
      g_self_path[n] = '\0';
    else
      g_self_path[0] = '\0'; /* reexec() will fall back gracefully */
  }

  signal(SIGUSR2, sigusr2_handler);

  /* Write PID file so the Android app can signal us.  The daemon runs with
   * umask 0, so create it 0644 explicitly (not the fopen default 0666): a
   * world-writable pidfile would let any local user overwrite it and redirect
   * the app's signals (SIGUSR2/kill) to an arbitrary PID.  O_NOFOLLOW guards
   * against a symlink planted in the workspace dir; no O_EXCL so a restart can
   * replace a stale pidfile. */
  {
    char pid_path[PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/droidspacesd.pid",
             get_workspace_dir());
    int pfd = open(pid_path,
                   O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (pfd >= 0) {
      FILE *pf = fdopen(pfd, "w");
      if (pf) {
        fprintf(pf, "%d\n", getpid());
        fclose(pf);
      } else {
        close(pfd);
      }
    }
  }

  int srv = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (srv < 0) {
    ds_error("daemon: socket: %s", strerror(errno));
    return 1;
  }

  struct sockaddr_un addr;
  socklen_t alen = ds_abstract_addr(&addr, DS_SOCK_NAME);
  if (bind(srv, (struct sockaddr *)&addr, alen) < 0) {
    ds_error("daemon: bind(@%s): %s", DS_SOCK_NAME, strerror(errno));
    if (errno == EADDRINUSE) {
      ds_log("Is another droidspaces daemon stuck? Check 'ps' to see.");
    }
    close(srv);
    return 1;
  }

  if (listen(srv, DS_BACKLOG) < 0) {
    ds_error("daemon: listen: %s", strerror(errno));
    close(srv);
    return 1;
  }

  signal(SIGCHLD, SIG_IGN); /* auto-reap children */
  signal(SIGPIPE, SIG_IGN); /* ignore broken pipes */

  fprintf(stdout, "\nDroidspaces Daemon - v" DS_VERSION "\n\n");
  fflush(stdout);
  ds_log("Listening on @" DS_SOCK_NAME " (PID %d)", getpid());

  for (;;) {
    /* Rotate log if it exceeds 4MB */
    rotate_daemon_log_if_needed();

    int conn = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
    if (conn < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      ds_error("accept4: %s", strerror(errno));
      continue;
    }

    /*
     * Authenticate the peer via the shared authorizer: only root or members of
     * the 'droidspaces' group, and only from our own PID namespace (the
     * abstract socket has no filesystem permissions).  Same model as Docker's
     * unix group.
     */
    if (!ds_peer_authorized(conn, "droidspaces")) {
      const char *msg = "permission denied: only root or 'droidspaces' group "
                        "members from the host PID ns may connect.\n";
      ds_send_frame(conn, MSG_ERR, msg, (uint32_t)strlen(msg));
      send_exit(conn, 1);
      close(conn);
      continue;
    }

    pid_t h = fork();
    if (h < 0) {
      close(conn);
      continue;
    }
    if (h == 0) {
      close(srv);
      signal(SIGCHLD, SIG_DFL);
      /* keep SIGPIPE ignored so we don't die on client disconnect */
      handle_conn(conn);
    }
    close(conn);
  }
  close(srv);
  return 0;
}

/*
 * Probe whether the daemon is alive.
 *
 * Abstract Unix socket connect() on Linux completes synchronously: the
 * kernel connects in-kernel without a network round-trip, so O_NONBLOCK +
 * poll() buys us nothing here.  A blocking connect() that finds no listener
 * returns ECONNREFUSED immediately.
 */
int ds_daemon_probe(void) {
  struct sockaddr_un addr;
  socklen_t alen = ds_abstract_addr(&addr, DS_SOCK_NAME);
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s < 0)
    return 0;
  int alive = (connect(s, (struct sockaddr *)&addr, alen) == 0);
  close(s);
  return alive;
}
