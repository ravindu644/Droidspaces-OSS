/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#define DS_VTTY_COUNT                                                          \
  6 /* /dev/tty1..6: masked in hw mode, null device nodes otherwise */

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

/*
 * ds_stage_dev_node()
 *
 * Create a device node in the staging tmpfs and bind it over the same path in
 * the container /dev.  In --hw-access mode /dev is the kernel devtmpfs, one
 * superblock shared by every mount of it and, on Linux, the host's own /dev.
 * A bind changes only this mount namespace, so the singleton is never
 * written.  The one exception is the placeholder bind_mount() creates when the
 * target does not exist at all, which only happens on Android where nothing
 * else mounts devtmpfs.
 */
int ds_stage_dev_node(const char *staging, const char *dev_dir, const char *rel,
                      mode_t mode, dev_t dev, gid_t gid) {
  char src[PATH_MAX], tgt[PATH_MAX];
  snprintf(src, sizeof(src), "%s/%s", staging, rel);
  snprintf(tgt, sizeof(tgt), "%s/%s", dev_dir, rel);

  const char *slash = strrchr(rel, '/');
  if (slash) {
    char parent[PATH_MAX];
    int len = (int)(slash - rel);
    snprintf(parent, sizeof(parent), "%s/%.*s", staging, len, rel);
    mkdir_p(parent, 0755);
    snprintf(parent, sizeof(parent), "%s/%.*s", dev_dir, len, rel);
    mkdir(parent, 0755);
  }

  if (mknod(src, mode, dev) < 0) {
    if (errno == EEXIST)
      return 0; /* reached twice by the GPU scan, already bound */
    ds_warn("mknod %s: %s", src, strerror(errno));
    return -1;
  }
  chmod(src, mode & 0777);
  if (chown(src, 0, gid) < 0)
    ds_warn("chown %s: %s", src, strerror(errno));

  if (bind_mount(src, tgt) < 0) {
    ds_warn("Failed to bind %s over %s: %s", src, tgt, strerror(errno));
    return -1;
  }
  return 0;
}

/* /dev setup */

