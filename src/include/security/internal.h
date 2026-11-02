/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_INTERNAL_H
#define DS_SECURITY_INTERNAL_H

/* AUDIT_ARCH_RISCV64 was added to linux/audit.h in 4.15.  Older kernel
 * headers don't have it; fall back to the canonical value
 * (EM_RISCV | __AUDIT_ARCH_64BIT | __AUDIT_ARCH_LE). */
#ifndef AUDIT_ARCH_RISCV64
#define AUDIT_ARCH_RISCV64 0xC00000F3u
#endif

#endif /* DS_SECURITY_INTERNAL_H */
