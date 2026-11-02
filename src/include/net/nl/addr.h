/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_ADDR_H
#define DS_NET_NL_ADDR_H

int ds_nl_add_addr4(ds_nl_ctx_t *ctx, const char *ifname, uint32_t ip_be,
                    uint8_t prefix);
int ds_nl_add_route4(ds_nl_ctx_t *ctx, uint32_t dst_be, uint8_t dst_len,
                     uint32_t gw_be, int oif_idx);
int ds_nl_add_addr6(ds_nl_ctx_t *ctx, const char *ifname,
                    const struct in6_addr *ip, uint8_t prefix);
int ds_nl_add_route6(ds_nl_ctx_t *ctx, const struct in6_addr *dst,
                     uint8_t dst_len, int oif_idx);

#endif /* DS_NET_NL_ADDR_H */
