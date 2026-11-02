/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_FDPASS_H
#define DS_CORE_FDPASS_H

#define DS_MAX_PASSED_FDS 8
int ds_send_fds(int sock, const void *data, size_t len, const int *fds,
                int count);
ssize_t ds_recv_fds(int sock, void *data, size_t len, int *fds, int max,
                    int *count);
int ds_send_fd(int sock, int fd);
int ds_recv_fd(int sock);

#endif /* DS_CORE_FDPASS_H */
