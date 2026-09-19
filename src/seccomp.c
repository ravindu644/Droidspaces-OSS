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

/* KernelSU container-escape hardening constants.
 *
 * KSU installs its [ksu_driver] anon fd via a kprobe on reboot(): when
 * reboot() is called with the magic pair (0xDEADBEEF, 0xCAFEBABE) it
 * returns -EINVAL (bad magic, as far as the kernel is concerned) but a
 * task_work callback installs the fd as a side effect.  That fd is the
 * only handle for issuing KSU ioctls, including GRANT_ROOT - the
 * container-escape primitive that installs full-root creds and disables
 * seccomp.  The ioctl below (only_root perm) sets TIF_KSU_DISABLE_ESCAPE_
 * WITH_ROOT on the calling thread so escape_with_root_profile() aborts. */
#define DS_KSU_INSTALL_MAGIC1 0xDEADBEEFu
#define DS_KSU_INSTALL_MAGIC2 0xCAFEBABEu
#define DS_KSU_IOCTL_DISABLE_ESCAPE_TO_ROOT _IO('K', 21)

/* Android System Call Filtering (Seccomp) */

/**
 * ds_seccomp_apply_minimal()
 *
 * Blocks direct host kernel takeover vectors (module loading, kexec).
 * Applied unconditionally to all kernels and all modes.
 */
int ds_seccomp_apply_minimal(int privileged_mask, int sandboxing_allowed) {
  /* noseccomp: skip everything, 32-bit binaries must work */
  if (privileged_mask & DS_PRIV_NOSEC)
    return 0;

  static struct sock_filter filter[74];
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
#elif defined(__riscv) && __riscv_xlen == 64
  filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                AUDIT_ARCH_RISCV64, 1, 0);
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

    if (!sandboxing_allowed) {
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
    }
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
    /* 9b. KernelSU magic-reboot fd-install suppression.
     *
     * KSU hooks reboot() via kprobe and, when invoked with the magic pair
     * (0xDEADBEEF, 0xCAFEBABE), installs an anonymous [ksu_driver] fd -
     * the only handle for KSU ioctls, including GRANT_ROOT, which installs
     * full-root creds and disables seccomp (the container-escape primitive).
     * Deny reboot() only when BOTH args match the KSU magic; every other
     * reboot() (already gated by CAP_SYS_BOOT in-kernel) is unaffected, and
     * wrong-magic reboot() already returns -EINVAL anyway.
     *
     * Pairs with ds_ksu_neutralize_root_escape(), which must run BEFORE this
     * filter is applied (it obtains its fd through this same magic reboot). */
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_reboot, 0, 6);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]));
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  DS_KSU_INSTALL_MAGIC1, 0, 3);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1]));
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  DS_KSU_INSTALL_MAGIC2, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
    /* Reload nr for any rules that follow this block. */
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    /*
     * 10. Block host clock modification syscalls.
     *
     * CAP_SYS_TIME dropped from the bounding set is insufficient: the kernel
     * checks the capability against the *initial* user namespace, and
     * Droidspaces containers run as real root without a user namespace, so
     * the check passes even after the bounding-set drop.  Seccomp is the
     * only reliable barrier.
     *
     * Blocked: settimeofday, adjtimex, clock_settime, clock_adjtime,
     *          clock_settime64 (32-bit ARM compat, ifdef-guarded).
     * TZ changes (/etc/localtime, TZ env) are pure userspace and unaffected.
     */
#ifdef __NR_settimeofday
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_settimeofday, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
#endif
#ifdef __NR_adjtimex
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_adjtimex, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
#endif
#ifdef __NR_clock_settime
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_clock_settime, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
#endif
#ifdef __NR_clock_adjtime
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_clock_adjtime, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
#endif
#ifdef __NR_clock_settime64
    filter[curr++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                  __NR_clock_settime64, 0, 1);
    filter[curr++] = (struct sock_filter)BPF_STMT(
        BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
#endif
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

void ds_ksu_neutralize_root_escape(void) {
  int fd = -1;
  /* KSU kprobes reboot(); with the magic pair it returns -EINVAL but a
   * task_work callback installs the [ksu_driver] fd.  On non-KSU kernels
   * this is just an -EINVAL reboot() and fd stays -1. */
  long ret = syscall(__NR_reboot, DS_KSU_INSTALL_MAGIC1, DS_KSU_INSTALL_MAGIC2,
                     0, &fd);
  (void)ret;
  if (fd < 0)
    return; /* KSU not present or no fd installed */

  /* Mark this thread so escape_with_root_profile() aborts for it
   * (KSU_IOCTL_DISABLE_ESCAPE_TO_ROOT, perm: only_root). */
  if (ioctl(fd, DS_KSU_IOCTL_DISABLE_ESCAPE_TO_ROOT, 0) == 0)
    ds_log("[SEC] escape_with_root disabled for pid %d", (int)getpid());
  /* fd is O_CLOEXEC, but drop it explicitly before any exec so it can
   * never be reused by a descendant for GRANT_ROOT. */
  close(fd);
}

/*
 * android_seccomp_setup()
 *
 * Kernels below 5.0 store Android FBE keys in the session keyring, and systemd
 * loses them when it creates its own keyring.  Return ENOSYS for keyctl so it
 * keeps using the one it inherited.  Nothing to do on newer kernels.
 */
int android_seccomp_setup(int privileged_mask) {
  if (privileged_mask & DS_PRIV_NOSEC)
    return 0;
  int major = 0, minor = 0;
  get_kernel_version(&major, &minor);
  if (major >= 5)
    return 0;

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
#elif defined(__riscv) && __riscv_xlen == 64
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS), /* wrong arch */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
  };

  struct sock_filter filter_keyring[] = {
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_keyctl, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA))};

  struct sock_filter filter_allow[] = {
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};

  int filter_len = sizeof(filter_base) / sizeof(struct sock_filter) +
                   sizeof(filter_keyring) / sizeof(struct sock_filter) +
                   sizeof(filter_allow) / sizeof(struct sock_filter);

  struct sock_filter *final_filter =
      malloc(filter_len * sizeof(struct sock_filter));
  if (!final_filter)
    return -1;

  int curr = 0;
  memcpy(final_filter + curr, filter_base, sizeof(filter_base));
  curr += sizeof(filter_base) / sizeof(struct sock_filter);

  memcpy(final_filter + curr, filter_keyring, sizeof(filter_keyring));
  curr += sizeof(filter_keyring) / sizeof(struct sock_filter);

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
