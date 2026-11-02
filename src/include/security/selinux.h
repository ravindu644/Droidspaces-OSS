/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_SELINUX_H
#define DS_SECURITY_SELINUX_H

int set_selinux_context(const char *path, const char *context);
void ds_set_selinux_permissive(int enable);
int ds_get_selinux_status(void);
int get_selinux_context(const char *path, char *buf, size_t size);
void ds_selinux_enter_domain(void);

#endif /* DS_SECURITY_SELINUX_H */
