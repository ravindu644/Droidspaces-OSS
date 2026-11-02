/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "container/internal.h"

/* The body of a restart. One lock covers the stop and the start, so nothing
 * else can act on the container in the gap where the old instance is gone and
 * the new one is not up yet. */
static int restart_rootfs_locked(struct ds_config *cfg, int *lock_fd, int argc,
                                 char **argv) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }
  ds_log("Restarting container %s...", cfg->container_name);
  if (stop_rootfs_locked(cfg, 1, DS_STOP_TIMEOUT) < 0) {
    return -1;
  }
  /* The stop above tore down using the booted config (its monitor's, loaded
   * while the container was alive). Reload the workspace copy so host-side
   * container.config edits made while it ran take effect on this restart,
   * then re-apply the CLI overrides (--reset included) so every flag behaves
   * exactly as it would on a fresh start. An explicit --conf never read the
   * booted config in the first place: cfg already holds that file plus the
   * overrides, so skip the reload entirely. start_rootfs finds the preserved
   * mount by name, so losing the booted paths is fine. */
  if (!cfg->config_file_specified) {
    /* Clean slate first: the load only overlays keys present in the file, so
     * without the reset every conditionally-written key would keep its value
     * from the booted config, and port/upstream lists would union across
     * the two loads instead of being replaced. */
    ds_config_reset_defaults(cfg);
    /* The host file the user edits, by path: a monitor still on its way out
     * would answer with the config this restart means to replace. */
    ds_config_load_host(cfg->container_name, cfg);
    if (ds_apply_cli_overrides(argc, argv, cfg, 0) != 0)
      return -1;
  }
  putchar('\n');
  print_ds_banner();
  return start_rootfs_locked(cfg, lock_fd, 1);
}

int restart_rootfs(struct ds_config *cfg, int argc, char **argv) {
  int lock_fd = lifecycle_lock_or_complain(cfg, "restart");
  if (lock_fd < 0)
    return -1;

  int ret = restart_rootfs_locked(cfg, &lock_fd, argc, argv);
  ds_container_unlock(lock_fd);
  return ret;
}
