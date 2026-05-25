/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <sys/uio.h>

/* ---------------------------------------------------------------------------
 * PTY Allocation
 * ---------------------------------------------------------------------------*/

/* Open master + slave without relying on /dev/ptmx symlink resolution.
 * TIOCGPTPEER (4.13+) opens slave directly from master fd.
 * Falls back to TIOCGPTN + path open for kernel 3.x. */
int ds_openpty(int *master, int *slave, char *name) {
  int m = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (m < 0)
    return -1;

  /* unlock slave */
  int unlock = 0;
  if (ioctl(m, TIOCSPTLCK, &unlock) < 0)
    goto err;

  /* try kernel 4.13+ path-free method first */
  int s = ioctl(m, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (s >= 0) {
    if (name) {
      unsigned int ptyno;
      if (ioctl(m, TIOCGPTN, &ptyno) == 0) {
        snprintf(name, PATH_MAX, "/dev/pts/%u", ptyno);
      }
    }
  } else {
    /* fallback: build /dev/pts/N path */
    unsigned int ptyno;
    if (ioctl(m, TIOCGPTN, &ptyno) < 0)
      goto err;
    snprintf(name, PATH_MAX, "/dev/pts/%u", ptyno);
    s = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (s < 0)
      goto err;
  }

  *master = m;
  *slave = s;
  return 0;
err:
  close(m);
  return -1;
}

int ds_terminal_create(struct ds_tty_info *tty) {
  if (ds_openpty(&tty->master, &tty->slave, tty->name) < 0) {
    ds_error("openpty failed: %s", strerror(errno));
    return -1;
  }

  /* tty group ownership + permissions */
  fchown(tty->slave, 0, 5);
  fchmod(tty->slave, 0620);

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
