/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_PROBE_H
#define DS_NET_NL_PROBE_H

/* 1 if the kernel can create a macvlan, cached per boot */
int ds_nl_probe_macvlan(void);

/* Kernel capability probe - call before any NAT setup */
int ds_nl_probe_nat_capability(char *reason, size_t rsz);

#endif /* DS_NET_NL_PROBE_H */
