/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_SECCOMP_H
#define DS_SECURITY_SECCOMP_H

int ds_seccomp_apply_minimal(int privileged_mask, int sandboxing_allowed);

/* KernelSU container-escape hardening: ask KSU to mark the current thread
 * (TIF_KSU_DISABLE_ESCAPE_WITH_ROOT via KSU_IOCTL_DISABLE_ESCAPE_TO_ROOT)
 * so escape_with_root_profile() refuses to escalate it.  Best-effort,
 * silent no-op on non-KSU kernels.  Must run BEFORE ds_seccomp_apply_minimal,
 * whose magic-reboot block would otherwise deny the very reboot() this uses
 * to obtain its fd.  The seccomp block is the load-bearing (tree-wide)
 * barrier; this is per-thread defense-in-depth. */
void ds_ksu_neutralize_root_escape(void);

#endif /* DS_SECURITY_SECCOMP_H */
