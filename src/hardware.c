/*
 * Droidspaces — Hardware Access Module
 *
 * This module manages GPU acceleration and hardware device nodes. To keep your
 * system stable, we exclusively use "render nodes" (/dev/dri/renderD*) for GPU
 * access.
 *
 * Why? Because render nodes allow multiple processes (like Droidspaces and your
 * host's X11/Wayland) to share the GPU safely. "Card nodes" (/dev/dri/card*)
 * are avoided because they require exclusive control (DRM master), and trying
 * to share them often leads to driver hangs or kernel panics on desktop Linux.
 *
 * This approach gives you full OpenGL, Vulkan, and video acceleration while
 * ensuring your host system stays rock solid. It's the same industry standard
 * used by major container projects like Docker and Podman.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <dirent.h>

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

/*
 * add_gpu_gid()
 *
 * Helper to stat a device and add its group ID if it's unique.
 */
static void add_gpu_gid(const char *path, gid_t *gids, int *count,
                        int max_gids) {
  struct stat st;
  if (stat(path, &st) < 0)
    return;

  gid_t gid = st.st_gid;
  if (gid == 0)
    return;

  for (int i = 0; i < *count; i++) {
    if (gids[i] == gid)
      return;
  }

  if (*count < max_gids) {
    gids[(*count)++] = gid;
    ds_log("GPU device %-30s → GID %d", path, (int)gid);
  }
}

/*
 * scan_gpu_dir()
 *
 * Dynamically scan a directory for device nodes matching a prefix.
 */
static void scan_gpu_dir(const char *dir_path, const char *prefix, gid_t *gids,
                         int *count, int max_gids) {
  DIR *dir = opendir(dir_path);
  if (!dir)
    return;

  struct dirent *entry;
  char full_path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    /* Skip . and .. */
    if (entry->d_name[0] == '.')
      continue;

    /* Restricted to character devices only */
    if (entry->d_type != DT_CHR && entry->d_type != DT_UNKNOWN)
      continue;

    if (prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;

    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
    add_gpu_gid(full_path, gids, count, max_gids);
  }

  closedir(dir);
}

/*
 * scan_host_gpu_gids()
 *
 * Scan known GPU device paths on the HOST and collect unique non-root GIDs.
 * Uses dynamic discovery where possible to avoid hardcoded path fragility.
 * Must be called BEFORE pivot_root while /dev still refers to the host.
 *
 * Returns: number of unique GIDs found (0 = no GPU devices)
 */
int scan_host_gpu_gids(gid_t *gids, int max_gids) {
  int count = 0;

  /* 1. Dynamic Scanning (Render Nodes, NVIDIA, V4L2, etc.) */
  scan_gpu_dir("/dev/dri", "renderD", gids, &count, max_gids);
  scan_gpu_dir("/dev", "nvidia", gids, &count, max_gids);
  scan_gpu_dir("/dev", "video", gids, &count, max_gids);
  scan_gpu_dir("/dev/nvidia-caps", NULL, gids, &count, max_gids);
  scan_gpu_dir("/dev", "mali", gids, &count, max_gids);
  scan_gpu_dir("/dev", "kgsl", gids, &count, max_gids);
  scan_gpu_dir("/dev/dma_heap", NULL, gids, &count, max_gids);

  /* 2. Static Comprehensive List (IPC, Compute, Tegra/PVR specific) */
  const char *static_devices[] = {
      /* Android IPC (Critical for Android containers/hosts) */
      "/dev/binder", "/dev/vndbinder", "/dev/hwbinder",

      /* Legacy Android Memory Allocators */
      "/dev/ion", "/dev/ashmem",

      /* ARM Mali / Adreno aliases */
      "/dev/mali", "/dev/genlock",

      /* AMD ROCm Compute */
      "/dev/kfd",

      /* PowerVR (IMG GPU) */
      "/dev/pvrsrvkm", "/dev/pvr_sync",

      /* Tegra (Comprehensive stack) */
      "/dev/nvhost-ctrl", "/dev/nvhost-gpu", "/dev/nvhost-ctrl-gpu",
      "/dev/nvhost-as-gpu", "/dev/nvhost-dbg-gpu", "/dev/nvhost-prof-gpu",
      "/dev/nvhost-tsg", "/dev/nvhost-tsg-gpu", "/dev/nvhost-vic",
      "/dev/nvhost-nvdec", "/dev/nvhost-nvdec1", "/dev/nvhost-nvenc",
      "/dev/nvhost-msenc", "/dev/nvmap",

      /* WSL2 (DirectX Proxy) */
      "/dev/dxg",

      /* Async Sync */
      "/dev/sw_sync",

      NULL};

  for (int i = 0; static_devices[i] != NULL; i++) {
    add_gpu_gid(static_devices[i], gids, &count, max_gids);
  }

  if (count > 0) {
    ds_log("Discovered %d unique GPU/Hardware group(s)", count);
  }

  return count;
}

