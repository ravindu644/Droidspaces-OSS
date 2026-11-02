/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NL_CORE_H
#define DS_NET_NL_CORE_H

ds_nl_ctx_t *ds_nl_open(void);
void ds_nl_close(ds_nl_ctx_t *ctx);

#endif /* DS_NET_NL_CORE_H */
