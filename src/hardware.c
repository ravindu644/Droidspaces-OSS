/*
 * Droidspaces v6 - Hardware Access Module
 *
 * This module manages GPU acceleration and hardware device nodes.
 *
 * --gpu mode mirrors a fixed table of GPU nodes into a private tmpfs /dev.
 * That table only carries render nodes (/dev/dri/renderD*), because render
 * nodes let several processes share the GPU while card nodes need DRM master.
 * --hw-access mode exposes the host devtmpfs as-is, card nodes included, and
 * only uses the same table to fill in nodes the devtmpfs is missing.
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
 * Unified GPU group bridge.
 *
 * Every hardware node mirrored into the container is chowned to this group.
 * root is added to it in setup_gpu_groups() so GPU access works regardless
 * of which host GID the node originally carried (GID 44 for video, GID 1006
 * for render, etc.). Users can be added manually with usermod.
 *
 * GID 786 is well outside the standard Debian/Ubuntu range (0-999 system,
 * 1000+ users) and has never been assigned to a named group in the FHS.
 */
#define DS_GPU_GROUP_NAME "droidspaces-gpu"
#define DS_GPU_UNIFIED_GID 786

/*
 * Shared GPU/hardware device lists.
 *
 * mirror_gpu_nodes() iterates these tables in both --gpu and --hw-access mode.
 */

/* Dynamic directories: { host_dir, prefix_or_NULL } */
static const struct {
  const char *dir;
  const char *prefix;
} gpu_scan_dirs[] = {
    {"/dev/dri", "renderD"},    {"/dev", "nvidia"}, {"/dev", "video"},
    {"/dev/nvidia-caps", NULL}, {"/dev", "mali"},   {"/dev", "kgsl"},
    {"/dev/dma_heap", NULL},    {NULL, NULL}, /* sentinel */
};

/* Static paths: individual nodes that don't fit a directory scan */
static const char *gpu_static_devices[] = {
    /* Android IPC (Critical for Android containers/hosts) */
    "/dev/binder",
    "/dev/vndbinder",
    "/dev/hwbinder",

    /* Legacy Android Memory Allocators */
    "/dev/ion",
    "/dev/ashmem",

    /* ARM Mali / Adreno aliases */
    "/dev/mali",
    "/dev/genlock",

    /* AMD ROCm Compute */
    "/dev/kfd",

    /* PowerVR */
    "/dev/pvrsrvkm",
    "/dev/pvr_sync",

    /* Tegra */
    "/dev/nvhost-ctrl",
    "/dev/nvhost-gpu",
    "/dev/nvhost-ctrl-gpu",
    "/dev/nvhost-as-gpu",
    "/dev/nvhost-dbg-gpu",
    "/dev/nvhost-prof-gpu",
    "/dev/nvhost-tsg",
    "/dev/nvhost-tsg-gpu",
    "/dev/nvhost-vic",
    "/dev/nvhost-nvdec",
    "/dev/nvhost-nvdec1",
    "/dev/nvhost-nvenc",
    "/dev/nvhost-msenc",
    "/dev/nvmap",

    /* WSL2 */
    "/dev/dxg",

    /* Async Sync */
    "/dev/sw_sync",

    NULL, /* sentinel */
};

/*
 * mirror_gpu_node()
 *
 * Make sure the container /dev carries a character device matching one host
 * GPU node.
 *
 * Background: on Android, /dev is a plain tmpfs populated by ueventd, not the
 * kernel's devtmpfs.  So GPU nodes like /dev/kgsl-3d0, /dev/mali0 and
 * /dev/dri/renderD128 exist in ueventd's tmpfs but are absent (or appear as
 * empty directories) in the devtmpfs we mount for --hw-access, and they are
 * naturally absent from the private tmpfs of --gpu mode.
 *
 * staging != NULL means /dev is the devtmpfs singleton: the node is created in
 * the staging tmpfs and bound over the path, so the singleton is never
 * written.  staging == NULL means /dev is our own tmpfs and mknod is direct.
 */