/*
 * setup_gpu_groups()
 *
 * After pivot_root, create matching groups inside the container's /etc/group
 * and add root to each. Groups are named "gpu_<gid>" to avoid conflicts
 * with existing groups.
 *
 * Idempotent: safe to call on container restart (skips existing groups).
 */

/* Helper to check if a username exists in a comma-separated list of users */
static int has_user(const char *users, const char *username) {
  if (!users || !username)
    return 0;

  size_t len = strlen(username);
  const char *p = users;

  while ((p = strstr(p, username)) != NULL) {
    /* Check if it's a whole word match */
    int at_start = (p == users);
    int prev_comma = (p > users && *(p - 1) == ',');
    int next_comma = (*(p + len) == ',' || *(p + len) == '\0');

    if ((at_start || prev_comma) && next_comma) {
      return 1;
    }
    p++;
  }
  return 0;
}

int setup_gpu_groups(gid_t *gpu_gids, int gid_count) {
  if (gid_count <= 0)
    return 0;

  /* Check if /etc/group exists — some minimal rootfs may not have it */
  if (access("/etc/group", F_OK) != 0) {
    ds_warn("No /etc/group found, skipping GPU group setup");
    return 0;
  }

  /* We'll rewrite the group file to a temporary location */
  const char *group_path = "/etc/group";
  const char *tmp_path = "/etc/group.tmp";

  FILE *fin = fopen(group_path, "r");
  if (!fin) {
    ds_warn("Cannot read /etc/group: %s", strerror(errno));
    return -1;
  }

  FILE *fout = fopen(tmp_path, "w");
  if (!fout) {
    ds_warn("Cannot create /etc/group.tmp: %s", strerror(errno));
    fclose(fin);
    return -1;
  }

  /* Track which GIDs we found in the file */
  int *found_gids = calloc((size_t)gid_count, sizeof(int));
  if (!found_gids) {
    ds_warn("Memory allocation failed for GPU group tracking");
    fclose(fin);
    fclose(fout);
    return -1;
  }

  char line[2048];
  int modified_count = 0;

  while (fgets(line, sizeof(line), fin)) {
    /* Format: name:password:GID:user_list
     * We need to find the GID (3rd field) */
    char name[256];
    int gid_val;
    char *users_ptr = NULL;

    /* Manual parsing to be safe and extract user list pointer */
    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));

    /* Remove trailing newline for parsing */
    char *nl = strrchr(line_copy, '\n');
    if (nl)
      *nl = '\0';

    int colons = 0;
    char *p = line_copy;
    char *gid_str = NULL;

    while (*p) {
      if (*p == ':') {
        colons++;
        if (colons == 2)
          gid_str = p + 1;
        else if (colons == 3) {
          *p = '\0';
          users_ptr = p + 1;
          break;
        }
      }
      p++;
    }

    if (gid_str && users_ptr) {
      gid_val = atoi(gid_str);

      /* Check if this GID is one of our target GPU GIDs */
      int gpu_idx = -1;
      for (int i = 0; i < gid_count; i++) {
        if (gpu_gids[i] == (gid_t)gid_val) {
          gpu_idx = i;
          break;
        }
      }

      if (gpu_idx != -1) {
        found_gids[gpu_idx] = 1;

        /* Check if 'root' is already in the user list */
        if (!has_user(users_ptr, "root")) {
          /* Extract group name for logging */
          p = line_copy;
          int i = 0;
          while (*p && *p != ':' && i < 255)
            name[i++] = *p++;
          name[i] = '\0';

          /* Add root to the members list */
          if (strlen(users_ptr) > 0)
            fprintf(fout, "%.*s:%s,root\n", (int)(users_ptr - line_copy - 1),
                    line_copy, users_ptr);
          else
            fprintf(fout, "%.*s:root\n", (int)(users_ptr - line_copy - 1),
                    line_copy);

          ds_log("Added root to existing group '%s' (GID %d)", name, gid_val);
          modified_count++;
          continue; /* Skip the default fputs below */
        }
      }
    }

    /* Print original line if not modified */
    fputs(line, fout);
  }

  /* Append absolutely missing GPU groups */
  for (int i = 0; i < gid_count; i++) {
    if (!found_gids[i]) {
      fprintf(fout, "gpu_%d:x:%d:root\n", (int)gpu_gids[i], (int)gpu_gids[i]);
      ds_log("Created new GPU group gpu_%d (GID %d)", (int)gpu_gids[i],
             (int)gpu_gids[i]);
      modified_count++;
    }
  }

  fclose(fin);
  fclose(fout);
  free(found_gids);

  /* Atomic replacement */
  if (modified_count > 0) {
    if (rename(tmp_path, group_path) < 0) {
      ds_warn("Failed to update /etc/group: %s", strerror(errno));
      unlink(tmp_path);
      return -1;
    }
    ds_log("Finalized GPU group membership (Updated %d entry/entries)",
           modified_count);
  } else {
    unlink(tmp_path);
  }

  return 0;
}

