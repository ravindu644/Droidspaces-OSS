/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/seccomp.h>
#include <stdarg.h>
#include <sys/prctl.h>

/* ---------------------------------------------------------------------------
 * Static status variables
 * ---------------------------------------------------------------------------*/

static int is_root = 0;

/* ---------------------------------------------------------------------------
 * Output buffering (for one-shot terminal output)
 * ---------------------------------------------------------------------------*/

#define CHECK_BUF_SIZE 16384
static char check_buf[CHECK_BUF_SIZE];
static size_t check_buf_pos = 0;

static void check_append(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(check_buf + check_buf_pos, CHECK_BUF_SIZE - check_buf_pos,
                    fmt, args);
  va_end(args);

  if (n > 0) {
    if (check_buf_pos + n < CHECK_BUF_SIZE) {
      check_buf_pos += n;
    } else {
      check_buf_pos = CHECK_BUF_SIZE - 1; /* Truncate if full */
    }
  }
}

/* ---------------------------------------------------------------------------
 * Requirement checks
 * ---------------------------------------------------------------------------*/

static int check_root(void) {
  is_root = (getuid() == 0);
  return is_root;
}

static int check_ns(int flag, const char *name) {
  /* 1. Fast check for kernel support via /proc */
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/self/ns/%s", name);
  if (access(path, F_OK) != 0)
    return 0;

  /* 2. Functional check: Try to actually unshare.
   * We fork because unshare() affects the current process. */
  pid_t p = fork();
  if (p < 0)
    return 0;

  if (p == 0) {
    if (unshare(flag) < 0) {
      exit(1);
    }
    exit(0);
  }

  int status;
  waitpid(p, &status, 0);
  return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static int check_pivot_root(void) {
  struct statfs st;
  if (statfs("/", &st) < 0)
    return 0;
  /* pivot_root is not supported if the root is on ramfs/tmpfs (unless it's a
   * submount) */
  /* RAMFS_MAGIC = 0x858458f6, TMPFS_MAGIC = 0x01021994 */
  return (st.f_type != 0x858458f6);
}

static int check_loop(void) { return access("/dev/loop-control", F_OK) == 0; }

static int check_cgroup_v1(const char *sub) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", sub);
  return access(path, F_OK) == 0;
}

static int check_cgroup_v2(void) {
  return access("/sys/fs/cgroup/cgroup.controllers", F_OK) == 0 ||
         grep_file("/proc/mounts", "cgroup2");
}

static int check_seccomp(void) {
  /* Probe for SECCOMP_MODE_FILTER support */
  return (prctl(PR_GET_SECCOMP, 0, 0, 0, 0) >= 0 || errno == EINVAL);
}

static int check_kernel_version_supported(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) < 0)
    return 0;
  if (major < DS_MIN_KERNEL_MAJOR)
    return 0;
  if (major == DS_MIN_KERNEL_MAJOR && minor < DS_MIN_KERNEL_MINOR)
    return 0;
  return 1;
}

/* ---------------------------------------------------------------------------
 * Minimal check for 'start' (used internaly)
 * ---------------------------------------------------------------------------*/

