/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_SECURITY_CAPS_H
#define DS_SECURITY_CAPS_H

void ds_apply_capability_hardening(int hw_access, int privileged_mask,
                                   int sandboxing);

#endif /* DS_SECURITY_CAPS_H */
