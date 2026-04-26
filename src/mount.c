/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/loop.h>

/* Forward declarations for loop helpers used in find_available_mountpoint */
static void loop_detach(const char *loop_dev);
static int get_backing_dev(const char *mnt, char *dev_out, size_t dev_size);

/* ---------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------*/

/* Check if a path is a mountpoint */
int is_mountpoint(const char *path) {
  struct stat st1, st2;
  if (stat(path, &st1) < 0)
    return 0;

  char parent[PATH_MAX];
  snprintf(parent, sizeof(parent), "%.4092s/..", path);
  if (stat(parent, &st2) < 0)
    return 0;

  return st1.st_dev != st2.st_dev;
}

/* Helper to force removal of a path, even if it is a directory */
static int force_unlink(const char *path) {
  if (unlink(path) < 0) {
    if (errno == EISDIR) {
      return rmdir(path);
    }
    if (errno == ENOENT) {
      return 0;
    }
    return -1;
  }
  return 0;
}

/* Find available mount point in /mnt/Droidspaces/ using container name.
 * If a mount point already exists for this name but is not associated
 * with an active container (stale), it will be cleaned up. */
static int find_available_mountpoint(const char *name, char *mount_path,
                                     size_t size) {
  const char *base_dir = DS_IMG_MOUNT_ROOT_UNIVERSAL;

  /* Create base directory if it doesn't exist */
  mkdir(base_dir, 0755);

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  snprintf(mount_path, size, "%s/%s", base_dir, safe_name);

  if (access(mount_path, F_OK) == 0) {
    if (is_mountpoint(mount_path)) {
      /* This is a stale mount point from a previous crashed run.
       * (We know it's stale because start_rootfs ensures the container name
       * itself is unique among currently running containers). */
      ds_warn("Found stale mount at %s, cleaning up...", mount_path);
      if (umount2(mount_path, MNT_DETACH) < 0) {
        /* umount2 failed: find and detach the backing loop device explicitly */
        char stale_dev[256] = {0};
        get_backing_dev(mount_path, stale_dev, sizeof(stale_dev));
        umount2(mount_path, MNT_DETACH | MNT_FORCE);
        if (stale_dev[0])
          loop_detach(stale_dev);
      }
    }
    return 0;
  }

  if (mkdir(mount_path, 0755) < 0) {
    ds_error("Failed to create mount directory %s: %s", mount_path,
             strerror(errno));
    return -1;
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Generic mount wrappers
 * ---------------------------------------------------------------------------*/

int domount(const char *src, const char *tgt, const char *fstype,
            unsigned long flags, const char *data) {
  if (mount(src, tgt, fstype, flags, data) < 0) {
    /* Don't log if it's already mounted (EBUSY) */
    if (errno != EBUSY) {
      ds_error("Failed to mount %s on %s (%s): %s", src ? src : "none", tgt,
               fstype ? fstype : "none", strerror(errno));
      return -1;
    }
  }
  return 0;
}

/* Like domount but logs failures at [DEBUG] level - used for best-effort
 * mounts where failure is expected on some environments (e.g. cgroup
 * bind-mounts on ROMs with non-standard controller paths). */
int domount_silent(const char *src, const char *tgt, const char *fstype,
                   unsigned long flags, const char *data) {
  if (mount(src, tgt, fstype, flags, data) < 0) {
    if (errno != EBUSY) {
      ds_log("[DEBUG] mount %s -> %s failed: %s", src ? src : "none",
             strerror(errno));
      return -1;
    }
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Shared empty-dir masker
 *
 * Instead of mounting a fresh tmpfs per masked directory (N tmpfs entries in
 * /proc/mounts), we mount ONE minimal tmpfs once and bind-mount it over every
 * subsequent directory target.  Kernel memory is charged once; the mount table
 * shows 1 tmpfs + N cheap bind entries instead of N independent tmpfs mounts.
 *
 * Falls back to per-dir tmpfs automatically if the shared mount fails (e.g.
 * on kernels that reject O_PATH-based bind sources).
 * ---------------------------------------------------------------------------*/
typedef struct {
  int fd;             /* O_PATH fd to the empty tmpfs root */
  char proc_path[32]; /* "/proc/self/fd/<n>" - used as mount(2) source */
  int ready;          /* 1 once initialised successfully */
} ds_mask_dir_t;

static ds_mask_dir_t g_mask_dir = {.fd = -1, .ready = 0};

/* Call once before the first ds_mask_path() invocation. */
static void ds_mask_dir_init(void) {
  if (g_mask_dir.ready)
    return;

  /* /run is guaranteed to exist at this point (created in boot.c step 5).
   * Mode 0000: the directory itself must be inaccessible. */
  const char *backing = "/run/.ds-maskdir";
  mkdir(backing, 0000);

  /* nr_inodes=2  → only . and ..  (absolute minimum)
   * size=0       → no data pages at all
   * mode=000     → directory inaccessible even if somehow reached */
  if (mount("none", backing, "tmpfs",
            MS_RDONLY | MS_NOSUID | MS_NOEXEC | MS_NODEV,
            "size=0,nr_inodes=2,mode=000") < 0) {
    ds_warn("[SEC] mask-dir tmpfs failed (%s), using per-dir tmpfs fallback",
            strerror(errno));
    rmdir(backing);
    return;
  }

  int fd = open(backing, O_PATH | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    ds_warn("[SEC] mask-dir open failed: %s", strerror(errno));
    umount2(backing, MNT_DETACH);
    rmdir(backing);
    return;
  }

  g_mask_dir.fd = fd;
  snprintf(g_mask_dir.proc_path, sizeof(g_mask_dir.proc_path),
           "/proc/self/fd/%d", fd);
  g_mask_dir.ready = 1;
  ds_log(
      "[SEC] Shared mask-dir ready (fd %d) - directory masks use bind mounts.",
      fd);
}

/* Helper to mask a path: bind-mounts /dev/null over files, and uses the
 * shared empty dir (or a fresh tmpfs fallback) for directories.
 * Silently skips if the path doesn't exist. */
static void ds_mask_path(const char *path) {
  struct stat st;
  if (stat(path, &st) < 0)
    return;

  if (S_ISDIR(st.st_mode)) {
    /* Prefer: bind from shared empty tmpfs (1 superblock, N bind entries) */
    if (g_mask_dir.ready) {
      if (mount(g_mask_dir.proc_path, path, NULL, MS_BIND | MS_RDONLY, NULL) ==
          0)
        return;
      ds_warn("[SEC] shared bind failed for %s (%s), falling back to tmpfs",
              path, strerror(errno));
    }
    /* Fallback: independent per-dir tmpfs */
    mount("none", path, "tmpfs", MS_RDONLY, NULL);
  } else {
    /* File: bind /dev/null */
    mount("/dev/null", path, NULL, MS_BIND, NULL);
  }
}

int bind_mount(const char *src, const char *tgt) {
  /* Ensure target exists */
  struct stat st_src, st_tgt;
  if (stat(src, &st_src) < 0)
    return -1;

  if (stat(tgt, &st_tgt) < 0) {
    if (S_ISDIR(st_src.st_mode)) {
      /* CRITICAL: Match source permissions - never hardcode 0755.
       * the kernel overlays the source transparently. */
      mkdir(tgt, st_src.st_mode & 07777);
      if (chown(tgt, st_src.st_uid, st_src.st_gid) < 0) {
        ds_warn("Failed to chown bind mount point %s: %s", tgt,
                strerror(errno));
      }
    } else {
      write_file(tgt, ""); /* Create empty file as mount point */
    }
  }

  return domount(src, tgt, NULL, MS_BIND | MS_REC, NULL);
}

/*
 * ds_apply_jail_mask()
 *
 * Secure sensitive kernel interfaces by masking (bind-mounting /dev/null)
 * or remounting as read-only.
 *
 * This reduces the container's attack surface and prevents it from manipulating
 * the host kernel via /proc and /sys.
 *
 * In Standard Mode (hw_access=0), we are very strict.
 * In Hardware Mode (hw_access=1), we preserve most paths to fulfill the
 * "everything possible" requirement for low-level hardware tools.
 */
int ds_apply_jail_mask(int hw_access, int privileged_mask) {
  if (privileged_mask & DS_PRIV_NOMASK) {
    ds_log(
        "[SEC] --privileged=nomask: skipping jail masks for /proc and /sys.");
    return 0;
  }
  /* Initialize the shared mask-dir before anything uses it */
  ds_mask_dir_init();

  /* Universal masks - dangerous for ANY container regardless of HW mode */
  const char *universal_masks[] = {"/proc/sysrq-trigger",
                                   "/proc/kcore",
                                   "/proc/latency_stats",
                                   "/proc/timer_list",
                                   "/proc/timer_stats",
                                   "/proc/sched_debug",
                                   NULL};

  /* Standard mode masks - restricted to protect host kernel surface */
  const char *standard_masks[] = {"/proc/keys",
                                  "/proc/partitions",
                                  "/proc/diskstats",
                                  "/sys/devices/virtual/powercap",
                                  "/sys/kernel/debug",
                                  "/sys/kernel/tracing",
                                  "/sys/block",
                                  "/sys/dev/block",
                                  "/sys/class/block",
                                  "/proc/mtk_mali",
                                  "/proc/gpufreqv2",
                                  "/proc/mmdvfs",
                                  "/proc/mmqos",
                                  "/proc/displowpower",
                                  "/proc/mtkfb_debug",
                                  "/proc/mtk_mdp_debug",
                                  "/proc/touch_boost",
                                  "/proc/perfmgr",
                                  "/proc/perfmgr_touch_boost",
                                  "/proc/mtk_scheduler",
                                  NULL};

  /* Standard mode read-only remounts.
   * /sys/kernel/security is intentionally here (not in masks): distro init
   * scripts (runit, openrc, systemd) try to mount securityfs onto it and
   * emit confusing errors if the path has been replaced with a tmpfs.
   * A RO remount preserves the directory while blocking writes. */
  const char *standard_ro[] = {
      "/proc/bus",  "/proc/fs",   "/proc/irq",     "/proc/asound",
      "/proc/acpi", "/proc/scsi", "/sys/firmware", "/sys/kernel/security",
      NULL};

  /* Apply universal masks */
  for (int i = 0; universal_masks[i]; i++) {
    ds_mask_path(universal_masks[i]);
  }

  /* Universal: mask all cgroup v1 release_agent files.
   *
   * In --force-cgroupv1 mode, host cgroup v1 hierarchies are bind-mounted
   * into the container. release_agent files are writable by root and the
   * kernel executes them AS REAL HOST ROOT, outside all namespaces, when the
   * last process leaves a cgroup (notify_on_release=1). This is the
   * CVE-2022-0492 class of escape - confirmed exploitable in testing.
   *
   * We mask them universally (not hw_access-gated). Bind-mounting /dev/null
   * over each release_agent file makes them unwritable while leaving the
   * rest of the cgroup hierarchy fully functional. */
  {
    DIR *cgdir = opendir("/sys/fs/cgroup");
    if (cgdir) {
      struct dirent *de;
      while ((de = readdir(cgdir)) != NULL) {
        if (de->d_name[0] == '.')
          continue;
        char agent_path[PATH_MAX];
        snprintf(agent_path, sizeof(agent_path),
                 "/sys/fs/cgroup/%s/release_agent", de->d_name);
        if (access(agent_path, F_OK) == 0) {
          if (mount("/dev/null", agent_path, NULL, MS_BIND, NULL) == 0) {
            ds_log("[SEC] Masked release_agent: %s", agent_path);
          } else {
            ds_warn("[SEC] Failed to mask release_agent %s: %s", agent_path,
                    strerror(errno));
          }
        }
      }
      closedir(cgdir);
    }
  }

  /*
   * Wholesale /proc/sys lockdown - applied in BOTH standard and hardware mode.
   *
   * /proc/sys reflects the host kernel's sysctl state and is NOT scoped to the
   * PID namespace. Even with a fresh procfs, a container running as root can
   * write to /proc/sys/kernel/unprivileged_bpf_disabled, /proc/sys/fs/, etc.
   * and corrupt Android host state (eBPF subsystem, dmesg, perf, hardlinks).
   *
   * Strategy: punch RW bind-mounts for the two genuinely namespace-scoped
   * subtrees FIRST, then lock all of /proc/sys read-only in one shot.
   * The pre-existing submounts shadow the parent remount.
   *
   * RW holes (namespace-scoped - safe for containers to write):
   *   /proc/sys/net              - entirely net-namespace scoped
   *   /proc/sys/kernel/hostname  - UTS-namespace scoped
   *   /proc/sys/kernel/domainname - UTS-namespace scoped
   *
   * Everything else is blocked: kernel/, vm/, fs/, dev/, abi/, debug/.
   * This covers all the dangerous sysctls in one mount entry instead of
   * playing whack-a-mole with individual paths.
   */
  {
    /* Step 1: Pin the namespace-scoped subtrees as independent mounts BEFORE
     * the parent is locked.  A self-bind creates a new mount entry whose RW
     * status is not inherited from the parent remount below. */
    const char *rw_holes[] = {"/proc/sys/net", "/proc/sys/kernel/hostname",
                              "/proc/sys/kernel/domainname", NULL};
    for (int i = 0; rw_holes[i]; i++) {
      if (access(rw_holes[i], F_OK) != 0)
        continue;
      if (mount(rw_holes[i], rw_holes[i], NULL, MS_BIND | MS_REC, NULL) < 0) {
        ds_warn("[SEC] Failed to pin RW hole %s: %s", rw_holes[i],
                strerror(errno));
      }
    }

    /* Step 2: Lock all of /proc/sys in one remount. */
    if (access("/proc/sys", F_OK) == 0) {
      if (mount("/proc/sys", "/proc/sys", NULL,
                MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) < 0) {
        /* Kernel < 3.5 fallback: bind first, then remount RO */
        mount("/proc/sys", "/proc/sys", NULL, MS_BIND | MS_REC, NULL);
        mount("/proc/sys", "/proc/sys", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY,
              NULL);
      }
      ds_log("[SEC] /proc/sys locked RO (net/hostname/domainname holes "
             "preserved).");
    }
  }

  if (hw_access) {
    ds_log("[SEC] Hardware Mode: preserved sensitive /proc and /sys paths.");
    return 0;
  }

  /* Apply standard mode masks */
  for (int i = 0; standard_masks[i]; i++) {
    ds_mask_path(standard_masks[i]);
  }

  /* Apply standard mode read-only remounts */
  for (int i = 0; standard_ro[i]; i++) {
    if (access(standard_ro[i], F_OK) == 0) {
      if (mount(standard_ro[i], standard_ro[i], NULL,
                MS_BIND | MS_REMOUNT | MS_RDONLY, NULL) < 0) {
        mount(standard_ro[i], standard_ro[i], NULL, MS_BIND | MS_REC, NULL);
        mount(standard_ro[i], standard_ro[i], NULL,
              MS_BIND | MS_REMOUNT | MS_RDONLY, NULL);
      }
    }
  }

  ds_log("[SEC] Jail mask applied (hardened /proc and /sys).");
  return 0;
}

/*
 * prune_host_devices()
 *
 * Scans the mounted /dev (devtmpfs) and unlinks dangerous nodes to isolate
 * the container from the host's display server, consoles, and GPU masters.
 */
static void prune_host_devices(const char *dev_path, int privileged_mask) {
  if (privileged_mask & DS_PRIV_UNFILTERED) {
    ds_log("[SEC] --privileged=unfiltered-dev: skipping hardware blocklist.");
    return;
  }
  DIR *dir = opendir(dev_path);
  if (!dir)
    return;

  struct dirent *entry;
  char path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    int should_unlink = 0;

    if (is_dangerous_node(name)) {
      should_unlink = 1;
    }

    if (should_unlink) {
      snprintf(path, sizeof(path), "%.3800s/%s", dev_path, name);
      /* Use force_unlink to handle potential bind-mount stale artifacts */
      umount2(path, MNT_DETACH);
      force_unlink(path);
      continue;
    }

    /* Subdirectory scanning for Tiers 1 and 2 (caps) */
    if (strcmp(name, "dri") == 0 || strcmp(name, "nvidia-caps") == 0) {
      snprintf(path, sizeof(path), "%.3800s/%s", dev_path, name);
      DIR *subdir = opendir(path);
      if (subdir) {
        struct dirent *subentry;
        while ((subentry = readdir(subdir)) != NULL) {
          int sub_unlink = 0;
          const char *subname = subentry->d_name;

          if (is_dangerous_node(subname)) {
            sub_unlink = 1;
          }

          if (sub_unlink) {
            char subpath[PATH_MAX];
            snprintf(subpath, sizeof(subpath), "%.3800s/%s", path, subname);
            unlink(subpath);
          }
        }
        closedir(subdir);

        /* Special case: Handle /dev/dri/by-path symlinks */
        if (strcmp(name, "dri") == 0) {
          char bp_path[PATH_MAX];
          snprintf(bp_path, sizeof(bp_path), "%.3800s/by-path", path);
          DIR *bp_dir = opendir(bp_path);
          if (bp_dir) {
            while ((subentry = readdir(bp_dir)) != NULL) {
              if (strstr(subentry->d_name, "-card")) {
                char bppath[PATH_MAX];
                snprintf(bppath, sizeof(bppath), "%.3800s/%s", bp_path,
                         subentry->d_name);
                unlink(bppath);
              }
            }
            closedir(bp_dir);
          }
        }
      }
    }
  }

  closedir(dir);
}

/* ---------------------------------------------------------------------------
 * /dev setup
 * ---------------------------------------------------------------------------*/

int setup_dev(const char *rootfs, int hw_access, int gpu_mode,
              int privileged_mask) {
  char dev_path[PATH_MAX];
  snprintf(dev_path, sizeof(dev_path), "%s/dev", rootfs);

  /* Ensure the directory exists */
  mkdir(dev_path, 0755);

  if (hw_access) {
    /* If hw_access is enabled, we mount host's devtmpfs.
     * WARNING: This is a shared singleton. We MUST be careful. */
    if (domount("devtmpfs", dev_path, "devtmpfs", MS_NOSUID | MS_NOEXEC,
                "mode=755") == 0) {
      /* Clean up conflicting nodes from the shared devtmpfs.
       * We MUST immediately recreate them in create_devices() as REAL
       * character devices to prevent host breakage. This also performs
       * DRM master and host display isolation. */
      prune_host_devices(dev_path, privileged_mask);

      /* devtmpfs is the kernel's own instance and does NOT contain nodes
       * that Android's ueventd created in its tmpfs-based /dev (kgsl-3d0,
       * mali0, dri/renderD128, etc.).  Mirror any missing GPU/hardware nodes
       * from the host into the freshly mounted devtmpfs now, before
       * create_devices() lays down the standard char nodes.
       * hw_access already implies full GPU wiring - no need to check gpu_mode
       * separately here. */
      mirror_gpu_nodes(dev_path);
    } else {
      ds_warn("Failed to mount devtmpfs, falling back to tmpfs");
      if (domount("none", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC,
                  "size=8M,mode=755") < 0)
        return -1;
    }
  } else {
    /* Secure isolated /dev using tmpfs */
    if (domount("none", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC,
                "size=8M,mode=755") < 0)
      return -1;

    /* --gpu mode: scan the host /dev for known GPU "smoking guns" and mknod
     * the found nodes into our isolated tmpfs.  This gives GPU acceleration
     * without exposing the full host devtmpfs.  mirror_gpu_nodes() honours
     * the is_dangerous_node() blocklist and only creates character devices
     * that exist on the host, so it is safe to call unconditionally here. */
    if (gpu_mode) {
      ds_log("[GPU] --gpu mode: mirroring host GPU nodes into isolated tmpfs");
      mirror_gpu_nodes(dev_path);
    }
  }

  /* Create minimal set of device nodes (creates secure console/ptmx/etc.) */
  int ret = create_devices(rootfs, hw_access, privileged_mask);
  if (ret < 0)
    return ret;

  /*
   * Slave-bind host /dev/block → container /dev/block  (hw_access only)
   *
   * Problem: Android's ueventd/vold intercepts USB storage uevents and
   * creates block nodes exclusively under /dev/block/ (e.g. /dev/block/sdf,
   * /dev/block/sdf1).  devtmpfs - a global singleton - never receives these
   * nodes, so mounting devtmpfs gives us mali0, video*, etc. but NO storage.
   *
   * Fix: MS_BIND | MS_REC establishes a live mirror of the host /dev/block
   * tree inside the container.  The follow-up MS_SLAVE | MS_REC makes the
   * propagation one-directional:
   *   HOST  → CONTAINER : new block nodes appear in real-time (ueventd/vold
   *                        creates /dev/block/sdX → instantly visible here)
   *   CONTAINER → HOST  : nothing leaks back (container mounts stay private)
   *
   * udev running inside the container receives the same NETLINK_KOBJECT_UEVENT
   * broadcast from the kernel and creates the canonical /dev/sdX symlinks/nodes
   * using its own rules, sourcing device info from the now-visible /dev/block/.
   *
   * Reference: mount_namespaces(7) - "MS_SLAVE" propagation type.
   */
  if (hw_access && access("/dev/block", F_OK) == 0) {
    char block_path[PATH_MAX];
    snprintf(block_path, sizeof(block_path), "%s/dev/block", rootfs);
    mkdir(block_path, 0755);

    if (mount("/dev/block", block_path, NULL, MS_BIND | MS_REC, NULL) == 0) {
      if (mount(NULL, block_path, NULL, MS_SLAVE | MS_REC, NULL) < 0)
        ds_warn("[DEBUG] /dev/block bind ok but MS_SLAVE failed: %s - "
                "block nodes visible but propagation is shared",
                strerror(errno));
      else
        ds_log("[DEBUG] /dev/block slave-bound from host "
               "(real-time block hotplug active)");
    } else {
      ds_warn("[DEBUG] /dev/block bind failed: %s - "
              "storage nodes won't appear automatically",
              strerror(errno));
    }
  }

  return ret;
}

int create_devices(const char *rootfs, int hw_access, int privileged_mask) {
  (void)hw_access;
  const struct {
    const char *name;
    mode_t mode;
    dev_t dev;
  } devices[] = {{"null", S_IFCHR | 0666, makedev(1, 3)},
                 {"zero", S_IFCHR | 0666, makedev(1, 5)},
                 {"full", S_IFCHR | 0666, makedev(1, 7)},
                 {"random", S_IFCHR | 0666, makedev(1, 8)},
                 {"urandom", S_IFCHR | 0666, makedev(1, 9)},
                 {"tty", S_IFCHR | 0666, makedev(5, 0)},
                 {"console", S_IFCHR | 0620, makedev(5, 1)},
                 {"ptmx", S_IFCHR | 0666, makedev(5, 2)},
                 {NULL, 0, 0}};

  char path[PATH_MAX];

  /* 1. Create standard devices */
  for (int i = 0; devices[i].name; i++) {
    snprintf(path, sizeof(path), "%s/dev/%s", rootfs, devices[i].name);

    /* If unfiltered mode is on and the node already exists (e.g. on devtmpfs),
     * we skip unlinking to avoid breaking the kernel-provided node. */
    if (!(privileged_mask & DS_PRIV_UNFILTERED)) {
      force_unlink(path);
    } else if (access(path, F_OK) == 0) {
      continue;
    }

    if (mknod(path, devices[i].mode, devices[i].dev) < 0) {
      /* Fallback for environments where mknod is restricted */
      char host_path[PATH_MAX];
      snprintf(host_path, sizeof(host_path), "/dev/%s", devices[i].name);
      bind_mount(host_path, path);
    } else {
      chmod(path, devices[i].mode & 0777);
      /* Success! Now set ownership to root:tty (gid 5) for console/tty nodes */
      if (strcmp(devices[i].name, "console") == 0 ||
          strcmp(devices[i].name, "tty") == 0) {
        if (chown(path, 0, 5) < 0) {
          /* Ignore failure */
        }
      }
    }
  }

  /* 2. Create /dev/net/tun */
  snprintf(path, sizeof(path), "%s/dev/net", rootfs);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/dev/net/tun", rootfs);
  force_unlink(path);
  if (mknod(path, S_IFCHR | 0666, makedev(10, 200)) < 0)
    bind_mount("/dev/net/tun", path);
  else
    chmod(path, 0666);

  /* 3. Create /dev/fuse */
  snprintf(path, sizeof(path), "%s/dev/fuse", rootfs);
  force_unlink(path);
  if (mknod(path, S_IFCHR | 0666, makedev(10, 229)) < 0)
    bind_mount("/dev/fuse", path);
  else
    chmod(path, 0666);

  /* 4. Create tty1...N nodes (mount targets for PTYs) */
  for (int i = 1; i <= DS_MAX_TTYS; i++) {
    snprintf(path, sizeof(path), "%s/dev/tty%d", rootfs, i);
    force_unlink(path);
    write_file(path, "");
    chmod(path, 0666);
  }
  /* Standard symlinks */
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/dev/fd", rootfs);
  if (symlink("/proc/self/fd", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/fd symlink: %s", strerror(errno));

  snprintf(tgt, sizeof(tgt), "%s/dev/stdin", rootfs);
  if (symlink("/proc/self/fd/0", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/stdin symlink: %s", strerror(errno));

  snprintf(tgt, sizeof(tgt), "%s/dev/stdout", rootfs);
  if (symlink("/proc/self/fd/1", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/stdout symlink: %s", strerror(errno));

  snprintf(tgt, sizeof(tgt), "%s/dev/stderr", rootfs);
  if (symlink("/proc/self/fd/2", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/stderr symlink: %s", strerror(errno));

  return 0;
}

int setup_devpts(int hw_access) {
  const char *pts_path = "/dev/pts";

  /* Unmount any existing devpts instance first */
  umount2(pts_path, MNT_DETACH);

  /* Create mountpoint */
  mkdir(pts_path, 0755);

  /* Try mounting devpts with newinstance flag (CRITICAL for private PTYs) */
  char optbuf[256];
  snprintf(optbuf, sizeof(optbuf), "gid=%d,newinstance,ptmxmode=0666,mode=0620",
           DS_DEFAULT_TTY_GID);

  char optbuf2[128];
  snprintf(optbuf2, sizeof(optbuf2), "gid=%d,newinstance,mode=0620",
           DS_DEFAULT_TTY_GID);

  const char *opts[] = {optbuf,        "newinstance,ptmxmode=0666,mode=0620",
                        optbuf2,       "newinstance,ptmxmode=0666",
                        "newinstance", NULL};

  for (int i = 0; opts[i]; i++) {
    if (domount("devpts", pts_path, "devpts", MS_NOSUID | MS_NOEXEC, opts[i]) ==
        0) {
      /* Setup /dev/ptmx to point to the new pts/ptmx */
      const char *ptmx_path = "/dev/ptmx";
      const char *pts_ptmx = "/dev/pts/ptmx";

      if (hw_access) {
        /* In HW access mode, /dev is a devtmpfs (shared singleton).
         * CRITICAL: Do NOT unlink. create_devices() already created
         * a real char device node (5,2) for us to bind-mount over. */
        if (mount(pts_ptmx, ptmx_path, NULL, MS_BIND, NULL) == 0) {
          return 0;
        }
      } else {
        /* Secure mode: /dev is a private tmpfs. Unlink is safe. */
        unlink(ptmx_path);

        /* Method 1: Bind mount (preferred) */
        if (write_file(ptmx_path, "") == 0) {
          if (mount(pts_ptmx, ptmx_path, NULL, MS_BIND, NULL) == 0) {
            return 0;
          }
        }

        /* Method 2: Symlink - but verify target exists first.
         * Kernel 3.x devpts newinstance may not create /dev/pts/ptmx. */
        unlink(ptmx_path);
        if (symlink("pts/ptmx", ptmx_path) == 0 && access(pts_ptmx, F_OK) == 0)
          return 0;

        /* Method 3: real c 5,2 node for kernel 3.x (not namespace-isolated
         * but /dev/ptmx actually exists and openpty works). */
        unlink(ptmx_path);
        if (mknod(ptmx_path, S_IFCHR | 0666, makedev(5, 2)) == 0) {
          chmod(ptmx_path, 0666);
          return 0;
        }
      }

      ds_warn("Failed to virtualize /dev/ptmx, PTYs might not work");
      return 0;
    }
  }

  ds_error("Failed to mount devpts with newinstance flag");
  return -1;
}

int check_volatile_mode(struct ds_config *cfg) {
  if (!cfg->volatile_mode)
    return 0;

  if (grep_file("/proc/filesystems", "overlay") != 1) {
    ds_error("OverlayFS is not supported by your kernel. Volatile mode cannot "
             "be used.");
    return -1;
  }

  /* Pre-flight: reject f2fs lowerdir - known Android kernel limitation */
  struct statfs sfs;
  if (statfs(cfg->rootfs_path, &sfs) == 0 && sfs.f_type == 0xF2F52010) {
    ds_error("Volatile mode cannot be used: Your rootfs is on f2fs, which is "
             "not supported as an OverlayFS lower layer on most Android "
             "kernels.");
    ds_error("Tip: Use a rootfs image (-i) instead of a directory (-r) "
             "for volatile mode on f2fs partitions.");
    return -1;
  }

  return 0;
}

int setup_volatile_overlay(struct ds_config *cfg) {
  /* 1. Create temporary workspace in Droidspaces/Volatile/<name> */
  char base[PATH_MAX];
  snprintf(base, sizeof(base), "%s/" DS_VOLATILE_SUBDIR "/%s",
           get_workspace_dir(), cfg->container_name);
  if (mkdir_p(base, 0755) < 0) {
    ds_error("Failed to create volatile workspace: %s", base);
    return -1;
  }
  safe_strncpy(cfg->volatile_dir, base, sizeof(cfg->volatile_dir));

  /* 2. Mount tmpfs as the backing store for upper/work */
  if (domount("none", base, "tmpfs", 0, "size=50%,mode=755") < 0)
    return -1;

  /* 3. Create subdirectories */
  char upper[PATH_MAX + 32], work[PATH_MAX + 32], merged[PATH_MAX + 32];
  snprintf(upper, sizeof(upper), "%s/upper", base);
  snprintf(work, sizeof(work), "%s/work", base);
  snprintf(merged, sizeof(merged), "%s/merged", base);
  mkdir(upper, 0755);
  mkdir(work, 0755);
  mkdir(merged, 0755);

  /* 4. Perform Overlay mount */
  char opts[32768];
  int n;

  if (is_android()) {
    n = snprintf(opts, sizeof(opts),
                 "lowerdir=%s,upperdir=%s/upper,workdir=%s/work,context=\"%s\"",
                 cfg->rootfs_path, base, base, DS_ANDROID_TMPFS_CONTEXT);
  } else {
    n = snprintf(opts, sizeof(opts),
                 "lowerdir=%s,upperdir=%s/upper,workdir=%s/work",
                 cfg->rootfs_path, base, base);
  }

  if (n < 0 || (size_t)n >= sizeof(opts)) {
    ds_error("OverlayFS options too long");
    cleanup_volatile_overlay(cfg);
    return -1;
  }

  if (domount("overlay", merged, "overlay", 0, opts) < 0) {
    ds_error("OverlayFS mount failed. Your kernel might not support it.");
    /* Cleanup: unmount tmpfs first, then remove workspace */
    umount2(base, MNT_DETACH);
    ds_error("OverlayFS mount failed: %s", strerror(errno));
    cleanup_volatile_overlay(cfg);
    return -1;
  }

  /* 9. Update cfg->rootfs_path to the merged view */
  safe_strncpy(cfg->rootfs_path, merged, sizeof(cfg->rootfs_path));

  return 0;
}

/**
 * is_mount_in_namespace() - Check if `path` is mounted in OUR namespace.
 *
 * Reads /proc/self/mountinfo and searches for an exact match of `path`
 * in the mount-point column (field 5, 0-indexed: 4).
 *
 * Unlike is_mountpoint() (which uses stat-based device ID comparison),
 * this checks the kernel's mount table directly. This is critical for
 * overlay mounts that may share the same device as the lowerdir.
 *
 * Returns 1 if mounted, 0 if not.
 */
static int is_mount_in_namespace(const char *path) {
  FILE *f = fopen("/proc/self/mountinfo", "r");
  if (!f)
    return 0;

  char io_buf[65536];
  setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

  char line[4096];
  size_t path_len = strlen(path);

  while (fgets(line, sizeof(line), f)) {
    /* mountinfo format: id parent_id major:minor root mount_point ... */
    /* We need field 5 (mount_point), skip first 4 fields */
    const char *p = line;
    for (int skip = 0; skip < 4 && *p; skip++) {
      while (*p && *p != ' ')
        p++;
      while (*p == ' ')
        p++;
    }
    /* p now points at the mount_point field */
    if (strncmp(p, path, path_len) == 0 &&
        (p[path_len] == ' ' || p[path_len] == '\n' || p[path_len] == '\0')) {
      fclose(f);
      return 1;
    }
  }
  fclose(f);
  return 0;
}

/**
 * cleanup_volatile_overlay() - Simplified OverlayFS cleanup.
 *
 * The overlay is mounted INSIDE the container's mount namespace (boot.c).
 * When the container dies, the kernel tears down the namespace and the
 * mounts vanish automatically.
 *
 * We simply check if the mount is visible in our namespace (host); if so,
 * we try to unmount it normally before deleting the workspace directory.
 */
int cleanup_volatile_overlay(struct ds_config *cfg) {
  if (cfg->volatile_dir[0] == '\0')
    return 0;

  char merged[PATH_MAX + 32];
  snprintf(merged, sizeof(merged), "%s/merged", cfg->volatile_dir);

  /* Skip logging for clean exits - nothing prints after 'Powering off.' */

  /* 1. Fast path: check if mounts already vanished (normal case) */
  if (!is_mount_in_namespace(merged) &&
      !is_mount_in_namespace(cfg->volatile_dir)) {
    goto done;
  }

  /* 2. Slow path: unmount visible mounts (e.g. stop-rootfs on live container)
   */
  sync();
  umount(merged);
  umount(cfg->volatile_dir);

done:
  /* settle time for kernel to release backing store info */
  usleep(DS_RETRY_DELAY_US / 2);
  int r = remove_recursive(cfg->volatile_dir);
  cfg->volatile_dir[0] = '\0';
  return r;
}

int setup_custom_binds(struct ds_config *cfg, const char *rootfs) {
  if (cfg->bind_count == 0 || !cfg->binds)
    return 0;

  /* Ensure mounts are processed in alphabetical order of destination
   * so parent directories are always mounted before children. */
  sort_bind_mounts(cfg);

  for (int i = 0; i < cfg->bind_count; i++) {
    char tgt[PATH_MAX * 2];
    int n = snprintf(tgt, sizeof(tgt), "%s%s", rootfs, cfg->binds[i].dest);
    if (n < 0 || (size_t)n >= sizeof(tgt)) {
      ds_warn("Bind mount target path too long, skipping: %s",
              cfg->binds[i].dest);
      continue;
    }

    /* Check if source exists on host and is not a symlink */
    struct stat st_src;
    if (lstat(cfg->binds[i].src, &st_src) != 0) {
      ds_warn("Skip bind mount: source path not found on host: %s",
              cfg->binds[i].src);
      continue;
    }
    if (S_ISLNK(st_src.st_mode)) {
      ds_error("Security Violation: Bind source %s is a symlink!",
               cfg->binds[i].src);
      continue;
    }

    /* Ensure parent directory exists */
    char parent[PATH_MAX];
    safe_strncpy(parent, tgt, sizeof(parent));
    char *slash = strrchr(parent, '/');
    if (slash) {
      *slash = '\0';
      mkdir_p(parent, 0755);
    }

    /* Security: Check if target exists and is a symlink */
    struct stat st_tgt;
    if (lstat(tgt, &st_tgt) == 0 && S_ISLNK(st_tgt.st_mode)) {
      ds_error("Security Violation: Bind target %s is a symlink!", tgt);
      continue;
    }

    /* Perform bind mount */
    if (bind_mount(cfg->binds[i].src, tgt) < 0) {
      ds_warn("Failed to bind mount %s on %s (skipping)", cfg->binds[i].src,
              tgt);
      continue;
    }

    /* Verify isolation: Ensure we didn't accidentally mount over a host path
     * if the container rootfs had a complex malicious structure. */
    if (!is_subpath(rootfs, tgt)) {
      ds_error("Security Violation: Bind destination %s escapes rootfs %s!",
               tgt, rootfs);
      umount2(tgt, MNT_DETACH);
      continue;
    }
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Rootfs Image Handling - Pure C loop device management (no host tools)
 * ---------------------------------------------------------------------------*/

/* Probe superblock magic bytes to identify the filesystem type. */
static const char *detect_fs_type(const char *img_path) {
  int fd = open(img_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return NULL;

  uint8_t buf[8];
  const char *result = NULL;

  /* offset 0x438: ext2/3/4 (0xEF53 LE) */
  if (pread(fd, buf, 2, 0x438) == 2) {
    uint16_t m = (uint16_t)buf[0] | (uint16_t)buf[1] << 8;
    if (m == 0xEF53) {
      result = "ext4";
      goto out;
    }
  }

  /* offset 0x10040: btrfs ("_BHRfS_M") */
  if (pread(fd, buf, 8, 0x10040) == 8) {
    if (memcmp(buf, "_BHRfS_M", 8) == 0) {
      result = "btrfs";
      goto out;
    }
  }

out:
  close(fd);
  return result;
}

/*
 * Resolve loop device node path after LOOP_CTL_GET_FREE.
 *
 * Android userspace (vold): /dev/block/loopN
 * Android recovery + desktop Linux: /dev/loopN
 *
 * Strategy: probe the environment-preferred path with retries for ueventd/udev,
 * cross-try the other path, then mknod as a last resort (major 7, minor=devnr).
 */
static int open_loop_dev(long devnr, char *path_out, size_t path_size) {
  int android = is_android();

  /* Android: /dev/block/loopN; recovery/desktop: /dev/loopN */
  if (android)
    snprintf(path_out, path_size, "/dev/block/loop%ld", devnr);
  else
    snprintf(path_out, path_size, "/dev/loop%ld", devnr);

  /* Wait up to 500ms for ueventd/udev to create the node */
  for (int i = 0; i < 5; i++) {
    int fd = open(path_out, O_RDWR | O_CLOEXEC);
    if (fd >= 0)
      return fd;
    usleep(100000);
  }

  /* Cross-environment fallback (recovery acts like desktop, etc.) */
  if (android)
    snprintf(path_out, path_size, "/dev/loop%ld", devnr);
  else
    snprintf(path_out, path_size, "/dev/block/loop%ld", devnr);

  int fd = open(path_out, O_RDWR | O_CLOEXEC);
  if (fd >= 0)
    return fd;

  /* Last resort: create the node ourselves */
  if (mknod(path_out, S_IFBLK | 0660, makedev(7, (int)devnr)) == 0) {
    fd = open(path_out, O_RDWR | O_CLOEXEC);
    if (fd >= 0)
      return fd;
  }

  return -1;
}

/*
 * Attach img_path to a free loop device via ioctls.
 * Sets LO_FLAGS_AUTOCLEAR so the kernel auto-releases the loop after umount.
 * Returns the open loop_fd on success (caller must close after mount()).
 * loop_path_out is filled with the device node path for the mount() call.
 */
static int loop_attach(const char *img_path, char *loop_path_out,
                       size_t path_size) {
  int ctl_fd = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
  if (ctl_fd < 0) {
    ds_error("open /dev/loop-control: %s", strerror(errno));
    return -1;
  }

  long devnr = ioctl(ctl_fd, LOOP_CTL_GET_FREE);
  close(ctl_fd);
  if (devnr < 0) {
    ds_error("LOOP_CTL_GET_FREE: %s", strerror(errno));
    return -1;
  }

  int loop_fd = open_loop_dev(devnr, loop_path_out, path_size);
  if (loop_fd < 0) {
    ds_error("Failed to open loop%ld: %s", devnr, strerror(errno));
    return -1;
  }

  int img_fd = open(img_path, O_RDWR | O_CLOEXEC);
  if (img_fd < 0) {
    ds_error("open image %s: %s", img_path, strerror(errno));
    close(loop_fd);
    return -1;
  }

  if (ioctl(loop_fd, LOOP_SET_FD, img_fd) < 0) {
    ds_error("LOOP_SET_FD: %s", strerror(errno));
    close(img_fd);
    close(loop_fd);
    return -1;
  }
  close(img_fd); /* kernel holds a ref; we're done with this fd */

  struct loop_info64 li;
  memset(&li, 0, sizeof(li));
  /* AUTOCLEAR: kernel auto-releases loop device after umount + all fds closed
   */
  li.lo_flags = LO_FLAGS_AUTOCLEAR;
  snprintf((char *)li.lo_file_name, LO_NAME_SIZE, "%.63s", img_path);

  if (ioctl(loop_fd, LOOP_SET_STATUS64, &li) < 0)
    ds_warn("LOOP_SET_STATUS64: %s (continuing)", strerror(errno));

  return loop_fd;
}

/* Detach a loop device explicitly via LOOP_CLR_FD (belt-and-suspenders). */
static void loop_detach(const char *loop_dev) {
  if (!loop_dev || !loop_dev[0])
    return;
  int fd = open(loop_dev, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return;
  ioctl(fd, LOOP_CLR_FD, 0);
  close(fd);
}

/* Find the block device (loop node) backing a given mount point via
 * /proc/mounts. */
static int get_backing_dev(const char *mnt, char *dev_out, size_t dev_size) {
  FILE *f = fopen("/proc/mounts", "r");
  if (!f)
    return -1;

  char line[PATH_MAX + 256];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char dev[256], mntpt[PATH_MAX];
    if (sscanf(line, "%255s %4095s", dev, mntpt) == 2 &&
        strcmp(mntpt, mnt) == 0) {
      safe_strncpy(dev_out, dev, dev_size);
      found = 1;
      break;
    }
  }
  fclose(f);
  return found ? 0 : -1;
}

int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size,
                     const char *name) {
  if (find_available_mountpoint(name, mount_point, mp_size) < 0) {
    ds_error("Failed to find available mount point for %s", name);
    return -1;
  }

  /* Detect filesystem type from superblock magic */
  const char *fstype = detect_fs_type(img_path);
  if (!fstype) {
    ds_warn("Unknown filesystem in %s. Only ext4 and btrfs are supported.",
            img_path);
    return -1;
  }

  /* e2fsck: only for ext images */
  if (strcmp(fstype, "ext4") == 0) {
    char *e2fsck_argv[] = {"e2fsck", "-f", "-y", (char *)(uintptr_t)img_path,
                           NULL};
    if (run_command_quiet(e2fsck_argv) == 0)
      ds_log("Image checked and repaired successfully.");
  }

  /* Settle time: prevent "device busy" on rapid restarts */
  sync();
  usleep(DS_RETRY_DELAY_US);

  /* Set SELinux context via xattr directly instead of spawning chcon */
  if (is_android())
    set_selinux_context(img_path, DS_ANDROID_VOLD_CONTEXT);

  /*
   * Build mount flags: base VFS flags + any fstype-specific extras.
   * pivot_root requires a writable mount to create .old_root, so no MS_RDONLY.
   */
  unsigned long mnt_flags = MS_NOATIME | MS_NODIRATIME;
  const char *mnt_data = NULL;

  if (strcmp(fstype, "ext4") == 0) {
    mnt_data = "nodelalloc,errors=remount-ro,init_itable=0";
  } else if (strcmp(fstype, "btrfs") == 0) {
    /* btrfs defaults are usually sane */
    mnt_data = NULL;
  }

  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt == 0)
      ds_log("Mounting %s rootfs image %s on %s...", fstype, img_path,
             mount_point);
    else
      ds_log("Mounting %s rootfs image %s on %s (Attempt %d/3)...", fstype,
             img_path, mount_point, attempt + 1);

    struct stat st;
    int is_blk = (stat(img_path, &st) == 0 && S_ISBLK(st.st_mode));
    char final_src[PATH_MAX];
    int loop_fd = -1;

    if (is_blk) {
      safe_strncpy(final_src, img_path, sizeof(final_src));
    } else {
      loop_fd = loop_attach(img_path, final_src, sizeof(final_src));
      if (loop_fd < 0)
        goto retry;
    }

    int ret = mount(final_src, mount_point, fstype, mnt_flags, mnt_data);
    if (loop_fd >= 0)
      close(loop_fd); /* AUTOCLEAR handles cleanup if mount failed */

    if (ret == 0) {
      /* Android FIX: Some kernels enforce nosuid/nodev on all loop mounts
       * if the backing file is on /data. Explicitly remount to clear them. */
      if (is_android())
        mount(NULL, mount_point, NULL, MS_REMOUNT | mnt_flags, mnt_data);
      return 0;
    }

    /* mount() failed: explicitly detach since AUTOCLEAR needs last-fd-close
     * + no active mounts to trigger; we already closed loop_fd so it should
     * auto-clear, but be explicit for kernels < 3.10 edge cases. */
    if (loop_fd >= 0)
      loop_detach(final_src);
    ds_warn("mount(%s, %s) failed: %s", final_src, fstype, strerror(errno));

  retry:
    if (attempt < 2) {
      ds_log("Retrying in 1s...");
      sync();
      usleep(DS_RETRY_DELAY_US * 5);
    }
  }

  ds_error("Failed to mount image %s after 3 attempts", img_path);
  return -1;
}

int unmount_rootfs_img(const char *mount_point, int silent) {
  if (!mount_point || !mount_point[0])
    return 0;

  /* Grab the backing loop device before we unmount (it disappears after) */
  char loop_dev[256] = {0};
  get_backing_dev(mount_point, loop_dev, sizeof(loop_dev));

  /* 1. Lazy unmount: detaches the mount even if files are open */
  sync();
  umount2(mount_point, MNT_DETACH);

  /* 2. Explicitly detach loop device (AUTOCLEAR also handles this, but be safe)
   */
  if (loop_dev[0])
    loop_detach(loop_dev);

  /* 3. Settle and force if still mounted (stubborn old kernels) */
  sync();
  usleep(DS_RETRY_DELAY_US);
  if (is_mountpoint(mount_point)) {
    umount2(mount_point, MNT_DETACH | MNT_FORCE);
    usleep(DS_RETRY_DELAY_US / 2);
  }

  /* 4. Cleanup and log */
  int still_mounted = is_mountpoint(mount_point);
  if (rmdir(mount_point) == 0 || !still_mounted) {
    if (!silent)
      ds_log("Unmounted rootfs image from %s.", mount_point);
  } else if (errno != ENOENT) {
    if (!silent)
      ds_warn("Cleanup warning: %s is still busy/mounted.", mount_point);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Container introspection helpers (used by info/show)
 * ---------------------------------------------------------------------------*/

/* Get filesystem type of a mountpoint inside container namespace.
 * Reads /proc/<pid>/mounts which is already namespace-aware. */
int get_container_mount_fstype(pid_t pid, const char *path, char *fstype,
                               size_t size) {
  char mounts_path[PATH_MAX];
  snprintf(mounts_path, sizeof(mounts_path), "/proc/%d/mounts", pid);

  FILE *fp = fopen(mounts_path, "r");
  if (!fp)
    return -1;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    char mount_path[PATH_MAX];
    char type[64];
    if (sscanf(line, "%*s %s %s", mount_path, type) == 2) {
      if (strcmp(mount_path, path) == 0) {
        safe_strncpy(fstype, type, size);
        fclose(fp);
        return 0;
      }
    }
  }
  fclose(fp);
  return -1;
}

/* Detect if Android storage is mounted inside the container */
int detect_android_storage_in_container(pid_t pid) {
  if (pid <= 0)
    return 0;

  char fstype[64];
  if (get_container_mount_fstype(pid, "/storage/emulated/0", fstype,
                                 sizeof(fstype)) != 0)
    return 0;

  /* Verify /storage/emulated/0/Android exists inside container */
  char path[PATH_MAX];
  struct stat st;
  if (build_proc_root_path(pid, "/storage/emulated/0/Android", path,
                           sizeof(path)) != 0)
    return 0;

  if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode))
    return 0;

  return 1;
}

/* Detect if HW access is enabled (devtmpfs mounted at /dev) */
int detect_hw_access_in_container(pid_t pid) {
  char fstype[64];
  if (get_container_mount_fstype(pid, "/dev", fstype, sizeof(fstype)) == 0)
    return strcmp(fstype, "devtmpfs") == 0;
  return 0;
}

/* Ensure host devpts is mounted - specifically for Android Recovery
 * environments where /dev/pts is often missing or unmounted, causing openpty()
 * to fail. */
int ds_fix_host_ptys(void) {
  const char *pts_path = "/dev/pts";

  /* If already a mountpoint, we are good */
  if (is_mountpoint(pts_path))
    return 0;

  /* Ensure directory exists */
  mkdir(pts_path, 0755);

  /* Mount host devpts. We use standard gid=5 (tty) and mode=620.
   * This is the 'host' global namespace version of setup_devpts. */
  if (mount("devpts", pts_path, "devpts", MS_NOSUID | MS_NOEXEC,
            "gid=5,mode=620") < 0) {
    if (errno != EBUSY) {
      /* EBUSY means already mounted (redundant with is_mountpoint but safe) */
      ds_warn("Failed to mount host devpts: %s", strerror(errno));
      return -1;
    }
  }

  ds_log("Host devpts mounted successfully (Recovery fix).");
  return 0;
}
