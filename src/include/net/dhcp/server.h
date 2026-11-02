/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_DHCP_SERVER_H
#define DS_NET_DHCP_SERVER_H

/* Start a single-lease DHCP server on veth_host (detached monitor thread).
 * Offers offer_ip_be to any DHCP client that broadcasts on the interface.
 * gw_ip_be becomes the router/server-id option (typically DS_NAT_GW_IP).
 * Isolation is enforced by AF_PACKET bind to veth_host's ifindex; no MAC
 * filter is needed or used (see net/dhcp/server.c for rationale). */
void ds_dhcp_server_start(struct ds_config *cfg, const char *veth_host,
                          uint32_t offer_ip_be, uint32_t gw_ip_be,
                          const struct in6_addr *ra_prefix);

/* Stop the DHCP server and unblock its recv() loop. Call before veth teardown.
 */
void ds_dhcp_server_stop(void);

#endif /* DS_NET_DHCP_SERVER_H */
