/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_STATIC_IP_H
#define DS_NET_STATIC_IP_H

/* Validate a user-supplied static NAT IP string.
 * Must be a valid IPv4 address inside DS_DEFAULT_SUBNET (172.28.0.0/16),
 * with octet3 in 1-254 and octet4 in 1-254 (excludes gateway row .0.x).
 * Returns 0 on success, -1 on failure (reason written to errbuf). */
int ds_net_validate_static_ip(const char *ip_str, char *errbuf, size_t errsize);

/* Scan all container.config files in the workspace for a static_nat_ip
 * collision.  exclude_name (the current container) is skipped.
 * Returns 0 if ip_str is unique across all other configs, 1 if collision. */
int ds_net_check_ip_collision(const char *ip_str, const char *exclude_name);

/* Called from start_rootfs() before the first config save and before fork.
 * Resolves cfg->static_nat_ip:
 *   - If user provided --nat-ip: validate + uniqueness check; fall back to
 *     auto-assign on failure.
 *   - If not provided (or fell back): derive a name-based deterministic IP,
 *     then walk forward until a unique slot is found.
 * After this call cfg->static_nat_ip is always a valid, unique IP string.
 * Callers must save config after this returns to persist the result. */
void ds_net_resolve_static_ip(struct ds_config *cfg);

#endif /* DS_NET_STATIC_IP_H */
