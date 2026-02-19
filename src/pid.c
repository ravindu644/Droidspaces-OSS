/*
 * Droidspaces v3 — High-performance Container Runtime
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

int ensure_workspace(void) {
  mkdir(get_workspace_dir(), 0755);
  mkdir(get_pids_dir(), 0755);

  /* Also ensure /data/local/Droidspaces/mounts on Android */
  if (is_android()) {
    char mounts_path[PATH_MAX];
    snprintf(mounts_path, sizeof(mounts_path), "%s/mounts",
             DS_WORKSPACE_ANDROID);
    mkdir(mounts_path, 0755);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Container Naming
 * ---------------------------------------------------------------------------*/

int generate_container_name(const char *rootfs_path, char *name, size_t size) {
  char os_release[PATH_MAX];
  snprintf(os_release, sizeof(os_release), "%s/etc/os-release", rootfs_path);

  char buf[4096];
  if (read_file(os_release, buf, sizeof(buf)) < 0) {
    /* Fallback if os-release is missing */
    safe_strncpy(name, "linux-container", size);
    return 0;
  }

  /* Parse ID and VERSION_ID */
  char id[64] = "linux", version[64] = "";
  char *p = strstr(buf, "\nID=");
  if (!p && strncmp(buf, "ID=", 3) == 0)
    p = buf;
  if (p) {
    if (*p == '\n')
      p++;
    p += 3;
    if (*p == '"')
      p++;
    int i = 0;
    while (p[i] && p[i] != '"' && p[i] != '\n' && i < 63) {
      id[i] = p[i];
      i++;
    }
    id[i] = '\0';
  }

  p = strstr(buf, "VERSION_ID=");
  if (p) {
    p += 11;
    if (*p == '"')
      p++;
    int i = 0;
    while (p[i] && p[i] != '"' && p[i] != '\n' && i < 63) {
      version[i] = p[i];
      i++;
    }
    version[i] = '\0';
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
    if (i > 0)
      snprintf(final_name, size, "%s-%d", base_name, i);

    resolve_pidfile_from_name(final_name, pidfile, sizeof(pidfile));
    if (access(pidfile, F_OK) != 0)
      return 0;

    /* Check if it's a stale pidfile */
    pid_t pid;
    if (read_and_validate_pid(pidfile, &pid) < 0) {
      unlink(pidfile); /* Stale or invalid, consume it */
      return 0;
    }
  }
  return -1;
}

static int is_pid_file(const char *name) {
  if (!name)
    return 0;
  size_t len = strlen(name);
  if (len < 4)
    return 0;
  return (strcmp(name + len - 4, ".pid") == 0);
}

/* ---------------------------------------------------------------------------
 * PID File Resolution
 * ---------------------------------------------------------------------------*/

int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size) {
  if (!name || !pidfile || size == 0)
    return -1;

  const char *dir = get_pids_dir();
  if (strlen(dir) + strlen(name) + 6 >= size) // +6 for "/.pid\0"
    return -1;

  int r = snprintf(pidfile, size, "%.4070s/%.20s.pid", dir, name);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

int count_running_containers(char *first_name, size_t size) {
  DIR *d = opendir(get_pids_dir());
  if (!d)
    return 0;

  struct dirent *ent;
  int count = 0;

  while ((ent = readdir(d)) != NULL) {
    if (is_pid_file(ent->d_name)) {
      char path[PATH_MAX];
      const char *pids_dir = get_pids_dir();
      if (snprintf(path, sizeof(path), "%.4070s/%.24s", pids_dir,
                   ent->d_name) >= (int)sizeof(path))
        continue;

      pid_t pid;
      if (read_and_validate_pid(path, &pid) == 0) {
        if (count == 0 && first_name && size > 0) {
          safe_strncpy(first_name, ent->d_name, size);
          char *dot = strrchr(first_name, '.');
          if (dot)
            *dot = '\0';
        }
        count++;
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
  char marker[64];
  snprintf(marker, sizeof(marker), "/run/%s", uuid);

  pid_t *pids;
  size_t count;
  char path[PATH_MAX];

  for (int retry = 0; retry < DS_PID_SCAN_RETRIES; retry++) {
    if (collect_pids(&pids, &count) < 0)
      continue;

    for (size_t i = 0; i < count; i++) {
      /* First check the UUID marker */
      build_proc_root_path(pids[i], marker, path, sizeof(path));
      if (access(path, F_OK) == 0) {
        /* Then perform stricter validation (cmdline, systemd marker) */
        if (is_valid_container_pid(pids[i])) {
          pid_t found = pids[i];
          free(pids);
          return found;
        }
      }
    }
    free(pids);
    usleep(DS_PID_SCAN_DELAY_US);
  }

  return 0;
}

int sync_pidfile(const char *src_pidfile, const char *name) {
  char dst[PATH_MAX];
  resolve_pidfile_from_name(name, dst, sizeof(dst));

  char buf[64];
  if (read_file(src_pidfile, buf, sizeof(buf)) < 0)
    return -1;
  return write_file(dst, buf);
}

/* ---------------------------------------------------------------------------
 * Status reporting
 * ---------------------------------------------------------------------------*/

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
  } containers[256];

  int count = 0;
  size_t max_name_len = 4; /* "NAME" */

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && count < 256) {
    if (!is_pid_file(ent->d_name))
      continue;

    char pidfile[PATH_MAX];
    if (snprintf(pidfile, sizeof(pidfile), "%.4070s/%.24s", get_pids_dir(),
                 ent->d_name) >= (int)sizeof(pidfile))
      continue;

    pid_t pid;
    if (read_and_validate_pid(pidfile, &pid) == 0) {
      safe_strncpy(containers[count].name, ent->d_name,
                   sizeof(containers[count].name));
      char *dot = strrchr(containers[count].name, '.');
      if (dot)
        *dot = '\0';

      containers[count].pid = pid;
      size_t nlen = strlen(containers[count].name);
      if (nlen > max_name_len)
        max_name_len = nlen;
      count++;
    } else if (pid == 0) {
      /* Stale PID file, nuke it */
      unlink(pidfile);
      remove_mount_path(pidfile);
    }
  }
  closedir(d);

  if (count == 0) {
    printf("\n(No containers running)\n\n");
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

  return 0;
}

int is_container_init(pid_t pid) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;

  char line[256];
  int is_init = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "NSpid:", 6) == 0) {
      /* NSpid: 12345 1  (last value is 1 if it's init in its namespace) */
      char *last_space = strrchr(line, ' ');
      if (!last_space)
        last_space = strrchr(line, '\t');
      if (last_space && atoi(last_space + 1) == 1) {
        is_init = 1;
      }
      break;
    }
  }
  fclose(f);
  return is_init;
}

