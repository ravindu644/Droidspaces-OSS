/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_HW_HW_H
#define DS_HW_HW_H

/* gpu.c */
void mirror_gpu_nodes(const char *dev_path, const char *staging);

/* hardware.c */
int setup_hardware_access(struct ds_config *cfg);

#endif /* DS_HW_HW_H */
