/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ROOTFS_IMAGE_H
#define DS_ROOTFS_IMAGE_H

int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size,
                     const char *name);
int unmount_rootfs_img(const char *mount_point, int silent);
int ds_img_mount_point(const char *name, char *buf, size_t size);

#endif /* DS_ROOTFS_IMAGE_H */
