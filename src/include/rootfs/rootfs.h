/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ROOTFS_ROOTFS_H
#define DS_ROOTFS_ROOTFS_H

/* binds.c */
int setup_custom_binds(struct ds_config *cfg, const char *rootfs);

/* image.c */
int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size,
                     const char *name);
int unmount_rootfs_img(const char *mount_point, int silent);
int ds_img_mount_point(const char *name, char *buf, size_t size);

/* mount.c */
int domount(const char *src, const char *tgt, const char *fstype,
            unsigned long flags, const char *data);
int bind_mount(const char *src, const char *tgt);
int is_mountpoint(const char *path);

/* volatile.c */
int setup_volatile_overlay(struct ds_config *cfg);
int cleanup_volatile_overlay(struct ds_config *cfg);
int check_volatile_mode(struct ds_config *cfg);

#endif /* DS_ROOTFS_ROOTFS_H */
