/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
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

#include "internal.h"

/* connect to the daemon and relay our command */

static int send_request(int sock, int argc, char **argv, uint32_t flags) {
  uint32_t nf = htonl(flags), na = htonl((uint32_t)argc);
  if (write_all(sock, &nf, 4) < 0 || write_all(sock, &na, 4) < 0)
    return -1;
  for (int i = 0; i < argc; i++) {
    uint32_t al = (uint32_t)strlen(argv[i]), nal = htonl(al);
    if (write_all(sock, &nal, 4) < 0)
      return -1;
    if (al && write_all(sock, argv[i], al) < 0)
      return -1;
  }
  if (flags & REQ_FLAG_PTY) {
    struct winsize ws = {24, 80, 0, 0};
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    uint16_t wd[2] = {htons(ws.ws_row), htons(ws.ws_col)};
    if (write_all(sock, wd, 4) < 0)
      return -1;
  }
  return 0;
}

int ds_client_run(int argc, char **argv, const char *cmd, int want_stdin) {
  if (argc < 1)
    return -2;
  if (!cmd)
    cmd = "";

  int is_enter = strcmp(cmd, "enter") == 0;
  int is_boot = strcmp(cmd, "start") == 0 || strcmp(cmd, "restart") == 0;
  int interactive = is_enter || is_boot || strcmp(cmd, "run") == 0;
  int has_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

  if (interactive && !has_tty) {
    if (is_enter) {
      ds_error("Interactive terminal is required for the enter command\n");
      return 1;
    }
    /* Strip -f/--foreground; start_rootfs() will warn and flip the switch. */
    if (is_boot) {
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
    interactive = 0;
  }

  /* --stdin on a pipe-mode run: relay our stdin to the command the way lxc
   * exec does. Never by default, because libsu style callers share their
   * stdin with the shell that is feeding them commands and we would eat the
   * next line. A terminal session relays stdin anyway. */
  int relay_stdin = want_stdin && !interactive && strcmp(cmd, "run") == 0;

  /* try to connect - single pass for execution path, retry loop only for 1st
   * connect */
  int sock = -1;
  struct sockaddr_un addr;
  socklen_t alen = ds_abstract_addr(&addr, DS_SOCK_NAME);

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

  /* The daemon may reject us and close before we finish writing the request.
   * Without this the kernel kills us with SIGPIPE: exit 141, no output. */
  signal(SIGPIPE, SIG_IGN);

  /* EPIPE means the daemon already closed. Its verdict (an error frame and
   * the exit code) is queued on our socket, so fall through and relay it. */
  mode_t mask = umask(0); /* musl has no getumask(), read it by swapping */
  umask(mask);
  uint32_t flags = (interactive ? REQ_FLAG_PTY : 0u) |
                   (relay_stdin ? REQ_FLAG_STDIN : 0u) | REQ_FLAG_UMASK |
                   ((uint32_t)(mask & 0777) << REQ_UMASK_SHIFT);
  if (send_request(sock, argc, argv, flags) < 0 && errno != EPIPE)
    goto send_err;

  /* run the relay loop */
  struct termios orig;
  int raw_tty_active = 0;
  int winch_sfd = -1;

  if (interactive && has_tty && tcgetattr(STDIN_FILENO, &orig) == 0) {
    raw_tty_active = 1;
    struct termios raw = orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    /* signalfd for SIGWINCH: no handler, no 200ms timeout needed */
    sigset_t ws;
    sigemptyset(&ws);
    sigaddset(&ws, SIGWINCH);
    sigprocmask(SIG_BLOCK, &ws, NULL);
    winch_sfd = signalfd(-1, &ws, SFD_NONBLOCK | SFD_CLOEXEC);
  }

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  struct epoll_event ev, events[4];

  /* A regular file on stdin cannot be polled (EPERM), so it is pumped one
   * chunk per loop turn instead. */
  int stdin_file = 0;
  /* Set while stdin has data but the socket is too full to take a frame
   * without blocking: a blocked client cannot drain the daemon's output, and
   * with both directions full the two would deadlock. Wake on EPOLLOUT. */
  int wait_out = 0;
  if (raw_tty_active || relay_stdin) {
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
      if (relay_stdin && errno == EPERM)
        stdin_file = 1;
      else if (relay_stdin)
        ds_send_frame(sock, MSG_OUT, NULL, 0); /* no stdin at all: EOF now */
    }
  }
  if (winch_sfd >= 0) {
    ev.events = EPOLLIN;
    ev.data.fd = winch_sfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, winch_sfd, &ev);
  }
  ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
  ev.data.fd = sock;
  epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);

  int exit_code = 0;
  char buf[DS_IOBUF];

  for (;;) {
    int nfds = epoll_wait(epfd, events, 4, (stdin_file && !wait_out) ? 0 : -1);
    if (nfds < 0 && errno != EINTR)
      break;

    int done = 0, stdin_ready = stdin_file;
    for (int i = 0; i < nfds && !done; i++) {
      int fd = events[i].data.fd;

      if (winch_sfd >= 0 && fd == winch_sfd) {
        struct signalfd_siginfo si;
        while (read(winch_sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
          struct winsize nws = {24, 80, 0, 0};
          ioctl(STDIN_FILENO, TIOCGWINSZ, &nws);
          uint16_t wd2[2] = {htons(nws.ws_row), htons(nws.ws_col)};
          ds_send_frame(sock, MSG_WINCH, wd2, 4);
        }
      } else if (fd == STDIN_FILENO) {
        stdin_ready = 1;
      } else if (fd == sock) {
        if (wait_out && (events[i].events & EPOLLOUT)) {
          wait_out = 0;
          ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
          ev.data.fd = sock;
          epoll_ctl(epfd, EPOLL_CTL_MOD, sock, &ev);
          /* A file is always readable; a pipe may be empty, so let its own
           * EPOLLIN drive the next read instead of blocking in read(). */
          if (stdin_file) {
            stdin_ready = 1;
          } else {
            ev.events = EPOLLIN;
            ev.data.fd = STDIN_FILENO;
            epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
          }
        }
        if (events[i].events & EPOLLIN) {
          /* drain all available frames without a redundant poll() probe */
          for (;;) {
            uint8_t type;
            uint32_t mlen;
            if (ds_recv_frame_hdr(sock, &type, &mlen) < 0) {
              done = 1;
              break;
            }

            if (type == MSG_EXIT) {
              uint32_t nc = 0;
              if (mlen >= 4)
                ds_read_exact(sock, &nc, 4);
              exit_code = (int)ntohl(nc);
              done = 1;
              break;
            }

            FILE *dest = (type == MSG_ERR) ? stderr : stdout;
            uint32_t rem = mlen;
            while (rem) {
              uint32_t c =
                  (rem < (uint32_t)sizeof(buf)) ? rem : (uint32_t)sizeof(buf);
              if (ds_read_exact(sock, buf, c) < 0) {
                done = 1;
                break;
              }
              fwrite(buf, 1, c, dest);
              rem -= c;
            }
            fflush(dest);
            if (done)
              break;

            /* check for more data without blocking */
            struct pollfd pfd = {sock, POLLIN, 0};
            if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN))
              break;
          }
        }

        if (!done && (events[i].events & (EPOLLHUP | EPOLLERR))) {
          done = 1;
          break;
        }
      }
    }

    /* A unix socket reports POLLOUT only with most of its send buffer free,
     * far more than one frame, so a write after a positive poll never blocks.
     */
    struct pollfd pw = {sock, POLLOUT, 0};
    if (!done && stdin_ready &&
        (poll(&pw, 1, 0) <= 0 || !(pw.revents & POLLOUT))) {
      wait_out = 1;
      stdin_ready = 0;
      ev.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR;
      ev.data.fd = sock;
      epoll_ctl(epfd, EPOLL_CTL_MOD, sock, &ev);
      /* Level-triggered: a readable stdin would wake us every turn. */
      if (!stdin_file)
        epoll_ctl(epfd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
    }
    if (!done && stdin_ready) {
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      int stop = 0;
      if (n > 0) {
        /* A failed send means the daemon stopped reading, normally because
         * the command is gone. Its exit frame is still queued for us, so
         * stop relaying and let the socket branch collect it. */
        stop = ds_send_frame(sock, MSG_OUT, buf, (uint32_t)n) < 0;
      } else if (raw_tty_active) {
        done = 1;
      } else {
        /* EOF: tell the daemon, then keep going for output and the exit code.
         */
        ds_send_frame(sock, MSG_OUT, NULL, 0);
        stop = 1;
      }
      if (stop) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
        stdin_file = 0;
      }
    }
    if (done)
      break;
  }

  if (raw_tty_active) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    if (winch_sfd >= 0) {
      sigset_t ws;
      sigemptyset(&ws);
      sigaddset(&ws, SIGWINCH);
      sigprocmask(SIG_UNBLOCK, &ws, NULL);
      close(winch_sfd);
    }
  }
  close(epfd);
  close(sock);
  return exit_code;

send_err:
  fprintf(stderr, "client: send failed: %s\n", strerror(errno));
  close(sock);
  return 1;
}
