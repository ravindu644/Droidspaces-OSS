/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Workspace / Paths
 * ---------------------------------------------------------------------------*/

const char *get_workspace_dir(void) {
  return is_android() ? DS_WORKSPACE_ANDROID : DS_WORKSPACE_LINUX;
}

const char *get_pids_dir(void) {
  static char pids_path[PATH_MAX];
  snprintf(pids_path, sizeof(pids_path), "%s/%s", get_workspace_dir(),
           DS_PIDS_SUBDIR);
  return pids_path;
}

const char *get_net_dir(void) {
  static char net_path[PATH_MAX];
  snprintf(net_path, sizeof(net_path), "%s/%s", get_workspace_dir(),
           DS_NET_SUBDIR);
  return net_path;
}

const char *get_logs_dir(void) {
  static char logs_path[PATH_MAX];
  snprintf(logs_path, sizeof(logs_path), "%s/%s", get_workspace_dir(),
           DS_LOGS_SUBDIR);
  return logs_path;
}

int ensure_workspace(void) {
  mkdir_p(get_workspace_dir(), 0755);
  mkdir_p(get_pids_dir(), 0755);
  mkdir_p(get_net_dir(), 0755);
  mkdir_p(get_logs_dir(), 0755);

  return 0;
}

/* ---------------------------------------------------------------------------
 * Container Naming
 * ---------------------------------------------------------------------------*/

int generate_container_name(const char *rootfs_path, char *name, size_t size) {
  char id[64], version[64];

  if (parse_os_release(rootfs_path, id, version, sizeof(id)) < 0) {
    /* Fallback if os-release is missing */
    safe_strncpy(name, "linux-container", size);
    return 0;
  }

  if (version[0])
    snprintf(name, size, "%s-%s", id, version);
  else
    safe_strncpy(name, id, size);

  return 0;
}

int find_available_name(const char *base_name, char *final_name, size_t size) {
  char pidfile[PATH_MAX];
  safe_strncpy(final_name, base_name, size);

  for (int i = 0; i < DS_MAX_CONTAINERS; i++) {
    if (i > 0) {
      size_t base_len = strlen(base_name);
      char suffix[16];
      int suffix_len = snprintf(suffix, sizeof(suffix), "-%d", i);

      if (base_len + suffix_len < size) {
        memcpy(final_name, base_name, base_len);
        memcpy(final_name + base_len, suffix, suffix_len);
        final_name[base_len + suffix_len] = '\0';
      } else {
        /* Truncate base_name to fit the suffix */
        size_t max_base = size - suffix_len - 1;
        memcpy(final_name, base_name, max_base);
        memcpy(final_name + max_base, suffix, suffix_len);
        final_name[size - 1] = '\0';
      }
    }

    resolve_pidfile_from_name(final_name, pidfile, sizeof(pidfile));
    if (access(pidfile, F_OK) != 0)
      return 0;

    /* Check if it's a stale pidfile - reuse the name slot without
     * unlinking (the next start will overwrite it). */
    pid_t pid;
    if (read_and_validate_pid(pidfile, &pid) < 0) {
      return 0;
    }
  }
  return -1;
}

static int is_pid_file(const char *name) {
  if (!name)
    return 0;
  size_t len = strlen(name);
  if (len < strlen(DS_EXT_PID))
    return 0;
  return (strcmp(name + len - strlen(DS_EXT_PID), DS_EXT_PID) == 0);
}

/* ---------------------------------------------------------------------------
 * PID File Resolution
 * ---------------------------------------------------------------------------*/

