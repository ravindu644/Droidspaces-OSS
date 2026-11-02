/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_DNS_H
#define DS_NET_DNS_H

int fix_networking_host(struct ds_config *cfg);
int ds_get_dns_servers(const char *custom_dns, char *out, size_t size);

#endif /* DS_NET_DNS_H */
