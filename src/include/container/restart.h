/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_RESTART_H
#define DS_CONTAINER_RESTART_H

/* argc/argv: the process's original arguments, so restart can re-apply CLI
 * overrides after its post-stop config reload. */
int restart_rootfs(struct ds_config *cfg, int argc, char **argv);

#endif /* DS_CONTAINER_RESTART_H */
