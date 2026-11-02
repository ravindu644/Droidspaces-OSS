/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_LOCK_H
#define DS_CONTAINER_LOCK_H

int ds_container_lock(const char *name, int wait);
void ds_container_unlock(int fd);
int ds_container_lock_orphan(const char *name);
void ds_container_claim_supervision(const char *name);
void ds_container_release_supervision(void);
void ds_container_prune_supervision(const char *name);
int ds_container_lock_helpers(const char *name);
void ds_container_wait_helpers(const char *name, int timeout_ms);

#endif /* DS_CONTAINER_LOCK_H */
