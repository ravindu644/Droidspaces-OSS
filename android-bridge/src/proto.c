/*
 * android-bridge: frame buffers shared by the daemon and the client
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

uint32_t get_be32(const unsigned char *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         p[3];
}

uint16_t get_be16(const unsigned char *p) {
  return (uint16_t)(p[0] << 8 | p[1]);
}

void put_be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

void put_be16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

size_t buf_room(const struct buf *b) { return sizeof(b->data) - b->len; }

void buf_consume(struct buf *b, size_t n) {
  memmove(b->data, b->data + n, b->len - n);
  b->len -= n;
}

int buf_frame(struct buf *b, uint8_t type, const void *p, uint32_t n) {
  if (buf_room(b) < (size_t)n + FRAME_HDR)
    return -1;
  unsigned char *d = b->data + b->len;
  d[0] = type;
  put_be32(d + 1, n);
  if (n)
    memcpy(d + FRAME_HDR, p, n);
  b->len += (size_t)n + FRAME_HDR;
  return 0;
}

/* >0 bytes read, 0 EOF, -1 error. EAGAIN reads as an error-free 0 bytes, so
 * it comes back as -2 to keep it apart from EOF. */
ssize_t buf_read(struct buf *b, int fd) {
  if (!buf_room(b))
    return -2; /* a zero-length read would look like EOF */
  for (;;) {
    ssize_t n = read(fd, b->data + b->len, buf_room(b));
    if (n > 0)
      b->len += (size_t)n;
    if (n >= 0)
      return n;
    if (errno == EINTR)
      continue;
    return errno == EAGAIN ? -2 : -1;
  }
}

int buf_flush(struct buf *b, int fd) {
  while (b->len) {
    ssize_t n = write(fd, b->data, b->len);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return errno == EAGAIN ? 0 : -1;
    }
    buf_consume(b, (size_t)n);
  }
  return 0;
}

/* For a non-blocking fd when there is nothing better to do than wait. */
int buf_flush_all(struct buf *b, int fd) {
  while (b->len) {
    if (buf_flush(b, fd) < 0)
      return -1;
    struct pollfd p = {.fd = fd, .events = POLLOUT};
    if (b->len && poll(&p, 1, -1) < 0 && errno != EINTR)
      return -1;
  }
  return 0;
}

/* 1 a whole frame is buffered, 0 not yet, -1 the peer is not speaking the
 * protocol. */
int frame_peek(const struct buf *b, uint8_t *type, uint32_t *len) {
  if (b->len < FRAME_HDR)
    return 0;
  *type = b->data[0];
  *len = get_be32(b->data + 1);
  if (*len > FRAME_MAX)
    return -1;
  return b->len >= (size_t)*len + FRAME_HDR;
}

int set_nonblock(int fd) {
  int fl = fcntl(fd, F_GETFL);
  return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int write_all(int fd, const void *p, size_t n) {
  const unsigned char *c = p;
  while (n) {
    ssize_t w = write(fd, c, n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    c += w;
    n -= (size_t)w;
  }
  return 0;
}

void sock_path(char *out, size_t size, const char *dir) {
  snprintf(out, size, "%s/%s", dir, AB_SOCK_NAME);
}
