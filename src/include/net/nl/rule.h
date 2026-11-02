/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_RULE_H
#define DS_NET_NL_RULE_H

int ds_nl_rule6(ds_nl_ctx_t *ctx, int add, const struct in6_addr *net,
                uint8_t len, int from_net, int table, int priority);
int ds_nl_add_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority);
int ds_nl_del_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority);

#endif /* DS_NET_NL_RULE_H */
