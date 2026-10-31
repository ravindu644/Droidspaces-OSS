/*
 * android-bridge: a root-only shell on Android, reachable from a container
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ANDROID_BRIDGE_H
#define ANDROID_BRIDGE_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define AB_VERSION "0.1.0"
#define AB_PROTO 1

/* The host directory is 0700 root, so path lookup already stops every other
 * uid with EACCES before the daemon sees a connection. Containers get it
 * through a read-only bind mount at AB_CONTAINER_DIR. */
#define AB_HOST_DIR "/dev/android-bridge"
#define AB_CONTAINER_DIR "/run/android-bridge"
#define AB_SOCK_NAME "bridge.sock"

/* Frame: 1 byte type, 4 byte big-endian payload length, payload. */
#define F_HELLO 1     /* client: proto, flags, rows, cols, term, command */
#define F_STDIN 2     /* client: bytes for the shell's stdin */
#define F_STDIN_EOF 3 /* client: its stdin hit EOF */
#define F_WINCH 4     /* client: rows, cols */
#define F_STDOUT 16   /* daemon: shell output (all of it in PTY mode) */
#define F_STDERR 17   /* daemon: shell stderr, pipe mode only */
#define F_EXIT 18     /* daemon: 4 byte exit code, last frame sent */

#define HELLO_PTY 0x01

#define FRAME_HDR 5
#define FRAME_MAX 65536 /* largest payload either side accepts */
#define CHUNK 16384     /* largest data payload either side sends */

struct buf {
  size_t len;
  unsigned char data[256 * 1024];
};

size_t buf_room(const struct buf *b);
void buf_consume(struct buf *b, size_t n);
int buf_frame(struct buf *b, uint8_t type, const void *p, uint32_t n);
ssize_t buf_read(struct buf *b, int fd);
int buf_flush(struct buf *b, int fd);
int buf_flush_all(struct buf *b, int fd);
int frame_peek(const struct buf *b, uint8_t *type, uint32_t *len);

uint32_t get_be32(const unsigned char *p);
uint16_t get_be16(const unsigned char *p);
void put_be32(unsigned char *p, uint32_t v);
void put_be16(unsigned char *p, uint16_t v);

int set_nonblock(int fd);
int write_all(int fd, const void *p, size_t n);
void sock_path(char *out, size_t size, const char *dir);

int run_daemon(const char *dir, int foreground);
int run_client(const char *path, int pty_mode, int argc, char **argv);

#endif
