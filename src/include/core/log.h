/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_LOG_H
#define DS_CORE_LOG_H

/* Logging macros & Centralized Engine */

extern int ds_log_silent;
extern char ds_log_container_name[256];
extern int ds_log_container_fd;

void ds_log_internal(const char *prefix, const char *color, int is_err,
                     const char *fmt, ...);
void ds_die_internal(const char *fmt, ...);
void rotate_log(const char *path, size_t max_size);

#define ds_log(fmt, ...) ds_log_internal("+", C_GREEN, 0, fmt, ##__VA_ARGS__)
#define ds_warn(fmt, ...) ds_log_internal("!", C_YELLOW, 1, fmt, ##__VA_ARGS__)
#define ds_error(fmt, ...) ds_log_internal("-", C_RED, 1, fmt, ##__VA_ARGS__)
#define ds_die(fmt, ...) ds_die_internal(fmt, ##__VA_ARGS__)

void write_monitor_debug_log(const char *name, const char *fmt, ...);
void ds_open_container_log(struct ds_config *cfg);
void ds_close_container_log(void);
void container_log_path(const char *name, const char *file, char *buf,
                        size_t size);

#endif /* DS_CORE_LOG_H */
