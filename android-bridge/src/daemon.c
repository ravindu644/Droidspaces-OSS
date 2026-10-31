/*
 * android-bridge: the Android side, started from a KernelSU module
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One process per connection. It checks the peer is real root, starts
 * /system/bin/sh as the shell user in u:r:shell:s0, the way adbd does, and
 * relays bytes until the shell exits or the client goes away. The shell is
 * never root: a user who wants root runs su, and the root manager decides.
 */

#include "bridge.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHELL_PATH "/system/bin/sh"
#define AID_SHELL 2000
#define CAP_SETGID_NR 6
#define CAP_SETUID_NR 7

/* adbd's groups for its shell: input, log, adb, sdcard_rw, sdcard_r,
 * ext_data_rw, ext_obb_rw, readtracefs, net_bt_admin, net_bt, inet,
 * net_bw_stats, readproc, uhid. Without them getevent, logcat, ping and
 * /sdcard behave differently from adb shell. */
static const gid_t shell_groups[] = {1004, 1007, 1011, 1015, 1028, 1078, 1079,
                                     1092, 3001, 3002, 3003, 3006, 3009, 3011};

struct hello {
  unsigned flags;
  uint16_t rows, cols;
  char term[64];
  char *cmd;
};

struct child {
  pid_t pid;
  int in, out, err; /* in == out for a pty master */
};

static struct buf tosock, tochild, in;

__attribute__((format(printf, 1, 2))) static void logmsg(const char *fmt, ...) {
  char ts[32];
  struct tm tm;
  time_t t = time(NULL);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", gmtime_r(&t, &tm));
  fprintf(stderr, "[%s] ", ts);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

static int write_str(const char *path, const char *s) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  int r = write_all(fd, s, strlen(s));
  close(fd);
  return r;
}

/* 1 same namespace, 0 different, -1 cannot tell */
static int ns_cmp(pid_t pid, const char *ns) {
  char path[64], a[64], b[64];
  snprintf(path, sizeof(path), "/proc/self/ns/%s", ns);
  ssize_t na = readlink(path, a, sizeof(a) - 1);
  snprintf(path, sizeof(path), "/proc/%d/ns/%s", (int)pid, ns);
  ssize_t nb = readlink(path, b, sizeof(b) - 1);
  if (na <= 0 || nb <= 0)
    return -1;
  a[na] = '\0';
  b[nb] = '\0';
  return strcmp(a, b) == 0;
}

/* The kernel reports the peer's uid as our user namespace sees it, so root
 * inside a user namespace shows up as the host uid it maps to, or as the
 * overflow uid. That half cannot race. The namespace compare covers what it
 * misses: real root mapping itself to 0 inside a child namespace, which gives
 * kuid 0 with no capabilities over the host. Anything unreadable denies. */
static int peer_is_real_root(int fd, struct ucred *cred) {
  socklen_t len = sizeof(*cred);
  memset(cred, 0, sizeof(*cred));
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, cred, &len) < 0 ||
      len != sizeof(*cred))
    return 0;
  if (cred->uid != 0 || cred->pid <= 0)
    return 0;
  /* Without CONFIG_USER_NS there is no ns/user link and exactly one user
   * namespace, so uid 0 already is the answer. ns/mnt is there since 3.8. */
  if (access("/proc/self/ns/user", F_OK) < 0 && errno == ENOENT &&
      access("/proc/self/ns/mnt", F_OK) == 0)
    return 1;
  return ns_cmp(cred->pid, "user") == 1;
}

static void deny(int conn) {
  static const char msg[] =
      "android-bridge: permission denied, only real root may connect\n";
  unsigned char f[2 * FRAME_HDR + sizeof(msg) + 4];
  size_t n = sizeof(msg) - 1;
  f[0] = F_STDERR;
  put_be32(f + 1, (uint32_t)n);
  memcpy(f + FRAME_HDR, msg, n);
  f[FRAME_HDR + n] = F_EXIT;
  put_be32(f + FRAME_HDR + n + 1, 4);
  put_be32(f + 2 * FRAME_HDR + n, 255);
  if (send(conn, f, 2 * FRAME_HDR + n + 4, MSG_DONTWAIT | MSG_NOSIGNAL) < 0) {
    /* the peer is not owed an explanation */
  }
}

