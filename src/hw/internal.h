/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_HW_INTERNAL_H
#define DS_HW_INTERNAL_H

/*
 * Unified GPU group bridge.
 *
 * Every hardware node mirrored into the container is chowned root:this group,
 * so GPU access works regardless of which host GID the node originally
 * carried (GID 44 for video, GID 1006 for render, etc.). Root opens the nodes
 * as their owner; any other user is added to the group with usermod.
 *
 * GID 786 is well outside the standard Debian/Ubuntu range (0-999 system,
 * 1000+ users) and has never been assigned to a named group in the FHS.
 */
#define DS_GPU_GROUP_NAME "droidspaces-gpu"
#define DS_GPU_UNIFIED_GID 786

#endif /* DS_HW_INTERNAL_H */
