/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_ANDROID_VIRGL_H
#define DS_ANDROID_VIRGL_H

int ds_virgl_daemon_start(struct ds_config *cfg);
void ds_virgl_daemon_stop(struct ds_config *cfg);
int ds_setup_virgl_socket(struct ds_config *cfg);

#endif /* DS_ANDROID_VIRGL_H */
