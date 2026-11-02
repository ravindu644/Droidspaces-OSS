/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_ROUTE_QUERY_H
#define DS_NET_NL_ROUTE_QUERY_H

int ds_nl_get_iface_table(ds_nl_ctx_t *ctx, int family, const char *ifname,
                          int *table_out);
int ds_nl_get_table_default_oif(ds_nl_ctx_t *ctx, int family, int table,
                                char *ifname_out);
int ds_nl_get_android_default(ds_nl_ctx_t *ctx, int family, char *ifname_out,
                              int *table_out);

#endif /* DS_NET_NL_ROUTE_QUERY_H */
