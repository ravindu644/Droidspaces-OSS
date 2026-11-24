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

/* toggles.c: per-feature split of --hw-access */
int ds_hw_toggles_active(const struct ds_config *cfg);
int ds_hw_any_toggle(const struct ds_config *cfg);
int ds_dev_block_validate(const char *list);
void ds_hw_toggles_setup_dev(const char *dev_dir, const struct ds_config *cfg);
void ds_hw_toggles_setup_sys(const struct ds_config *cfg);

/* hardware.c */
int setup_hardware_access(struct ds_config *cfg);

#endif /* DS_HW_HW_H */
