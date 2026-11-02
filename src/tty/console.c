/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* The console pair on its way from container init to the monitor, or to a
 * foreground start. Both fds travel together so the holder keeps the slave
 * open too: with no slave open the master hangs up, and the session would end
 * between an init's death and the next cycle's pair arriving. */
int ds_console_send(int sock, int master, int slave) {
  int fds[2] = {master, slave};
  return ds_send_fds(sock, "FD", 2, fds, 2);
}

int ds_console_recv(int sock, int *master, int *slave) {
  char tag[2];
  int fds[2], count;
  if (ds_recv_fds(sock, tag, sizeof(tag), fds, 2, &count) < 0)
    return -1;
  if (count != 2) {
    for (int i = 0; i < count; i++)
      close(fds[i]);
    return -1;
  }
  *master = fds[0];
  *slave = fds[1];
  return 0;
}

/* Console Monitor Loop */

/* The console pair arrives on console_sock from container init, once per boot
 * cycle: init sends it before it execs and wait_for_boot() returns at that
 * exec, so the first pair is already queued when we get here. An in-container
 * reboot brings a fresh pair, and the old one is swapped out for it. */
int console_monitor_loop(int console_sock, pid_t monitor_pid,
                         struct ds_config *cfg) {
  int epfd, sfd;
  int master_fd = -1, slave_fd = -1;
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

  /* 2. Watch for the console pair; the master joins once it arrives */
  ev.events = EPOLLIN;
  ev.data.fd = console_sock;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, console_sock, &ev) < 0)
    ds_warn("epoll_ctl(console_sock) failed: %s", strerror(errno));

  /* 3. Watch signalfd */
  ev.events = EPOLLIN;
  ev.data.fd = sfd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) < 0)
    ds_warn("epoll_ctl(sig_fd) failed: %s", strerror(errno));

  /* Set terminal to raw mode */
  struct termios oldtios;
  int is_tty = ds_setup_tios(STDIN_FILENO, &oldtios);

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

          if (master_fd < 0)
            continue; /* typed before the console arrived */
          if (write_all(master_fd, buf, (size_t)n) < 0) {
            running = 0;
            break;
          }
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
      } else if (fd == console_sock) {
        int m, s;
        if (ds_console_recv(console_sock, &m, &s) < 0) {
          /* End of file: the monitor is gone and its SIGCHLD is on its way.
           * Level-triggered, the socket would wake us until then. */
          epoll_ctl(epfd, EPOLL_CTL_DEL, console_sock, NULL);
          continue;
        }
        if (master_fd >= 0) {
          /* epoll keys on the open file, which the sender's copy may still
           * keep alive, so close() alone would leave the old master in the
           * set under a number the new one may reuse. Its last lines are
           * still worth showing. */
          epoll_ctl(epfd, EPOLL_CTL_DEL, master_fd, NULL);
          int fl = fcntl(master_fd, F_GETFL, 0);
          if (fl >= 0)
            (void)fcntl(master_fd, F_SETFL, fl | O_NONBLOCK);
          while ((n = read(master_fd, buf, sizeof(buf))) > 0)
            write_all(STDOUT_FILENO, buf, (size_t)n);
          close(master_fd);
          close(slave_fd);
        }
        master_fd = m;
        slave_fd = s;
        ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
        ev.data.fd = master_fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, master_fd, &ev) < 0)
          ds_warn("epoll_ctl(master_fd) failed: %s", strerror(errno));
        struct winsize ws;
        if (is_tty == 0 && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
          ioctl(master_fd, TIOCSWINSZ, &ws);
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
          if (master_fd >= 0 && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
            ioctl(master_fd, TIOCSWINSZ, &ws);
        } else if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
          /* Forward to container init (read live PID) */
          /* The pidfile follows an in-container reboot, so it names the
           * live init. */
          pid_t live_pid = -1;
          if (read_and_validate_pid(cfg->pidfile, &live_pid) == 0 &&
              live_pid > 0)
            kill(live_pid, (int)fdsi.ssi_signo);
        }
      }
    }
  }

  /* Restore terminal settings */
  if (is_tty == 0) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldtios);
  }

  if (master_fd >= 0) {
    close(master_fd);
    close(slave_fd);
  }
  close(epfd);
  close(sfd);
  return ret;
}