int scan_containers(void) {
  ds_log("Scanning system for untracked Droidspaces containers...");

  pid_t *pids;
  size_t count;
  if (collect_pids(&pids, &count) < 0)
    return -1;

  /* 1. Tracked Mount Points (to detect orphaned mounts) */
  char tracked_mounts[128][PATH_MAX];
  int tracked_mount_count = 0;

  /* 2. Get list of already tracked PIDs */
  pid_t tracked[128];
  int tracked_count = 0;
  DIR *d = opendir(get_pids_dir());
  if (d) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && tracked_count < 128) {
      if (!is_pid_file(ent->d_name))
        continue;
      char pf[PATH_MAX];
      snprintf(pf, sizeof(pf), "%s/%s", get_pids_dir(), ent->d_name);

      pid_t p;
      if (read_and_validate_pid(pf, &p) == 0) {
        tracked[tracked_count++] = p;
        /* Also capture the mount path if tracked */
        if (tracked_mount_count < 128) {
          if (read_mount_path(pf, tracked_mounts[tracked_mount_count],
                              PATH_MAX) > 0)
            tracked_mount_count++;
        }
      } else if (p == 0) {
        /* Stale PID file, nuke it */
        unlink(pf);
        remove_mount_path(pf);
      }
    }
    closedir(d);
  }

  /* 3. Scan for untracked containers (init processes) */
  int untracked_found = 0;
  for (size_t i = 0; i < count; i++) {
    pid_t pid = pids[i];
    if (pid <= 1)
      continue;

    /* Skip if already tracked */
    int already = 0;
    for (int j = 0; j < tracked_count; j++) {
      if (tracked[j] == pid) {
        already = 1;
        break;
      }
    }
    if (already)
      continue;

    /* Validate it's a Droidspaces container and the init process */
    if (is_valid_container_pid(pid) && is_container_init(pid)) {
      ds_log("Found untracked container PID %d", pid);

      char proc_root[PATH_MAX];
      snprintf(proc_root, sizeof(proc_root), "/proc/%d/root", pid);

      char base_name[128], final_name[128];
      if (generate_container_name(proc_root, base_name, sizeof(base_name)) ==
          0) {
        if (find_available_name(base_name, final_name, sizeof(final_name)) ==
            0) {
          char pf[PATH_MAX];
          resolve_pidfile_from_name(final_name, pf, sizeof(pf));
          char pid_str[32];
          snprintf(pid_str, sizeof(pid_str), "%d", pid);
          if (write_file(pf, pid_str) == 0) {
            ds_log("Tracked untracked container: %s (PID %d)", final_name, pid);
          }
        }
      }
      untracked_found++;
    }
  }
  free(pids);

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
          unmount_rootfs_img(mpath);
          orphaned_found++;
        }
      } else {
        /* If it's just an empty directory in /mnt/Droidspaces, clean it too */
        rmdir(mpath);
      }
    }
    closedir(md);
  }

  if (untracked_found == 0 && orphaned_found == 0)
    ds_log("No untracked resources found.");
  else
    ds_log(
        "Scan complete: found %d container(s), cleaned %d orphaned mount(s).",
        untracked_found, orphaned_found);

  return 0;
}
