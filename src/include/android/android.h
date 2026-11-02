/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_ANDROID_H
#define DS_ANDROID_ANDROID_H

void android_optimizations(int enable);
void android_remount_data_suid(void);
int android_setup_storage(const char *rootfs_path);

/* SELinux + Termux privilege helpers */
const char *ds_extract_mls(const char *ctx);
void ds_selinux_dyntransition(const char *mls);
int ds_drop_privileges(int uid);
int ds_resolve_termux_uid(void);

#endif /* DS_ANDROID_ANDROID_H */
