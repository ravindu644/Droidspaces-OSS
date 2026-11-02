/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ROOTFS_MOUNT_H
#define DS_ROOTFS_MOUNT_H

int domount(const char *src, const char *tgt, const char *fstype,
            unsigned long flags, const char *data);
int bind_mount(const char *src, const char *tgt);
int is_mountpoint(const char *path);

#endif /* DS_ROOTFS_MOUNT_H */