static void say(int conn, const char *msg) {
  unsigned char code[4];
  put_be32(code, 255);
  buf_frame(&tosock, F_STDERR, msg, (uint32_t)strlen(msg));
  buf_frame(&tosock, F_EXIT, code, 4);
  buf_flush_all(&tosock, conn);
}

static int read_full(int fd, void *p, size_t n) {
  unsigned char *c = p;
  while (n) {
    ssize_t r = read(fd, c, n);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
      return -1;
    c += r;
    n -= (size_t)r;
  }
  return 0;
}

static int valid_term(const char *t) {
  for (; *t; t++)
    if (!isalnum((unsigned char)*t) && !strchr("-_.+", *t))
      return 0;
  return 1;
}

/* -1 garbage, -2 a client from another protocol version */
static int recv_hello(int fd, struct hello *h) {
  /* A connection that never says hello must not pin a process forever. */
  struct timeval tv = {.tv_sec = 10};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  unsigned char hdr[FRAME_HDR];
  if (read_full(fd, hdr, sizeof(hdr)) < 0)
    return -1;
  uint32_t len = get_be32(hdr + 1);
  if (hdr[0] != F_HELLO || len < 7 || len > FRAME_MAX)
    return -1;
  unsigned char *p = malloc(len + 1);
  if (!p || read_full(fd, p, len) < 0)
    return -1;

  tv.tv_sec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (p[0] != AB_PROTO)
    return -2;
  h->flags = p[1];
  h->rows = get_be16(p + 2);
  h->cols = get_be16(p + 4);
  size_t tlen = p[6];
  if (7 + tlen > len || tlen >= sizeof(h->term))
    return -1;
  memcpy(h->term, p + 7, tlen);
  h->term[tlen] = '\0';
  if (!valid_term(h->term))
    h->term[0] = '\0';
  size_t clen = len - 7 - tlen;
  h->cmd = (char *)p + 7 + tlen;
  if (memchr(h->cmd, '\0', clen))
    return -1;
  h->cmd[clen] = '\0';
  return 0;
}

static int selinux_on(void) {
  return access("/sys/fs/selinux/enforce", F_OK) == 0;
}

/* Dropping uid 0 clears the permitted and effective sets unless something
 * asked to keep them. Check rather than trust. */
static int caps_clear(void) {
  FILE *f = fopen("/proc/self/status", "re");
  if (!f)
    return 0;
  char line[256];
  int seen = 0, clear = 1;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "CapPrm:", 7) && strncmp(line, "CapEff:", 7) &&
        strncmp(line, "CapAmb:", 7))
      continue;
    seen++;
    if (strtoull(line + 7, NULL, 16) != 0)
      clear = 0;
  }
  fclose(f);
  return clear && seen >= 2; /* CapAmb only exists since 4.3 */
}

