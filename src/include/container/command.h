/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_COMMAND_H
#define DS_CONTAINER_COMMAND_H

/* Requests on a monitor's command socket, answered from the config the
 * container booted with. The host asks these instead of reading anything
 * under the guest's /run, which root inside the container controls. */
/* reply: the init pid (u32 big-endian, 0 if none), then the config as its
 * file holds it, so both come from the same boot cycle */
#define DS_CMD_CONFIG ((uint8_t)'C')

/* Monitor side. serve_start starts the thread that answers on the
 * container's command socket; a forked child never keeps its listener.
 * publish sets what it answers with, the booted config and init pid of this
 * boot cycle; NULL withdraws it and frees the name. */
void ds_cmd_serve_start(const char *name);
void ds_cmd_publish(struct ds_config *cfg);

/* Client side. Both fail (-1, 0) when no monitor of ours answers, for example
 * one started by an older build: callers fall back to the host config. */
int ds_cmd_get_config(const char *name, struct ds_config *cfg, pid_t *pid);
pid_t ds_cmd_get_pid(const char *name);
int ds_cmd_for_each(void (*visit)(struct ds_config *booted, pid_t pid,
                                  void *arg),
                    void *arg);

#endif /* DS_CONTAINER_COMMAND_H */
