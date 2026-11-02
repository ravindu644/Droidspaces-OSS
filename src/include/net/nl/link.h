/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_LINK_H
#define DS_NET_NL_LINK_H

int ds_nl_link_exists(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_get_ifindex(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_create_bridge(ds_nl_ctx_t *ctx, const char *name);
int ds_nl_create_veth(ds_nl_ctx_t *ctx, const char *host, const char *peer);
int ds_nl_create_macvlan(ds_nl_ctx_t *ctx, const char *name, int parent_index,
                         int netns_fd, uint32_t mode, const uint8_t mac[6]);
int ds_nl_create_veth_in(ds_nl_ctx_t *ctx, const char *host, const char *peer,
                         int peer_netns_fd, const uint8_t *peer_mac);
int ds_nl_set_master(ds_nl_ctx_t *ctx, const char *ifname, const char *master);
int ds_nl_link_up(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_link_down(ds_nl_ctx_t *ctx, const char *ifname);

/* 1 = removed by this call, 0 = was already gone, negative = error. */
int ds_nl_del_link(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_rename(ds_nl_ctx_t *ctx, const char *ifname, const char *newname);
int ds_nl_set_mac(ds_nl_ctx_t *ctx, const char *ifname, const uint8_t mac[6]);

#endif /* DS_NET_NL_LINK_H */