int setup_dev(const char *rootfs, int hw_access, int gpu_mode, int allow_vts) {
  char dev_path[PATH_MAX];
  snprintf(dev_path, sizeof(dev_path), "%s/dev", rootfs);

  /* Ensure the directory exists */
  mkdir(dev_path, 0755);

  if (hw_access) {
    /* The kernel devtmpfs is one superblock shared by every mount of it, and
     * on Linux it is the host's live /dev.  We never write into it: every
     * node we need is created in a throwaway tmpfs and bound over the
     * devtmpfs path.  Binds hold their own reference, so the staging mount is
     * detached as soon as they exist and the container never sees it. */
    if (domount("devtmpfs", dev_path, "devtmpfs", MS_NOSUID | MS_NOEXEC,
                "mode=755") == 0) {
      /* No MS_NODEV here: a bind inherits the source mount's flags and a
       * nodev /dev/null cannot be opened.  The rootfs /run directory already
       * exists (boot step 7) and gets its real tmpfs at step 12. */
      char staging[PATH_MAX];
      snprintf(staging, sizeof(staging), "%s/run", rootfs);
      if (domount("tmpfs", staging, "tmpfs", MS_NOSUID | MS_NOEXEC,
                  "size=64k,mode=755") < 0)
        return -1;

      int r = create_devices(rootfs, staging);

      /* The devtmpfs carries the host's real VTs.  A systemd container will
       * start getty@tty1 and logind spawns autovt@ on tty2-6 (NAutoVTs=6),
       * which lands its login prompts on the host console.  Mask tty1-6
       * with a 1:3 (null) node each unless the user asked for the VTs.  One
       * node per VT, not a re-bind of the staged null: agetty fchmods its
       * tty to 0620 root:tty and a shared inode would take /dev/null with it.
       */
      if (!allow_vts) {
        char tty[16];
        for (int i = 1; i <= DS_VTTY_COUNT; i++) {
          snprintf(tty, sizeof(tty), "tty%d", i);
          ds_stage_dev_node(staging, dev_path, tty, S_IFCHR | 0666,
                            makedev(1, 3), 0);
        }
      }

      /* Android's devtmpfs lacks the nodes ueventd hand-made in its own
       * tmpfs /dev (kgsl-3d0, mali0, dri/renderD128). Fill them in. */
      mirror_gpu_nodes(dev_path, staging);
      umount2(staging, MNT_DETACH);
      return r;
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

    /* --gpu mode: mknod the known GPU nodes into our isolated tmpfs.  GPU
     * acceleration without exposing the host devtmpfs. */
    if (gpu_mode) {
      ds_log("[GPU] --gpu mode: mirroring host GPU nodes into isolated tmpfs");
      mirror_gpu_nodes(dev_path, NULL);
    }
  }

  return create_devices(rootfs, NULL);
}

/* Lay down the device nodes every init expects.  staging is the tmpfs to
 * create them in when /dev is the devtmpfs singleton (--hw-access), NULL when
 * /dev is our own tmpfs and nodes are created in place. */
int create_devices(const char *rootfs, const char *staging) {
  const struct {
    const char *name;
    mode_t mode;
    dev_t dev;
    gid_t gid;
  } devices[] = {{"null", S_IFCHR | 0666, makedev(1, 3), 0},
                 {"zero", S_IFCHR | 0666, makedev(1, 5), 0},
                 {"full", S_IFCHR | 0666, makedev(1, 7), 0},
                 {"random", S_IFCHR | 0666, makedev(1, 8), 0},
                 {"urandom", S_IFCHR | 0666, makedev(1, 9), 0},
                 {"tty", S_IFCHR | 0666, makedev(5, 0), DS_DEFAULT_TTY_GID},
                 {"console", S_IFCHR | 0620, makedev(5, 1), DS_DEFAULT_TTY_GID},
                 {"ptmx", S_IFCHR | 0666, makedev(5, 2), 0},
                 {"net/tun", S_IFCHR | 0666, makedev(10, 200), 0},
                 {"fuse", S_IFCHR | 0666, makedev(10, 229), 0},
                 {NULL, 0, 0, 0}};

  char dev_dir[PATH_MAX], path[PATH_MAX];
  snprintf(dev_dir, sizeof(dev_dir), "%s/dev", rootfs);

  for (int i = 0; devices[i].name; i++) {
    if (staging) {
      ds_stage_dev_node(staging, dev_dir, devices[i].name, devices[i].mode,
                        devices[i].dev, devices[i].gid);
      continue;
    }

    snprintf(path, sizeof(path), "%s/%s", dev_dir, devices[i].name);
    const char *slash = strrchr(devices[i].name, '/');
    if (slash) {
      char parent[PATH_MAX];
      snprintf(parent, sizeof(parent), "%s/%.*s", dev_dir,
               (int)(slash - devices[i].name), devices[i].name);
      mkdir(parent, 0755);
    }

    /* Always recreate: a node the rootfs shipped may carry host permissions
     * that break non-root users in the container. */
    force_unlink(path);
    if (mknod(path, devices[i].mode, devices[i].dev) < 0) {
      /* Fallback for environments where mknod is restricted */
      char host_path[PATH_MAX];
      snprintf(host_path, sizeof(host_path), "/dev/%s", devices[i].name);
      bind_mount(host_path, path);
    } else {
      chmod(path, devices[i].mode & 0777);
      if (devices[i].gid && chown(path, 0, devices[i].gid) < 0) {
        /* Ignore failure */
      }
    }
  }

  /* Private tmpfs only: tty1..N as their own 1:3 (null) nodes so non-systemd
   * inits (openrc, busybox) stop logging "can't open /dev/ttyN" and their
   * getty has a tty to own. Never symlinks to /dev/null: getty fchmods its
   * tty to 0620 root:tty, and through a symlink that lands on /dev/null
   * itself, the host's own node when it had to be bind-mounted above. Where
   * mknod is refused the VT stays absent, which only costs a log line. hw
   * mode masks the real VTs in setup_dev(). */
  for (int i = 1; !staging && i <= DS_VTTY_COUNT; i++) {
    snprintf(path, sizeof(path), "%s/tty%d", dev_dir, i);
    force_unlink(path);
    if (mknod(path, S_IFCHR | 0666, makedev(1, 3)) == 0)
      chmod(path, 0666); /* the umask of a direct start would land on it */
  }

  /* Standard symlinks.  A symlink cannot be bind-mounted, so in hw mode these
   * are created in the devtmpfs itself: on Linux they already exist (EEXIST),
   * on Android the singleton is private to us. */
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/fd", dev_dir);
  if (symlink("/proc/self/fd", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/fd symlink: %s", strerror(errno));

  snprintf(tgt, sizeof(tgt), "%s/stdin", dev_dir);
  if (symlink("/proc/self/fd/0", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/stdin symlink: %s", strerror(errno));

  snprintf(tgt, sizeof(tgt), "%s/stdout", dev_dir);
  if (symlink("/proc/self/fd/1", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/stdout symlink: %s", strerror(errno));

  snprintf(tgt, sizeof(tgt), "%s/stderr", dev_dir);
  if (symlink("/proc/self/fd/2", tgt) < 0 && errno != EEXIST)
    ds_warn("Failed to create /dev/stderr symlink: %s", strerror(errno));

  return 0;
}
