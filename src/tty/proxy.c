/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Runtime Utilities */

static volatile sig_atomic_t g_sigwinch_received = 0;
static void handle_sigwinch(int sig) {
  (void)sig;
  g_sigwinch_received = 1;
}

static void update_terminal_size(int master_fd) {
  struct winsize ws;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
    ioctl(master_fd, TIOCSWINSZ, &ws);
  }
}

int ds_terminal_proxy(int master_fd) {
  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0)
    return -1;

  struct epoll_event ev, events[10];
  char buf[8192];

  /* NOTE: Do NOT set O_NONBLOCK on STDIN or master_fd here.
   * This is an epoll-driven loop - read() is only called after epoll_wait()
   * signals readability, so it will never block.  O_NONBLOCK causes read() to
   * return -1 EAGAIN between events, which the n<=0 EOF check then
   * misinterprets as a hangup, closes master_fd, and sends EIO to the slave
   * (bash), killing the session. */

  /* Propagate initial window size */
  update_terminal_size(master_fd);

  struct sigaction sa;
  sa.sa_handler = handle_sigwinch;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa, NULL);

  /* 1. Watch stdin */
  ev.events = EPOLLIN;
  ev.data.fd = STDIN_FILENO;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
    close(epfd);
    return -1;
  }

  /* 2. Watch master PTY */
  ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
  ev.data.fd = master_fd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, master_fd, &ev) < 0) {
    close(epfd);
    return -1;
  }

  int running = 1;
  while (running) {
    int nfds = epoll_wait(epfd, events, 10, -1);
    if (g_sigwinch_received) {
      g_sigwinch_received = 0;
      update_terminal_size(master_fd);
    }

    if (nfds < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;

      if (fd == STDIN_FILENO) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) {
          running = 0;
          break;
        }
        if (write_all(master_fd, buf, (size_t)n) < 0) {
          running = 0;
          break;
        }
      } else if (fd == master_fd) {
        if (events[i].events & (EPOLLHUP | EPOLLERR)) {
          running = 0;
          break;
        }
        ssize_t n = read(master_fd, buf, sizeof(buf));
        if (n > 0) {
          if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) {
            running = 0;
            break;
          }
        } else {
          running = 0;
          break;
        }
      }
    }
  }

  sigaction(SIGWINCH, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);
  close(epfd);

  return 0;
}