/*
 * setup_x11_socket()
 *
 * Bind mount X11 socket directory for GUI application support.
 * Supports both desktop Linux and Termux X11 (Android).
 *
 * Only the .X11-unix subdirectory is mounted — never the entire /tmp.
 * Binding /tmp causes "required key not available" errors on encrypted
 * Android devices due to FBE keyring conflicts.
 *
 * Non-fatal: silently returns 0 if no X11 socket is found.
 *
 */

/*
 * stop_termux_if_running()
 *
 * Checks if Termux is running and aggressively stops it to prevent
 * blocking during tmpfs mount.
 */
void stop_termux_if_running(void) {
  struct stat st;
  if (stat("/data/data/com.termux", &st) != 0) {
    return; /* Termux not installed */
  }

  /* Check if Termux is actually running */
  if (system("pidof com.termux >/dev/null 2>&1") != 0) {
    return; /* Not running, nothing to do */
  }

  ds_log("Stopping Termux to prepare unified /tmp...");

  /* Method 1: Use Android Activity Manager to stop app */
  int ret = system("am force-stop com.termux 2>/dev/null");

  /* Method 2: Fallback to pkill if am fails */
  if (ret != 0) {
    system("pkill -9 com.termux 2>/dev/null");
  }

  /* Give it a moment to die */
  nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 500000000}, NULL);
}

/*
 * setup_unified_tmpfs()
 *
 */
