/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

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

  snprintf(mount_path, size, "%s/%s", base_dir, name);

  if (access(mount_path, F_OK) == 0) {
    if (is_mountpoint(mount_path)) {
      /* This is a stale mount point from a previous crashed run.
       * (We know it's stale because start_rootfs ensures the container name
       * itself is unique among currently running containers). */
      ds_warn("Found stale mount at %s, cleaning up...", mount_path);
      if (umount2(mount_path, MNT_DETACH) < 0) {
        /* If detach fails, try unmount -d to clean loop device */
        char *umount_argv[] = {"umount", "-d", "-l", mount_path, NULL};
        run_command_quiet(umount_argv);
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

int bind_mount(const char *src, const char *tgt) {
  /* Ensure target exists */
  struct stat st_src, st_tgt;
  if (stat(src, &st_src) < 0)
    return -1;

  if (stat(tgt, &st_tgt) < 0) {
    if (S_ISDIR(st_src.st_mode))
      mkdir(tgt, 0755);
    else
      write_file(tgt, ""); /* Create empty file as mount point */
  }

  return domount(src, tgt, NULL, MS_BIND | MS_REC, NULL);
}

/* ---------------------------------------------------------------------------
 * /dev setup
 * ---------------------------------------------------------------------------*/

int setup_dev(const char *rootfs, int hw_access) {
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
       * character devices to prevent host breakage. */
      const char *conflicts[] = {"console", "tty",     "full", "null", "zero",
                                 "random",  "urandom", "ptmx", NULL};
      for (int i = 0; conflicts[i]; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%.4080s/%s", dev_path, conflicts[i]);
        /* Unmount if it was somehow bind-mounted */
        umount2(path, MNT_DETACH);
        force_unlink(path);
      }
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
  }

  /* Create minimal set of device nodes (creates secure console/ptmx/etc.) */
  return create_devices(rootfs, hw_access);
}

int create_devices(const char *rootfs, int hw_access) {
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
    force_unlink(
        path); /* Always start fresh to ensure correct type/permissions */

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
    if (access(path, F_OK) != 0) {
      write_file(path, "");
    }
    chmod(path, 0666);
  }
  /* Standard symlinks */
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/dev/fd", rootfs);
  symlink("/proc/self/fd", tgt);
  snprintf(tgt, sizeof(tgt), "%s/dev/stdin", rootfs);
  symlink("/proc/self/fd/0", tgt);
  snprintf(tgt, sizeof(tgt), "%s/dev/stdout", rootfs);
  symlink("/proc/self/fd/1", tgt);
  snprintf(tgt, sizeof(tgt), "%s/dev/stderr", rootfs);
  symlink("/proc/self/fd/2", tgt);

  return 0;
}

int setup_devpts(int hw_access) {
  const char *pts_path = "/dev/pts";

  /* Unmount any existing devpts instance first */
  umount2(pts_path, MNT_DETACH);

  /* Create mountpoint */
  mkdir(pts_path, 0755);

  /* Try mounting devpts with newinstance flag (CRITICAL for private PTYs) */
  const char *opts[] = {"gid=5,newinstance,ptmxmode=0666,mode=0620",
                        "newinstance,ptmxmode=0666,mode=0620",
                        "gid=5,newinstance,mode=0620",
                        "newinstance,ptmxmode=0666",
                        "newinstance",
                        NULL};

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

        /* Method 2: Symlink (fallback) */
        unlink(ptmx_path);
        if (symlink("pts/ptmx", ptmx_path) == 0) {
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

int setup_cgroups(void) {
  mkdir("/sys/fs/cgroup", 0755);

  /* Detect Cgroup v2 (unified hierarchy) */
  if (access("/sys/fs/cgroup/cgroup.controllers", F_OK) == 0 ||
      grep_file("/proc/mounts", "cgroup2")) {
    return domount("cgroup2", "/sys/fs/cgroup", "cgroup2",
                   MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
  }

  /* Fallback to Cgroup v1 legacy hierarchies */
  if (domount("none", "/sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755") < 0)
    return -1;

  const char *subs[] = {"cpu",   "cpuacct", "devices", "memory", "freezer",
                        "blkio", "pids",    "systemd", NULL};
  char path[PATH_MAX];
  for (int i = 0; subs[i]; i++) {
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", subs[i]);
    mkdir(path, 0755);
    domount("cgroup", path, "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC,
            subs[i]);
  }
  return 0;
}

int setup_volatile_overlay(struct ds_config *cfg) {
  if (!cfg->volatile_mode)
    return 0;

  if (grep_file("/proc/filesystems", "overlay") != 1) {
    ds_error("OverlayFS is not supported by your kernel. Volatile mode cannot "
             "be used.");
    return -1;
  }

  /* Pre-flight: reject f2fs lowerdir — known Android kernel limitation */
  struct statfs sfs;
  if (statfs(cfg->rootfs_path, &sfs) == 0 && sfs.f_type == 0xF2F52010) {
    ds_error("Volatile mode cannot be used: Your rootfs is on f2fs, which is "
             "not supported as an OverlayFS lower layer on most Android "
             "kernels.");
    ds_error("Tip: Use a rootfs image (-i) instead of a directory (-r) "
             "for volatile mode on f2fs partitions.");
    return -1;
  }

  ds_log("Entering volatile mode (OverlayFS)...");

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
  char *p = opts;
  size_t len;

  len = strlen(cfg->rootfs_path);
  memcpy(p, "lowerdir=", 9);
  p += 9;
  memcpy(p, cfg->rootfs_path, len);
  p += len;

  len = strlen(upper);
  memcpy(p, ",upperdir=", 10);
  p += 10;
  memcpy(p, upper, len);
  p += len;

  len = strlen(work);
  memcpy(p, ",workdir=", 9);
  p += 9;
  memcpy(p, work, len);
  p += len;

  /* On Android, SELinux blocks writes through overlay when the upperdir
   * creates files with tmpfs context that can't associate with the overlay's
   * default filesystem label. Use tmpfs context to match the upperdir. */
  if (is_android()) {
    const char *ctx = ",context=\"u:object_r:tmpfs:s0\"";
    size_t ctx_len = strlen(ctx);
    memcpy(p, ctx, ctx_len);
    p += ctx_len;
  }

  *p = '\0';

  if (domount("overlay", merged, "overlay", 0, opts) < 0) {
    ds_error("OverlayFS mount failed. Your kernel might not support it.");
    /* Cleanup: unmount tmpfs first, then remove workspace */
    umount2(base, MNT_DETACH);
    remove_recursive(cfg->volatile_dir);
    cfg->volatile_dir[0] = '\0';
    return -1;
  }

  /* 5. Switch rootfs path to the merged view */
  ds_log("OverlayFS ready: Changes will be written to RAM and lost on exit.");
  safe_strncpy(cfg->rootfs_path, merged, sizeof(cfg->rootfs_path));

  return 0;
}

int cleanup_volatile_overlay(struct ds_config *cfg) {
  if (cfg->volatile_dir[0] == '\0')
    return 0;

  ds_log("Cleaning up volatile overlay...");

  char merged[PATH_MAX + 32];
  snprintf(merged, sizeof(merged), "%s/merged", cfg->volatile_dir);

  /* Unmount merged (lazy) */
  umount2(merged, MNT_DETACH);

  /* Unmount base tmpfs (lazy) */
  umount2(cfg->volatile_dir, MNT_DETACH);

  /* Remove directories */
  remove_recursive(cfg->volatile_dir);

  return 0;
}

int setup_custom_binds(struct ds_config *cfg, const char *rootfs) {
  if (cfg->bind_count == 0)
    return 0;

  ds_log("Setting up %d custom bind mount(s)...", cfg->bind_count);

  for (int i = 0; i < cfg->bind_count; i++) {
    char tgt[PATH_MAX];
    snprintf(tgt, sizeof(tgt), "%s%s", rootfs, cfg->binds[i].dest);

    /* Check if source exists on host */
    if (access(cfg->binds[i].src, F_OK) != 0) {
      ds_warn("Skip bind mount: source path not found on host: %s",
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

    /* Perform bind mount */
    if (bind_mount(cfg->binds[i].src, tgt) < 0) {
      ds_warn("Failed to bind mount %s on %s (skipping)", cfg->binds[i].src,
              tgt);
      continue;
    }
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Rootfs Image Handling
 * ---------------------------------------------------------------------------*/

int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size,
                     int readonly, const char *name) {
  if (find_available_mountpoint(name, mount_point, mp_size) < 0) {
    ds_error("Failed to find available mount point for %s", name);
    return -1;
  }

  /* Run e2fsck first if it's an ext image */
  char *e2fsck_argv[] = {"e2fsck", "-f", "-y", (char *)(uintptr_t)img_path,
                         NULL};
  if (run_command_quiet(e2fsck_argv) == 0) {
    ds_log("Image checked and repaired successfully.");
  }

  ds_log("Mounting rootfs image %s on %s...", img_path, mount_point);

  /* Mount via loop device */
  char *opts = readonly ? "loop,ro" : "loop";
  char *mount_argv[] = {"mount",     "-o", opts, (char *)(uintptr_t)img_path,
                        mount_point, NULL};
  if (run_command_quiet(mount_argv) != 0) {
    ds_error("Failed to mount image %s", img_path);
    return -1;
  }

  return 0;
}

int unmount_rootfs_img(const char *mount_point) {
  if (!mount_point || !mount_point[0])
    return 0;

  ds_log("Unmounting rootfs image from %s...", mount_point);

  /* 1. Try lazy unmount first (most reliable for detached loop mounts) */
  if (umount2(mount_point, MNT_DETACH) < 0) {
    /* Fallback to shell command with loop detach */
    char *umount_argv[] = {"umount", "-d", "-l", (char *)(uintptr_t)mount_point,
                           NULL};
    run_command_quiet(umount_argv);
  }

  /* 2. Give the kernel a moment to settle the unmount (critical for loop
   * devices) */
  usleep(100000); /* 100ms */

  /* 3. Try to remove the directory */
  if (rmdir(mount_point) < 0 && errno != ENOENT) {
    /* If it failed because it wasn't empty, it might still be a mountpoint or
     * contain files. Try one more time with a slightly longer wait. */
    usleep(200000);
    rmdir(mount_point);
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
