/*
 * Droidspaces v5 - High-performance Container Runtime
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
 * ds_seccomp_apply_minimal()
 *
 * Blocks direct host kernel takeover vectors (module loading, kexec).
 * Applied unconditionally to all kernels and all modes.
 */
int ds_seccomp_apply_minimal(int hw_access, int allow_user_ns) {
  (void)hw_access;
  struct sock_filter filter_base[] = {
      /* Validate architecture.
       * KILL on wrong arch - ALLOW was a bypass: on x86-64 a process can
       * invoke 32-bit syscalls via int 0x80, which the kernel reports as
       * AUDIT_ARCH_I386, causing the arch check to fail and the old ALLOW
       * to skip the entire filter. KILL_PROCESS closes that hole. */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
#if defined(__aarch64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#elif defined(__arm__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__i386__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS), /* wrong arch */

      /* Load syscall number */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

#if defined(__x86_64__)
      /* Block x32 ABI syscalls (nr >= 0x40000000).
       * On x86-64 the x32 ABI uses the same arch value (AUDIT_ARCH_X86_64)
       * but sets bit 30 in the syscall number. Without this check, a process
       * can invoke x32 variants of any blocked syscall (e.g. x32 kexec_load)
       * and bypass the filter entirely since the JEQ checks below only match
       * the native 64-bit numbers. */
      BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif

      /* Kernel module loading */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_init_module, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_finit_module, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_delete_module, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

      /* kexec */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_kexec_load, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#ifdef __NR_kexec_file_load
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_kexec_file_load, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif

#ifdef __NR_clone3
      /* Block clone3 to force fallback to clone. Seccomp cannot inspect
       * clone3's struct arguments (since it cannot dereference pointers),
       * making it a bypass vector for our clone/unshare flag filters. */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone3, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
#endif
  };

  struct sock_filter filter_userns[] = {
      /* unshare(CLONE_NEWUSER) - a new user namespace grants a full capability
       * set within it, enabling further kernel exploits.
       * Block the CLONE_NEWUSER flag only - systemd legitimately calls
       * unshare(CLONE_NEWNS | CLONE_NEWUTS | ...) and must not be affected.
       *
       * This is a kernel attack surface restriction and applies to ALL modes.
       *
       * Jump layout (4 instructions skipped by jf so nr stays in acc):
       *   jf=4 skips: LD args[0], JSET, RET EPERM, LD nr → lands at clone check
       *   JSET jf=1 skips: RET EPERM → lands at LD nr → falls to clone check */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_unshare, 0, 4),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x10000000 /* CLONE_NEWUSER */, 0,
               1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
      /* Reload syscall nr - reached by both "unshare without CLONE_NEWUSER"
       * (JSET jf=1) and falls through to the clone check below */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

      /* clone(CLONE_NEWUSER) - same attack via the clone() syscall path.
       *
       * Jump layout (3 instructions skipped by jf):
       *   jf=3 skips: LD args[0], JSET, RET EPERM → lands at ALLOW
       *   JSET jf=1 skips: RET EPERM → lands at ALLOW */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 3),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x10000000 /* CLONE_NEWUSER */, 0,
               1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
  };

  struct sock_filter filter_allow[] = {
      /* Allow everything else */
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };


  int filter_len = sizeof(filter_base) / sizeof(struct sock_filter);
  if (!allow_user_ns) filter_len += sizeof(filter_userns) / sizeof(struct sock_filter);
  filter_len += sizeof(filter_allow) / sizeof(struct sock_filter);

  struct sock_filter *final_filter = malloc(filter_len * sizeof(struct sock_filter));
  if (!final_filter) return -1;


  int curr = 0;
  memcpy(final_filter + curr, filter_base, sizeof(filter_base));
  curr += sizeof(filter_base) / sizeof(struct sock_filter);

  if (!allow_user_ns) {
    memcpy(final_filter + curr, filter_userns, sizeof(filter_userns));
    curr += sizeof(filter_userns) / sizeof(struct sock_filter);
  } else {
    ds_log("[SEC] User Namespaces explicitly allowed (--allow-user-ns). Security reduced.");
  }

  memcpy(final_filter + curr, filter_allow, sizeof(filter_allow));

  struct sock_fprog prog = {
      .len = (unsigned short)filter_len,
      .filter = final_filter,
  };

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    ds_warn("[SEC] Failed to apply minimal seccomp filter: %s",
            strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * android_seccomp_setup()
 *
 * Applies a seccomp BPF filter for Android compatibility.
 *
 * 1. Keyring compat (ENOSYS): Applied on legacy kernels (< 5.0) to avoid
 *    traversing missing systems.
 * 2. Deadlock Shield (EPERM): Blocks namespace creation (unshare/clone).
 *    Applied ONLY if block_nested_ns is true (manual override).
 */
int android_seccomp_setup(int is_systemd, int block_nested_ns) {
  (void)is_systemd;
  int major = 0, minor = 0;
  get_kernel_version(&major, &minor);

  /* ns_mask covers: CLONE_NEWNS|CLONE_NEWCGROUP|CLONE_NEWUTS|CLONE_NEWIPC|
   *                 CLONE_NEWUSER|CLONE_NEWPID|CLONE_NEWNET */
  const uint32_t ns_mask = 0x7E020000;

  if (!block_nested_ns && major >= 5) {
    return 0;
  }

  /* Define base filter (arch check + load nr) */
  struct sock_filter filter_base[] = {
      /* Same wrong-arch fix as ds_seccomp_apply_minimal: KILL on mismatch. */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
#if defined(__aarch64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#elif defined(__arm__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__i386__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS), /* wrong arch */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
  };

  struct sock_filter filter_keyring[] = {
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_keyctl, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA))};

  struct sock_filter filter_ns[] = {
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_unshare, 1, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 3),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, ns_mask, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA))};

  struct sock_filter filter_allow[] = {
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};

  /* Combine filters based on conditions */
  int filter_len = sizeof(filter_base) / sizeof(struct sock_filter);
  if (major < 5)
    filter_len += sizeof(filter_keyring) / sizeof(struct sock_filter);
  if (block_nested_ns)
    filter_len += sizeof(filter_ns) / sizeof(struct sock_filter);
  filter_len += sizeof(filter_allow) / sizeof(struct sock_filter);

  struct sock_filter *final_filter =
      malloc(filter_len * sizeof(struct sock_filter));
  if (!final_filter)
    return -1;

  int curr = 0;
  memcpy(final_filter + curr, filter_base, sizeof(filter_base));
  curr += sizeof(filter_base) / sizeof(struct sock_filter);

  if (major < 5) {
    memcpy(final_filter + curr, filter_keyring, sizeof(filter_keyring));
    curr += sizeof(filter_keyring) / sizeof(struct sock_filter);
  }

  if (block_nested_ns) {
    ds_log(
        "[SEC] --block-nested-namespaces: force blocking namespace syscalls.");
    memcpy(final_filter + curr, filter_ns, sizeof(filter_ns));
    curr += sizeof(filter_ns) / sizeof(struct sock_filter);
  }

  memcpy(final_filter + curr, filter_allow, sizeof(filter_allow));

  struct sock_fprog prog = {
      .len = (unsigned short)filter_len,
      .filter = final_filter,
  };

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    ds_warn("Failed to apply Seccomp filter: %s", strerror(errno));
    free(final_filter);
    return -1;
  }

  free(final_filter);
  return 0;
}
