/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* Mask a sensitive path by self-binding and remounting read-only.
 * Silently skips if the path doesn't exist.  The resulting mount entry
 * preserves the parent filesystem type (e.g. "proc on /proc/kcore type
 * proc (ro)") - matching LXC's clean approach. */
static void ds_mask_path(const char *path) {
  if (access(path, F_OK) != 0)
    return;
  mount(path, path, NULL, MS_BIND, NULL);
  mount(path, path, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL);
}

/*
 * ds_apply_jail_mask()
 *
 * Secure sensitive kernel interfaces by self-binding and remounting them
 * read-only.  This reduces the container's attack surface and prevents it
 * from manipulating the host kernel via /proc and /sys.
 *
 * In Standard Mode (hw_access=0), we are very strict.
 * In Hardware Mode (hw_access=1), we preserve most paths to fulfill the
 * "everything possible" requirement for low-level hardware tools.
 */
int ds_apply_jail_mask(int hw_access, int privileged_mask, int sandboxing) {
  if (privileged_mask & DS_PRIV_NOMASK) {
    ds_log(
        "[SEC] --privileged=nomask: skipping jail masks for /proc and /sys.");
    return 0;
  }

  /* Universal masks - dangerous for ANY container regardless of HW mode */
  const char *universal_masks[] = {"/proc/sysrq-trigger", "/proc/kcore",
                                   "/proc/timer_list", NULL};

  /* Standard mode read-only remounts - preserves paths, blocks writes.
   * Covers both sensitive proc subtrees and dangerous sys interfaces. */
  const char *standard_ro[] = {"/proc/irq",
                               "/sys/firmware",
                               "/sys/kernel/security",
                               "/sys/kernel/debug",
                               "/sys/kernel/tracing",
                               "/sys/block",
                               NULL};

  /* Apply universal masks */
  for (int i = 0; universal_masks[i]; i++) {
    ds_mask_path(universal_masks[i]);
  }

  /* Universal: mask all cgroup v1 release_agent files.
   *
   * In --force-cgroupv1 mode, host cgroup v1 hierarchies are bind-mounted
   * into the container. release_agent files are writable by root and the
   * kernel executes them AS REAL HOST ROOT, outside all namespaces, when the
   * last process leaves a cgroup (notify_on_release=1). This is the
   * CVE-2022-0492 class of escape - confirmed exploitable in testing.
   *
   * Self-bind + RO remount makes them unwritable while leaving the rest of
   * the cgroup hierarchy fully functional. */
  {
    DIR *cgdir = opendir("/sys/fs/cgroup");
    if (cgdir) {
      struct dirent *de;
      while ((de = readdir(cgdir)) != NULL) {
        if (de->d_name[0] == '.')
          continue;
        char agent_path[PATH_MAX];
        snprintf(agent_path, sizeof(agent_path),
                 "/sys/fs/cgroup/%s/release_agent", de->d_name);
        ds_mask_path(agent_path);
      }
      closedir(cgdir);
    }
  }

  /*
   * Wholesale /proc/sys lockdown - applied in BOTH standard and hardware mode.
   *
   * /proc/sys reflects the host kernel's sysctl state and is NOT scoped to the
   * PID namespace. Even with a fresh procfs, a container running as root can
   * write to /proc/sys/kernel/unprivileged_bpf_disabled, /proc/sys/fs/, etc.
   * and corrupt Android host state (eBPF subsystem, dmesg, perf, hardlinks).
   *
   * Strategy: punch RW bind-mounts for the two genuinely namespace-scoped
   * subtrees FIRST, then lock all of /proc/sys read-only in one shot.
   * The pre-existing submounts shadow the parent remount.
   *
   * RW holes (namespace-scoped - safe for containers to write):
   *   /proc/sys/net              - entirely net-namespace scoped
   *   /proc/sys/kernel/hostname  - UTS-namespace scoped
   *   /proc/sys/kernel/domainname - UTS-namespace scoped
   *
   * Everything else is blocked: kernel/, vm/, fs/, dev/, abi/, debug/.
   * This covers all the dangerous sysctls in one mount entry instead of
   * playing whack-a-mole with individual paths.
   */
  {
    /* Step 1: Lock all of /proc/sys RO via self-bind + remount.
     * Must happen BEFORE pinning RW holes - once the parent is RO, new
     * bind mounts stacked on top of it can be independently RW. */
    if (access("/proc/sys", F_OK) == 0) {
      mount("/proc/sys", "/proc/sys", NULL, MS_BIND, NULL);
      mount("/proc/sys", "/proc/sys", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY,
            NULL);
      ds_log("[SEC] /proc/sys locked RO.");
    }

    /* Step 2: Stack RW bind mounts on top of the now-RO /proc/sys.
     * Bind inherits RO from parent, so explicitly remount RW after.
     *
     * /proc/sys/user is per user namespace, but the container lives in the
     * host's, so it only opens with sandboxing: bwrap --disable-userns writes
     * user.max_user_namespaces through the container's own /proc before it
     * unshares its second user namespace, and the pristine copy under
     * /run/droidspaces already exposes the same tree read-write. */
    const char *rw_holes[] = {"/proc/sys/net", "/proc/sys/kernel/hostname",
                              "/proc/sys/kernel/domainname",
                              sandboxing ? "/proc/sys/user" : NULL, NULL};
    for (int i = 0; rw_holes[i]; i++) {
      if (access(rw_holes[i], F_OK) != 0)
        continue;
      if (mount(rw_holes[i], rw_holes[i], NULL, MS_BIND, NULL) < 0) {
        ds_warn("[SEC] Failed to bind RW hole %s: %s", rw_holes[i],
                strerror(errno));
        continue;
      }
      if (mount(rw_holes[i], rw_holes[i], NULL,
                MS_BIND | MS_REMOUNT | MS_NOSUID | MS_NODEV | MS_NOEXEC,
                NULL) < 0)
        ds_warn("[SEC] Failed to remount RW hole %s: %s", rw_holes[i],
                strerror(errno));
    }
    ds_log("[SEC] /proc/sys RW holes preserved (net/hostname/domainname%s).",
           sandboxing ? "/user" : "");
  }

  if (hw_access) {
    ds_log("[SEC] Hardware Mode: preserved sensitive /proc and /sys paths.");
    return 0;
  }

  /* Apply standard mode read-only remounts */
  for (int i = 0; standard_ro[i]; i++) {
    ds_mask_path(standard_ro[i]);
  }

  ds_log("[SEC] Jail mask applied (hardened /proc and /sys).");
  return 0;
}
