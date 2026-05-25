/*
 * Droidspaces v6 daemon & client mode
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
 *
 * note on pipe hangs: start_rootfs leaks descriptors to the intermediate
 * monitor process. waiting for epollhup would block forever. we poll waitpid
 * instead and do a final non-blocking drain when the direct child exits.
 */

#include "droidspace.h"
#ifdef DS_ENABLE_SOCKETD_BACKEND
#include "socketd_bridge.h"
#endif

#include <arpa/inet.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

/* constants */

#define DS_SOCK_NAME "droidspaces"
#define DS_BACKLOG SOMAXCONN
#define DS_MAX_ARGC 64
#define DS_MAX_ARG 8192
#define DS_IOBUF 8192
#define DS_POLL_MS 100

#define MSG_OUT ((uint8_t)0x01)
#define MSG_ERR ((uint8_t)0x02)
#define MSG_WINCH ((uint8_t)0x03)
#define MSG_EXIT ((uint8_t)0xFF)

#define REQ_FLAG_PTY (1u << 0)
#define EXIT_PENDING (-1)

static FILE *g_daemon_log_fp = NULL;

/*
 * Tee helper: writes a plain (no ANSI) line to g_daemon_log_fp.
 * Used only in foreground mode; background mode relies on dup2.
 */
static void daemon_log_tee(const char *prefix, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void daemon_log_tee(const char *prefix, const char *fmt, ...) {
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

#undef ds_log
#define ds_log(fmt, ...)                                                       \
  do {                                                                         \
    ds_log_internal("+", C_GREEN, 0, fmt, ##__VA_ARGS__);                      \
    daemon_log_tee("+", fmt, ##__VA_ARGS__);                                   \
  } while (0)

#undef ds_warn
#define ds_warn(fmt, ...)                                                      \
  do {                                                                         \
    ds_log_internal("!", C_YELLOW, 1, fmt, ##__VA_ARGS__);                     \
    daemon_log_tee("!", fmt, ##__VA_ARGS__);                                   \
  } while (0)

#undef ds_error
#define ds_error(fmt, ...)                                                     \
  do {                                                                         \
    ds_log_internal("-", C_RED, 1, fmt, ##__VA_ARGS__);                        \
    daemon_log_tee("-", fmt, ##__VA_ARGS__);                                   \
  } while (0)

static volatile sig_atomic_t g_client_sigwinch = 0;

/*
 * g_self_path is populated once during daemon startup (after daemonize()).
 * Subsequent fork()+reexec() children inherit a copy of this path.
 * After an atomic `mv` replaces the binary on disk, this path resolves to
 * the NEW binary - so all future sessions transparently use the updated code
 * without restarting the daemon.
 */
static char g_self_path[PATH_MAX];
static volatile sig_atomic_t g_sigusr2_received = 0;

/* wire protocol helpers */

static int read_exact(int fd, void *buf, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n) {
    ssize_t r = read(fd, p, n);
    if (r <= 0)
      return -1;
    p += r;
    n -= (size_t)r;
  }
  return 0;
}

static int send_frame(int fd, uint8_t type, const void *data, uint32_t len) {
  uint8_t hdr[5];
  uint32_t nl = htonl(len);
  hdr[0] = type;
  memcpy(hdr + 1, &nl, 4);
  if (write_all(fd, hdr, 5) < 0)
    return -1;
  if (len && write_all(fd, data, len) < 0)
    return -1;
  return 0;
}

static int recv_frame_hdr(int fd, uint8_t *type_out, uint32_t *len_out) {
  uint8_t hdr[5];
  if (read_exact(fd, hdr, 5) < 0)
    return -1;
  *type_out = hdr[0];
  uint32_t nl;
  memcpy(&nl, hdr + 1, 4);
  *len_out = ntohl(nl);
  return 0;
}

static void send_exit(int fd, int code) {
  uint32_t nc = htonl((uint32_t)code);
  send_frame(fd, MSG_EXIT, &nc, 4);
}

/* abstract socket setup */

static socklen_t make_addr(struct sockaddr_un *addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  size_t nlen = strlen(DS_SOCK_NAME);
  memcpy(addr->sun_path + 1, DS_SOCK_NAME, nlen);
  return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + nlen);
}

/* request parsing */

typedef struct {
  uint32_t flags;
  int argc;
  char *argv[DS_MAX_ARGC + 1];
  uint16_t rows, cols;
} ds_req_t;

static void free_req(ds_req_t *r) {
  for (int i = 0; i < r->argc; i++) {
    if (r->argv[i]) {
      free(r->argv[i]);
      r->argv[i] = NULL;
    }
  }
}

static int recv_req(int fd, ds_req_t *r) {
  memset(r, 0, sizeof(*r));
  uint32_t nf, na;
  if (read_exact(fd, &nf, 4) < 0 || read_exact(fd, &na, 4) < 0)
    return -1;
  r->flags = ntohl(nf);
  uint32_t argc = ntohl(na);
  if (!argc || argc > DS_MAX_ARGC)
    return -1;

  for (uint32_t i = 0; i < argc; i++) {
    uint32_t nl;
    if (read_exact(fd, &nl, 4) < 0)
      return -1;
    uint32_t al = ntohl(nl);
    if (al > DS_MAX_ARG)
      return -1;
    r->argv[i] = (char *)malloc((size_t)al + 1);
    if (!r->argv[i])
      return -1;
    if (al && read_exact(fd, r->argv[i], al) < 0)
      return -1;
    r->argv[i][al] = '\0';
    r->argc++; /* safely increment once we actually have the string */
  }
  r->argv[r->argc] = NULL;

  if (r->flags & REQ_FLAG_PTY) {
    uint16_t ws[2];
    if (read_exact(fd, ws, 4) < 0)
      return -1;
    r->rows = ntohs(ws[0]);
    r->cols = ntohs(ws[1]);
    if (!r->rows)
      r->rows = 24;
    if (!r->cols)
      r->cols = 80;
  }
  return 0;
}

/* re-execute the droidspaces binary */

static void reexec(char **argv) {
  /*
   * Use the path pinned at daemon startup. After an atomic mv swap,
   * g_self_path resolves to the new binary automatically. If for some
   * reason it was never set, fall back to the symlink (which may have
   * (deleted) but is better than nothing).
   */
  const char *path = (g_self_path[0] != '\0') ? g_self_path : "/proc/self/exe";
  execv(path, argv);
  _exit(127);
}

static char **make_exec_argv(ds_req_t *r) {
  char **av = (char **)malloc((size_t)(r->argc + 2) * sizeof(char *));
  if (!av)
    return NULL;
  av[0] = (char *)"droidspaces";
  for (int i = 0; i < r->argc; i++)
    av[i + 1] = r->argv[i];
  av[r->argc + 1] = NULL;
  return av;
}

static void drain_fd(int fd, int conn, uint8_t type) {
  char buf[DS_IOBUF];
  int fl = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0)
      break;
    send_frame(conn, type, buf, (uint32_t)n);
  }
  fcntl(fd, F_SETFL, fl);
}

/* unified session handler for both pty and pipe modes */

static void handle_session(int conn, ds_req_t *r) {
  int is_pty = (r->flags & REQ_FLAG_PTY);
  int master = -1, slave = -1;
  int out[2] = {-1, -1}, err[2] = {-1, -1};
  char buf[DS_IOBUF];

  if (is_pty) {
    if (ds_openpty(&master, &slave, NULL) < 0) {
      send_frame(conn, MSG_ERR, "daemon: openpty failed\n", 23);
      send_exit(conn, 1);
      return;
    }
    struct winsize ws = {r->rows, r->cols, 0, 0};
    ioctl(master, TIOCSWINSZ, &ws);
    fcntl(master, F_SETFD, FD_CLOEXEC);
  } else {
    /*
     * Create pipes with O_CLOEXEC only - NOT O_NONBLOCK.
     *
     * O_NONBLOCK is a file-description flag (shared across dup'd fds).
     * If we set it in pipe2(), the child's stdout/stderr (dup2'd from
     * the write ends) inherit the flag, causing EAGAIN / short writes for
     * programs that write more than PIPE_BUF in one call.
     *
     * We set O_NONBLOCK on the READ ends only, in the parent, after the
     * fork and after the write ends have been closed.  The child's write
     * ends are a separate file description and remain blocking.
     */
    if (pipe2(out, O_CLOEXEC) < 0 || pipe2(err, O_CLOEXEC) < 0) {
      send_frame(conn, MSG_ERR, "daemon: pipe2 failed\n", 21);
      send_exit(conn, 1);
      if (out[0] >= 0) {
        close(out[0]);
        close(out[1]);
      }
      if (err[0] >= 0) {
        close(err[0]);
        close(err[1]);
      }
      return;
    }
  }

  char **av = make_exec_argv(r);
  if (!av) {
    if (is_pty) {
      close(master);
      close(slave);
    } else {
      close(out[0]);
      close(out[1]);
      close(err[0]);
      close(err[1]);
    }
    send_exit(conn, 1);
    return;
  }

  pid_t child = fork();
  if (child < 0) {
    free(av);
    if (is_pty) {
      close(master);
      close(slave);
    } else {
      close(out[0]);
      close(out[1]);
      close(err[0]);
      close(err[1]);
    }
    send_frame(conn, MSG_ERR, "daemon: fork failed\n", 20);
    send_exit(conn, 1);
    return;
  }

  if (child == 0) {
    close(conn);
    if (is_pty) {
      close(master);
      setsid();
      ioctl(slave, TIOCSCTTY, 0);
      dup2(slave, STDIN_FILENO);
      dup2(slave, STDOUT_FILENO);
      dup2(slave, STDERR_FILENO);
      if (slave > STDERR_FILENO)
        close(slave);
    } else {
      close(out[0]);
      close(err[0]);
      int dn = open("/dev/null", O_RDWR);
      if (dn >= 0) {
        dup2(dn, STDIN_FILENO);
        if (dn > STDERR_FILENO)
          close(dn);
      }
      dup2(out[1], STDOUT_FILENO);
      dup2(err[1], STDERR_FILENO);
      close(out[1]);
      close(err[1]);
    }
    /* Prevent the spawned child from proxying back to the daemon */
    setenv("DS_NO_PROXY", "1", 1);
    reexec(av);
  }

  free(av);

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    if (is_pty) {
      close(master);
      close(slave);
    } else {
      close(out[0]);
      close(out[1]);
      close(err[0]);
      close(err[1]);
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    send_exit(conn, 1);
    return;
  }

  struct epoll_event ev, events[8];
  int active_reads = 0;

  if (is_pty) {
    close(slave);
    int fl = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.fd = master;
    epoll_ctl(epfd, EPOLL_CTL_ADD, master, &ev);
    active_reads = 1;
  } else {
    close(out[1]);
    close(err[1]);
    out[1] = err[1] = -1;
    /* Read ends are now exclusively owned by the parent.  Set O_NONBLOCK
     * here so epoll's edge reads can drain without blocking. */
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    fcntl(err[0], F_SETFL, O_NONBLOCK);
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.fd = out[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, out[0], &ev);
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.fd = err[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, err[0], &ev);
    active_reads = 2;
  }

  /* watch the connection for dead clients or pty input */
  ev.events = EPOLLHUP | EPOLLERR;
  if (is_pty)
    ev.events |= EPOLLIN;
  ev.data.fd = conn;
  epoll_ctl(epfd, EPOLL_CTL_ADD, conn, &ev);

  int exit_code = EXIT_PENDING;
  int child_done = 0; /* 0=running, 1=killed, 2=normal-exit-seen */

  for (;;) {
    int nfds = epoll_wait(epfd, events, 8, DS_POLL_MS);
    if (nfds < 0 && errno != EINTR)
      break;

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;

      if (fd == conn) {
        if (events[i].events & (EPOLLHUP | EPOLLERR)) {
          /* client died unexpectedly, kill the child */
          kill(child, is_pty ? SIGHUP : SIGTERM);
          waitpid(child, NULL, 0);
          goto session_end;
        }
        if (is_pty && (events[i].events & EPOLLIN)) {
          uint8_t type;
          uint32_t mlen;
          if (recv_frame_hdr(conn, &type, &mlen) < 0) {
            kill(child, SIGHUP);
            waitpid(child, NULL, 0);
            goto session_end;
          }
          if (type == MSG_OUT && mlen > 0 && mlen <= (uint32_t)sizeof(buf)) {
            if (read_exact(conn, buf, mlen) == 0)
              write_all(master, buf, mlen);
          } else if (type == MSG_WINCH && mlen == 4) {
            uint16_t wd[2];
            if (read_exact(conn, wd, 4) == 0) {
              struct winsize nws = {ntohs(wd[0]), ntohs(wd[1]), 0, 0};
              ioctl(master, TIOCSWINSZ, &nws);
              kill(-child, SIGWINCH); /* signal the whole process group */
            }
          } else {
            /* drain unknown frames so we don't stall the pipe */
            uint32_t rem = mlen;
            while (rem) {
              uint32_t c =
                  (rem < (uint32_t)sizeof(buf)) ? rem : (uint32_t)sizeof(buf);
              if (read_exact(conn, buf, c) < 0)
                goto session_end;
              rem -= c;
            }
          }
        }
      } else {
        /* this is a read fd from the child */
        if (events[i].events & (EPOLLIN | EPOLLHUP)) {
          int drained = 0;
          for (;;) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
              uint8_t t = (fd == err[0]) ? MSG_ERR : MSG_OUT;
              if (send_frame(conn, t, buf, (uint32_t)n) < 0) {
                kill(child, is_pty ? SIGHUP : SIGTERM);
                waitpid(child, NULL, 0);
                goto session_end;
              }
            } else if (n == 0) {
              /* Truly EOF */
              drained = 1;
              break;
            } else {
              if (errno == EINTR)
                continue;
              if (errno == EAGAIN)
                break;
              if (errno == EWOULDBLOCK)
                break;
              drained = 1;
              break; /* treat other errors as closure */
            }
          }

          if (drained) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            if (fd == master)
              master = -1;
            else if (fd == out[0])
              out[0] = -1;
            else if (fd == err[0])
              err[0] = -1;
            if (--active_reads <= 0)
              child_done = 2;
          }
        }
      }
    }

    if (!child_done) {
      int st;
      if (waitpid(child, &st, WNOHANG) == child) {
        exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        /* for pipes we wait for eof. for ptys, the child exiting is the end. */
        child_done = is_pty ? 2 : 1;
      }
    }

    /* wait for eofs on pipes even if the child exited, because the monitor
     * might still be holding it */
    if (child_done == 1) {
      if (out[0] >= 0)
        drain_fd(out[0], conn, MSG_OUT);
      if (err[0] >= 0)
        drain_fd(err[0], conn, MSG_ERR);
      break;
    } else if (child_done == 2) {
      if (is_pty && master >= 0)
        drain_fd(master, conn, MSG_OUT);
      break;
    }
  }

  if (exit_code == EXIT_PENDING) {
    int st;
    waitpid(child, &st, 0);
    exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
  }

session_end:
  close(epfd);
  if (master >= 0)
    close(master);
  if (out[0] >= 0)
    close(out[0]);
  if (err[0] >= 0)
    close(err[0]);
  send_exit(conn, exit_code == EXIT_PENDING ? 0 : exit_code);
}

