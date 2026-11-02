/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <sys/socket.h>

/* FD Passing (SCM_RIGHTS) */

int ds_send_fds(int sock, const void *data, size_t len, const int *fds,
                int count) {
  if (count < 1 || count > DS_MAX_PASSED_FDS)
    return -1;
  struct msghdr msg = {0};
  char buf[CMSG_SPACE(sizeof(int) * DS_MAX_PASSED_FDS)];
  memset(buf, 0, sizeof(buf));

  struct iovec io = {.iov_base = (void *)(uintptr_t)data, .iov_len = len};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)count);

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)count);
  memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * (size_t)count);

  ssize_t n = sendmsg(sock, &msg, MSG_NOSIGNAL);
  return n == (ssize_t)len ? 0 : -1;
}

/* Receives up to max fds with the message. fds the sender attached beyond max
 * are closed, since the kernel has already installed them in this process.
 * Returns the number of data bytes read, -1 on error or when the control
 * message was truncated. */
ssize_t ds_recv_fds(int sock, void *data, size_t len, int *fds, int max,
                    int *count) {
  struct msghdr msg = {0};
  char buf[CMSG_SPACE(sizeof(int) * DS_MAX_PASSED_FDS)];
  memset(buf, 0, sizeof(buf));

  struct iovec io = {.iov_base = data, .iov_len = len};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);

  *count = 0;
  ssize_t n = recvmsg(sock, &msg, MSG_CMSG_CLOEXEC);
  if (n < 0)
    return -1;

  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg;
       cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
      continue;
    int got = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
    const int *src = (const int *)CMSG_DATA(cmsg);
    for (int i = 0; i < got; i++) {
      int fd;
      memcpy(&fd, src + i, sizeof(fd));
      if (*count < max)
        fds[(*count)++] = fd;
      else
        close(fd);
    }
  }
  if (msg.msg_flags & MSG_CTRUNC) {
    for (int i = 0; i < *count; i++)
      close(fds[i]);
    *count = 0;
    return -1;
  }
  return n;
}

int ds_send_fd(int sock, int fd) { return ds_send_fds(sock, "FD", 2, &fd, 1); }

int ds_recv_fd(int sock) {
  char data[2];
  int fd, count;
  if (ds_recv_fds(sock, data, sizeof(data), &fd, 1, &count) < 0 || count != 1)
    return -1;
  return fd;
}