static const char *drop_to_shell(void) {
  /* adb shell runs in u:r:shell:s0. Staying in the root manager's domain
   * would hand the shell user a policy written for root. */
  if (selinux_on() && write_str("/proc/self/attr/exec", "u:r:shell:s0") < 0)
    return "cannot set the SELinux exec context to u:r:shell:s0";

  int last = 63;
  FILE *f = fopen("/proc/sys/kernel/cap_last_cap", "re");
  if (f) {
    if (fscanf(f, "%d", &last) != 1)
      last = 63;
    fclose(f);
  }
  /* adbd keeps setuid and setgid in the bounding set so run-as still works
   * through its file capabilities. Nothing else can ever come back. */
  for (int c = 0; c <= last; c++) {
    if (c == CAP_SETGID_NR || c == CAP_SETUID_NR)
      continue;
    if (prctl(PR_CAPBSET_DROP, c, 0, 0, 0) < 0 && errno != EINVAL)
      return "cannot drop capabilities";
  }

  if (setgroups(sizeof(shell_groups) / sizeof(*shell_groups), shell_groups) <
          0 ||
      setresgid(AID_SHELL, AID_SHELL, AID_SHELL) < 0 ||
      setresuid(AID_SHELL, AID_SHELL, AID_SHELL) < 0)
    return "cannot switch to the shell user";
  if (setresuid(0, 0, 0) == 0 || !caps_clear())
    return "privileges survived the switch to the shell user";
  return NULL;
}

/* The daemon inherits the environment ksud runs module scripts with, which
 * carries busybox standalone mode and the manager's PATH. adbd's shell gets
 * init's environment, so keep what init exports and set the rest the way
 * adbd does. */
static int keep_env(const char *e) {
  static const char *const keys[] = {"BOOTCLASSPATH=",
                                     "DEX2OATBOOTCLASSPATH=",
                                     "SYSTEMSERVERCLASSPATH=",
                                     "STANDALONE_SYSTEMSERVER_JARS=",
                                     "EXTERNAL_STORAGE=",
                                     "ASEC_MOUNTPOINT=",
                                     NULL};
  /* init hands services their sockets and files this way, not the shell's */
  if (!strncmp(e, "ANDROID_SOCKET_", 15) || !strncmp(e, "ANDROID_FILE_", 13))
    return 0;
  if (!strncmp(e, "ANDROID_", 8))
    return 1;
  for (int i = 0; keys[i]; i++)
    if (!strncmp(e, keys[i], strlen(keys[i])))
      return 1;
  return 0;
}

static void become_shell(const struct hello *h, int pty) {
  extern char **environ;
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, NULL);
  /* An ignored disposition survives exec. */
  signal(SIGPIPE, SIG_DFL);
  signal(SIGCHLD, SIG_DFL);
  /* The daemon shields itself from the low memory killer, the shell must not
   * inherit that. */
  write_str("/proc/self/oom_score_adj", "0");

  const char *err = drop_to_shell();
  if (err || chdir("/") < 0) {
    dprintf(2, "android-bridge: %s\n", err ? err : "cannot chdir to /");
    _exit(126);
  }

  char term[80];
  snprintf(term, sizeof(term), "TERM=%s", pty && h->term[0] ? h->term : "dumb");
  char *env[64] = {
      "PATH=/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/"
      "bin:/system_ext/bin:/system/bin:/system/xbin:/odm/bin:/vendor/bin:/"
      "vendor/xbin",
      "HOME=/",
      "SHELL=" SHELL_PATH,
      "USER=shell",
      "LOGNAME=shell",
      "TMPDIR=/data/local/tmp",
      term};
  int n = 7;
  for (char **e = environ; *e && n < 63; e++)
    if (keep_env(*e))
      env[n++] = *e;
  env[n] = NULL;

  if (h->cmd[0])
    execle(SHELL_PATH, "sh", "-c", h->cmd, (char *)NULL, env);
  else
    execle(SHELL_PATH, "-sh", (char *)NULL, env);
  dprintf(2, "android-bridge: cannot run " SHELL_PATH ": %s\n",
          strerror(errno));
  _exit(127);
}

