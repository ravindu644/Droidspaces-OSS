/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Console Monitor Loop
 * ---------------------------------------------------------------------------*/

/* Read the current container init PID from the pidfile.
 * After an in-container reboot, the intermediate process updates the pidfile
 * with the new init PID, so this always returns the LIVE container PID. */
static pid_t read_current_container_pid(const char *pidfile) {
  if (!pidfile || !pidfile[0])
    return -1;

  pid_t pid = -1;
  if (read_and_validate_pid(pidfile, &pid) == 0)
    return pid;
  return -1;
}

int console_monitor_loop(int master_fd, pid_t monitor_pid,
                         struct ds_config *cfg) {
  int epfd, sfd;
  sigset_t mask;
  struct signalfd_siginfo fdsi;
  struct epoll_event ev, events[10];
  char buf[4096];
  ssize_t n;
  int ret = 0;

  /* Setup signalfd for monitor signals */
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGWINCH);
  if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
    return -1;

  sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sfd < 0)
    return -1;

  /* Setup epoll */
  epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    close(sfd);
    return -1;
  }

  /* 1. Watch user stdin */
  ev.events = EPOLLIN;
  ev.data.fd = STDIN_FILENO;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0)
    ds_warn("epoll_ctl(stdin) failed: %s", strerror(errno));

  /* 2. Watch PTY master */
  ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
  ev.data.fd = master_fd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, master_fd, &ev) < 0)
    ds_warn("epoll_ctl(master_fd) failed: %s", strerror(errno));

  /* 3. Watch signalfd */
  ev.events = EPOLLIN;
  ev.data.fd = sfd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) < 0)
    ds_warn("epoll_ctl(sig_fd) failed: %s", strerror(errno));

  /* Set terminal to raw mode */
  struct termios oldtios;
  int is_tty = ds_setup_tios(STDIN_FILENO, &oldtios);

  /* Initial window size sync */
  if (is_tty == 0) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
      ioctl(master_fd, TIOCSWINSZ, &ws);
  }

  int running = 1;
  while (running) {
    int nfds = epoll_wait(epfd, events, 10, -1);
    if (nfds < 0) {
      if (errno == EINTR)
        continue;
      ret = -1;
      break;
    }

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;

      if (fd == STDIN_FILENO) {
        /* User input -> Container master */
        n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) {
          /* Check for CTRL+ALT+Q (\x1b\x11) escape sequence */
          if (n >= 2 && buf[0] == '\x1b' && buf[1] == '\x11') {
            static int exit_detected = 0;
            if (exit_detected == 0) {
              /* Droidspaces Specific: Graceful background shutdown.
               * We fork a detached child to call stop_rootfs() silently.
               * This allows the current console_monitor_loop to keep running,
               * streaming the systemd/sysvinit shutdown logs to the user's
               * terminal until the container naturally dies and the PTY hangs
               * up.
               */
              pid_t bg_pid = fork();
              if (bg_pid == 0) {
                /* Background shutdown process */
                setsid();
                ds_log_silent = 1;
                stop_rootfs(cfg, 0);
                _exit(0);
              } else if (bg_pid > 0) {
                /* Parent console loop just marks exit and continues streaming
                 */
                exit_detected = 1;
              }
            }
            continue; /* Don't write the CTRL+ALT+Q sequence to the PTY */
          }

          if (write_all(master_fd, buf, (size_t)n) < 0) {
            running = 0;
            break;
          }
        } else if (n == 0) {
          /* EOF on stdin */
        }
      } else if (fd == master_fd) {
        /* Container output -> User stdout */
        if (events[i].events & (EPOLLHUP | EPOLLERR)) {
          running = 0;
          break;
        }
        n = read(master_fd, buf, sizeof(buf));
        if (n > 0) {
          write_all(STDOUT_FILENO, buf, (size_t)n);
        } else {
          running = 0;
        }
      } else if (fd == sfd) {
        /* Signal handling */
        n = read(sfd, &fdsi, sizeof(fdsi));
        if (n != sizeof(fdsi))
          continue;

        if (fdsi.ssi_signo == SIGCHLD) {
          int status;
          pid_t child = waitpid(monitor_pid, &status, WNOHANG);
          if (child == monitor_pid) {
            /* Monitor exited - container is fully done */
            running = 0;
          }
        } else if (fdsi.ssi_signo == SIGWINCH) {
          struct winsize ws;
          if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
            ioctl(master_fd, TIOCSWINSZ, &ws);
        } else if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
          /* Forward to container init (read live PID) */
          pid_t live_pid = read_current_container_pid(cfg->pidfile);
          if (live_pid > 0)
            kill(live_pid, (int)fdsi.ssi_signo);
        }
      }
    }
  }

  /* Restore terminal settings */
  if (is_tty == 0) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldtios);
  }

  close(epfd);
  close(sfd);
  return ret;
}