int check_requirements(void) {
  int missing = 0;

  if (!check_root()) {
    ds_error("Must be run as root");
    ds_log("This tool requires root privileges for namespace and mount "
           "operations.");
    missing++;
  }

  if (grep_file("/proc/filesystems", "devtmpfs") == 0) {
    ds_error("devtmpfs is not supported by the kernel");
    ds_log("This is a REQUIRED feature for hardware node management.");
    missing++;
  }

  /* Functional namespace checks */
  if (!check_ns(CLONE_NEWNS, "mnt")) {
    ds_error("Mount namespace is not supported by the kernel");
    ds_log("This is a REQUIRED feature for filesystem isolation.");
    missing++;
  }
  if (!check_ns(CLONE_NEWPID, "pid")) {
    ds_error("PID namespace is not supported by the kernel");
    ds_log("This is a REQUIRED feature for process isolation.");
    missing++;
  }
  if (!check_ns(CLONE_NEWUTS, "uts")) {
    ds_error("UTS namespace is not supported by the kernel");
    ds_log("This is a REQUIRED feature for hostname isolation.");
    missing++;
  }
  if (!check_ns(CLONE_NEWIPC, "ipc")) {
    ds_error("IPC namespace is not supported by the kernel");
    ds_log("This is a REQUIRED feature for IPC isolation.");
    missing++;
  }

  if (!check_pivot_root()) {
    ds_error("pivot_root syscall is not supported on the current filesystem");
    ds_log("Droidspaces requires a rootfs that supports pivot_root (not "
           "ramfs).");
    missing++;
  }

  /* Cgroup check (v1 or v2) */
  if (!check_cgroup_v1("devices") && !check_cgroup_v2()) {
    ds_error("Kernel missing required cgroup support (v1 devices or v2)");
    ds_log("systemd requires at least one of these for container management.");
    missing++;
  }

  if (!check_kernel_version_supported()) {
    ds_error("Kernel version is too old");
    ds_log("Droidspaces requires at least Linux %d.%d.0.", DS_MIN_KERNEL_MAJOR,
           DS_MIN_KERNEL_MINOR);
    missing++;
  }

  if (missing > 0) {
    printf("\n");
    ds_error("Missing %d required feature(s) - cannot proceed", missing);
    ds_log("Please run " C_BOLD "./droidspaces check" C_RESET
           " for a full diagnostic report.");
    return -1;
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Detailed 'check' command
 * ---------------------------------------------------------------------------*/

/* Helper to check and close an FD-based feature probe */
static int check_fd_feature(int fd) {
  if (fd >= 0) {
    close(fd);
    return 1;
  }
  return 0;
}

void print_ds_check(const char *name, const char *desc, int status,
                    const char *level) {
  const char *c_sym =
      status ? C_GREEN : (strcmp(level, "MUST") == 0 ? C_RED : C_YELLOW);
  const char *sym = status ? "✓" : "✗";

  check_append("  [%s%s%s] %s\n", c_sym, sym, C_RESET, name);
  if (!status) {
    check_append("      " C_DIM "%s" C_RESET "\n", desc);
    if (strstr(name, "namespace") || strstr(name, "Root")) {
      if (!is_root)
        check_append("      " C_YELLOW
                     "(Note: Namespace checks require root privileges)" C_RESET
                     "\n");
    }
  }
}

int check_requirements_detailed(void) {
  check_buf_pos = 0;
  check_buf[0] = '\0';

  check_root();

  check_append("\n" C_BOLD
               "Droidspaces v%s — Checking system requirements..." C_RESET
               "\n\n",
               DS_VERSION);

  /* MUST HAVE */
  check_append(C_BOLD
               "[MUST HAVE]" C_RESET
               "\nThese features are required for Droidspaces to work:\n\n");

  print_ds_check("Root privileges",
                 "Running as root user (required for container operations)",
                 is_root, "MUST");

  char kver_desc[128];
  snprintf(kver_desc, sizeof(kver_desc),
           "Linux kernel version %d.%d.0 or later", DS_MIN_KERNEL_MAJOR,
           DS_MIN_KERNEL_MINOR);
  print_ds_check("Linux version", kver_desc, check_kernel_version_supported(),
                 "MUST");

  print_ds_check("PID namespace", "Process ID namespace isolation",
                 check_ns(CLONE_NEWPID, "pid"), "MUST");

  print_ds_check("Mount namespace", "Filesystem namespace isolation",
                 check_ns(CLONE_NEWNS, "mnt"), "MUST");

  print_ds_check("UTS namespace", "Hostname/domainname isolation",
                 check_ns(CLONE_NEWUTS, "uts"), "MUST");

  print_ds_check("IPC namespace", "Inter-process communication isolation",
                 check_ns(CLONE_NEWIPC, "ipc"), "MUST");

  print_ds_check("devtmpfs support", "Kernel support for devtmpfs",
                 grep_file("/proc/filesystems", "devtmpfs"), "MUST");

  print_ds_check("cgroup support", "Control Groups (v1 or v2) support",
                 check_cgroup_v1("devices") || check_cgroup_v2(), "MUST");

  print_ds_check("pivot_root syscall",
                 "Kernel support for the pivot_root syscall",
                 check_pivot_root(), "MUST");

  print_ds_check("/proc filesystem", "Proc filesystem mount support",
                 access("/proc/self", F_OK) == 0, "MUST");

  print_ds_check("/sys filesystem", "Sys filesystem mount support",
                 access("/sys/kernel", F_OK) == 0, "MUST");

  print_ds_check("Seccomp support", "Kernel support for Seccomp (Bypass Mode)",
                 check_seccomp(), "MUST");

  /* RECOMMENDED */
  check_append("\n" C_BOLD "[RECOMMENDED]" C_RESET
               "\nThese features improve functionality but are not strictly "
               "required:\n\n");

  print_ds_check("epoll support", "Efficient I/O event notification",
                 check_fd_feature(epoll_create(1)), "OPT");

  sigset_t mask;
  sigemptyset(&mask);
  print_ds_check("signalfd support", "Signal handling via file descriptors",
                 check_fd_feature(signalfd(-1, &mask, 0)), "OPT");

  print_ds_check("PTY support", "Unix98 PTY support",
                 access("/dev/ptmx", F_OK) == 0, "OPT");

  print_ds_check("devpts support", "Virtual terminal filesystem support",
                 access("/dev/pts", F_OK) == 0, "OPT");

  print_ds_check("Loop device", "Required for rootfs.img mounting",
                 check_loop(), "OPT");

  print_ds_check("ext4 filesystem", "Ext4 filesystem support",
                 grep_file("/proc/filesystems", "ext4"), "OPT");

  /* OPTIONAL */
  check_append("\n" C_BOLD "[OPTIONAL]" C_RESET
               "\nThese features are optional and only used for specific "
               "functionality:\n\n");

  print_ds_check("IPv6 support", "IPv6 networking support",
                 access("/proc/sys/net/ipv6", F_OK) == 0, "OPT");
  print_ds_check("FUSE support", "Filesystem in Userspace support",
                 access("/dev/fuse", F_OK) == 0 ||
                     grep_file("/proc/filesystems", "fuse"),
                 "OPT");
  print_ds_check("TUN/TAP support", "Virtual network device support",
                 access("/dev/net/tun", F_OK) == 0, "OPT");
  print_ds_check("OverlayFS support", "Required for --volatile mode",
                 grep_file("/proc/filesystems", "overlay"), "OPT");

  /* FINAL SUMMARY */
  int missing_must = 0;
  if (!is_root)
    missing_must++;
  if (!check_ns(CLONE_NEWNS, "mnt"))
    missing_must++;
  if (!check_ns(CLONE_NEWPID, "pid"))
    missing_must++;
  if (!check_ns(CLONE_NEWUTS, "uts"))
    missing_must++;
  if (!check_ns(CLONE_NEWIPC, "ipc"))
    missing_must++;
  if (access("/dev/null", F_OK) != 0)
    missing_must++;
  if (!(check_cgroup_v1("devices") || check_cgroup_v2()))
    missing_must++;
  if (!check_pivot_root())
    missing_must++;
  if (!check_kernel_version_supported())
    missing_must++;

  check_append("\n" C_BOLD "Summary:" C_RESET "\n");
  if (missing_must > 0)
    check_append(
        "  [" C_RED "✗" C_RESET
        "] %d required feature(s) missing - Droidspaces will not work\n",
        missing_must);
  else
    check_append("  [" C_GREEN "✓" C_RESET "] All required features found!\n");

  if (!is_root) {
    check_append(C_BOLD C_YELLOW "\n[!] Warning: You are not root. Some checks "
                                 "may be inaccurate.\n" C_RESET);
  }
  check_append("\n");

  /* One-shot output to terminal */
  fwrite(check_buf, 1, check_buf_pos, stdout);
  fflush(stdout);

  return 0;
}
