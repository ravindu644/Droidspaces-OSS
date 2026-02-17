/*
 * Droidspaces v3 — Mounting logic
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------*/

/* Check if a path is a mountpoint */
static int is_mountpoint(const char *path) {
  struct stat st1, st2;
  if (stat(path, &st1) < 0)
    return 0;

  char parent[PATH_MAX];
  snprintf(parent, sizeof(parent), "%s/..", path);
  if (stat(parent, &st2) < 0)
    return 0;

  return st1.st_dev != st2.st_dev;
}

/* Find available mount point in /mnt/Droidspaces/ (Universal) */
static int find_available_mountpoint(char *mount_path, size_t size) {
  const char *base_dir = DS_IMG_MOUNT_ROOT_UNIVERSAL;

  /* Create base directory if it doesn't exist */
  mkdir(base_dir, 0755);

  /* Try numbers 0-99 */
  for (int i = 0; i < DS_MAX_MOUNT_TRIES; i++) {
    snprintf(mount_path, size, "%s/%d", base_dir, i);

    /* Check if directory exists and is not a mountpoint */
    struct stat st;
    if (stat(mount_path, &st) < 0) {
      /* Directory doesn't exist, create it */
      if (mkdir(mount_path, 0755) == 0) {
        return 0;
      }
    } else if (S_ISDIR(st.st_mode) && !is_mountpoint(mount_path)) {
      /* Directory exists and is not mounted, use it */
      return 0;
    }
  }

  return -1; /* No available mount point */
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

  if (hw_access) {
    /* If hw_access is enabled, we mount host's devtmpfs.
     * WARNING: This is insecure but provides full hardware access. */
    return domount("devtmpfs", dev_path, "devtmpfs", MS_NOSUID | MS_NOEXEC,
                   NULL);
  } else {
    /* Secure isolated /dev using tmpfs */
    if (domount("none", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC,
                "size=4M,mode=755") < 0)
      return -1;

    /* Create minimal set of device nodes */
    return create_devices(rootfs);
  }
}

int create_devices(const char *rootfs) {
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
                 {"console", S_IFCHR | 0600, makedev(5, 1)},
                 {"ptmx", S_IFCHR | 0666, makedev(5, 2)},
                 {NULL, 0, 0}};

  char path[PATH_MAX];

  /* 1. Create standard devices */
  for (int i = 0; devices[i].name; i++) {
    snprintf(path, sizeof(path), "%s/dev/%s", rootfs, devices[i].name);
    if (mknod(path, devices[i].mode, devices[i].dev) < 0 && errno != EEXIST) {
      if (S_ISREG(devices[i].mode)) {
        write_file(path, "");
      } else {
        char host_path[PATH_MAX];
        snprintf(host_path, sizeof(host_path), "/dev/%s", devices[i].name);
        bind_mount(host_path, path);
      }
    }
  }

  /* 2. Create tty1...N nodes (mount targets for PTYs) */
  for (int i = 1; i <= DS_MAX_TTYS; i++) {
    snprintf(path, sizeof(path), "%s/dev/tty%d", rootfs, i);
    if (access(path, F_OK) != 0) {
      write_file(path, "");
    }
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

int setup_devpts(void) {
  mkdir("/dev/pts", 0755);
  /* Use newinstance flag to get private PTY namespace */
  return domount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
                 "newinstance,ptmxmode=0666,mode=0620,gid=5");
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

/* ---------------------------------------------------------------------------
 * Rootfs Image Handling
 * ---------------------------------------------------------------------------*/

int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size) {
  if (find_available_mountpoint(mount_point, mp_size) < 0) {
    ds_error("Failed to find available mount point in %s",
             DS_IMG_MOUNT_ROOT_UNIVERSAL);
    return -1;
  }

  ds_log("Mounting rootfs image %s on %s...", img_path, mount_point);

  /* Run e2fsck first if it's an ext image */
  char cmd[PATH_MAX + 64];
  snprintf(cmd, sizeof(cmd), "e2fsck -f -y %s >/dev/null 2>&1", img_path);
  if (system(cmd) == 0) {
    ds_log("Image checked and repaired successfully.");
  }

  /* Mount via loop device */
  snprintf(cmd, sizeof(cmd), "mount -o loop %s %s >/dev/null 2>&1", img_path,
           mount_point);
  if (system(cmd) != 0) {
    ds_error("Failed to mount image %s", img_path);
    return -1;
  }

  return 0;
}

int unmount_rootfs_img(const char *mount_point) {
  if (!mount_point || !mount_point[0])
    return 0;

  /* Try unmounting: prefer aggressive lazy unmount syscall first */
  if (umount2(mount_point, MNT_DETACH) < 0) {
    /* Fallback to standard umount via shell with loop detach flag */
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "umount -d -l %s 2>/dev/null", mount_point);
    system(cmd);
  }

  /* Try to remove the directory (will only succeed if empty) */
  rmdir(mount_point);
  return 0;
}
