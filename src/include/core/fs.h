/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_FS_H
#define DS_CORE_FS_H

int is_ramfs(const char *path);
int write_file(const char *path, const char *content);
int read_file(const char *path, char *buf, size_t size);
int write_file_atomic(const char *path, const char *content);
ssize_t write_all(int fd, const void *buf, size_t count);
int generate_uuid(char *buf, size_t size);
int mkdir_p(const char *path, mode_t mode);
int remove_recursive(const char *path);
int grep_file(const char *path, const char *pattern);
int count_folders(const char *path);

#endif /* DS_CORE_FS_H */
