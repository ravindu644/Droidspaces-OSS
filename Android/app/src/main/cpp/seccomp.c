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

/* AUDIT_ARCH_RISCV64 was added to linux/audit.h in 4.15.  Older kernel
 * headers don't have it; fall back to the canonical value
 * (EM_RISCV | __AUDIT_ARCH_64BIT | __AUDIT_ARCH_LE). */
#ifndef AUDIT_ARCH_RISCV64
#define AUDIT_ARCH_RISCV64 0xC00000F3u
#endif

/* ---------------------------------------------------------------------------
 * Android System Call Filtering (Seccomp)
 * ---------------------------------------------------------------------------*/

/**
 * ds_seccomp_apply_minimal()
 *
 * Blocks direct host kernel takeover vectors (module loading, kexec).
 * Applied unconditionally to all kernels and all modes.
 */
int ds_seccomp_apply_minimal(int hw_access, int privileged_mask, int rootless) {
  /*
   * Dynamically growable BPF filter array.  The maximum number of
   * instructions across all arch/feature combinations is ~110 so the
   * initial allocation of 256 safely covers any current config and
   * leaves room for future rules without risking silent stack overflow.
   */
  size_t filter_cap = 256;
  struct sock_filter *filter = malloc(filter_cap * sizeof(struct sock_filter));
  if (!filter)
    return -1;
  int curr = 0;

#define DS_FILTER_APPEND(...)                                                  \
  do {                                                                         \
    if ((size_t)curr >= filter_cap) {                                          \
      size_t new_cap = filter_cap * 2;                                         \
      struct sock_filter *new_f =                                              \
          realloc(filter, new_cap * sizeof(struct sock_filter));               \
      if (!new_f) {                                                            \
        free(filter);                                                          \
        return -1;                                                             \
      }                                                                        \
      filter = new_f;                                                          \
      filter_cap = new_cap;                                                    \
    }                                                                          \
    filter[curr++] = (struct sock_filter)__VA_ARGS__;                          \
  } while (0)

  /* 1. Validate Architecture */
  DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                            offsetof(struct seccomp_data, arch)));
#if defined(__aarch64__)
  DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0));
#elif defined(__x86_64__)
  DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0));
#elif defined(__arm__)
  DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0));
#elif defined(__i386__)
  DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0));
#elif defined(__riscv) && __riscv_xlen == 64
  DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, 1, 0));
#endif
  DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

  /* 2. Load syscall number */
  DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                            offsetof(struct seccomp_data, nr)));

#if defined(__x86_64__)
  /* 3. Block x32 ABI */
  DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1));
  DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
#endif

  if (!(privileged_mask & DS_PRIV_NOSEC)) {
    /* 4. Kernel module loading */
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_init_module, 0, 1));
    DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_finit_module, 0, 1));
    DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_delete_module, 0, 1));
    DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

    /* 5. kexec */
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_kexec_load, 0, 1));
    DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
#ifdef __NR_kexec_file_load
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_kexec_file_load, 0, 1));
    DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
#endif

#ifdef __NR_clone3
    /* 6. Block clone3 */
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone3, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)));
#endif

    /* 7. unshare(CLONE_NEWUSER) — allowed in rootless mode */
    if (!rootless) {
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_unshare, 0, 4));
      DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                offsetof(struct seccomp_data, args[0])));
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x10000000, 0, 1));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));

      /* 8. clone(CLONE_NEWUSER) */
      DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 3));
      DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                offsetof(struct seccomp_data, args[0])));
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x10000000, 0, 1));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
    }

    /*
     * 9. CVE-2026-31431 ("Copy Fail") - mitigation layer 2.
     * Block socket(AF_ALG, ...).  Reload nr after arg-inspecting blocks above.
     */
    DS_FILTER_APPEND(
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 0, 4));
    DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                              offsetof(struct seccomp_data, args[0])));
    DS_FILTER_APPEND(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_ALG, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));

    /*
     * 10. Block host clock modification syscalls.
     * CAP_SYS_TIME bounding-set drop is insufficient without user namespaces.
     * Seccomp is the only reliable barrier.
     */
#ifdef __NR_settimeofday
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_settimeofday, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
#endif
#ifdef __NR_adjtimex
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_adjtimex, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
#endif
#ifdef __NR_clock_settime
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clock_settime, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
#endif
#ifdef __NR_clock_adjtime
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clock_adjtime, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
#endif
#ifdef __NR_clock_settime64
    DS_FILTER_APPEND(
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clock_settime64, 0, 1));
    DS_FILTER_APPEND(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
#endif

    if (!hw_access) {
      /*
       * 11. Block mknod/mknodat for block/char devices.
       * S_IFCHR | S_IFBLK both have bit 0x2000 set — a single BPF_JSET
       * catches both device types while allowing pipes, sockets, fifos.
       */
#ifdef __NR_mknod
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mknod, 0, 4));
      DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                offsetof(struct seccomp_data, args[1])));
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, S_IFCHR, 0, 1));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
#endif
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mknodat, 0, 4));
      DS_FILTER_APPEND(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                offsetof(struct seccomp_data, args[2])));
      DS_FILTER_APPEND(
          BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, S_IFCHR, 0, 1));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));
      DS_FILTER_APPEND(
          BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
    }
  }

  /* Allow everything else */
  DS_FILTER_APPEND(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

#undef DS_FILTER_APPEND

  struct sock_fprog prog = {
      .len = (unsigned short)curr,
      .filter = filter,
  };

  int ret = 0;
  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    ds_warn("[SEC] Failed to apply minimal seccomp filter: %s",
            strerror(errno));
    ret = -1;
  }

  free(filter);
  return ret;
}

int android_seccomp_setup(int is_systemd) {
  /* No pre-5.10 Android workarounds needed — keyring compat (pre-5.0)
   * and the 4.14.113 VFS deadlock shield are both irrelevant on 5.10+. */
  (void)is_systemd;
  return 0;
}
