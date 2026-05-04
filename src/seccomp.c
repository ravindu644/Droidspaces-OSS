/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/if_alg.h>
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
int ds_seccomp_apply_minimal(int hw_access, int privileged_mask) {
  (void)hw_access;
  static struct sock_filter filter[84];
  int curr = 0;

  /* 1. Validate Architecture */
  filter[curr++] = (struct sock_filter)BPF_STMT(
      BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
#if defined(__aarch64__)
  filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                AUDIT_ARCH_AARCH64, 1, 0);
#elif defined(__x86_64__)
  filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                AUDIT_ARCH_X86_64, 1, 0);
#elif defined(__arm__)
  filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                AUDIT_ARCH_ARM, 1, 0);
#elif defined(__i386__)
  filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                AUDIT_ARCH_I386, 1, 0);
#endif
  filter[curr++] =
      (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);

  /* 2. Load syscall number */
  filter[curr++] = (struct sock_filter)BPF_STMT(
      BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));

#if defined(__x86_64__)
  /* 3. Block x32 ABI */
  filter[curr++] =
      (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1);
  filter[curr++] =
      (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif

  if (!(privileged_mask & DS_PRIV_NOSEC)) {
    /* 4. Kernel module loading */
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_init_module, 0, 1);
    filter[curr++] =
        (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_finit_module, 0, 1);
    filter[curr++] =
        (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_delete_module, 0, 1);
    filter[curr++] =
        (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);

    /* 5. kexec */
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_kexec_load, 0, 1);
    filter[curr++] =
        (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#ifdef __NR_kexec_file_load
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_kexec_file_load, 0, 1);
    filter[curr++] =
        (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif

#ifdef __NR_clone3
    /* 6. Block clone3 */
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_clone3, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA));
#endif

    /* 7. unshare(CLONE_NEWUSER) */
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_unshare, 0, 4);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]));
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K,
                                                  0x10000000, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));

    /* 8. clone(CLONE_NEWUSER) */
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_clone, 0, 3);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]));
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K,
                                                  0x10000000, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));

    /*
     * 9. CVE-2026-31431 ("Copy Fail") - mitigation layer 2.
     *
     * Block socket(AF_ALG, ...) - the mandatory first step of the exploit.
     * AF_ALG == 38.  The filter must reload the syscall number after the
     * argument-inspecting unshare/clone blocks above (those leave the acc
     * pointing at args[0]).  Pattern mirrors the unshare handler above.
     *
     * Instruction budget: JEQ/socket(5) + JEQ/arg(4) = 6 insns.
     * filter[] was sized to 72 to accommodate this.
     */
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_socket, 0, 4);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]));
    filter[curr++] =
        (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_ALG, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
    /* Reload nr for any rules that follow this block. */
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));

    /*
     * 10. Mount hardening (standard mode only - gated by hw_access).
     * Rule: Block MS_REMOUNT without MS_RDONLY (prevent 'remount rw').
     */
    if (!hw_access) {
      filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                    __NR_mount, 0, 5);
      filter[curr++] = (struct sock_filter)BPF_STMT(
          BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[3]));
      /* If MS_REMOUNT not set -> allow (skip next 3) */
      filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K,
                                                    MS_REMOUNT, 0, 3);
      /* If MS_RDONLY set -> allow (skip next 1) */
      filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K,
                                                    MS_RDONLY, 1, 0);
      filter[curr++] = (struct sock_filter)BPF_STMT(
          BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
      /* Reload nr */
      filter[curr++] = (struct sock_filter)BPF_STMT(
          BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    }
  }

  /* Allow everything else */
  filter[curr++] =
      (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);

  struct sock_fprog prog = {
      .len = (unsigned short)curr,
      .filter = filter,
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
