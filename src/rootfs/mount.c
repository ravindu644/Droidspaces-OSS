/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* Helpers */

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

/* Generic mount wrappers */

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
  int src_fd = open(src, O_PATH | O_NOFOLLOW | O_CLOEXEC);
  if (src_fd < 0) {
    /* If it failed because of ELOOP, it's a symlink we should reject anyway */
    return -1;
  }

  struct stat st_src;
  if (fstat(src_fd, &st_src) < 0) {
    close(src_fd);
    return -1;
  }

  /* Reject symlinks explicitly */
  if (S_ISLNK(st_src.st_mode)) {
    close(src_fd);
    errno = ELOOP;
    return -1;
  }

  /* Create the target if it does not exist yet.  Use O_NOFOLLOW | O_EXCL for
   * the file case so a symlink planted at the final component (e.g. by a
   * malicious rootfs) is never followed or silently reused. */
  struct stat st_tgt;
  if (lstat(tgt, &st_tgt) < 0) {
    if (S_ISDIR(st_src.st_mode)) {
      if (mkdir(tgt, st_src.st_mode & 07777) < 0 && errno != EEXIST) {
        close(src_fd);
        return -1;
      }
      if (chown(tgt, st_src.st_uid, st_src.st_gid) < 0) {
        /* ignore chown failure, not critical for bind mount setup */
      }
    } else {
      int tf =
          open(tgt, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
      if (tf < 0 && errno != EEXIST) {
        close(src_fd);
        return -1;
      }
      if (tf >= 0)
        close(tf);
    }
  } else if (S_ISLNK(st_tgt.st_mode)) {
    ds_error("Security Violation: Bind target %s is a symlink!", tgt);
    close(src_fd);
    errno = ELOOP;
    return -1;
  }

  /* Re-open the target through O_PATH | O_NOFOLLOW and mount onto the resulting
   * descriptor via /proc/self/fd.  This pins the exact inode we validated, so
   * the classic lstat()->mount() swap race (target replaced by a symlink in
   * between) cannot redirect the bind outside the rootfs.  O_NOFOLLOW also
   * rejects a symlink at the final component here.  (A symlink in an
   * intermediate component is still resolved at open() time; fully closing
   * that needs openat2(RESOLVE_NO_SYMLINKS), which is unavailable on the older
   * kernels Droidspaces supports.  Custom binds get an additional is_subpath()
   * post-check in setup_custom_binds().) */
  int tgt_fd = open(tgt, O_PATH | O_NOFOLLOW | O_CLOEXEC);
  if (tgt_fd < 0) {
    close(src_fd);
    return -1;
  }

  char src_proc[64], tgt_proc[64];
  snprintf(src_proc, sizeof(src_proc), "/proc/self/fd/%d", src_fd);
  snprintf(tgt_proc, sizeof(tgt_proc), "/proc/self/fd/%d", tgt_fd);

  int res = domount(src_proc, tgt_proc, NULL, MS_BIND | MS_REC, NULL);
  close(src_fd);
  close(tgt_fd);
  return res;
}
