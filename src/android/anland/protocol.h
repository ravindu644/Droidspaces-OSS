/*
 * Anland 5.x control protocol, the part the broker needs.
 *
 * Vendored from https://github.com/superturtlee/anland, branch legacy,
 * libdisplay_producer/protocol.h, and cut down to the control channel: the
 * broker only relays the hello handshake, the screen info and the fd set. The
 * data, input and audio messages travel on fds the broker never reads.
 *
 * Copyright (C) 2026 the Anland authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DISPLAY_PROTOCOL_H
#define DISPLAY_PROTOCOL_H

#include <stdint.h>

#define CTRL_MSG_CONSUMER_HELLO 1
#define CTRL_MSG_PRODUCER_HELLO 2
#define CTRL_MSG_SCREEN_INFO 7
#define CTRL_MSG_PICKUP_FDS 9
#define CTRL_MSG_FDS_READY 10

struct ctrl_msg {
  uint32_t type;
  uint32_t size; /* payload bytes that follow this header */
} __attribute__((packed));

struct screen_info {
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t refresh;
} __attribute__((packed));

#endif /* DISPLAY_PROTOCOL_H */
