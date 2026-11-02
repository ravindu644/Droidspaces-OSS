/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_SECCOMP_SHIMS_H
#define DS_SECURITY_SECCOMP_SHIMS_H

int ds_seccomp_keyring_shim(int privileged_mask);
int ds_kernel_has_mount_api(void);
int ds_mount_api_shim_wanted(const struct ds_config *cfg);
int ds_seccomp_mount_api_shim(const struct ds_config *cfg);

#endif /* DS_SECURITY_SECCOMP_SHIMS_H */