/* handle incoming client connections */

static void handle_conn(int conn) {
  ds_req_t req;
  if (recv_req(conn, &req) < 0) {
    send_frame(conn, MSG_ERR, "daemon: bad request\n", 20);
    send_exit(conn, 1);
    close(conn);
    _exit(1);
  }

  /*
   * Block recursive daemon/client invocations.
   * We skip any argv[i] whose predecessor starts with '-', because that arg
   * is an option VALUE (e.g. --name daemon-container), not a sub-command.
   * DS_NO_PROXY=1 is the primary guard (set before reexec); this is a
   * defence-in-depth check.
   */
  for (int i = 0; i < req.argc; i++) {
    /* If the previous token was an option flag, this token is its value. */
    if (i > 0 && req.argv[i - 1][0] == '-')
      continue;
    if (strcmp(req.argv[i], "daemon") == 0 ||
        strcmp(req.argv[i], "client") == 0) {
      send_frame(conn, MSG_ERR, "daemon: recursive call refused\n", 31);
      send_exit(conn, 1);
      free_req(&req);
      close(conn);
      _exit(1);
    }
  }

  /* log the request */
  {
    char cmdline[DS_MAX_ARG * 2] = {0};
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
  /*
   * Clear environment to avoid inheriting parent shell's variables (like
   * Termux). This prevents false-positive detections and ensures a clean state
   * for all Droidspaces tasks.
   */
  clearenv();
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

  /* android OOM hardening: try to make us unkillable (-1000) */
  if (is_android()) {
    FILE *f = fopen("/proc/self/oom_score_adj", "w");
    if (f) {
      fprintf(f, "-1000\n");
      fclose(f);
    }
  }
}

static void sigusr2_handler(int sig) {
  (void)sig;
  g_sigusr2_received = 1;
}

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

  /* Already in the right domain - nothing to do */
  if (strcmp(cur, DS_SELINUX_CTX) == 0)
    return;

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
      write(sfd, DS_SELINUX_CTX, strlen(DS_SELINUX_CTX));
      close(sfd);
    }
  }
  /* Clear the stale exec context regardless of whether setcon worked */
  fd = open("/proc/self/attr/exec", O_WRONLY);
  if (fd >= 0) {
    write(fd, "\0", 1);
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

  /* SIGUSR2: app sends this after a live binary swap as an acknowledgment */
  signal(SIGUSR2, sigusr2_handler);

#ifdef DS_ENABLE_SOCKETD_BACKEND
  if (ds_socketd_bridge_start() < 0)
    ds_warn("Failed to start droidspaces-socketd backend bridge: %s", strerror(errno));
#endif

  /* Write PID file so the Android app can signal us */
  {
    char pid_path[PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/droidspacesd.pid",
             get_workspace_dir());
    FILE *pf = fopen(pid_path, "w");
    if (pf) {
      fprintf(pf, "%d\n", getpid());
      fclose(pf);
    }
  }

  int srv = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (srv < 0) {
    ds_error("daemon: socket: %s", strerror(errno));
    return 1;
  }

  struct sockaddr_un addr;
  socklen_t alen = make_addr(&addr);
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
    /* Acknowledge a live binary swap signalled by the Android app. */
    if (g_sigusr2_received) {
      g_sigusr2_received = 0;
      ds_log("Binary updated on disk (SIGUSR2). New sessions will use updated "
             "binary automatically.");
    }

    int conn = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
    if (conn < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      ds_error("accept4: %s", strerror(errno));
      continue;
    }

    /*
     * authenticate the peer: only root or members of the 'droidspaces' group
     * may connect. abstract socket has no filesystem permissions, so we
     * enforce this via SO_PEERCRED + getgrouplist() -- same model as Docker's
     * unix group.
     */
    {
#define DS_GROUP "droidspaces"
      struct ucred cred;
      socklen_t clen = sizeof(cred);
      if (getsockopt(conn, SOL_SOCKET, SO_PEERCRED, &cred, &clen) < 0) {
        close(conn);
        continue;
      }

      int allowed = (cred.uid == 0);
      if (!allowed) {
        struct group *gr = getgrnam(DS_GROUP);
        struct passwd *pw = getpwuid(cred.uid);
        if (gr && pw) {
          int ngroups = 64;
          gid_t groups[64];
          getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups);
          for (int i = 0; i < ngroups; i++) {
            if (groups[i] == gr->gr_gid) {
              allowed = 1;
              break;
            }
          }
        }
      }

      if (!allowed) {
        const char *msg = "permission denied: only root or '" DS_GROUP
                          "' group members may connect.";
        send_frame(conn, MSG_ERR, msg, (uint32_t)strlen(msg));
        send_exit(conn, 1);
        close(conn);
        continue;
      }
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
  socklen_t alen = make_addr(&addr);
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s < 0)
    return 0;
  int alive = (connect(s, (struct sockaddr *)&addr, alen) == 0);
  close(s);
  return alive;
}

