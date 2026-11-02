/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANLAND_ANLAND_H
#define DS_ANLAND_ANLAND_H

int ds_anland_daemon_start(struct ds_config *cfg);
void ds_anland_daemon_stop(struct ds_config *cfg);
int ds_setup_anland_socket(struct ds_config *cfg);
void ds_anland_load_sock(struct ds_config *cfg);

#endif /* DS_ANLAND_ANLAND_H */
