/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Static status variables
 * ---------------------------------------------------------------------------*/

static int is_root = 0;

/* ---------------------------------------------------------------------------
 * Requirement checks
 * ---------------------------------------------------------------------------*/

static int check_root(void) {
  is_root = (getuid() == 0);
  return is_root;
}

static int check_ns(const char *name) {
  /* 1. Check kernel support via /proc */
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/self/ns/%s", name);
  int supported = (access(path, F_OK) == 0);

  /* 2. Functional check: requires root for Droidspaces namespaces */
  return (supported && is_root);
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

/* ---------------------------------------------------------------------------
 * Minimal check for 'start' (used internaly)
 * ---------------------------------------------------------------------------*/

int check_requirements(void) {
  if (!check_root())
    return -1;
  if (!check_ns("mnt"))
    return -1;
  if (!check_ns("pid"))
    return -1;
  if (!check_ns("uts"))
    return -1;
  if (!check_ns("ipc"))
    return -1;
  if (!check_pivot_root())
    return -1;

  /* Cgroup check (v1 or v2) */
  if (!check_cgroup_v1("devices") && !check_cgroup_v2()) {
    ds_error("Kernel missing required cgroup support (v1 devices or v2).");
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

  printf("  [%s%s%s] %s\n", c_sym, sym, C_RESET, name);
  if (!status) {
    printf("      " C_DIM "%s" C_RESET "\n", desc);
    if (strstr(name, "namespace") || strstr(name, "Root")) {
      if (!is_root)
        printf("      " C_YELLOW
               "(Note: Namespace checks require root privileges)" C_RESET "\n");
    }
  }
}

int check_requirements_detailed(void) {
  check_root();

  printf("\n" C_BOLD "Droidspaces v%s — Checking system requirements..." C_RESET
         "\n\n",
         DS_VERSION);

  /* MUST HAVE */
  printf(C_BOLD "[MUST HAVE]" C_RESET
                "\nThese features are required for Droidspaces to work:\n\n");

  print_ds_check("Root privileges",
                 "Running as root user (required for container operations)",
                 is_root, "MUST");

  print_ds_check("PID namespace", "Process ID namespace isolation",
                 check_ns("pid"), "MUST");

  print_ds_check("Mount namespace", "Filesystem namespace isolation",
                 check_ns("mnt"), "MUST");

  print_ds_check("UTS namespace", "Hostname/domainname isolation",
                 check_ns("uts"), "MUST");

  print_ds_check("IPC namespace", "Inter-process communication isolation",
                 check_ns("ipc"), "MUST");

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

  /* RECOMMENDED */
  printf("\n" C_BOLD "[RECOMMENDED]" C_RESET
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
  printf("\n" C_BOLD "[OPTIONAL]" C_RESET
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

  /* FINAL SUMMARY */
  int missing_must = 0;
  if (!is_root)
    missing_must++;
  if (!check_ns("mnt"))
    missing_must++;
  if (!check_ns("pid"))
    missing_must++;
  if (!check_ns("uts"))
    missing_must++;
  if (!check_ns("ipc"))
    missing_must++;
  if (access("/dev/null", F_OK) != 0)
    missing_must++;
  if (!(check_cgroup_v1("devices") || check_cgroup_v2()))
    missing_must++;
  if (!check_pivot_root())
    missing_must++;

  printf("\n" C_BOLD "Summary:" C_RESET "\n");
  if (missing_must > 0)
    printf("  [" C_RED "✗" C_RESET
           "] %d required feature(s) missing - Droidspaces will not work\n",
           missing_must);
  else
    printf("  [" C_GREEN "✓" C_RESET "] All required features found!\n");

  if (!is_root) {
    printf(C_YELLOW "\n[!] Warning: You are not root. Some checks may be "
                    "inaccurate.\n" C_RESET);
  }
  printf("\n");

  return 0;
}
