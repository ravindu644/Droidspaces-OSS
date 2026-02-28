/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <sys/uio.h>

/* ---------------------------------------------------------------------------
 * PTY Allocation
 * ---------------------------------------------------------------------------*/

int ds_terminal_create(struct ds_tty_info *tty) {
  /* openpty() allocates a master/slave pair.
   * slave name is returned in tty->name. */
  if (openpty(&tty->master, &tty->slave, tty->name, NULL, NULL) < 0) {
    ds_error("openpty failed: %s", strerror(errno));
    return -1;
  }

  /* Set ownership and permissions for the slave TTY */
  if (fchown(tty->slave, 0, 5) < 0) {
    /* 5 is usually 'tty' group, failure ignored on some platforms */
  }
  if (fchmod(tty->slave, 0620) < 0) {
    /* Failure ignored */
  }

  /* Set FD_CLOEXEC so they don't leak to the container's init */
  if (fcntl(tty->master, F_SETFD, FD_CLOEXEC) < 0)
    ds_warn("fcntl(master, FD_CLOEXEC) failed: %s", strerror(errno));
  if (fcntl(tty->slave, F_SETFD, FD_CLOEXEC) < 0)
    ds_warn("fcntl(slave, FD_CLOEXEC) failed: %s", strerror(errno));

  return 0;
}

int ds_terminal_set_stdfds(int fd) {
  if (dup2(fd, STDIN_FILENO) < 0)
    return -1;
  if (dup2(fd, STDOUT_FILENO) < 0)
    return -1;
  if (dup2(fd, STDERR_FILENO) < 0)
    return -1;
  return 0;
}

int ds_terminal_make_controlling(int fd) {
  /* Drop existing controlling terminal and session */
  setsid();

  /* Make fd the new controlling terminal */
  if (ioctl(fd, TIOCSCTTY, (char *)NULL) < 0) {
    ds_error("TIOCSCTTY failed: %s", strerror(errno));
    return -1;
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Termios / TIOS
 * ---------------------------------------------------------------------------*/

int ds_setup_tios(int fd, struct termios *old) {
  struct termios new_tios;

  if (!isatty(fd))
    return -1;

  if (tcgetattr(fd, old) < 0)
    return -1;

  /* Ignore signals during transition */
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);

  new_tios = *old;

  /* Raw mode - mirroring LXC/SSH settings for best compatibility */
  new_tios.c_iflag |= IGNPAR;
  new_tios.c_iflag &=
      (tcflag_t) ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
#ifdef IUCLC
  new_tios.c_iflag &= (tcflag_t)~IUCLC;
#endif
  new_tios.c_lflag &=
      (tcflag_t) ~(TOSTOP | ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
#ifdef IEXTEN
  new_tios.c_lflag &= (tcflag_t)~IEXTEN;
#endif
  /* Keep host's ONLCR enabled to avoid staircase output if the container side
   * stops sending \r (e.g. during shutdown or sudo execution). Duplicate \r
   * are harmless. */
  // new_tios.c_oflag &= (tcflag_t)~ONLCR;
  new_tios.c_oflag |= OPOST;
  new_tios.c_cc[VMIN] = 1;
  new_tios.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSAFLUSH, &new_tios) < 0)
    return -1;

  return 0;
}

/* ---------------------------------------------------------------------------
 * Runtime Utilities
 * ---------------------------------------------------------------------------*/

void build_container_ttys_string(struct ds_tty_info *ttys, int count, char *buf,
                                 size_t size) {
  size_t offset = 0;

  if (size == 0)
    return;

  buf[0] = '\0';

  for (int i = 0; i < count; i++) {
    const char *name = ttys[i].name;
    size_t len = strlen(name);

    /* Add space between entries */
    if (i > 0) {
      if (offset + 1 >= size)
        break;
      buf[offset++] = ' ';
    }

    /* Copy name safely */
    if (offset + len >= size) {
      len = size - offset - 1;
    }

    memcpy(buf + offset, name, len);
    offset += len;

    if (offset >= size - 1)
      break;
  }

  buf[offset] = '\0';
}

static volatile int proxy_master_fd = -1;
static void handle_sigwinch(int sig) {
  (void)sig;
  struct winsize ws;
  if (proxy_master_fd != -1 && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
    ioctl(proxy_master_fd, TIOCSWINSZ, &ws);
  }
}

int ds_terminal_proxy(int master_fd) {
  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0)
    return -1;

  struct epoll_event ev, events[10];
  char buf[8192];
  proxy_master_fd = master_fd;

  /* Propagate initial window size */
  handle_sigwinch(SIGWINCH);
  signal(SIGWINCH, handle_sigwinch);

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

  signal(SIGWINCH, SIG_DFL);
  proxy_master_fd = -1;
  close(epfd);
  return 0;
}