static int spawn(const struct hello *h, struct child *c) {
  if (h->flags & HELLO_PTY) {
    int m, s;
    struct winsize ws = {.ws_row = h->rows, .ws_col = h->cols};
    if (openpty(&m, &s, NULL, NULL, &ws) < 0)
      return -1;
    fcntl(m, F_SETFD, FD_CLOEXEC);
    fcntl(s, F_SETFD, FD_CLOEXEC);
    c->pid = fork();
    if (c->pid == 0) {
      setsid();
      ioctl(s, TIOCSCTTY, 0);
      dup2(s, 0);
      dup2(s, 1);
      dup2(s, 2);
      become_shell(h, 1);
    }
    close(s);
    if (c->pid < 0) {
      close(m);
      return -1;
    }
    c->in = c->out = m;
    c->err = -1;
    return 0;
  }

  int pin[2], pout[2], perr[2];
  if (pipe2(pin, O_CLOEXEC) < 0 || pipe2(pout, O_CLOEXEC) < 0 ||
      pipe2(perr, O_CLOEXEC) < 0)
    return -1;
  c->pid = fork();
  if (c->pid == 0) {
    setsid(); /* its own process group, so a hangup reaches all of it */
    dup2(pin[0], 0);
    dup2(pout[1], 1);
    dup2(perr[1], 2);
    become_shell(h, 0);
  }
  close(pin[0]);
  close(pout[1]);
  close(perr[1]);
  if (c->pid < 0)
    return -1;
  c->in = pin[1];
  c->out = pout[0];
  c->err = perr[0];
  return 0;
}

/* -1 fd is finished (EOF, or EIO from a pty whose slave side is gone),
 * 0 it would block, 1 the socket buffer is full. */
