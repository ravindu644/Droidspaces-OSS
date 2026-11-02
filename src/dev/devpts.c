/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

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

  /* The gid-less retry is LXC's, for a kernel that refuses the option. */
  const char *opts[] = {optbuf, "newinstance,ptmxmode=0666,mode=0620", NULL};

  for (int i = 0; opts[i]; i++) {
    if (domount("devpts", pts_path, "devpts", MS_NOSUID | MS_NOEXEC, opts[i]) ==
        0) {
      /* Setup /dev/ptmx to point to the new pts/ptmx */
      const char *ptmx_path = "/dev/ptmx";
      const char *pts_ptmx = "/dev/pts/ptmx";

      if (hw_access) {
        /* In HW access mode, /dev is the devtmpfs singleton and nothing in
         * it may be unlinked.  The ptmx underneath is the staged 5,2 node
         * create_devices() bound over the devtmpfs one; stack on top. */
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
