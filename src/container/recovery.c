/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

#include "container/internal.h"

/* Restore host-side metadata (config, pid, env, mount) from internal markers.
 * Returns 0 on success, -1 on failure. */
int ds_metadata_sync(pid_t pid) {
  if (pid <= 1 || !is_valid_container_pid(pid))
    return -1;

  char path[PATH_MAX];
  char name[256] = {0};

  /* 1. Resolve Identity */
  build_proc_root_path(pid, "/run/droidspaces/name", path, sizeof(path));
  if (read_file(path, name, sizeof(name)) < 0)
    return -1;
  name[strcspn(name, "\n")] = '\0';
  if (reject_container_name(name) < 0)
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  /* 2. Restore Workspace Directory */
  char container_dir[PATH_MAX];
  snprintf(container_dir, sizeof(container_dir), "%s/Containers/%s",
           get_workspace_dir(), safe_name);
  mkdir_p(container_dir, 0755);

  /* 3. Restore Configuration */
  struct ds_config recovery_cfg = {0};
  char pidfile[PATH_MAX];
  resolve_pidfile_from_name(safe_name, pidfile, sizeof(pidfile));

  build_proc_root_path(pid, "/run/droidspaces/container.config", path,
                       sizeof(path));

  if (ds_config_peek_path(path, &recovery_cfg) == 0) {
    snprintf(recovery_cfg.config_file, sizeof(recovery_cfg.config_file),
             "%.3800s/container.config", container_dir);

    if (access(recovery_cfg.config_file, F_OK) != 0) {
      if (ds_config_save(recovery_cfg.config_file, &recovery_cfg) < 0) {
        ds_warn("Recovery: Failed to persist configuration for PID %d", pid);
      } else {
        ds_log("Recovery: Restored missing configuration for container '%s'",
               safe_name);
      }
    }
  }

  /* 4. Restore PID Sidecar */
  if (access(pidfile, F_OK) != 0) {
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    write_file_atomic(pidfile, pid_str);
  }

  /* 5. Restore ENV Sidecar */
  if (recovery_cfg.env_file[0] && access(recovery_cfg.env_file, F_OK) != 0) {
    build_proc_root_path(pid, "/run/droidspaces.env", path, sizeof(path));
    if (access(path, F_OK) == 0) {
      write_plain_env_file(path, recovery_cfg.env_file);
    }
  }

  free_config_binds(&recovery_cfg);
  return 0;
}

int scan_containers(void) {
  ds_log("Scanning system for untracked Droidspaces containers...");

  pid_t *pids;
  size_t count;
  if (collect_pids(&pids, &count) < 0)
    return -1;

  /* 1. Process all running PIDs */
  int recovered_found = 0;
  for (size_t i = 0; i < count; i++) {
    pid_t pid = pids[i];
    if (pid <= 1)
      continue;

    /* If it's a Droidspaces init process, synchronize its metadata.
     * This handles both untracked containers and tracked containers
     * with a missing pidfile, env file or config. */
    if (is_valid_container_pid(pid) && is_container_init(pid)) {
      if (ds_metadata_sync(pid) == 0) {
        recovered_found++;
      }
    }
  }

  /* 2. Prune pidfiles nobody owns any more */
  DIR *d = opendir(get_pids_dir());
  if (d) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      if (!is_pid_file(ent->d_name))
        continue;
      char pf[PATH_MAX];
      snprintf(pf, sizeof(pf), "%s/%s", get_pids_dir(), ent->d_name);

      pid_t p;
      if (read_and_validate_pid(pf, &p) < 0 && p == 0) {
        char name[256];
        get_container_name_from_pidfile(ent->d_name, name, sizeof(name));
        prune_stale_pidfile(name, pf);
      }
    }
    closedir(d);
  }

  /* 3. Scan for orphaned loop mounts in /mnt/Droidspaces */
  int orphaned_found = 0;
  DIR *md = opendir(DS_IMG_MOUNT_ROOT_UNIVERSAL);
  if (md) {
    struct dirent *ent;
    while ((ent = readdir(md)) != NULL) {
      if (ent->d_name[0] == '.')
        continue;

      char mpath[PATH_MAX];
      snprintf(mpath, sizeof(mpath), "%s/%s", DS_IMG_MOUNT_ROOT_UNIVERSAL,
               ent->d_name);

      /* The directory is named after its container. A live pidfile means the
       * container is running: a monitor from a build before the lifecycle lock
       * holds no lock at all, so the lock probe below alone would not protect
       * it. Unmounted is not the same as orphaned either: a start mounts here
       * before it writes the pidfile, and a restart keeps the mount while no
       * instance is running. Both hold the lifecycle lock. */
      char pf[PATH_MAX];
      pid_t p;
      if (resolve_pidfile_from_name(ent->d_name, pf, sizeof(pf)) == 0 &&
          read_and_validate_pid(pf, &p) == 0)
        continue;
      int lock_fd = ds_container_lock_orphan(ent->d_name);
      if (lock_fd < 0)
        continue;

      if (is_mountpoint(mpath)) {
        ds_warn("Found orphaned mount: %s, cleaning up...", mpath);
        unmount_rootfs_img(mpath, 0);
        orphaned_found++;
      } else {
        rmdir(mpath);
      }
      ds_container_unlock(lock_fd);
    }
    closedir(md);
  }

  free(pids);

  if (recovered_found == 0 && orphaned_found == 0)
    ds_log("No untracked resources found.");
  else
    ds_log("Scan complete: synchronized %d container(s), cleaned %d orphaned "
           "mount(s).",
           recovered_found, orphaned_found);

  return 0;
}
