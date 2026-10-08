/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/seccomp.h>
#include <stdarg.h>
#include <sys/prctl.h>

/* Static status variables */

static int is_root = 0;
/* check --format: emit one JSON int per probe instead of the text report */
static int json_mode = 0;
static int json_first = 1;

/* Output buffering (for one-shot terminal output) */

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

/* Requirement checks */

static int check_root(void) {
  is_root = (getuid() == 0);
  return is_root;
}

int check_ns(int flag, const char *name) {
  /* The ns file is the kernel's own answer for CONFIG_<name>_NS. */
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/self/ns/%s", name);
  if (access(path, F_OK) != 0)
    return 0;

  /* A network namespace is not probed by creating one. Its teardown holds
   * net_mutex for dozens of RCU grace periods on kernels before 4.17, and the
   * real unshare(CLONE_NEWNET) a moment later waits for it: 400 ms on every
   * NAT, gateway or none start of a 4.14 phone. The real unshare fails loudly
   * if anything else is wrong. The other namespaces are cheap to tear down,
   * so they keep the functional test below. */
  if (flag == CLONE_NEWNET)
    return 1;

  /* Try to actually unshare, in a child because unshare() would change this
   * process. */
  pid_t p = fork();
  if (p < 0)
    return 0;

  if (p == 0) {
    if (unshare(flag) < 0) {
      _exit(1);
    }
    _exit(0);
  }

  int status;
  waitpid(p, &status, 0);
  return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static int check_pivot_root(void) {
  /* Probe for pivot_root syscall presence without actually executing it
   * with dangerous arguments. We check if the syscall is implemented
   * by passing invalid pointers (-1) or NULLs; if it returns ENOSYS,
   * it's missing. If it returns EFAULT or EINVAL, it exists. */
  if (syscall(__NR_pivot_root, NULL, NULL) < 0 && errno == ENOSYS)
    return 0;
  return 1;
}

static int check_loop(void) { return access("/dev/loop-control", F_OK) == 0; }

static int check_seccomp(void) {
  /* Probe for SECCOMP_MODE_FILTER support */
  return (prctl(PR_GET_SECCOMP, 0, 0, 0, 0) >= 0 || errno == EINVAL);
}

/* Live kernel probes for NAT networking capability
 *
 * Mirrors the logic in ds_nl_probe_nat_capability() but split into two
 * independent functions so check.c can report bridge and veth separately.
 *
 * Both probes attempt a real RTM_NEWLINK roundtrip and immediately clean up.
 * This is accurate even when modules are built-in (=y) rather than loadable
 * (=m), which is the common case on Android.
 *
 * Both require root to open a NETLINK_ROUTE socket - guarded early.
 * If a stale probe interface from a previous crashed session is present,
 * its existence already proves kernel support - treated as green. */
static int check_bridge_support(void) {
  if (!is_root)
    return 0;
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return 0;
  /* Stale probe interface from a previous crashed session → already proves
   * support */
  if (ds_nl_link_exists(ctx, "ds-cap-br0")) {
    ds_nl_close(ctx);
    return 1;
  }
  int ret = ds_nl_create_bridge(ctx, "ds-cap-br0");
  if (ret == 0)
    ds_nl_del_link(ctx, "ds-cap-br0");
  ds_nl_close(ctx);
  return (ret == 0);
}

static int check_veth_support(void) {
  if (!is_root)
    return 0;
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return 0;
  /* Stale host-side probe veth from a previous crashed session → already proves
   * support */
  if (ds_nl_link_exists(ctx, "ds-cap-h0")) {
    ds_nl_close(ctx);
    return 1;
  }
  int ret = ds_nl_create_veth(ctx, "ds-cap-h0", "ds-cap-p0");
  if (ret == 0)
    ds_nl_del_link(ctx,
                   "ds-cap-h0"); /* deleting host side also kills the peer */
  ds_nl_close(ctx);
  return (ret == 0);
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

/* Minimal check for 'start' (used internaly) */

int check_requirements(void) { return check_requirements_hw(0); }

int check_requirements_hw(int hw_access) {
  int missing = 0;

  if (!check_root()) {
    ds_error("Must be run as root");
    ds_log("This tool requires root privileges for namespace and mount "
           "operations.");
    missing++;
  }

  /* devtmpfs is only needed for --hw-access; without it we use tmpfs */
  if (hw_access && grep_file("/proc/filesystems", "devtmpfs") == 0) {
    ds_warn("Hardware access mode is active but this kernel does not support "
            "devtmpfs. GPU and hardware nodes may not be available.");
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

/* Detailed 'check' command */

/* Helper to check and close an FD-based feature probe */
static int check_fd_feature(int fd) {
  if (fd >= 0) {
    close(fd);
    return 1;
  }
  return 0;
}

void print_ds_check(const char *key, const char *name, const char *desc,
                    int status, const char *level) {
  if (json_mode) {
    ds_json_int(key, status, &json_first);
    return;
  }
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

int check_requirements_detailed(int format_output) {
  check_buf_pos = 0;
  check_buf[0] = '\0';

  /* In JSON mode the headers and summary still land in check_buf and are
   * simply never written; cheaper than guarding every check_append. */
  json_mode = format_output;
  json_first = 1;
  if (json_mode)
    printf("{");

  check_root();

  check_append("\n" C_BOLD
               "Droidspaces v%s - Checking system requirements..." C_RESET
               "\n\n",
               DS_VERSION);

  int missing_must = 0;

  /* MUST HAVE */
  check_append(C_BOLD
               "[MUST HAVE]" C_RESET
               "\nThese features are required for Droidspaces to work:\n\n");

  if (!is_root)
    missing_must++;
  print_ds_check("root", "Root privileges",
                 "Running as root user (required for container operations)",
                 is_root, "MUST");

  char kver_desc[128];
  snprintf(kver_desc, sizeof(kver_desc),
           "Linux kernel version %d.%d.0 or later", DS_MIN_KERNEL_MAJOR,
           DS_MIN_KERNEL_MINOR);
  int kver_ok = check_kernel_version_supported();
  if (!kver_ok)
    missing_must++;
  print_ds_check("kernel_version", "Linux version", kver_desc, kver_ok, "MUST");

  int has_pid_ns = check_ns(CLONE_NEWPID, "pid");
  if (!has_pid_ns)
    missing_must++;
  print_ds_check("pid_ns", "PID namespace", "Process ID namespace isolation",
                 has_pid_ns, "MUST");

  int has_mnt_ns = check_ns(CLONE_NEWNS, "mnt");
  if (!has_mnt_ns)
    missing_must++;
  print_ds_check("mnt_ns", "Mount namespace", "Filesystem namespace isolation",
                 has_mnt_ns, "MUST");

  int has_uts_ns = check_ns(CLONE_NEWUTS, "uts");
  if (!has_uts_ns)
    missing_must++;
  print_ds_check("uts_ns", "UTS namespace", "Hostname/domainname isolation",
                 has_uts_ns, "MUST");

  int has_ipc_ns = check_ns(CLONE_NEWIPC, "ipc");
  if (!has_ipc_ns)
    missing_must++;
  print_ds_check("ipc_ns", "IPC namespace",
                 "Inter-process communication isolation", has_ipc_ns, "MUST");

  int has_pivot = check_pivot_root();
  if (!has_pivot)
    missing_must++;
  print_ds_check("pivot_root", "pivot_root syscall",
                 "Kernel support for the pivot_root syscall", has_pivot,
                 "MUST");

  int has_proc_fs = access("/proc/self", F_OK) == 0;
  if (!has_proc_fs)
    missing_must++;
  print_ds_check("procfs", "/proc filesystem", "Proc filesystem mount support",
                 has_proc_fs, "MUST");

  int has_sys_fs = access("/sys/kernel", F_OK) == 0;
  if (!has_sys_fs)
    missing_must++;
  print_ds_check("sysfs", "/sys filesystem", "Sys filesystem mount support",
                 has_sys_fs, "MUST");

  int has_seccomp = check_seccomp();
  if (!has_seccomp)
    missing_must++;
  print_ds_check("seccomp", "Seccomp support",
                 "Kernel support for Seccomp (Bypass Mode)", has_seccomp,
                 "MUST");

  /* RECOMMENDED */
  check_append("\n" C_BOLD "[RECOMMENDED]" C_RESET
               "\nThese features improve functionality but are not strictly "
               "required:\n\n");

  print_ds_check("epoll", "epoll support", "Efficient I/O event notification",
                 check_fd_feature(epoll_create1(0)), "OPT");

  sigset_t mask;
  sigemptyset(&mask);
  print_ds_check("signalfd", "signalfd support",
                 "Signal handling via file descriptors",
                 check_fd_feature(signalfd(-1, &mask, 0)), "OPT");

  print_ds_check("pty", "PTY support", "Unix98 PTY support",
                 access("/dev/ptmx", F_OK) == 0, "OPT");

  print_ds_check("devpts", "devpts support",
                 "Virtual terminal filesystem support",
                 access("/dev/pts", F_OK) == 0, "OPT");

  print_ds_check("loop", "Loop device", "Required for rootfs.img mounting",
                 check_loop(), "OPT");

  print_ds_check("ext4", "ext4 filesystem", "Ext4 filesystem support",
                 grep_file("/proc/filesystems", "ext4"), "OPT");

  print_ds_check("cgroup2", "Cgroup v2 support",
                 "Unified Control Group hierarchy support",
                 grep_file("/proc/filesystems", "cgroup2"), "OPT");

  print_ds_check("cgroup_ns", "Cgroup namespace",
                 "Control Group namespace isolation",
                 check_ns(CLONE_NEWCGROUP, "cgroup"), "OPT");

  int has_devtmpfs = grep_file("/proc/filesystems", "devtmpfs");
  print_ds_check(
      "devtmpfs", "devtmpfs support",
      "Required for hardware access mode; tmpfs fallback used otherwise",
      has_devtmpfs, "OPT");

  /* OPTIONAL */
  check_append("\n" C_BOLD "[OPTIONAL]" C_RESET
               "\nThese features are optional and only used for specific "
               "functionality:\n\n");

  print_ds_check("fuse", "FUSE support", "Filesystem in Userspace support",
                 access("/dev/fuse", F_OK) == 0 ||
                     grep_file("/proc/filesystems", "fuse"),
                 "OPT");
  print_ds_check("tun", "TUN/TAP support", "Virtual network device support",
                 access("/dev/net/tun", F_OK) == 0, "OPT");
  print_ds_check("overlayfs", "OverlayFS support",
                 "Required for --volatile mode",
                 grep_file("/proc/filesystems", "overlay"), "OPT");
  print_ds_check("net_ns", "Network namespace",
                 "Network namespace isolation for --net=nat/none",
                 check_ns(CLONE_NEWNET, "net"), "OPT");
  print_ds_check("bridge", "Bridge device support",
                 "Required for --net=nat (bridge mode); bridgeless fallback "
                 "used if absent",
                 check_bridge_support(), "OPT");
  print_ds_check("veth", "Veth pair support",
                 "Required for --net=nat; no fallback exists if absent",
                 check_veth_support(), "OPT");
  /* An IPv6 stack alone is not enough for NAT mode, which also needs the
   * IPv6 nat table. This is the probe the runtime runs before it gives a
   * container IPv6, so it is the only IPv6 line worth showing. */
  print_ds_check("ipv6_nat", "IPv6 NAT support",
                 "CONFIG_IP6_NF_NAT and CONFIG_IP6_NF_TARGET_MASQUERADE; "
                 "--net=nat containers are IPv4 only if absent",
                 is_root && ds_ipt6_available(), "OPT");
  /* Asked of the running kernel, not of a config dump. A limit is applied on
   * whichever cgroup hierarchy owns its controller, so the controller
   * existing is enough. CPU quota is a feature of the cpu controller, and
   * its sysctl is only registered with CONFIG_CFS_BANDWIDTH. */
  print_ds_check("memory_limit", "Memory limit support",
                 "CONFIG_MEMCG, and no cgroup_disable=memory on the kernel "
                 "command line; --memory is skipped if absent",
                 ds_cgroup_has_controller("memory"), "OPT");
  print_ds_check(
      "cpu_limit", "CPU limit support",
      "CONFIG_CFS_BANDWIDTH, and no cgroup_disable=cpu on the kernel command "
      "line; --cpus is skipped if absent",
      access("/proc/sys/kernel/sched_cfs_bandwidth_slice_us", F_OK) == 0 &&
          ds_cgroup_has_controller("cpu"),
      "OPT");
  /* A container's CPU time comes from cpu.stat in cgroup2, which the root
   * only has where every cgroup has it (4.15 and later), or else from the
   * v1 cpuacct controller. */
  print_ds_check(
      "cpu_accounting", "CPU usage accounting",
      "CONFIG_CGROUP_CPUACCT on kernels before 4.15, and no "
      "cgroup_disable=cpuacct on the kernel command line; without it "
      "info shows no CPU usage and a CPU-limited container sees "
      "the host's figures in /proc/stat",
      access("/sys/fs/cgroup/cpu.stat", F_OK) == 0 ||
          ds_cgroup_has_controller("cpuacct"),
      "OPT");
  print_ds_check("pids_limit", "Process limit support",
                 "CONFIG_CGROUP_PIDS, and no cgroup_disable=pids on the kernel "
                 "command line; --pids-limit is skipped if absent",
                 ds_cgroup_has_controller("pids"), "OPT");

  print_ds_check("user_ns", "Sandboxing (user namespaces)",
                 "CONFIG_USER_NS; enable per container with "
                 "--allow-sandboxing. Needed by unprivileged Docker and "
                 "Podman, by sandboxed apps (Flatpak, Bubblewrap, browsers) "
                 "and by desktop environments",
                 check_ns(CLONE_NEWUSER, "user"), "OPT");

  /* FINAL SUMMARY */
  check_append("\n" C_BOLD "Summary:" C_RESET "\n\n");
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

  if (json_mode) {
    ds_json_str("version", DS_VERSION, &json_first);
    ds_json_int("requirements_met", missing_must == 0, &json_first);
    printf("}\n");
    return 0;
  }

  /* One-shot output to terminal */
  fwrite(check_buf, 1, check_buf_pos, stdout);
  fflush(stdout);

  return 0;
}
