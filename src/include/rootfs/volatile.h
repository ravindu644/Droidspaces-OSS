/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ROOTFS_VOLATILE_H
#define DS_ROOTFS_VOLATILE_H

int setup_volatile_overlay(struct ds_config *cfg);
int cleanup_volatile_overlay(struct ds_config *cfg);
int check_volatile_mode(struct ds_config *cfg);

#endif /* DS_ROOTFS_VOLATILE_H */
