/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_GATEWAY_LIFECYCLE_H
#define DS_NET_GATEWAY_LIFECYCLE_H

/* Gateway LAN lifecycle: bridge-only veth plumbing, no NAT/DHCP/firewall. */
int setup_gateway_veth_side(struct ds_config *cfg, pid_t child_pid);

/* Gateway self-heal: when the gateway container (re)boots, re-wire its LAN
 * cable to every running client that delegates to it, with no client restart.
 * Called from the gateway container's monitor on each boot cycle. */
void ds_net_rewire_gateway_clients(const char *gateway_name, pid_t gateway_pid);
int ds_net_gateway_reconcile(struct ds_config *cfg, pid_t client_pid);

/* Gateway teardown: when a container that ACTS AS A GATEWAY stops, explicitly
 * delete the gateway-side veth(s) it serves and reap any now-idle delegated
 * bridge.  The kernel does not auto-reap these (the host-side veth pins its
 * orphan peer netns), so this prevents the leak.  No-op for a non-gateway. */
void ds_net_gateway_teardown(const char *gateway_name);

#endif /* DS_NET_GATEWAY_LIFECYCLE_H */
