/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_DEV_DEV_H
#define DS_DEV_DEV_H

/* dev.c */
int ds_stage_dev_node(const char *staging, const char *dev_dir, const char *rel,
                      mode_t mode, dev_t dev, gid_t gid);
int setup_dev(const char *rootfs, int hw_access, int gpu_mode, int allow_vts);
int create_devices(const char *rootfs, const char *staging);

/* devpts.c */
int setup_devpts(int hw_access);

#endif /* DS_DEV_DEV_H */
