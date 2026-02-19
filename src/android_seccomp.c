/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>

/* ---------------------------------------------------------------------------
 * Android System Call Filtering (Seccomp)
 * ---------------------------------------------------------------------------*/

/**
 * android_seccomp_setup() - Apply Seccomp filter for Android compatibility.
 *
 * This function applies a Seccomp BPF filter to intercept and modify the
 * behavior of specific system calls that are known to cause issues on Android:
 *
 * 1. Keyring Management (keyctl, add_key, request_key):
 *    Returns ENOSYS to prevent systemd from creating new session keyrings.
 *    This is necessary on Android hosts with File-Based Encryption (FBE) to
 *    ensure that the container process chain retains the necessary encryption
 *    keys for filesystem access.
 *
 * 2. Sandboxing / Isolation (unshare, clone):
 *    Returns EPERM when specific isolation flags (e.g., CLONE_NEWNS) are
 *    requested. On some Android kernels (notably 4.14), these isolation
 * features can trigger VFS deadlocks in the kernel during mount operations. By
 * returning EPERM, child processes (like systemd units) gracefully fall back to
 * the main container namespace.
 */
int android_seccomp_setup(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) < 0)
    return -1;

  /* Modern kernels (5.0+) are stable enough for systemd sandboxing and
   * do not suffer from the keyring/FBE deadlock bugs observed on 4.14.
   * We skip filtering here to ensure full compatibility with advanced
   * container features like Docker. */
  if (major >= 5)
    return 0;

  ds_log(
      "Legacy kernel detected (%d.%d): Applying compatibility Seccomp shield.",
      major, minor);

  /* Namespace flags to filter on legacy kernels:
   * CLONE_NEWNS(0x20000), CLONE_NEWUTS(0x04000000), CLONE_NEWIPC(0x08000000),
   * CLONE_NEWUSER(0x10000000), CLONE_NEWPID(0x20000000),
   * CLONE_NEWNET(0x40000000), CLONE_NEWCGROUP(0x02000000) -> Mask: 0x7E020000
   */
  const uint32_t ns_mask = 0x7E020000;

  struct sock_filter filter[] = {
      /* [0] Load architecture */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),

  /* [1] Validate architecture */
#if defined(__aarch64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#elif defined(__arm__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__i386__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

      /* [2] Load syscall number */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

      /* [3] Filter Keyring Operations (ENOSYS) */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_keyctl, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_add_key, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_request_key, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),

      /* [4] Filter Sandboxing/Namespaces (EPERM if mask matches) */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_unshare, 1, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 3),

      /* [5] Flag Check for unshare/clone */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, ns_mask, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),

      /* [6] Default: Allow */
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };

  struct sock_fprog prog = {
      .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
      .filter = filter,
  };

  /* Set NO_NEW_PRIVS before applying seccomp filter */
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
    ds_warn("Failed to set PR_SET_NO_NEW_PRIVS: %s", strerror(errno));
  }

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    ds_warn("Failed to apply Android Seccomp filter: %s", strerror(errno));
    return -1;
  }

  return 0;
}