static int pump_out(int fd, uint8_t type) {
  unsigned char tmp[CHUNK];
  while (buf_room(&tosock) >= CHUNK + FRAME_HDR) {
    ssize_t n = read(fd, tmp, sizeof(tmp));
    if (n > 0) {
      buf_frame(&tosock, type, tmp, (uint32_t)n);
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    return n < 0 && errno == EAGAIN ? 0 : -1;
  }
  return 1;
}

static void close_out(struct child *c, int *fd, int pty) {
  close(*fd);
  *fd = -1;
  if (pty)
    c->in = -1; /* the same master */
}

/* Whatever the shell wrote before exiting still sits in the pipe or pty. */
static int drain(int conn, struct child *c, int *fd, uint8_t type, int pty) {
  while (*fd >= 0) {
    int r = pump_out(*fd, type);
    if (r < 0)
      close_out(c, fd, pty);
    if (buf_flush_all(&tosock, conn) < 0)
      return -1;
    if (r == 0)
      break;
  }
  return 0;
}

static int relay(int conn, struct child *c, int pty, int sfd) {
  int stdin_eof = 0, exited = 0, code = 0;
  set_nonblock(conn);
  set_nonblock(c->out);
  if (!pty) {
    set_nonblock(c->in);
    set_nonblock(c->err);
  }

  while (!exited) {
    /* Frames already buffered may have been waiting for room in tochild. */
    uint8_t t;
    uint32_t len;
    int r;
    while ((r = frame_peek(&in, &t, &len)) == 1) {
      unsigned char *p = in.data + FRAME_HDR;
      if (t == F_STDIN) {
        if (c->in >= 0) {
          if (buf_room(&tochild) < len)
            break;
          memcpy(tochild.data + tochild.len, p, len);
          tochild.len += len;
        }
      } else if (t == F_STDIN_EOF) {
        stdin_eof = 1;
      } else if (t == F_WINCH && len == 4) {
        struct winsize ws = {.ws_row = get_be16(p), .ws_col = get_be16(p + 2)};
        if (pty && c->out >= 0)
          ioctl(c->out, TIOCSWINSZ, &ws);
      } else {
        r = -1;
        break;
      }
      buf_consume(&in, FRAME_HDR + len);
    }
    if (r < 0)
      goto hangup;

    if (c->in >= 0 && tochild.len && buf_flush(&tochild, c->in) < 0) {
      /* The shell closed its stdin, or the pty is going away. */
      tochild.len = 0;
      if (!pty)
        close(c->in);
      c->in = -1;
    }
    /* A pty has no EOF to pass on, ^D is just a byte the client sends. */
    if (!pty && stdin_eof && c->in >= 0 && !tochild.len) {
      close(c->in);
      c->in = -1;
    }
    if (buf_flush(&tosock, conn) < 0)
      goto hangup;

    /* An fd that hung up keeps reporting POLLHUP, so only watch the output
     * side while there is room to take what it has. */
    struct pollfd pf[5]; /* conn, out, err, in, signalfd */
    int n = 0, i_out = -1, i_err = -1;
    int room = buf_room(&tosock) >= CHUNK + FRAME_HDR;
    pf[n++] = (struct pollfd){
        conn,
        (short)((buf_room(&in) ? POLLIN : 0) | (tosock.len ? POLLOUT : 0)), 0};
    if (c->out >= 0 && room) {
      i_out = n;
      pf[n++] = (struct pollfd){c->out, POLLIN, 0};
    }
    if (c->err >= 0 && room) {
      i_err = n;
      pf[n++] = (struct pollfd){c->err, POLLIN, 0};
    }
    if (c->in >= 0 && tochild.len) {
      if (pty && i_out >= 0)
        pf[i_out].events |= POLLOUT;
      else
        pf[n++] = (struct pollfd){c->in, POLLOUT, 0};
    }
    int i_sig = n;
    pf[n++] = (struct pollfd){sfd, POLLIN, 0};

    if (poll(pf, (nfds_t)n, -1) < 0) {
      if (errno == EINTR)
        continue;
      goto hangup;
    }
    if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
      ssize_t rr = buf_read(&in, conn);
      if (rr == 0 || rr == -1)
        goto hangup;
    }
    if (i_out >= 0 && pf[i_out].revents & (POLLIN | POLLHUP | POLLERR) &&
        pump_out(c->out, F_STDOUT) < 0)
      close_out(c, &c->out, pty);
    if (i_err >= 0 && pf[i_err].revents & (POLLIN | POLLHUP | POLLERR) &&
        pump_out(c->err, F_STDERR) < 0)
      close_out(c, &c->err, 0);
    if (pf[i_sig].revents & POLLIN) {
      struct signalfd_siginfo si;
      while (read(sfd, &si, sizeof(si)) > 0)
        ;
      int st;
      if (waitpid(c->pid, &st, WNOHANG) == c->pid) {
        exited = 1;
        code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
      }
    }
  }

  if (drain(conn, c, &c->out, F_STDOUT, pty) < 0 ||
      drain(conn, c, &c->err, F_STDERR, 0) < 0)
    goto hangup;
  unsigned char b[4];
  put_be32(b, (uint32_t)code);
  buf_frame(&tosock, F_EXIT, b, 4);
  buf_flush_all(&tosock, conn);
  return code;

hangup:
  /* The client is gone or broke the protocol. Hang up on the shell the way
   * a closed terminal would, the session leader's pid is its group. */
  kill(-c->pid, SIGHUP);
  return -1;
}

static int session(int conn, const struct ucred *cred) {
  struct hello h;
  int r = recv_hello(conn, &h);
  if (r < 0) {
    if (r == -2)
      say(conn, "android-bridge: the client and the daemon are different "
                "versions\n");
    logmsg("pid %d: dropped, no valid hello", (int)cred->pid);
    return 1;
  }

  sigset_t chld;
  sigemptyset(&chld);
  sigaddset(&chld, SIGCHLD);
  sigprocmask(SIG_BLOCK, &chld, NULL);
  int sfd = signalfd(-1, &chld, SFD_CLOEXEC | SFD_NONBLOCK);
  int pty = h.flags & HELLO_PTY;
  struct child c;
  if (sfd < 0 || spawn(&h, &c) < 0) {
    say(conn, "android-bridge: cannot start a shell\n");
    logmsg("pid %d: cannot start a shell: %s", (int)cred->pid, strerror(errno));
    return 1;
  }
  logmsg("pid %d (%s): %s %s, shell pid %d", (int)cred->pid,
         ns_cmp(cred->pid, "pid") == 1 ? "host" : "container",
         h.cmd[0] ? "command" : "interactive", pty ? "pty" : "pipe",
         (int)c.pid);
  int code = relay(conn, &c, pty, sfd);
  if (code < 0)
    logmsg("shell pid %d: client hung up", (int)c.pid);
  else
    logmsg("shell pid %d: exit %d", (int)c.pid, code);
  return 0;
}

