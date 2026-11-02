/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_ROUTE_MONITOR_H
#define DS_NET_ROUTE_MONITOR_H

void ds_net_start_route_monitor(const char *container_name);
void ds_net_stop_route_monitor(void);

/* Marks route_localnet as required; route monitor re-asserts it every cycle
 * (same pattern as ip_forward). Called once port-forward rules with localhost
 * DNAT are installed. Sticky for process lifetime - never cleared. */
void ds_net_mark_local_forward_active(void);

#endif /* DS_NET_ROUTE_MONITOR_H */
