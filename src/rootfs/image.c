/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/loop.h>

/* Forward declarations for loop helpers used in find_available_mountpoint */
static void loop_detach(const char *loop_dev);
static int get_backing_dev(const char *mnt, char *dev_out, size_t dev_size);

/* Where a container's image is mounted, if it is mounted at all. The path is a
 * function of the name alone, so nothing has to record it: a stop, a restart or
 * a scan asks is_mountpoint() on it. */
int ds_img_mount_point(const char *name, char *buf, size_t size) {
  /* An empty or dotted name would resolve to the mount root itself, which
   * cleanup then force-detaches. */
  if (!validate_container_name(name))
    return -1;
  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));
  int r = snprintf(buf, size, "%s/%s", DS_IMG_MOUNT_ROOT_UNIVERSAL, safe_name);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* Find available mount point in /mnt/Droidspaces/ using container name.
 * If a mount point already exists for this name but is not associated
 * with an active container (stale), it will be cleaned up. */
static int find_available_mountpoint(const char *name, char *mount_path,
                                     size_t size) {
  /* Create base directory if it doesn't exist */
  mkdir(DS_IMG_MOUNT_ROOT_UNIVERSAL, 0755);

  if (ds_img_mount_point(name, mount_path, size) < 0)
    return -1;

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

/* Rootfs Image Handling - Pure C loop device management (no host tools) */

/* Probe superblock magic bytes to identify the filesystem type. */
/* sb receives the 1 KiB ext superblock (at offset 1 KiB), which is also
 * where ext4_needs_fsck() looks, so the image is read once for both. */
static const char *detect_fs_type(const char *img_path,
                                  unsigned char sb[1024]) {
  int fd = open(img_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return NULL;

  uint8_t buf[8];
  const char *result = NULL;

  /* superblock offset 0x38: ext2/3/4 magic (0xEF53 LE) */
  if (pread(fd, sb, 1024, 1024) == 1024 && sb[0x38] == 0x53 &&
      sb[0x39] == 0xEF) {
    result = "ext4";
    goto out;
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

/* Qualcomm kernels with the old PFK inline crypto glue (4.9 on SM-T295) find
 * a bio's key from its page, and loop direct I/O hands ext4 the loop's own
 * page cache pages, so ICE skips the key and the loop reads raw ciphertext
 * (and would write plaintext). Read the head of the image through the loop's
 * page cache, as a mount would, and compare it with the file. O_DIRECT here
 * would hide the bug: anonymous pages take PFK's working path. 72 KiB covers
 * the ext4 superblock and group descriptors and the btrfs superblock. */
static int loop_dio_reads_back(int loop_fd, const char *img_path) {
  enum { LEN = 72 * 1024 };
  int ok = 0, img_fd = open(img_path, O_RDONLY | O_CLOEXEC);
  unsigned char *a = malloc(LEN), *b = malloc(LEN);
  if (img_fd >= 0 && a && b && pread(loop_fd, a, LEN, 0) == LEN &&
      pread(img_fd, b, LEN, 0) == LEN && memcmp(a, b, LEN) == 0) {
    /* An all-zero head proves nothing, so it fails closed */
    for (int i = 0; i < LEN && !ok; i++)
      ok = b[i] != 0;
  }
  if (img_fd >= 0)
    close(img_fd);
  free(a);
  free(b);
  return ok;
}

/*
 * Attach img_path to a free loop device via ioctls.
 * Sets LO_FLAGS_AUTOCLEAR so the kernel auto-releases the loop after umount.
 * Returns the open loop_fd on success (caller must close after mount()).
 * loop_path_out is filled with the device node path for the mount() call.
 */
static int loop_attach(const char *img_path, char *loop_path_out,
                       size_t path_size, unsigned blksize) {
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

  /* Buffered, the loop device's single worker blocks on every page cache
   * miss, so a queue of 16 random reads is served one at a time: 4.7k IOPS
   * cold against 63k with direct I/O on the same image, which hands each
   * request to the backing filesystem asynchronously. The kernel refuses DIO
   * while the loop's sector size is below the backing disk's (4 KiB on UFS),
   * so raise it to the guest filesystem's block size first. Both ioctls are
   * best effort, kernels without them keep the buffered path. blksize 0 asks
   * for that path outright. */
  if (!blksize)
    return loop_fd;
  ioctl(loop_fd, LOOP_SET_BLOCK_SIZE, (unsigned long)blksize);
  if (ioctl(loop_fd, LOOP_SET_DIRECT_IO, 1UL) < 0) {
    ds_log("[DEBUG] Loop direct I/O unavailable for %s: %s", img_path,
           strerror(errno));
    return loop_fd;
  }
  if (loop_dio_reads_back(loop_fd, img_path))
    return loop_fd;

  /* A fresh loop, so none of the bad pages stay cached on this one */
  ds_log("[DEBUG] Loop direct I/O reads %s back wrong, attaching buffered",
         img_path);
  ioctl(loop_fd, LOOP_CLR_FD, 0);
  close(loop_fd);
  return loop_attach(img_path, loop_path_out, path_size, 0);
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

/* The questions e2fsck asks before it decides a filesystem needs no pass
 * (e2fsck/unix.c, check_if_skip), answered from the superblock, so a clean
 * image skips the fork and the dynamic linker as well. */
static int ext4_needs_fsck(const unsigned char sb[1024]) {
#define SB16(off) ((uint16_t)sb[off] | (uint16_t)sb[(off) + 1] << 8)
#define SB32(off) ((uint32_t)SB16(off) | (uint32_t)SB16((off) + 2) << 16)
  uint16_t mnt_count = SB16(0x34), state = SB16(0x3a);
  int16_t max_mnt_count = (int16_t)SB16(0x36);
  uint32_t lastcheck = SB32(0x40), checkinterval = SB32(0x44);
  uint32_t incompat = SB32(0x60), ro_compat = SB32(0x64);
#undef SB16
#undef SB32

  if (!(state & 1) || (state & 2)) /* not EXT2_VALID_FS, or EXT2_ERROR_FS */
    return 1;
  if (incompat & 0x4) /* RECOVER: the journal has entries to replay */
    return 1;
  if (ro_compat & 0x10000) /* ORPHAN_PRESENT: the orphan file has entries */
    return 1;
  if (max_mnt_count > 0 && mnt_count >= (uint16_t)max_mnt_count)
    return 1;
  if (checkinterval && (uint32_t)time(NULL) - lastcheck >= checkinterval)
    return 1;
  return 0;
}

int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size,
                     const char *name) {
  if (find_available_mountpoint(name, mount_point, mp_size) < 0) {
    ds_error("Failed to find available mount point for %s", name);
    return -1;
  }

  /* Detect filesystem type from superblock magic */
  unsigned char sb[1024];
  const char *fstype = detect_fs_type(img_path, sb);
  if (!fstype) {
    ds_warn("Unknown filesystem in %s. Only ext4 and btrfs are supported.",
            img_path);
    return -1;
  }

  /* e2fsck: only for ext images, and only when the superblock says it has
   * something to do. No -f: e2fsck replays the journal when it must and runs
   * a full repair only when the fs is marked unclean or in error. */
  if (strcmp(fstype, "ext4") == 0) {
    if (ext4_needs_fsck(sb)) {
      char *e2fsck_argv[] = {"e2fsck", "-y", (char *)(uintptr_t)img_path, NULL};
      if (run_command_quiet(e2fsck_argv) == 0)
        ds_log("Image checked and repaired successfully.");
    }
  }

  /* Set SELinux context via xattr directly instead of spawning chcon */
  if (is_android())
    set_selinux_context(img_path, DS_ANDROID_VOLD_CONTEXT);

  /*
   * Build mount flags: base VFS flags + any fstype-specific extras.
   * pivot_root requires a writable mount to create .old_root, so no MS_RDONLY.
   */
  unsigned long mnt_flags = MS_NOATIME | MS_NODIRATIME;
  const char *mnt_data = NULL;

  /* Loop sector size for direct I/O. It must not exceed the filesystem's
   * block size or ext4 refuses to mount; mkfs gives images under 512 MiB
   * 1 KiB blocks (s_log_block_size at 0x18). */
  unsigned blksize = 4096;
  if (strcmp(fstype, "ext4") == 0 && sb[0x18] < 2)
    blksize = 1024u << sb[0x18];

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
      loop_fd = loop_attach(img_path, final_src, sizeof(final_src), blksize);
      if (loop_fd < 0)
        goto retry;
    }

    int ret = mount(final_src, mount_point, fstype, mnt_flags, mnt_data);
    int mount_errno = errno;
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
    ds_warn("mount(%s, %s) failed: %s", final_src, fstype,
            strerror(mount_errno));

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

  /* 1. Lazy unmount: detaches the mount even if files are open. The kernel
   * writes the filesystem back as the last reference goes, and the stop path
   * has already run sync() before getting here. */
  umount2(mount_point, MNT_DETACH);

  /* 2. Explicitly detach loop device (AUTOCLEAR also handles this, but be safe)
   */
  if (loop_dev[0])
    loop_detach(loop_dev);

  /* 3. A mount still there after MNT_DETACH (stubborn old kernels) gets a
   * settle and a forced detach. The usual case pays nothing. */
  if (is_mountpoint(mount_point)) {
    sync();
    usleep(DS_RETRY_DELAY_US);
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
