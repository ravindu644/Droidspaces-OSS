/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <sys/xattr.h>

int get_selinux_context(const char *path, char *buf, size_t size) {
  if (!path || !buf || size == 0)
    return -1;

  ssize_t len = lgetxattr(path, "security.selinux", buf, size - 1);
#ifdef SYS_lgetxattr
  if (len < 0)
    len = syscall(SYS_lgetxattr, path, "security.selinux", buf, size - 1);
#endif
  if (len < 0 || len >= (ssize_t)(size - 1))
    return -1;

  buf[len] = '\0';
  return 0;
}

/* Transition self into u:r:droidspacesd:s0.
 * Best-effort: if the write fails (policy not loaded, permissive host),
 * the caller inherits whatever domain the root process already holds.
 * No MLS suffix -- droidspacesd is declared permissive in sepolicy.rule. */
void ds_selinux_enter_domain(void) {
  const char *ctx = "u:r:droidspacesd:s0";
  int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return;
  if (write(fd, ctx, strlen(ctx) + 1) < 0) { /* ignore -- best effort */
  }
  close(fd);
}

int ds_get_selinux_status(void) {
  char buf[16];
  if (read_file("/sys/fs/selinux/enforce", buf, sizeof(buf)) < 0)
    return -1;
  return atoi(buf);
}

void ds_set_selinux_permissive(int enable) {
  int status = ds_get_selinux_status();
  if (status == -1) {
    if (enable)
      ds_warn("SELinux not supported or interface missing. Skipping permissive "
              "mode.");
    return;
  }

  if (enable) {
    if (status == 1) {
      ds_log("Setting SELinux to permissive...");
      if (write_file("/sys/fs/selinux/enforce", "0") < 0) {
        /* Try setenforce command as fallback */
        char *args[] = {"setenforce", "0", NULL};
        run_command_quiet(args);
      }
    }
  } else {
    /* Set back to Enforcing if it's currently Permissive */
    if (status == 0) {
      if (write_file("/sys/fs/selinux/enforce", "1") < 0) {
        char *args[] = {"setenforce", "1", NULL};
        run_command_quiet(args);
      }
    }
  }
}

int set_selinux_context(const char *path, const char *context) {
  if (!path || !context)
    return -1;

  size_t len = strlen(context);
  if (lsetxattr(path, "security.selinux", context, len, 0) < 0) {
#ifdef SYS_lsetxattr
    if (syscall(SYS_lsetxattr, path, "security.selinux", context, len, 0) < 0) {
      return -1;
    }
#else
    return -1;
#endif
  }

  return 0;
}
