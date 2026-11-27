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

static void drain_fd(int fd, int conn, uint8_t type) {
  char buf[DS_IOBUF];
  int fl = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0)
      break;
    ds_send_frame(conn, type, buf, (uint32_t)n);
  }
  fcntl(fd, F_SETFL, fl);
}

static void close_pipe(int p[2]) {
  if (p[0] >= 0)
    close(p[0]);
  if (p[1] >= 0)
    close(p[1]);
  p[0] = p[1] = -1;
}

/* unified session handler for both pty and pipe modes
 *
 * note on pipe hangs: start_rootfs leaks descriptors to the intermediate
 * monitor process. waiting for epollhup would block forever. we poll waitpid
 * instead and do a final non-blocking drain when the direct child exits. */

void handle_session(int conn, ds_req_t *r) {
  int is_pty = (r->flags & REQ_FLAG_PTY);
  int master = -1, slave = -1;
  int out[2] = {-1, -1}, err[2] = {-1, -1}, in[2] = {-1, -1};
  char buf[DS_IOBUF];

  if (is_pty) {
    if (ds_openpty("/dev/ptmx", &master, &slave, NULL) < 0) {
      ds_send_frame(conn, MSG_ERR, "daemon: openpty failed\n", 23);
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
    /* Without REQ_FLAG_STDIN the child reads EOF from /dev/null, as every
     * caller that shares its stdin with a command shell (libsu) relies on. */
    if (pipe2(out, O_CLOEXEC) < 0 || pipe2(err, O_CLOEXEC) < 0 ||
        ((r->flags & REQ_FLAG_STDIN) && pipe2(in, O_CLOEXEC) < 0)) {
      ds_send_frame(conn, MSG_ERR, "daemon: pipe2 failed\n", 21);
      send_exit(conn, 1);
      close_pipe(out);
      close_pipe(err);
      close_pipe(in);
      return;
    }
  }

  char **av = make_exec_argv(r);
  if (!av) {
    if (is_pty) {
      close(master);
      close(slave);
    } else {
      close_pipe(out);
      close_pipe(err);
      close_pipe(in);
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
      close_pipe(out);
      close_pipe(err);
      close_pipe(in);
    }
    ds_send_frame(conn, MSG_ERR, "daemon: fork failed\n", 20);
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
      int dn = in[0] >= 0 ? in[0] : open("/dev/null", O_RDWR);
      if (dn >= 0) {
        dup2(dn, STDIN_FILENO);
        if (dn > STDERR_FILENO)
          close(dn);
      }
      if (in[1] >= 0)
        close(in[1]);
      dup2(out[1], STDOUT_FILENO);
      dup2(err[1], STDERR_FILENO);
      close(out[1]);
      close(err[1]);
    }
    /* The command runs as if the client had run it, like lxc-attach does in
     * the caller's own process. A client too old to send its umask gets the
     * daemon's 022. */
    if (r->flags & REQ_FLAG_UMASK)
      umask((r->flags >> REQ_UMASK_SHIFT) & 0777);
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
      close_pipe(out);
      close_pipe(err);
      close_pipe(in);
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    send_exit(conn, 1);
    return;
  }

  /* signalfd for zero-latency SIGCHLD: avoids 100ms poll-waitpid loop */
  sigset_t ss;
  sigemptyset(&ss);
  sigaddset(&ss, SIGCHLD);
  sigprocmask(SIG_BLOCK, &ss, NULL);
  int sfd = signalfd(-1, &ss, SFD_NONBLOCK | SFD_CLOEXEC);

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
    if (in[0] >= 0) {
      close(in[0]);
      in[0] = -1;
      fcntl(in[1], F_SETFL, O_NONBLOCK);
    }
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.fd = out[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, out[0], &ev);
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.fd = err[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, err[0], &ev);
    active_reads = 2;
  }

  /* Where client input goes: the pty master, the command's stdin pipe, or
   * nowhere (-1) when the client did not ask to relay stdin. */
  int in_fd = is_pty ? master : in[1];
  int in_eof = 0; /* pipe mode: close in_fd once the write buffer drains */

  if (sfd >= 0) {
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev);
  }

  /* watch the connection for dead clients and client input */
  ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
  ev.data.fd = conn;
  epoll_ctl(epfd, EPOLL_CTL_ADD, conn, &ev);

  int exit_code = EXIT_PENDING;
  int child_done = 0; /* 0=running, 1=killed, 2=normal-exit-seen */

  /* Non-blocking write buffer for client input on its way to in_fd.
   * Prevents blocking the event loop when the pty or pipe is full (a large
   * paste, a slow reader). Excess bytes are queued here and flushed via
   * EPOLLOUT on in_fd. */
  uint8_t *pty_wbuf = NULL;
  size_t pty_wbuf_len = 0;
  size_t pty_wbuf_cap = 0;
  int conn_suspended = 0; /* 1 = conn EPOLLIN removed (wbuf high-water) */

  for (;;) {
    int nfds = epoll_wait(epfd, events, 8, -1);
    if (nfds < 0 && errno != EINTR)
      break;

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;

      if (sfd >= 0 && fd == sfd) {
        /* drain all pending SIGCHLD notifications */
        struct signalfd_siginfo si;
        while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
          if (!child_done) {
            int st;
            if (waitpid(child, &st, WNOHANG) == child) {
              exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
              child_done = is_pty ? 2 : 1;
            }
          }
        }
      } else if (fd == conn) {
        if (events[i].events & (EPOLLHUP | EPOLLERR)) {
          /* client died unexpectedly, kill the child */
          kill(child, is_pty ? SIGHUP : SIGTERM);
          waitpid(child, NULL, 0);
          goto session_end;
        }
        if (events[i].events & EPOLLIN) {
          uint8_t type;
          uint32_t mlen;
          if (ds_recv_frame_hdr(conn, &type, &mlen) < 0) {
            kill(child, is_pty ? SIGHUP : SIGTERM);
            waitpid(child, NULL, 0);
            goto session_end;
          }
          if (type == MSG_OUT && mlen <= (uint32_t)sizeof(buf)) {
            /* Input with nowhere to go is read off the wire and dropped. */
            int got = ds_read_exact(conn, buf, mlen) == 0 && in_fd >= 0;
            if (got && mlen == 0) {
              /* The client's stdin hit EOF. A pty has no EOF to pass on, so
               * only the stdin pipe is closed, once what is queued for it
               * has drained. */
              if (!is_pty && pty_wbuf_len > 0) {
                in_eof = 1;
              } else if (!is_pty) {
                close(in_fd);
                in_fd = in[1] = -1;
              }
            } else if (got) {
              /* Non-blocking write toward the child; queue overflow into
               * wbuf. */
              size_t written = 0;
              if (pty_wbuf_len == 0) {
                /* Fast path: no pending data, try direct write first. */
                ssize_t w = write(in_fd, buf, mlen);
                if (w > 0)
                  written = (size_t)w;
              }
              size_t rem = mlen - written;
              if (rem > 0) {
                /* Buffer remainder; grow wbuf if needed (cap at PTY_WBUF_MAX).
                 */
                if (pty_wbuf_len + rem <= PTY_WBUF_MAX) {
                  if (pty_wbuf_len + rem > pty_wbuf_cap) {
                    /* Double capacity without overflow: clamp before shifting.
                     */
                    size_t ncap = pty_wbuf_cap ? pty_wbuf_cap : DS_IOBUF;
                    while (ncap < pty_wbuf_len + rem) {
                      if (ncap > PTY_WBUF_MAX / 2) {
                        ncap = PTY_WBUF_MAX;
                        break;
                      }
                      ncap *= 2;
                    }
                    if (ncap > PTY_WBUF_MAX)
                      ncap = PTY_WBUF_MAX;
                    /* Preserve old pointer on realloc failure to avoid leak. */
                    uint8_t *nb = realloc(pty_wbuf, ncap);
                    if (nb) {
                      pty_wbuf = nb;
                      pty_wbuf_cap = ncap;
                    }
                    /* If realloc failed, pty_wbuf unchanged; fall through to
                     * the capacity check below which will drop the frame. */
                  }
                  if (pty_wbuf_len + rem <= pty_wbuf_cap) {
                    memcpy(pty_wbuf + pty_wbuf_len, buf + written, rem);
                    pty_wbuf_len += rem;
                    /* Arm EPOLLOUT on in_fd to flush wbuf. The pipe end is
                     * only registered while a flush is pending, so EEXIST on
                     * a re-arm is harmless. */
                    ev.events = EPOLLOUT | EPOLLHUP | EPOLLERR;
                    if (is_pty)
                      ev.events |= EPOLLIN;
                    ev.data.fd = in_fd;
                    epoll_ctl(epfd, is_pty ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,
                              in_fd, &ev);
                    /* High-watermark: suspend conn input to apply backpressure.
                     * Prevents unbounded growth; conn resumes at low-watermark.
                     */
                    if (!conn_suspended && pty_wbuf_len >= PTY_WBUF_HIGH) {
                      ev.events = EPOLLHUP | EPOLLERR; /* no EPOLLIN */
                      ev.data.fd = conn;
                      epoll_ctl(epfd, EPOLL_CTL_MOD, conn, &ev);
                      conn_suspended = 1;
                    }
                  }
                }
                /* wbuf full and conn suspended: no data lost, client stalls. */
              }
            }
          } else if (type == MSG_WINCH && mlen == 4) {
            uint16_t wd[2];
            if (ds_read_exact(conn, wd, 4) == 0 && is_pty) {
              struct winsize nws = {ntohs(wd[0]), ntohs(wd[1]), 0, 0};
              ioctl(master, TIOCSWINSZ, &nws);
              kill(child, SIGWINCH);
            }
          } else {
            /* drain unknown frames so we don't stall the pipe */
            uint32_t rem = mlen;
            while (rem) {
              uint32_t c =
                  (rem < (uint32_t)sizeof(buf)) ? rem : (uint32_t)sizeof(buf);
              if (ds_read_exact(conn, buf, c) < 0)
                goto session_end;
              rem -= c;
            }
          }
        }
      } else if (fd == in_fd && (!is_pty || (events[i].events & EPOLLOUT))) {
        /* Flush pending wbuf toward the child. */
        while (pty_wbuf_len > 0) {
          ssize_t w = write(in_fd, pty_wbuf, pty_wbuf_len);
          if (w > 0) {
            pty_wbuf_len -= (size_t)w;
            if (pty_wbuf_len > 0)
              memmove(pty_wbuf, pty_wbuf + w, pty_wbuf_len);
          } else if (w < 0 && errno == EAGAIN) {
            break; /* still full; wait for the next EPOLLOUT */
          } else {
            /* Reader gone (EIO on the master, EPIPE on the pipe). Nothing is
             * left to flush to, and keeping the bytes would spin on EPOLLERR.
             */
            pty_wbuf_len = 0;
            break;
          }
        }
        if (pty_wbuf_len == 0) {
          /* All flushed; stop watching for writability. */
          if (is_pty) {
            ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
            ev.data.fd = in_fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, in_fd, &ev);
          } else {
            epoll_ctl(epfd, EPOLL_CTL_DEL, in_fd, NULL);
            if (in_eof) {
              close(in_fd);
              in_fd = in[1] = -1;
            }
          }
        }
        /* Low-watermark: resume conn input once pressure drops. */
        if (conn_suspended && pty_wbuf_len < PTY_WBUF_LOW) {
          ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
          ev.data.fd = conn;
          epoll_ctl(epfd, EPOLL_CTL_MOD, conn, &ev);
          conn_suspended = 0;
        }
        /* Also service any pending output from master in this iteration. */
        if (is_pty && (events[i].events & EPOLLIN)) {
          for (;;) {
            ssize_t n = read(master, buf, sizeof(buf));
            if (n > 0) {
              if (ds_send_frame(conn, MSG_OUT, buf, (uint32_t)n) < 0) {
                kill(child, SIGHUP);
                waitpid(child, NULL, 0);
                goto session_end;
              }
            } else {
              if (errno == EINTR)
                continue;
              break;
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
              if (ds_send_frame(conn, t, buf, (uint32_t)n) < 0) {
                kill(child, is_pty ? SIGHUP : SIGTERM);
                waitpid(child, NULL, 0);
                goto session_end;
              }
            } else if (n == 0) {
              drained = 1;
              break;
            } else {
              if (errno == EINTR)
                continue;
              if (errno == EAGAIN)
                break;
              drained = 1;
              break;
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
  free(pty_wbuf);
  if (sfd >= 0) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, sfd, NULL);
    close(sfd);
  }
  sigprocmask(SIG_UNBLOCK, &ss, NULL);
  close(epfd);
  if (master >= 0)
    close(master);
  if (in[1] >= 0)
    close(in[1]);
  if (out[0] >= 0)
    close(out[0]);
  if (err[0] >= 0)
    close(err[0]);
  send_exit(conn, exit_code == EXIT_PENDING ? 0 : exit_code);
}