static void mirror_gpu_node(const char *host_path, const char *dev_path,
                            const char *staging) {
  if (strncmp(host_path, "/dev/", 5) != 0)
    return;

  /* Host node must be a character device.  Ownership does not matter, the
   * mirrored node is re-owned to the unified group below. */
  struct stat host_st;
  if (stat(host_path, &host_st) < 0 || !S_ISCHR(host_st.st_mode))
    return;

  const char *rel = host_path + 5; /* strip leading "/dev/" */
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/%s", dev_path, rel);

  struct stat tgt_st;
  int tgt_exists = lstat(tgt, &tgt_st) == 0;

  if (staging) {
    if (tgt_exists && S_ISDIR(tgt_st.st_mode)) {
      /* Leftover from older releases that wrote into the singleton.  A node
       * cannot be bound over a directory and we no longer delete anything in
       * devtmpfs; a reboot clears it. */
      ds_warn("[GPU] %s is a directory in devtmpfs, skipping", tgt);
      return;
    }
    if (ds_stage_dev_node(staging, dev_path, rel, S_IFCHR | 0660,
                          host_st.st_rdev, DS_GPU_UNIFIED_GID) == 0)
      ds_log("[GPU] Mirrored node: %-30s (%d:%d)", tgt,
             (int)major(host_st.st_rdev), (int)minor(host_st.st_rdev));
    return;
  }

  /* Private tmpfs: create the parent directory and the node in place. */
  char parent[PATH_MAX];
  snprintf(parent, sizeof(parent), "%s", tgt);
  char *slash = strrchr(parent, '/');
  if (slash && slash != parent) {
    *slash = '\0';
    mkdir(parent, 0755);
  }

  if (tgt_exists) {
    if (S_ISCHR(tgt_st.st_mode)) {
      /* Reached twice (static list and prefix scan). Just re-own it. */
      if (chown(tgt, 0, DS_GPU_UNIFIED_GID) < 0)
        ds_warn("[GPU] chown %s: %s", tgt, strerror(errno));
      chmod(tgt, 0660);
      return;
    }
    if (S_ISDIR(tgt_st.st_mode)) {
      if (rmdir(tgt) < 0) {
        ds_warn("[GPU] Cannot remove stale directory %s: %s", tgt,
                strerror(errno));
        return;
      }
    } else {
      unlink(tgt);
    }
  }

  if (mknod(tgt, S_IFCHR | 0660, host_st.st_rdev) < 0) {
    ds_warn("[GPU] mknod %s (%d:%d) failed: %s", tgt,
            (int)major(host_st.st_rdev), (int)minor(host_st.st_rdev),
            strerror(errno));
    return;
  }

  /* Unified GPU group so UID 1000 can open the node regardless of the GID the
   * host assigned to it. */
  if (chown(tgt, 0, DS_GPU_UNIFIED_GID) < 0)
    ds_warn("[GPU] chown %s: %s", tgt, strerror(errno));
  chmod(tgt, 0660);

  ds_log("[GPU] Mirrored node: %-30s (%d:%d)", tgt, (int)major(host_st.st_rdev),
         (int)minor(host_st.st_rdev));
}

/*
 * do_mirror_gpu_dir()
 *
 * Walk a host directory (e.g. /dev/dri, /dev/dma_heap) and call
 * mirror_gpu_node() for every entry that matches the optional prefix.
 *
 * We deliberately do NOT pre-filter by d_type here.  d_type can be
 * DT_UNKNOWN on some Android kernels/filesystems, which would silently
 * drop valid root-owned char devices before mirror_gpu_node() ever sees
 * them.  mirror_gpu_node() does the stat()+S_ISCHR check itself.
 */
static void do_mirror_gpu_dir(const char *host_dir, const char *prefix,
                              const char *dev_path, const char *staging) {
  DIR *dir = opendir(host_dir);
  if (!dir)
    return;

  struct dirent *entry;
  char full_path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    if (prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;

    snprintf(full_path, sizeof(full_path), "%s/%s", host_dir, entry->d_name);
    mirror_gpu_node(full_path, dev_path, staging);
  }

  closedir(dir);
}

