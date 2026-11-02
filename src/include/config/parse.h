/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_PARSE_H
#define DS_CONFIG_PARSE_H

void sort_bind_mounts(struct ds_config *cfg);
int validate_bind_destination(const char *dest);

/* --net / net_mode= value to its mode, -1 if unknown. */
int ds_parse_net_mode(const char *s);
const char *ds_net_mode_name(enum ds_net_mode mode);

/* --macvlan-mode / macvlan_mode= value to MACVLAN_MODE_*, 0 if unknown. */
uint32_t ds_parse_macvlan_mode(const char *s);
const char *ds_macvlan_mode_name(uint32_t mode);

/* One SRC:DEST[:ro] or HOST[:CONTAINER][/proto] spec into cfg, cut up in
 * place. 0 stored, 1 skipped as a duplicate or overlap (port forwards only,
 * already warned about), -1 with the reason in err. */
int ds_parse_bind_spec(char *spec, struct ds_config *cfg, char *err,
                       size_t err_size);
int ds_parse_port_forward(char *spec, struct ds_config *cfg, char *err,
                          size_t err_size);
void ds_privileged_names(int mask, char *buf, size_t size, const char *sep);
int ds_config_add_bind(struct ds_config *cfg, const char *src, const char *dest,
                       int ro);
void free_config_binds(struct ds_config *cfg);
int parse_privileged(const char *value, struct ds_config *cfg);

#endif /* DS_CONFIG_PARSE_H */
