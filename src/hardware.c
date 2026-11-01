/*
 * Droidspaces v7 - Hardware Access Module
 *
 * This module manages GPU acceleration and hardware device nodes.
 *
 * --gpu mode mirrors a fixed table of GPU nodes into a private tmpfs /dev.
 * That table only carries render nodes (/dev/dri/renderD*), because render
 * nodes let several processes share the GPU while card nodes need DRM master.
 * On Android it keeps only the hardware codecs of /dev/video*, see
 * do_mirror_gpu_dir().
 * --hw-access mode exposes the host devtmpfs as-is, card nodes included, and
 * only uses the same table to fill in nodes the devtmpfs is missing.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <dirent.h>
#include <linux/videodev2.h>

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

  /* On Android 12+ /dev/binder is a symlink into binderfs, whose device
   * numbers are not in the kernel's chrdev table: a mknod copy has the right
   * numbers and still opens with ENXIO.  Bind the host's node instead. */
  struct statfs sfs;
  if (statfs(host_path, &sfs) == 0 && sfs.f_type == BINDERFS_SUPER_MAGIC) {
    char real[PATH_MAX];
    if (!realpath(host_path, real) || bind_mount(real, tgt) < 0)
      ds_warn("[GPU] bind %s over %s: %s", host_path, tgt, strerror(errno));
    else
      ds_log("[GPU] Bound binderfs node: %s", tgt);
    return;
  }

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

static int is_v4l2_codec(int fd) {
  struct v4l2_capability cap;
  /* musl takes the request as int and this one has bit 31 set */
  if (ioctl(fd, (int)VIDIOC_QUERYCAP, &cap) < 0)
    return 0;
  __u32 c = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                      : cap.capabilities;
  if (c & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE))
    return 1;
  /* Exynos MFC reports capture and output on one node but no M2M bit. */
  return (c & V4L2_CAP_STREAMING) &&
         (c & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) &&
         (c & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE));
}

/*
 * do_mirror_gpu_dir()
 *
 * Walk a host directory (e.g. /dev/dri, /dev/dma_heap) and call
 * mirror_gpu_node() for every entry that matches the optional prefix, except
 * the non-codec /dev/video* nodes that --gpu skips on Android (see below).
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

  /* ffmpeg's v4l2m2m codecs, and Jellyfin through them, open every
   * /dev/video* and enumerate its formats. A Galaxy S10 has 60 camera ISP
   * nodes that answer that enumeration forever, so the probe hangs at 100%
   * CPU. Android apps reach the camera through its HAL, never these nodes,
   * so --gpu on Android keeps only codec nodes. Linux hosts keep everything
   * (webcams), as does --hw-access (staging set). A node that will not open
   * is skipped with its reason logged: on the S10 only camera nodes ever
   * refuse (always, or while the camera app runs, which would let them in
   * to hang ffmpeg once it closes); the MFC opened while Gallery decoded and
   * a screen recording encoded. Codec fds stay open until the scan is over:
   * the MFC reloads its firmware on every last close. Other nodes close at
   * once, because a held camera node makes its siblings refuse to open. */
  int probe_codecs =
      !staging && is_android() && prefix && strcmp(prefix, "video") == 0;
  int held[256], nheld = 0;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    if (prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;

    snprintf(full_path, sizeof(full_path), "%s/%s", host_dir, entry->d_name);

    if (probe_codecs) {
      /* QUERYCAP reports Exynos' secure MFC nodes exactly like the normal
       * ones, so only the name tells them apart. They need protected memory
       * no container app can supply, and ffmpeg probing one before the
       * normal decoder fed it plain memory: a secure SysMMU fault, which
       * panics the S10. */
      char sys[PATH_MAX], name[64];
      snprintf(sys, sizeof(sys), "/sys/class/video4linux/%s/name",
               entry->d_name);
      if (read_file(sys, name, sizeof(name)) >= 0 && strstr(name, "secure")) {
        ds_log("[GPU] Skipped %s: secure codec (%s)", full_path, name);
        continue;
      }

      int fd = open(full_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) {
        ds_log("[GPU] Skipped %s: %s", full_path, strerror(errno));
        continue;
      }
      if (!is_v4l2_codec(fd)) {
        close(fd);
        continue;
      }
      if (nheld < (int)(sizeof(held) / sizeof(held[0])))
        held[nheld++] = fd;
      else
        close(fd);
    }

    mirror_gpu_node(full_path, dev_path, staging);
  }

  while (nheld > 0)
    close(held[--nheld]);
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
 * After pivot_root, make sure the container's /etc/group carries the unified
 * GPU group. A rootfs that already has GID 786, under whatever name, is left
 * alone, so the call is idempotent across restarts.
 */
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
