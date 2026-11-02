/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_DHCP_RA_H
#define DS_NET_DHCP_RA_H

/* ra.c: the Router Advertisement the DHCP thread sends when ra_prefix (a /64)
 * is given, so the container also configures IPv6 by itself. */
int ds_ra_is_solicit(const uint8_t *frame, size_t len);
void ds_ra_build(uint8_t *out, const uint8_t mac[6],
                 const struct in6_addr *prefix);

#endif /* DS_NET_DHCP_RA_H */
