/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NAMING_H
#define DS_NET_NAMING_H

/* Populate a ds_net_handshake from a container init PID + resolved config.
 * peer_name is derived from the PID (stable per-boot interface name).
 * ip_str    is taken from cfg->static_nat_ip (stable across boots). */
void ds_net_derive_handshake(pid_t init_pid, struct ds_config *cfg,
                             struct ds_net_handshake *hs);

#endif /* DS_NET_NAMING_H */
