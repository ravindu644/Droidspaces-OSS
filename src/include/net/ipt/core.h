/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_IPT_CORE_H
#define DS_NET_IPT_CORE_H

void parse_cidr(const char *cidr, uint32_t *ip_out, uint32_t *mask_out);

#endif /* DS_NET_IPT_CORE_H */