int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size) {
  if (!name || !pidfile || size == 0)
    return -1;
  pidfile[0] = '\0';
  if (!validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  const char *dir = get_pids_dir();
  int r = snprintf(pidfile, size, "%.3800s/%.256s.pid", dir, safe_name);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

int is_container_running(struct ds_config *cfg, pid_t *pid_out) {
  if (cfg->pidfile[0] == '\0') {
    if (cfg->container_name[0] == '\0')
      return 0;
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
  }

  pid_t pid = 0;
  int ret = read_and_validate_pid(cfg->pidfile, &pid);
  if (pid_out)
    *pid_out = pid;

  if (ret == 0 && pid > 0)
    return 1;

  /*
   * Fallback discovery:
   * If the PID file check failed (stale, missing, or not-yet-validated)
   * but we have a UUID, perform a deep discovery scan as the final authority.
   */
  if (cfg->uuid[0] != '\0') {
    pid_t deep_pid = find_container_init_pid(cfg->uuid);
    if (deep_pid > 0) {
      if (pid_out)
        *pid_out = deep_pid;

      /* Self-healing: pidfile was missing or stale but container is
       * confirmed alive via UUID scan. Restore it immediately so
       * subsequent calls hit the fast pidfile path instead of scanning
       * /proc again. Works even if the file was nuked externally. */
      char pid_str[32];
      snprintf(pid_str, sizeof(pid_str), "%d", deep_pid);

      if (cfg->pidfile[0] != '\0') {
        write_file_atomic(cfg->pidfile, pid_str);
      } else if (cfg->container_name[0] != '\0') {
        /* pidfile path itself was empty - resolve and restore both */
        char restored_pidfile[PATH_MAX];
        resolve_pidfile_from_name(cfg->container_name, restored_pidfile,
                                  sizeof(restored_pidfile));
        write_file_atomic(restored_pidfile, pid_str);
        safe_strncpy(cfg->pidfile, restored_pidfile, sizeof(cfg->pidfile));
      }

      return 1;
    }
  }

  return 0;
}

static void get_container_name_from_pidfile(const char *pidfile, char *name,
                                            size_t size) {
  safe_strncpy(name, pidfile, size);
  char *dot = strrchr(name, '.');
  if (dot)
    *dot = '\0';
}

int count_running_containers(char *first_name, size_t size) {
  DIR *d = opendir(get_pids_dir());
  if (!d)
    return 0;

  struct dirent *ent;
  int count = 0;

  while ((ent = readdir(d)) != NULL) {
    if (is_pid_file(ent->d_name)) {
      struct ds_config tmp_cfg = {0};
      char clean_name[256];
      get_container_name_from_pidfile(ent->d_name, clean_name,
                                      sizeof(clean_name));

      safe_strncpy(tmp_cfg.container_name, clean_name,
                   sizeof(tmp_cfg.container_name));
      resolve_pidfile_from_name(clean_name, tmp_cfg.pidfile,
                                sizeof(tmp_cfg.pidfile));

      pid_t pid;
      if (is_container_running(&tmp_cfg, &pid)) {
        if (count == 0 && first_name && size > 0) {
          safe_strncpy(first_name, clean_name, size);
        }
        count++;
      } else if (pid == 0 && access(tmp_cfg.pidfile, F_OK) == 0) {
        /* Explicit pruning during scan */
        unlink(tmp_cfg.pidfile);
        remove_mount_path(tmp_cfg.pidfile);
      }
    }
  }
  closedir(d);
  return count;
}

int auto_resolve_pidfile(struct ds_config *cfg) {
  /* 1. If pidfile is explicitly provided, resolve name from it if needed */
  if (cfg->pidfile[0]) {
    if (cfg->container_name[0] == '\0') {
      char *base = strrchr(cfg->pidfile, '/');
      base = base ? base + 1 : cfg->pidfile;
      safe_strncpy(cfg->container_name, base, sizeof(cfg->container_name));
      char *dot = strrchr(cfg->container_name, '.');
      if (dot)
        *dot = '\0';
    }
    return 0;
  }

  /* 2. If name is provided, resolve pidfile from it */
  if (cfg->container_name[0]) {
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
    return 0;
  }

  /* 3. Otherwise, look for the ONLY running container */
  char found_name[256];
  int count = count_running_containers(found_name, sizeof(found_name));

  if (count == 1) {
    safe_strncpy(cfg->container_name, found_name, sizeof(cfg->container_name));
    resolve_pidfile_from_name(found_name, cfg->pidfile, sizeof(cfg->pidfile));
    return 0;
  }

  if (count > 1) {
    ds_error("Multiple containers running. Please specify --name.");
    return -1;
  }

  ds_error("No containers running.");
  return -1;
}

/* ---------------------------------------------------------------------------
 * PID Discovery (UUID Scan)
 * ---------------------------------------------------------------------------*/

pid_t find_container_init_pid(const char *uuid) {
  if (!uuid || uuid[0] == '\0')
    return 0;

  char marker[PATH_MAX];
  snprintf(marker, sizeof(marker), "/run/droidspaces/%s", uuid);

  pid_t *pids = NULL;
  size_t count = 0;
  char path[PATH_MAX];

  if (collect_pids(&pids, &count) < 0)
    return 0;

  for (size_t i = 0; i < count; i++) {
    /* Fast check: does /run/droidspaces exist?
     * This avoids expensive deep path checks for host processes. */
    if (build_proc_root_path(pids[i], "/run/droidspaces", path, sizeof(path)) <
        0)
      continue;

    if (access(path, F_OK) == 0) {
      /* Now check for the specific UUID marker */
      build_proc_root_path(pids[i], marker, path, sizeof(path));
      if (access(path, F_OK) == 0) {
        if (is_valid_container_pid(pids[i])) {
          pid_t found = pids[i];
          free(pids);
          return found;
        }
      }
    }
  }

  free(pids);
  return 0;
}

pid_t find_container_by_name(const char *name) {
  if (!name || name[0] == '\0')
    return 0;

  pid_t *pids = NULL;
  size_t count = 0;
  char path[PATH_MAX];

  if (collect_pids(&pids, &count) < 0)
    return 0;

  for (size_t i = 0; i < count; i++) {
    /* Fast check: does /run/droidspaces exist? */
    if (build_proc_root_path(pids[i], "/run/droidspaces", path, sizeof(path)) <
        0)
      continue;

    if (access(path, F_OK) != 0)
      continue;

    /* Read the tiny name marker - no config parse needed */
    char name_marker[PATH_MAX];
    char stored_name[256] = {0};
    build_proc_root_path(pids[i], "/run/droidspaces/name", name_marker,
                         sizeof(name_marker));

    if (read_file(name_marker, stored_name, sizeof(stored_name)) >= 0) {
      /* Strip trailing newline if any */
      stored_name[strcspn(stored_name, "\n")] = '\0';

      if (strcmp(stored_name, name) == 0 && is_valid_container_pid(pids[i])) {
        pid_t found = pids[i];
        free(pids);
        return found;
      }
    }
  }

  if (pids)
    free(pids);
  return 0;
}

int sync_pidfile(const char *src_pidfile, const char *name) {
  char dst[PATH_MAX];
  if (resolve_pidfile_from_name(name, dst, sizeof(dst)) < 0)
    return -1;

  char buf[64];
  if (read_file(src_pidfile, buf, sizeof(buf)) < 0)
    return -1;
  return write_file(dst, buf);
}

/* ---------------------------------------------------------------------------
 * Status reporting
 * ---------------------------------------------------------------------------*/

int show_containers(void) {
  DIR *d = opendir(get_pids_dir());
  if (!d) {
    if (errno == ENOENT) {
      printf("\n(No containers running)\n\n");
      return 0;
    }
    ds_error("Failed to open PIDs directory: %s", strerror(errno));
    return -1;
  }

  struct container_info {
    char name[128];
    pid_t pid;
  } *containers = NULL;

  int count = 0;
  int cap = 32;
  containers = malloc(cap * sizeof(struct container_info));
  if (!containers) {
    closedir(d);
    return -1;
  }

  size_t max_name_len = 4; /* "NAME" */

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (is_pid_file(ent->d_name)) {
      struct ds_config tmp_cfg = {0};
      char clean_name[128];
      get_container_name_from_pidfile(ent->d_name, clean_name,
                                      sizeof(clean_name));

      safe_strncpy(tmp_cfg.container_name, clean_name,
                   sizeof(tmp_cfg.container_name));
      resolve_pidfile_from_name(clean_name, tmp_cfg.pidfile,
                                sizeof(tmp_cfg.pidfile));

      pid_t pid;
      if (is_container_running(&tmp_cfg, &pid)) {
        if (count >= cap) {
          if (cap > 8192) {
            free(containers);
            closedir(d);
            return -1;
          }
          cap *= 2;
          struct container_info *tmp =
              realloc(containers, (size_t)cap * sizeof(struct container_info));
          if (!tmp) {
            free(containers);
            closedir(d);
            return -1;
          }
          containers = tmp;
        }

        safe_strncpy(containers[count].name, clean_name,
                     sizeof(containers[count].name));
        containers[count].pid = pid;
        size_t nlen = strlen(containers[count].name);
        if (nlen > max_name_len)
          max_name_len = nlen;
        count++;
      } else if (pid == 0 && access(tmp_cfg.pidfile, F_OK) == 0) {
        /* Explicit pruning during scan */
        unlink(tmp_cfg.pidfile);
        remove_mount_path(tmp_cfg.pidfile);
      }
    }
  }
  closedir(d);

  if (count == 0) {
    printf("\n(No containers running)\n\n");
    free(containers);
    return 0;
  }

  if (max_name_len > 60)
    max_name_len = 60;

/* Helper to print horizontal line */
#define PRINT_LINE(start, mid, end)                                            \
  do {                                                                         \
    printf("%s", start);                                                       \
    for (size_t i = 0; i < max_name_len + 2; i++)                              \
      printf("─");                                                             \
    printf("%s", mid);                                                         \
    for (size_t i = 0; i < 10; i++)                                            \
      printf("─");                                                             \
    printf("%s\n", end);                                                       \
  } while (0)

  printf("\n");
  PRINT_LINE("┌", "┬", "┐");
  printf("│ %-*s │ %-8s │\n", (int)max_name_len, "NAME", "PID");
  PRINT_LINE("├", "┼", "┤");

  for (int i = 0; i < count; i++) {
    printf("│ %-*s │ %-8d │\n", (int)max_name_len, containers[i].name,
           containers[i].pid);
  }

  PRINT_LINE("└", "┴", "┘");
  printf("\n");
#undef PRINT_LINE

  free(containers);
  return 0;
}

int is_container_init(pid_t pid) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  FILE *f = fopen(path, "re");
  if (!f)
    return 0;

  char line[1024];
  int is_init = 0;
  int nspid_found = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "NSpid:", 6) == 0) {
      /* NSpid line format: "NSpid: <pid1> <pid2> ... <pidN>"
       * The last value is the PID in the innermost namespace.
       * We use a robust tokenizer to avoid issues with tabs/spaces.
       * NOTE: NSpid was added in Linux 4.1. On older kernels (e.g. 3.10),
       * this line is absent and we fall back to the ns/pid inode check. */
      nspid_found = 1;
      char *p = line + 6;
      char *last_val = NULL;
      char *saveptr;
      char *token = strtok_r(p, " \t\n\r", &saveptr);
      while (token) {
        last_val = token;
        token = strtok_r(NULL, " \t\n\r", &saveptr);
      }
      if (last_val && strcmp(last_val, "1") == 0) {
        is_init = 1;
      }
      break;
    }
  }
  fclose(f);

  if (nspid_found)
    return is_init;

  /*
   * Fallback for kernels < 4.1 (e.g. 3.10) where NSpid is absent:
   * Compare the inode of /proc/<pid>/ns/pid vs /proc/1/ns/pid.
   * Available since Linux 3.8 (namespaces(7)).
   * If inodes differ, the process lives in a different PID namespace.
   * Combined with the /run/droidspaces marker check in
   * is_valid_container_pid(), this is sufficient to identify a
   * container init process.
   */
  struct stat st_pid, st_host;
  char ns_path[PATH_MAX];

  snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/pid", pid);
  if (stat(ns_path, &st_pid) < 0)
    return 0;

  if (stat("/proc/1/ns/pid", &st_host) < 0)
    return 0;

  /* Different inode == different PID namespace == process is a container init
   */
  return (st_pid.st_ino != st_host.st_ino) ? 1 : 0;
}

