/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_GUEST_H
#define DS_NET_GUEST_H

int fix_networking_rootfs(struct ds_config *cfg);
int setup_veth_child_side_named(struct ds_config *cfg, const char *peer_name,
                                const char *ip_str);

#endif /* DS_NET_GUEST_H */
