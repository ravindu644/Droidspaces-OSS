/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_IPT_RULES_H
#define DS_NET_IPT_RULES_H

/* family is AF_INET or AF_INET6. Each call tries the raw ip_tables /
 * ip6_tables socket first and falls back to the iptables / ip6tables binary
 * for whatever the kernel rejects. */
int ds_ipt_ensure_masquerade(int family, const char *src_cidr);
int ds_ipt_host_rules_present(int family, const char *iface,
                              const char *src_cidr, int expect_dnat);
int ds_ipt_ensure_forward_accept(int family, const char *iface);
int ds_ipt_ensure_input_accept(int family, const char *iface);
int ds_ipt_ensure_mss_clamp(int family);
int ds_ipt_remove_iface_rules(int family, const char *iface);
int ds_ipt_remove_ds_rules(int family);
int ds_ipt6_available(void);

#endif /* DS_NET_IPT_RULES_H */