/* Restore host-side metadata (config, pid, env, mount) from internal markers.
 * Returns 0 on success, -1 on failure. */
int ds_metadata_sync(pid_t pid) {
  if (pid <= 1 || !is_valid_container_pid(pid))
    return -1;

  char path[PATH_MAX];
  char name[256] = {0};
  char mount[PATH_MAX] = {0};

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

  if (ds_config_load(path, &recovery_cfg) == 0) {
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

  /* 6. Restore MOUNT Sidecar */
  char mpath[PATH_MAX];
  if (read_mount_path(pidfile, mpath, sizeof(mpath)) < 0) {
    build_proc_root_path(pid, "/run/droidspaces/mount", path, sizeof(path));
    if (read_file(path, mount, sizeof(mount)) >= 0) {
      mount[strcspn(mount, "\n")] = '\0';
      save_mount_path(pidfile, mount);
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

  /* 1. Tracked Mount Points (to detect orphaned mounts) */
  typedef char mount_path_t[PATH_MAX];
  mount_path_t *tracked_mounts =
      calloc(DS_MAX_TRACKED_ENTRIES, sizeof(mount_path_t));
  if (!tracked_mounts) {
    free(pids);
    return -1;
  }
  int tracked_mount_count = 0;

  /* 2. Process all running PIDs */
  int recovered_found = 0;
  for (size_t i = 0; i < count; i++) {
    pid_t pid = pids[i];
    if (pid <= 1)
      continue;

    /* If it's a Droidspaces init process, synchronize its metadata.
     * This handles both untracked containers and tracked containers
     * with missing sidecars (.env, .mount, .config). */
    if (is_valid_container_pid(pid) && is_container_init(pid)) {
      if (ds_metadata_sync(pid) == 0) {
        recovered_found++;
      }
    }
  }

  /* 3. Get list of newly tracked mount points to detect orphans */
  tracked_mount_count = 0;
  DIR *d = opendir(get_pids_dir());
  if (d) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL &&
           tracked_mount_count < DS_MAX_TRACKED_ENTRIES) {
      if (!is_pid_file(ent->d_name))
        continue;
      char pf[PATH_MAX];
      snprintf(pf, sizeof(pf), "%s/%s", get_pids_dir(), ent->d_name);

      pid_t p;
      if (read_and_validate_pid(pf, &p) == 0) {
        if (read_mount_path(pf, tracked_mounts[tracked_mount_count], PATH_MAX) >
            0)
          tracked_mount_count++;
      } else if (p == 0) {
        /* Stale PID file, nuke it */
        unlink(pf);
        remove_mount_path(pf);
      }
    }
    closedir(d);
  }

  /* 4. Scan for orphaned loop mounts in /mnt/Droidspaces */
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

      if (is_mountpoint(mpath)) {
        int is_tracked = 0;
        for (int i = 0; i < tracked_mount_count; i++) {
          if (strcmp(mpath, tracked_mounts[i]) == 0) {
            is_tracked = 1;
            break;
          }
        }

        if (!is_tracked) {
          ds_warn("Found orphaned mount: %s, cleaning up...", mpath);
          unmount_rootfs_img(mpath, 0);
          orphaned_found++;
        }
      } else {
        rmdir(mpath);
      }
    }
    closedir(md);
  }

  free(pids);
  free(tracked_mounts);

  if (recovered_found == 0 && orphaned_found == 0)
    ds_log("No untracked resources found.");
  else
    ds_log("Scan complete: synchronized %d container(s), cleaned %d orphaned "
           "mount(s).",
           recovered_found, orphaned_found);

  return 0;
}

