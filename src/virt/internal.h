/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_VIRT_INTERNAL_H
#define DS_VIRT_INTERNAL_H

int container_cpus(struct ds_config *cfg);
/* Per-resource content generators
 * Each returns a malloc'd buffer + length. Caller must free(). */
char *gen_meminfo(struct ds_config *cfg, size_t *out_len);
char *gen_swaps(struct ds_config *cfg, size_t *out_len);
char *gen_cpuinfo(struct ds_config *cfg, size_t *out_len);
char *gen_stat(struct ds_config *cfg, size_t *out_len);
char *gen_uptime(struct ds_config *cfg, size_t *out_len);
char *gen_loadavg(struct ds_config *cfg, size_t *out_len);
char *gen_cpu_sysfs(struct ds_config *cfg, size_t *out_len);
void ds_virtualize_affinity(struct ds_config *cfg);

#endif /* DS_VIRT_INTERNAL_H */
