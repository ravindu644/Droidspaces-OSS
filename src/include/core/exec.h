/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CORE_EXEC_H
#define DS_CORE_EXEC_H

int run_command(char *const argv[]);
int run_command_quiet(char *const argv[]);
int run_command_log(char *const argv[]);
void ds_oom_protect(void);
void ds_oom_unprotect(void);
int ds_thread_create(pthread_t *tid, void *(*fn)(void *), void *arg);

#endif /* DS_CORE_EXEC_H */