static int prepare_dir(const char *dir) {
  if (mkdir(dir, 0700) < 0 && errno != EEXIST)
    return -1;
  int fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat st;
  int r = fstat(fd, &st) < 0 || fchown(fd, 0, 0) < 0 || fchmod(fd, 0700) < 0
              ? -1
              : 0;
  close(fd);
  return r;
}

/* -2 another daemon already answers on this socket */
static int open_listener(const char *dir) {
  struct sockaddr_un a = {.sun_family = AF_UNIX};
  if (strlen(dir) + sizeof(AB_SOCK_NAME) + 1 > sizeof(a.sun_path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  sock_path(a.sun_path, sizeof(a.sun_path), dir);

  /* A live socket means a daemon is serving, a dead one is left from a
   * crash or the last boot. */
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s < 0)
    return -1;
  if (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0) {
    close(s);
    return -2;
  }
  close(s);
  struct stat st;
  if (lstat(a.sun_path, &st) == 0) {
    if (!S_ISSOCK(st.st_mode)) {
      errno = EEXIST;
      return -1;
    }
    unlink(a.sun_path);
  }

  s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s < 0)
    return -1;
  mode_t old = umask(0177);
  int r = bind(s, (struct sockaddr *)&a, sizeof(a));
  umask(old);
  if (r < 0 || listen(s, 16) < 0) {
    close(s);
    return -1;
  }
  return s;
}

static int daemonize(void) {
  pid_t p = fork();
  if (p < 0)
    return -1;
  if (p > 0)
    _exit(0);
  setsid();
  p = fork();
  if (p < 0)
    _exit(1);
  if (p > 0)
    _exit(0);
  int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
  if (fd >= 0) {
    dup2(fd, 0);
    close(fd);
  }
  return chdir("/");
}

int run_daemon(const char *dir, int foreground) {
  if (getuid() != 0 || geteuid() != 0) {
    fprintf(stderr, "android-bridge: the daemon must run as root\n");
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);
  if (prepare_dir(dir) < 0) {
    logmsg("cannot prepare %s: %s", dir, strerror(errno));
    return 1;
  }
  int srv = open_listener(dir);
  if (srv == -2) {
    logmsg("already running on %s", dir);
    return 0;
  }
  if (srv < 0) {
    logmsg("cannot listen in %s: %s", dir, strerror(errno));
    return 1;
  }
  if (!foreground && daemonize() < 0)
    return 1;

  /* Losing the daemon to the low memory killer locks a headless phone out. */
  write_str("/proc/self/oom_score_adj", "-1000");
  signal(SIGCHLD, SIG_IGN);
  logmsg("android-bridge %s listening on %s/%s, pid %d", AB_VERSION, dir,
         AB_SOCK_NAME, (int)getpid());

  for (;;) {
    int conn = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
    if (conn < 0) {
      if (errno != EINTR && errno != ECONNABORTED) {
        logmsg("accept: %s", strerror(errno));
        sleep(1);
      }
      continue;
    }
    struct ucred cred;
    if (!peer_is_real_root(conn, &cred)) {
      logmsg("denied pid %d uid %d", (int)cred.pid, (int)cred.uid);
      deny(conn);
      close(conn);
      continue;
    }
    pid_t p = fork();
    if (p == 0) {
      close(srv);
      signal(SIGCHLD, SIG_DFL);
      _exit(session(conn, &cred));
    }
    if (p < 0)
      logmsg("fork: %s", strerror(errno));
    close(conn);
  }
}
