/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/*
 * setup_gpu_groups()
 *
 * After pivot_root, make sure the container's /etc/group carries the unified
 * GPU group. A rootfs that already has GID 786, under whatever name, is left
 * alone, so the call is idempotent across restarts.
 */
static int setup_gpu_groups(void) {
  /* Check if /etc/group exists - some minimal rootfs may not have it */
  if (access("/etc/group", F_OK) != 0) {
    ds_warn("No /etc/group found, skipping GPU group setup");
    return 0;
  }

  /* We'll rewrite the group file to a temporary location */
  const char *group_path = "/etc/group";
  /* Randomized name + O_EXCL (mkstemp) so a symlink pre-planted at a
   * predictable /etc/group.tmp cannot redirect this root-owned write. */
  char tmp_path[] = "/etc/group.XXXXXX";

  FILE *fin = fopen(group_path, "re");
  if (!fin) {
    ds_warn("Cannot read /etc/group: %s", strerror(errno));
    return -1;
  }

  int tfd = mkstemp(tmp_path);
  if (tfd < 0) {
    ds_warn("Cannot create temp group file: %s", strerror(errno));
    fclose(fin);
    return -1;
  }
  (void)fcntl(tfd, F_SETFD, FD_CLOEXEC);
  if (fchmod(tfd, 0644) < 0) { /* best effort; group files are 0644 */
  }
  FILE *fout = fdopen(tfd, "w");
  if (!fout) {
    ds_warn("Cannot create temp group file: %s", strerror(errno));
    close(tfd);
    unlink(tmp_path);
    fclose(fin);
    return -1;
  }

  char line[2048];
  int unified_group_found = 0;

  while (fgets(line, sizeof(line), fin)) {
    /* name:password:GID:users; only the GID matters here */
    const char *gid = strchr(line, ':');
    gid = gid ? strchr(gid + 1, ':') : NULL;
    if (gid && atoi(gid + 1) == DS_GPU_UNIFIED_GID)
      unified_group_found = 1;
    fputs(line, fout);
  }

  /* No members: the nodes are owned by root, and anyone else is added by hand.
   */
  if (!unified_group_found) {
    fprintf(fout, DS_GPU_GROUP_NAME ":x:%d:\n", DS_GPU_UNIFIED_GID);
    ds_log("[GPU] Created unified group " DS_GPU_GROUP_NAME " (GID %d)",
           DS_GPU_UNIFIED_GID);
  }

  fclose(fin);
  fclose(fout);

  if (unified_group_found) {
    unlink(tmp_path);
  } else if (rename(tmp_path, group_path) < 0) {
    ds_warn("Failed to update /etc/group: %s", strerror(errno));
    unlink(tmp_path);
    return -1;
  }

  return 0;
}

/*
 * setup_hardware_access()
 *
 * Top-level entry point called from boot/init.c AFTER pivot_root.
 * Orchestrates GPU group creation and X11 socket mounting.
 *
 * All operations are non-fatal: failures produce warnings but don't
 * prevent the container from booting.
 *
 */
int setup_hardware_access(struct ds_config *cfg) {
  /* 1. Create GPU groups inside the container.
   *    hw_access: full hardware passthrough - always set up GPU groups.
   *    gpu_mode:  isolated tmpfs with GPU nodes mirrored in - also needs the
   *               unified droidspaces-gpu group so the container user can
   *               actually open those nodes. */
  if (cfg->hw_access || cfg->gpu_mode)
    setup_gpu_groups();

  /* 2. Bridge the host X11 socket when --x11 is set */
  ds_setup_x11_socket(cfg);

  /* 3. Setup VirGL socket (Android only) */
  ds_setup_virgl_socket(cfg);

  /* 4. Setup PulseAudio socket (Android only) */
  ds_setup_pulse_socket(cfg);

  /* 5. Setup anland display socket -> /run/display.sock (Android only) */
  ds_setup_anland_socket(cfg);

  return 0;
}
