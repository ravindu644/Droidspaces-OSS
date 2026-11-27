/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <sys/socket.h>
#include <sys/un.h>

/* The wire format the daemon and the monitor command socket share: a type
 * byte, a big-endian u32 length, then the payload. */

int ds_read_exact(int fd, void *buf, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n) {
    ssize_t r = read(fd, p, n);
    if (r <= 0)
      return -1;
    p += r;
    n -= (size_t)r;
  }
  return 0;
}

int ds_send_frame(int fd, uint8_t type, const void *data, uint32_t len) {
  uint8_t hdr[5];
  uint32_t nl = htonl(len);
  hdr[0] = type;
  memcpy(hdr + 1, &nl, 4);
  if (write_all(fd, hdr, 5) < 0)
    return -1;
  if (len && write_all(fd, data, len) < 0)
    return -1;
  return 0;
}

int ds_recv_frame_hdr(int fd, uint8_t *type_out, uint32_t *len_out) {
  uint8_t hdr[5];
  if (ds_read_exact(fd, hdr, 5) < 0)
    return -1;
  *type_out = hdr[0];
  uint32_t nl;
  memcpy(&nl, hdr + 1, 4);
  *len_out = ntohl(nl);
  return 0;
}

/* An abstract unix address for name. The length is exact, with no trailing
 * NUL, so "droidspaces" and "droidspaces/x/command" never collide. */
socklen_t ds_abstract_addr(struct sockaddr_un *addr, const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  size_t nlen = strlen(name);
  if (nlen > sizeof(addr->sun_path) - 1)
    nlen = sizeof(addr->sun_path) - 1;
  memcpy(addr->sun_path + 1, name, nlen);
  return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + nlen);
}