/*
 * mirror_gpu_nodes()
 *
 * Public entry point called from setup_dev() once /dev is mounted.  staging
 * is the tmpfs to create nodes in when /dev is the devtmpfs singleton, or
 * NULL when /dev is our own tmpfs.
 *
 * Must be called BEFORE pivot_root while the host /dev is still accessible.
 */
void mirror_gpu_nodes(const char *dev_path, const char *staging) {
  for (int i = 0; gpu_scan_dirs[i].dir != NULL; i++)
    do_mirror_gpu_dir(gpu_scan_dirs[i].dir, gpu_scan_dirs[i].prefix, dev_path,
                      staging);

  for (int i = 0; gpu_static_devices[i] != NULL; i++)
    mirror_gpu_node(gpu_static_devices[i], dev_path, staging);
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

int setup_gpu_groups(void) {
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
  int modified_count = 0;
  int unified_group_found = 0; /* set when DS_GPU_GROUP_NAME is seen */

  while (fgets(line, sizeof(line), fin)) {
    /* Format: name:password:GID:user_list */
    char *users_ptr = NULL;

    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));

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
      int gid_val = atoi(gid_str);

      /* If the unified group already exists, ensure root is a member */
      if (gid_val == DS_GPU_UNIFIED_GID) {
        unified_group_found = 1;

        if (!has_user(users_ptr, "root")) {
          char new_members[2048];
          safe_strncpy(new_members, users_ptr, sizeof(new_members));
          if (strlen(new_members) > 0)
            strncat(new_members, ",root",
                    sizeof(new_members) - strlen(new_members) - 1);
          else
            safe_strncpy(new_members, "root", sizeof(new_members));

          fprintf(fout, "%.*s:%s\n", (int)(users_ptr - line_copy - 1),
                  line_copy, new_members);
          ds_log("[GPU] Updated " DS_GPU_GROUP_NAME " (GID %d) members: %s",
                 DS_GPU_UNIFIED_GID, new_members);
          modified_count++;
          continue;
        }
        /* root already present - fall through to fputs */
      }
    }

    /* Print original line if not modified */
    fputs(line, fout);
  }

  /* Create the unified group if it wasn't already in the file */
  if (!unified_group_found) {
    fprintf(fout, DS_GPU_GROUP_NAME ":x:%d:root\n", DS_GPU_UNIFIED_GID);
    ds_log("[GPU] Created unified group " DS_GPU_GROUP_NAME " (GID %d)",
           DS_GPU_UNIFIED_GID);
    modified_count++;
  }

  fclose(fin);
  fclose(fout);

  /* Atomic replacement */
  if (modified_count > 0) {
    if (rename(tmp_path, group_path) < 0) {
      ds_warn("Failed to update /etc/group: %s", strerror(errno));
      unlink(tmp_path);
      return -1;
    }
    ds_log("[GPU] Finalized GPU group membership (Updated %d entry/entries)",
           modified_count);
  } else {
    unlink(tmp_path);
  }

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
int setup_hardware_access(struct ds_config *cfg) {
  /* 1. Create GPU groups inside the container.
   *    hw_access: full hardware passthrough - always set up GPU groups.
   *    gpu_mode:  isolated tmpfs with GPU nodes mirrored in - also needs the
   *               unified droidspaces-gpu group so the container user can
   *               actually open those nodes. */
  if (cfg->hw_access || cfg->gpu_mode)
    setup_gpu_groups();

  /* 2. Mount X11 socket for GUI applications (always attempt on Linux, check
   * flag on Android) */
  ds_setup_x11_socket(cfg);

  /* 3. Setup VirGL socket (Android only) */
  ds_setup_virgl_socket(cfg);

  /* 4. Setup PulseAudio socket (Android only) */
  ds_setup_pulse_socket(cfg);

  /* 5. Setup anland display socket -> /run/display.sock (Android only) */
  ds_setup_anland_socket(cfg);

  return 0;
}
