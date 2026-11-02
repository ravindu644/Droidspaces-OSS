/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_FRAME_H
#define DS_CORE_FRAME_H

struct sockaddr_un;

int ds_read_exact(int fd, void *buf, size_t n);
int ds_send_frame(int fd, uint8_t type, const void *data, uint32_t len);
int ds_recv_frame_hdr(int fd, uint8_t *type_out, uint32_t *len_out);
socklen_t ds_abstract_addr(struct sockaddr_un *addr, const char *name);

#endif /* DS_CORE_FRAME_H */