/* connect to the daemon and relay our command */

static void client_sigwinch_handler(int sig) {
  (void)sig;
  g_client_sigwinch = 1;
}

int ds_client_run(int argc, char **argv) {
  if (argc < 1)
    return -2;

  int interactive = 0;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "enter") == 0 || strcmp(argv[i], "run") == 0 ||
        strcmp(argv[i], "start") == 0 || strcmp(argv[i], "restart") == 0) {
      interactive = 1;
      break;
    }
  }

  int has_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

  if (interactive && !has_tty) {
    int forces_tty = 0;
    int is_enter = 0;
    for (int i = 0; i < argc; i++) {
      if (strcmp(argv[i], "enter") == 0) {
        forces_tty = 1;
        is_enter = 1;
        break;
      }
      if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
        for (int j = 0; j < argc; j++) {
          if (strcmp(argv[j], "start") == 0 ||
              strcmp(argv[j], "restart") == 0) {
            forces_tty = 1;
            break;
          }
        }
        if (forces_tty)
          break;
      }
    }
    if (forces_tty) {
      if (is_enter) {
        ds_error("Interactive terminal is required for the enter command\n");
        return 1;
      } else {
        /* Strip -f/--foreground; start_rootfs() will warn and flip the switch.
         */
        for (int i = 0; i < argc; i++) {
          if (strcmp(argv[i], "-f") == 0 ||
              strcmp(argv[i], "--foreground") == 0) {
            for (int j = i; j < argc - 1; j++)
              argv[j] = argv[j + 1];
            argv[--argc] = NULL;
            break;
          }
        }
      }
    }
    interactive = 0;
  }

  /* try to connect - single pass for execution path, retry loop only for 1st
   * connect */
  int sock = -1;
  struct sockaddr_un addr;
  socklen_t alen = make_addr(&addr);

  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s < 0) {
    fprintf(stderr, "client: socket: %s\n", strerror(errno));
    return 1;
  }
  if (connect(s, (struct sockaddr *)&addr, alen) < 0) {
    int err = errno;
    close(s);
    if (err == ECONNREFUSED || err == ENOENT)
      return -2; /* Daemon not listening */
    fprintf(stderr, "[-] Connection to daemon failed: %s\n", strerror(err));
    return 1;
  }
  sock = s;

  /* send the request */
  uint32_t flags = interactive ? REQ_FLAG_PTY : 0u;
  uint32_t nf = htonl(flags), na = htonl((uint32_t)argc);
  if (write_all(sock, &nf, 4) < 0 || write_all(sock, &na, 4) < 0)
    goto send_err;
  for (int i = 0; i < argc; i++) {
    uint32_t al = (uint32_t)strlen(argv[i]), nal = htonl(al);
    if (write_all(sock, &nal, 4) < 0)
      goto send_err;
    if (al && write_all(sock, argv[i], al) < 0)
      goto send_err;
  }

  if (interactive) {
    struct winsize ws = {24, 80, 0, 0};
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    uint16_t wd[2] = {htons(ws.ws_row), htons(ws.ws_col)};
    if (write_all(sock, wd, 4) < 0)
      goto send_err;
  }

  /* run the relay loop */
  struct termios orig;
  int raw_tty_active = 0;

  if (interactive && has_tty && tcgetattr(STDIN_FILENO, &orig) == 0) {
    raw_tty_active = 1;
    struct termios raw = orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = client_sigwinch_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);
  }

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  struct epoll_event ev, events[4];

  if (raw_tty_active) {
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
  }
  ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
  ev.data.fd = sock;
  epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);

  int exit_code = 0;
  char buf[DS_IOBUF];

  for (;;) {
    if (raw_tty_active && g_client_sigwinch) {
      g_client_sigwinch = 0;
      struct winsize nws = {24, 80, 0, 0};
      ioctl(STDIN_FILENO, TIOCGWINSZ, &nws);
      uint16_t wd2[2] = {htons(nws.ws_row), htons(nws.ws_col)};
      send_frame(sock, MSG_WINCH, wd2, 4);
    }

    int nfds = epoll_wait(epfd, events, 4, raw_tty_active ? 200 : -1);
    if (nfds < 0 && errno != EINTR)
      break;

    int done = 0;
    for (int i = 0; i < nfds && !done; i++) {
      int fd = events[i].data.fd;

      if (fd == STDIN_FILENO) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) {
          if (send_frame(sock, MSG_OUT, buf, (uint32_t)n) < 0)
            done = 1;
        } else {
          done = 1;
        }
      } else if (fd == sock) {
        if (events[i].events & (EPOLLIN | EPOLLHUP)) {
          /* Read as many frames as possible before checking HUP */
          for (;;) {
            uint8_t type;
            uint32_t mlen;

            /* Probe the socket for pending data */
            int pending = 0;
            struct pollfd pfd = {sock, POLLIN, 0};
            if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
              pending = 1;

            if (!pending)
              break;

            if (recv_frame_hdr(sock, &type, &mlen) < 0) {
              done = 1;
              break;
            }

            if (type == MSG_EXIT) {
              uint32_t nc = 0;
              if (mlen >= 4)
                read_exact(sock, &nc, 4);
              exit_code = (int)ntohl(nc);
              done = 1;
              break;
            }

            FILE *dest = (type == MSG_ERR) ? stderr : stdout;
            uint32_t rem = mlen;
            while (rem) {
              uint32_t c =
                  (rem < (uint32_t)sizeof(buf)) ? rem : (uint32_t)sizeof(buf);
              if (read_exact(sock, buf, c) < 0) {
                done = 1;
                break;
              }
              fwrite(buf, 1, c, dest);
              rem -= c;
            }
            fflush(dest);
          }
        }

        if (!done && (events[i].events & (EPOLLHUP | EPOLLERR))) {
          done = 1;
          break;
        }
      }
    }
    if (done)
      break;
  }

  if (raw_tty_active)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
  close(epfd);
  close(sock);
  return exit_code;

send_err:
  fprintf(stderr, "client: send failed: %s\n", strerror(errno));
  close(sock);
  return 1;
}
