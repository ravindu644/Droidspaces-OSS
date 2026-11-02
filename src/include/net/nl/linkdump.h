/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_LINKDUMP_H
#define DS_NET_NL_LINKDUMP_H

int ds_nl_count_ifaces_with_prefix(ds_nl_ctx_t *ctx, const char *prefix);

/* Count links enslaved to `bridge` whose name starts with `prefix`.
 * Returns 0 if the bridge does not exist. */
int ds_nl_count_bridge_members_with_prefix(ds_nl_ctx_t *ctx, const char *bridge,
                                           const char *prefix);
int ds_nl_list_ifaces(ds_nl_ctx_t *ctx, char names[][IFNAMSIZ], int max);

#endif /* DS_NET_NL_LINKDUMP_H */
