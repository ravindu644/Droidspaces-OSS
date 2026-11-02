/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/*
 * Create a file node at dst (if absent), chown+chmod 0666, then bind-mount
 * src onto dst.  label is used in the warning message on failure.
 * Returns 0 on success, -1 on failure.
 */
int ds_bind_mount_socket(const char *src, const char *dst, uid_t uid,
                         const char *label) {
  /* O_NOFOLLOW: never create through, chown/chmod, or bind-mount over a symlink
   * planted at dst (which lives in the container's own /tmp).  Operate on the
   * opened inode via fchown/fchmod so there is no path-re-resolution race. */
  int fd = open(dst, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0666);
  if (fd >= 0) {
    if (fchown(fd, uid, uid) < 0) { /* ignore */
    }
    fchmod(fd, 0666);
    close(fd);
  } else {
    struct stat st;
    if (lstat(dst, &st) == 0 && S_ISLNK(st.st_mode)) {
      ds_warn("[%s] refusing to bind-mount socket: %s is a symlink", label,
              dst);
      return -1;
    }
  }
  if (mount(src, dst, NULL, MS_BIND, NULL) != 0) {
    ds_warn("[%s] failed to bind-mount socket: %s", label, strerror(errno));
    return -1;
  }
  return 0;
}

int ds_bridge_termux_socket(const char *leaf, const char *dst,
                            const char *env_key, const char *env_val,
                            const char *label) {
  char src[PATH_MAX];
  snprintf(src, sizeof(src), "%s/%s", DS_TERMUX_TMP_OLDROOT, leaf);

  struct stat st;
  if (stat(src, &st) != 0) {
    ds_warn("%s: socket not found at %s - skipping socket bridge", label, src);
    return 0;
  }

  if (ds_bind_mount_socket(src, dst, st.st_uid, label) < 0)
    return 0;

  ds_log("%s: socket bind-mounted into container", label);
  setenv(env_key, env_val, 1);
  return 0;
}
