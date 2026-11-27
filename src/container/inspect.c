/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <pwd.h>

/* The symlink target of <rootfs>/sbin/init, or the start of the file when it
 * is a real one (a wrapper script, or the init binary itself). 1 for a
 * symlink, 0 for a file, -1 when there is neither. */
static int read_sbin_init(const char *rootfs, char *buf, size_t size) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/sbin/init", rootfs) >= (int)sizeof(path))
    return -1;
  ssize_t len = readlink(path, buf, size - 1);
  if (len >= 0) {
    buf[len] = '\0';
    return 1;
  }
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
      read_file(path, buf, size) < 0)
    return -1;
  return 0;
}

/* A systemd binary where distros put it, or /sbin/init pointing at or
 * wrapping one (NixOS keeps the binary in the store). /nix/store alone proves
 * nothing: finit, openrc and runit live there too. */
int is_systemd_rootfs(const char *path) {
  static const char *const bins[] = {"/lib/systemd/systemd",
                                     "/usr/lib/systemd/systemd", "/bin/systemd",
                                     "/usr/bin/systemd"};
  char buf[4096];
  struct stat st;
  if (!path)
    return 0;
  for (size_t i = 0; i < sizeof(bins) / sizeof(bins[0]); i++) {
    if (snprintf(buf, sizeof(buf), "%s%s", path, bins[i]) >= (int)sizeof(buf))
      continue;
    if (stat(buf, &st) == 0 && S_ISREG(st.st_mode))
      return 1;
  }
  return read_sbin_init(path, buf, sizeof(buf)) >= 0 &&
         strstr(buf, "systemd") != NULL;
}

ds_init_type_t detect_container_init(const char *path) {
  static const struct {
    const char *bin;
    ds_init_type_t type;
  } inits[] = {
      {"/sbin/procd", DS_INIT_PROCD},
      {"/usr/sbin/procd", DS_INIT_PROCD},
      {"/sbin/openrc-init", DS_INIT_OPENRC},
      {"/usr/bin/openrc-init", DS_INIT_OPENRC},
      {"/sbin/openrc", DS_INIT_OPENRC},
      {"/sbin/runit", DS_INIT_RUNIT},
      {"/usr/bin/runit", DS_INIT_RUNIT},
      {"/bin/s6-svscan", DS_INIT_S6},
      {"/usr/bin/s6-svscan", DS_INIT_S6},
  };
  if (!path || path[0] == '\0')
    return DS_INIT_UNKNOWN;
  if (is_systemd_rootfs(path))
    return DS_INIT_SYSTEMD;

  char buf[4096];
  struct stat st;
  for (size_t i = 0; i < sizeof(inits) / sizeof(inits[0]); i++) {
    if (snprintf(buf, sizeof(buf), "%s%s", path, inits[i].bin) >=
        (int)sizeof(buf))
      continue;
    if (stat(buf, &st) == 0 && S_ISREG(st.st_mode))
      return inits[i].type;
  }

  /* The rest are told apart by /sbin/init itself. */
  int kind = read_sbin_init(path, buf, sizeof(buf));
  if (kind < 0)
    return DS_INIT_UNKNOWN;
  if (strstr(buf, "busybox"))
    return DS_INIT_BUSYBOX;
  if (kind == 1)
    return strstr(buf, "sysvinit") || strstr(buf, "init.sysv")
               ? DS_INIT_SYSVINIT
               : DS_INIT_UNKNOWN;
  /* A real file: a Nix wrapper for some other init stays unknown so the
   * caller can cope, anything else is sysvinit's own binary. */
  return strstr(buf, "/nix/store") ? DS_INIT_UNKNOWN : DS_INIT_SYSVINIT;
}

int get_user_shell(const char *user, char *shell_buf, size_t size) {
  /* Runs inside the container's mount namespace, so musl's getpwnam reads the
   * guest's /etc/passwd, and asks a guest nscd only for a user that file does
   * not have. */
  struct passwd *pw = user && user[0] ? getpwnam(user) : NULL;
  if (!pw || !pw->pw_shell || !pw->pw_shell[0] || !shell_buf || !size)
    return -1;
  safe_strncpy(shell_buf, pw->pw_shell, size);
  return 0;
}