int setup_unified_tmpfs(void) {
  const char *termux_tmp = DS_TERMUX_TMP_DIR;
  struct stat st;
  struct statfs fs;

  /* Check if Termux exists */
  if (stat("/data/data/com.termux", &st) != 0) {
    return 0; /* Non-fatal: Termux not installed */
  }

  /* Ensure tmp directory exists */
  mkdir_p(termux_tmp, 0755);

  /* Already mounted? Just ensure ownership is correct. */
  if (statfs(termux_tmp, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    chown(termux_tmp, st.st_uid, st.st_gid);
    chmod(termux_tmp, 01777);
    return 0;
  }

  /* Detect Termux SELinux context (including categories) */
  char context[256] = {0};
  if (get_selinux_context("/data/data/com.termux", context, sizeof(context)) <
      0) {
    safe_strncpy(context, "u:object_r:app_data_file:s0", sizeof(context));
  }

  /* Mount tmpfs with proper permissions and ownership */
  char mount_opts[256];
  snprintf(mount_opts, sizeof(mount_opts), "size=256M,mode=1777,uid=%d,gid=%d",
           (int)st.st_uid, (int)st.st_gid);

  if (mount("tmpfs", termux_tmp, "tmpfs", MS_NOSUID | MS_NODEV, mount_opts) !=
      0) {
    ds_warn("Failed to create unified /tmp: %s", strerror(errno));
    return -1;
  }

  /* Explicitly apply SELinux context to the mount point */
  if (set_selinux_context(termux_tmp, context) < 0) {
    ds_warn("Failed to apply SELinux context to unified /tmp: %s",
            strerror(errno));
  }

  return 0;
}

/*
 * cleanup_unified_tmpfs()
 *
 */
void cleanup_unified_tmpfs(void) {
  struct statfs fs;

  /* Only unmount if a tmpfs is actually present at the target path */
  if (statfs(DS_TERMUX_TMP_DIR, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    umount2(DS_TERMUX_TMP_DIR, MNT_DETACH);
  }
}

/*
 * setup_x11_and_virgl_sockets()
 *
 */
int setup_x11_and_virgl_sockets(struct ds_config *cfg) {
  (void)cfg;

  if (!is_android()) {
    /* Desktop Linux path */
    const char *x11_source = DS_X11_PATH_DESKTOP;
    if (access(x11_source, F_OK) == 0) {
      mkdir_p("/tmp", 01777);
      mkdir_p(DS_X11_CONTAINER_DIR, 01777);
      if (mount(x11_source, DS_X11_CONTAINER_DIR, NULL, MS_BIND | MS_REC,
                NULL) < 0) {
        ds_warn("Failed to bind mount X11 socket: %s", strerror(errno));
        return -1;
      }
      ds_log("Bridged host's X11 unix socket with container");
    } else {
      ds_warn("X11 support skipped: No host X11 socket detected");
    }
    return 0;
  }

  /* Android path: bridge Termux /tmp into container's /tmp */
  const char *bridge_source =
      DS_TERMUX_TMP_OLDROOT; /* FIX: Use explicit macro */
  const char *container_tmp = "/tmp";

  /* Verify source exists */
  if (access(bridge_source, F_OK) != 0) {
    ds_warn("Termux not installed - X11/VirGL socket bridge unavailable");
    return 0; /* Non-fatal */
  }

  ds_log("Bridging Termux and container for X11/VirGL sockets...");

  /* Ensure container /tmp exists */
  mkdir_p(container_tmp, 01777);

  /* Targeted mount for .X11-unix specifically */
  char source_x11[PATH_MAX], target_x11[PATH_MAX];
  snprintf(source_x11, sizeof(source_x11), "%s/.X11-unix", bridge_source);
  snprintf(target_x11, sizeof(target_x11), "%s/.X11-unix", container_tmp);

  if (access(source_x11, F_OK) == 0) {
    mkdir_p(target_x11, 01777);
    if (mount(source_x11, target_x11, NULL, MS_BIND, NULL) != 0) {
      ds_warn("Failed to bridge X11 socket: %s", strerror(errno));
    }
  }

  /* Targeted mount for .virgl_test specifically */
  char source_virgl[PATH_MAX], target_virgl[PATH_MAX];
  snprintf(source_virgl, sizeof(source_virgl), "%s/.virgl_test", bridge_source);
  snprintf(target_virgl, sizeof(target_virgl), "%s/.virgl_test", container_tmp);

  if (access(source_virgl, F_OK) == 0) {
    mkdir_p(target_virgl, 01777);
    if (mount(source_virgl, target_virgl, NULL, MS_BIND, NULL) != 0) {
      ds_warn("Failed to bridge VirGL socket: %s", strerror(errno));
    }
  }

  /* Ensure permissions are correct */
  chmod(container_tmp, 01777);

  return 0;
}

/*
 * setup_hardware_access()
 *
 * Top-level entry point called from boot.c AFTER pivot_root.
 * Orchestrates GPU group creation and X11 socket mounting.
 *
 * All operations are non-fatal: failures produce warnings but don't
 * prevent the container from booting.
 *
 */
int setup_hardware_access(struct ds_config *cfg, gid_t *gpu_gids,
                          int gid_count) {
  if (!cfg->hw_access && !cfg->termux_x11)
    return 0;

  /* 1. Create GPU groups inside the container */
  if (cfg->hw_access)
    setup_gpu_groups(gpu_gids, gid_count);

  /* 2. Mount X11 socket for GUI applications */
  if (cfg->hw_access || cfg->termux_x11)
    setup_x11_and_virgl_sockets(cfg);

  return 0;
}
