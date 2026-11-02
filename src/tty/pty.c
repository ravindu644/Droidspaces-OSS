/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <sys/uio.h>

/* PTY Allocation */

/* Open master + slave from the ptmx node given. "/dev/ptmx" on the host;
 * inside a container "/dev/pts/ptmx", which is the newinstance node whatever
 * setup_devpts had to fall back to for /dev/ptmx itself.
 * TIOCGPTPEER (4.13+) opens slave directly from master fd.
 * Falls back to TIOCGPTN + path open for kernel 3.x. */
int ds_openpty(const char *ptmx, int *master, int *slave, char *name) {
  int m = open(ptmx, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (m < 0)
    return -1;

  /* best-effort: vendor 4.9 kernels may return EINVAL/EIO on newinstance
   * devpts mounts; kernel auto-unlocks if needed */
  int unlock = 0;
  (void)ioctl(m, TIOCSPTLCK, &unlock);

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
    char pts_path[PATH_MAX];
    snprintf(pts_path, PATH_MAX, "/dev/pts/%u", ptyno);
    if (name)
      snprintf(name, PATH_MAX, "%s", pts_path);
    s = open(pts_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
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
  if (ds_openpty("/dev/ptmx", &tty->master, &tty->slave, tty->name) < 0) {
    ds_error("openpty failed: %s", strerror(errno));
    return -1;
  }

  /* tty group ownership + permissions */
  if (fchown(tty->slave, 0, 5) < 0) {
    /* best-effort, ignore */
  }
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

/* Termios / TIOS */

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

  /* Raw mode as LXC and SSH set it: cfmakeraw, which also forces 8N1 and
   * drops break handling (both moot on a pty), plus the flow-control and
   * job-control bits it leaves alone. OPOST goes with it: with ONLCR active
   * on the host PTY the line discipline would turn \n into \r\n and corrupt
   * TUI escape sequences from tmux, vim, etc. The container shell sets its
   * own ONLCR on the inner slave, so the translation happens exactly once. */
  cfmakeraw(&new_tios);
  new_tios.c_iflag |= IGNPAR;
  new_tios.c_iflag &= (tcflag_t) ~(IXANY | IXOFF);
#ifdef IUCLC
  new_tios.c_iflag &= (tcflag_t)~IUCLC;
#endif
  new_tios.c_lflag &= (tcflag_t) ~(TOSTOP | ECHOE | ECHOK);

  if (tcsetattr(fd, TCSAFLUSH, &new_tios) < 0)
    return -1;

  return 0;
}
