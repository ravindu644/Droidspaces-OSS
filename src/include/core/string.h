/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_STRING_H
#define DS_CORE_STRING_H

void safe_strncpy(char *dst, const char *src, size_t size);
int ds_parse_iface_csv(const char *val, char ifaces[][IFNAMSIZ], int *count,
                       int max);
void ds_format_uptime(long uptime_sec, char *buf, size_t size);
int ds_split_flags(const char *str, char ***out_argv, int *out_argc);
void ds_free_split_flags(char **argv, int argc);
long long ds_parse_size(const char *str);
void ds_format_size(long long bytes, char *buf, size_t sz);

#endif /* DS_CORE_STRING_H */
