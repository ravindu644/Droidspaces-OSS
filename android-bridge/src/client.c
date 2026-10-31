/*
 * android-bridge: the container side
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bridge.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

static struct buf tosock, in;
static struct termios saved;
static int raw_on;

static void restore_tty(void) {
  if (raw_on)
    tcsetattr(0, TCSADRAIN, &saved);
  raw_on = 0;
}

static void on_fatal(int sig) {
  restore_tty();
  signal(sig, SIG_DFL);
  raise(sig);
}

static int connect_bridge(const char *path) {
  struct sockaddr_un a = {.sun_family = AF_UNIX};
  if (strlen(path) >= sizeof(a.sun_path)) {
    fprintf(stderr, "android-bridge: socket path too long: %s\n", path);
    return -1;
  }
  strcpy(a.sun_path, path);
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s >= 0 && connect(s, (struct sockaddr *)&a, sizeof(a)) == 0)
    return s;

  int e = errno;
  if (e == EACCES || e == EPERM)
    fprintf(stderr, "android-bridge: permission denied, only root may use "
                    "the bridge\n");
  else if (e == ENOENT)
    fprintf(stderr,
            "android-bridge: no bridge at %s\n"
            "Is the daemon running, and is " AB_HOST_DIR
            " bind-mounted at " AB_CONTAINER_DIR "?\n",
            path);
  else if (e == ECONNREFUSED)
    fprintf(stderr, "android-bridge: the daemon is not running (%s)\n", path);
  else
    fprintf(stderr, "android-bridge: %s: %s\n", path, strerror(e));
  return -1;
}

static void get_size(uint16_t *rows, uint16_t *cols) {
  struct winsize ws = {0};
  if (ioctl(0, TIOCGWINSZ, &ws) < 0)
    ioctl(1, TIOCGWINSZ, &ws);
  *rows = ws.ws_row;
  *cols = ws.ws_col;
}

/* adb shell joins its arguments with spaces and hands them to sh -c, so
 * quoting works the same way here as people already expect from adb. */
static char *join(int argc, char **argv) {
  size_t len = 1;
  for (int i = 0; i < argc; i++)
    len += strlen(argv[i]) + 1;
  char *s = calloc(1, len);
  if (!s)
    return NULL;
  for (int i = 0; i < argc; i++) {
    if (i)
      strcat(s, " ");
    strcat(s, argv[i]);
  }
  return s;
}

static int send_hello(int pty, const char *cmd) {
  const char *term = getenv("TERM");
  size_t tlen = term ? strlen(term) : 0, clen = strlen(cmd);
  if (tlen > 63)
    tlen = 0; /* no real terminal type is this long */
  size_t len = 7 + tlen + clen;
  if (len > FRAME_MAX) {
    fprintf(stderr, "android-bridge: command too long\n");
    return -1;
  }
  unsigned char *p = malloc(len);
  if (!p)
    return -1;
  uint16_t rows, cols;
  get_size(&rows, &cols);
  p[0] = AB_PROTO;
  p[1] = pty ? HELLO_PTY : 0;
  put_be16(p + 2, rows);
  put_be16(p + 4, cols);
  p[6] = (unsigned char)tlen;
  memcpy(p + 7, term, tlen);
  memcpy(p + 7 + tlen, cmd, clen);
  buf_frame(&tosock, F_HELLO, p, (uint32_t)len);
  free(p);
  return 0;
}

/* pty_mode: -1 like adb (a terminal only for an interactive shell on a
 * terminal), 0 never, 1 always. */
int run_client(const char *path, int pty_mode, int argc, char **argv) {
  char *cmd = join(argc, argv);
  if (!cmd)
    return 255;
  int pty = pty_mode >= 0 ? pty_mode : !cmd[0] && isatty(0);
  int s = connect_bridge(path);
  if (s < 0 || send_hello(pty, cmd) < 0)
    return 255;
  free(cmd);

  sigset_t winch;
  sigemptyset(&winch);
  sigaddset(&winch, SIGWINCH);
  sigprocmask(SIG_BLOCK, &winch, NULL);
  int sfd = signalfd(-1, &winch, SFD_CLOEXEC | SFD_NONBLOCK);

  if (pty && isatty(0) && tcgetattr(0, &saved) == 0) {
    struct termios t = saved;
    cfmakeraw(&t);
    tcsetattr(0, TCSADRAIN, &t);
    raw_on = 1;
    atexit(restore_tty);
    signal(SIGTERM, on_fatal);
    signal(SIGHUP, on_fatal);
    signal(SIGINT, on_fatal);
  }

  /* Only the socket goes non-blocking: O_NONBLOCK on a shared terminal would
   * leak into the shell that started us. Local writes may block, which just
   * pushes back on the daemon, and it never blocks on us. */
  set_nonblock(s);
  int stdin_eof = 0;
  for (;;) {
    uint8_t t;
    uint32_t len;
    int r;
    while ((r = frame_peek(&in, &t, &len)) == 1) {
      unsigned char *p = in.data + FRAME_HDR;
      if (t == F_STDOUT)
        write_all(1, p, len);
      else if (t == F_STDERR)
        write_all(2, p, len);
      else if (t == F_EXIT && len == 4)
        return (int)(get_be32(p) & 0xff);
      else
        r = -1;
      if (r < 0)
        break;
      buf_consume(&in, FRAME_HDR + len);
    }
    if (r < 0) {
      restore_tty();
      fprintf(stderr, "android-bridge: protocol error\n");
      return 255;
    }
    if (buf_flush(&tosock, s) < 0)
      break;

    struct pollfd pf[3] = {
        {s, (short)(POLLIN | (tosock.len ? POLLOUT : 0)), 0},
        {sfd, POLLIN, 0},
        {0, 0, 0},
    };
    int n = 2;
    if (!stdin_eof && buf_room(&tosock) >= CHUNK + FRAME_HDR) {
      pf[2].events = POLLIN;
      n = 3;
    }
    if (poll(pf, (nfds_t)n, -1) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
      ssize_t rr = buf_read(&in, s);
      if (rr == 0 || rr == -1)
        break;
    }
    if (pf[1].revents & POLLIN) {
      struct signalfd_siginfo si;
      while (read(sfd, &si, sizeof(si)) > 0)
        ;
      unsigned char ws[4];
      uint16_t rows, cols;
      get_size(&rows, &cols);
      put_be16(ws, rows);
      put_be16(ws + 2, cols);
      if (pty)
        buf_frame(&tosock, F_WINCH, ws, 4);
    }
    if (n == 3 && pf[2].revents) {
      unsigned char tmp[CHUNK];
      ssize_t rd = read(0, tmp, sizeof(tmp));
      if (rd > 0) {
        buf_frame(&tosock, F_STDIN, tmp, (uint32_t)rd);
      } else if (rd == 0 || (errno != EINTR && errno != EAGAIN)) {
        stdin_eof = 1;
        buf_frame(&tosock, F_STDIN_EOF, NULL, 0);
      }
    }
  }
  restore_tty();
  fprintf(stderr, "android-bridge: lost the connection to the daemon\n");
  return 255;
}