/**
 * Scans all installed containers and their running status to determine
 * if the host SELinux permissive mode is still needed.
 *
 * Returns:
 *  -1: No containers with 'selinux-permissive=yes' are installed.
 *   0: At least one permissive container is installed, but none are running.
 *   1: At least one permissive container is currently running.
 */
int check_selinux_permissive_needs(void) {
  char pids_path[PATH_MAX];
  snprintf(pids_path, sizeof(pids_path), "%s", get_pids_dir());
  DIR *pd = opendir(pids_path);
  if (!pd)
    return -1;

  /* Phase 1: Fast check - are any permissive containers currently running? */
  struct dirent *ent;
  while ((ent = readdir(pd)) != NULL) {
    if (!is_pid_file(ent->d_name))
      continue;

    char name[256];
    get_container_name_from_pidfile(ent->d_name, name, sizeof(name));

    struct ds_config tmp_cfg = {0};
    if (ds_config_load_by_name(name, &tmp_cfg) == 0) {
      pid_t pid;
      /* Check if THIS running container needs permissive mode */
      if (tmp_cfg.selinux_permissive && is_container_running(&tmp_cfg, &pid)) {
        free_config_binds(&tmp_cfg);
        free_config_env_vars(&tmp_cfg);
        free_config_unknown_lines(&tmp_cfg);
        closedir(pd);
        return 1; /* Found a running permissive container - stay permissive */
      }
      free_config_binds(&tmp_cfg);
      free_config_env_vars(&tmp_cfg);
      free_config_unknown_lines(&tmp_cfg);
    }
  }
  closedir(pd);

  /* Phase 2: None are running. But is at least one permissive container
   * installed? (Requirement: do nothing if none installed). */
  char containers_path[PATH_MAX];
  snprintf(containers_path, sizeof(containers_path), "%s/Containers",
           get_workspace_dir());
  DIR *cd = opendir(containers_path);
  if (!cd)
    return -1;

  int permissive_installed = 0;
  while ((ent = readdir(cd)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    struct ds_config tmp_cfg = {0};
    if (ds_config_load_by_name(ent->d_name, &tmp_cfg) == 0) {
      if (tmp_cfg.selinux_permissive) {
        permissive_installed = 1;
        free_config_binds(&tmp_cfg);
        free_config_env_vars(&tmp_cfg);
        free_config_unknown_lines(&tmp_cfg);
        break;
      }
      free_config_binds(&tmp_cfg);
      free_config_env_vars(&tmp_cfg);
      free_config_unknown_lines(&tmp_cfg);
    }
  }
  closedir(cd);

  return permissive_installed ? 0 : -1;
}
