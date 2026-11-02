/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_JSON_H
#define DS_CORE_JSON_H

/* Flat JSON members, `,"key":value`. *first suppresses the leading comma. */
void ds_json_str(const char *key, const char *val, int *first);
void ds_json_int(const char *key, long long val, int *first);

#endif /* DS_CORE_JSON_H */
