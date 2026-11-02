/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

void ds_config_free(struct ds_config *cfg) {
  free_config_binds(cfg);
  free_config_env_vars(cfg);
  free_config_unknown_lines(cfg);
  free(cfg->tx11_extra_flags);
  cfg->tx11_extra_flags = NULL;
  free(cfg->virgl_extra_flags);
  cfg->virgl_extra_flags = NULL;
}

/* An empty config: zeroed, which already means NAT networking, with the
 * monitor pipes closed so a stray close() on them is harmless. */
void ds_config_init(struct ds_config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->net_ready_pipe[0] = cfg->net_ready_pipe[1] = -1;
  cfg->net_done_pipe[0] = cfg->net_done_pipe[1] = -1;
}

/* Wipe cfg back to defaults while preserving container identity. Shared by
 * --reset and by restart's post-stop reload, which must start from the same
 * clean slate a fresh start gets: ds_config_load only overlays the keys
 * present in the file, so loading into a lived-in cfg would leave stale
 * snapshot values behind for every conditionally-written key. */
void ds_config_reset_defaults(struct ds_config *cfg) {
  char save_name[256], save_rootfs[PATH_MAX], save_img[PATH_MAX];
  char save_config[PATH_MAX], save_prog[64], save_uuid[64];
  int save_is_img = cfg->is_img_mount;
  int save_specified = cfg->config_file_specified;
  int save_existed = cfg->config_file_existed;
  struct ds_config_line *save_head = cfg->unknown_head;
  struct ds_config_line *save_tail = cfg->unknown_tail;

  safe_strncpy(save_name, cfg->container_name, sizeof(save_name));
  safe_strncpy(save_rootfs, cfg->rootfs_path, sizeof(save_rootfs));
  safe_strncpy(save_img, cfg->rootfs_img_path, sizeof(save_img));
  safe_strncpy(save_config, cfg->config_file, sizeof(save_config));
  safe_strncpy(save_prog, cfg->prog_name, sizeof(save_prog));
  safe_strncpy(save_uuid, cfg->uuid, sizeof(save_uuid));

  free_config_env_vars(cfg);
  free_config_binds(cfg);
  free(cfg->tx11_extra_flags);
  free(cfg->virgl_extra_flags);
  cfg->tx11_extra_flags = NULL;
  cfg->virgl_extra_flags = NULL;
  ds_config_init(cfg);

  safe_strncpy(cfg->container_name, save_name, sizeof(cfg->container_name));
  safe_strncpy(cfg->rootfs_path, save_rootfs, sizeof(cfg->rootfs_path));
  safe_strncpy(cfg->rootfs_img_path, save_img, sizeof(cfg->rootfs_img_path));
  safe_strncpy(cfg->config_file, save_config, sizeof(cfg->config_file));
  safe_strncpy(cfg->prog_name, save_prog, sizeof(cfg->prog_name));
  safe_strncpy(cfg->uuid, save_uuid, sizeof(cfg->uuid));
  cfg->is_img_mount = save_is_img;
  cfg->config_file_specified = save_specified;
  cfg->config_file_existed = save_existed;
  cfg->unknown_head = save_head;
  cfg->unknown_tail = save_tail;
}

void apply_reset_config(struct ds_config *cfg) {
  ds_config_reset_defaults(cfg);
  ds_config_save_by_name(cfg->container_name, cfg);
}

/* Mirrors ContainerManager.sanitizeContainerName() in the Android app.
 * Replaces spaces with dashes so directory names are consistent. */
void sanitize_container_name(const char *name, char *out, size_t size) {
  /* Guard size == 0: 'size - 1' is size_t, so it would wrap to SIZE_MAX and
   * turn the copy into an unbounded write.  (safe_strncpy/read_file guard this
   * the same way.) */
  if (!out || size == 0)
    return;
  if (!name) {
    out[0] = '\0';
    return;
  }
  size_t i;
  for (i = 0; i < size - 1 && name[i] != '\0'; i++)
    out[i] = (name[i] == ' ') ? '-' : name[i];
  out[i] = '\0';
}

int validate_container_name(const char *name) {
  if (!name || !name[0])
    return 0;

  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 0;

  size_t len = strlen(name);
  if (len >= 256)
    return 0;

  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' '))
      return 0;
  }

  return 1;
}

int reject_container_name(const char *name) {
  if (!validate_container_name(name)) {
    ds_error("Invalid container name '%s'. Use only letters, numbers, "
             "'.', '_', '-' and spaces.",
             name);
    return -1;
  }
  return 0;
}
