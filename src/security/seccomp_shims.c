/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>

#include "security/internal.h"

/* Arch check, then these rules, then allow everything. Both shims below are
 * built from it. */
static int install_rules(const struct sock_filter *rules, size_t n_rules,
                         const char *what) {
  static const struct sock_filter base[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
#if defined(__aarch64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#elif defined(__arm__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__i386__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
#elif defined(__riscv) && __riscv_xlen == 64
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
  };
  size_t n_base = sizeof(base) / sizeof(base[0]);
  struct sock_filter filter[n_base + n_rules + 1];
  memcpy(filter, base, sizeof(base));
  memcpy(filter + n_base, rules, n_rules * sizeof(*rules));
  filter[n_base + n_rules] =
      (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  struct sock_fprog prog = {
      .len = (unsigned short)(n_base + n_rules + 1),
      .filter = filter,
  };

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    ds_warn("Failed to apply the %s seccomp filter: %s", what, strerror(errno));
    return -1;
  }
  return 0;
}

/* Kernels below 5.0 store Android FBE keys in the session keyring, and systemd
 * loses them when it creates its own keyring.  Return ENOSYS for keyctl so it
 * keeps using the one it inherited.  Nothing to do on newer kernels. */
int ds_seccomp_keyring_shim(int privileged_mask) {
  if (privileged_mask & DS_PRIV_NOSEC)
    return 0;
  int major = 0, minor = 0;
  get_kernel_version(&major, &minor);
  if (major >= 5)
    return 0;

  static const struct sock_filter keyring[] = {
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_keyctl, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
  };
  return install_rules(keyring, sizeof(keyring) / sizeof(keyring[0]),
                       "keyring");
}

/* 1 when the kernel has fsopen() and the rest of the 5.2 mount API, 0 when the
 * syscall does not exist, -1 when the probe failed for another reason, such as
 * running check without privileges.  Cached: once the shim below is in, the
 * same call answers EPERM, and the init arguments are built after that. */
int ds_kernel_has_mount_api(void) {
  static int cached = -2;
  if (cached == -2) {
    int fd = (int)syscall(__NR_fsopen, "tmpfs", 0);
    if (fd >= 0)
      close(fd);
    cached = fd >= 0 ? 1 : errno == ENOSYS ? 0 : -1;
  }
  return cached;
}

/* systemd carries its version in the name of its private library, such as
 * /usr/lib/systemd/libsystemd-shared-259.so; Debian keeps it under the
 * multiarch directory. 0 when none is found. Runs on the container root. */
static int systemd_version(void) {
  glob_t g = {0};
  glob("/usr/lib*/systemd/libsystemd-shared-*.so", 0, NULL, &g);
  glob("/usr/lib/*/systemd/libsystemd-shared-*.so", GLOB_APPEND, NULL, &g);
  int v = 0;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    const char *p = strstr(g.gl_pathv[i], "shared-");
    if (p && atoi(p + 7) > v)
      v = atoi(p + 7);
  }
  globfree(&g);
  return v;
}

/* One answer for the shim and for the LIBMOUNT_FORCE_MOUNT2 plumbing in
 * boot/init.c and ds_env_save, so the two cannot drift apart. Only a systemd
 * 259+ PID 1 needs it: older systemd falls back on ENOSYS by itself, and under
 * the filter a util-linux 2.39+ mount(8) that misses the variable fails, so the
 * filter must not go where nothing needs it. */
int ds_mount_api_shim_wanted(const struct ds_config *cfg) {
  return cfg->init_type == DS_INIT_SYSTEMD && !cfg->custom_init[0] &&
         !(cfg->privileged_mask & DS_PRIV_NOSEC) &&
         ds_kernel_has_mount_api() == 0 && systemd_version() >= 259;
}

/* systemd 259+ builds service credentials and private /proc instances with
 * fsopen(), fsmount() and move_mount().  On ENOSYS it gives up and the unit
 * fails; on EPERM it takes the fallbacks it has for unprivileged user
 * managers.  libmount is the reverse, it falls back to mount(2) only on
 * ENOSYS, so every unit and login shell under this PID 1 gets
 * LIBMOUNT_FORCE_MOUNT2=always (boot/init.c, ds_env_save).  A mount(8) behind
 * an environment reset, sudo's env_reset for one, misses it and fails; that is
 * the known ceiling.  mount_setattr() stays untouched, systemd handles its
 * ENOSYS correctly. */
int ds_seccomp_mount_api_shim(const struct ds_config *cfg) {
  if (!ds_mount_api_shim_wanted(cfg))
    return 0;

  static const struct sock_filter mount_api[] = {
      BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, __NR_open_tree, 0, 2),
      BPF_JUMP(BPF_JMP | BPF_JGT | BPF_K, __NR_fspick, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
  };
  if (install_rules(mount_api, sizeof(mount_api) / sizeof(mount_api[0]),
                    "mount API") < 0)
    return -1;
  ds_log("[SEC] Kernel has no fsopen(), answering EPERM for the new mount API "
         "so systemd falls back gracefully");
  return 0;
}
