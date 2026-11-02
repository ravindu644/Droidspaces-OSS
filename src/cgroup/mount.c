/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

#include "cgroup/internal.h"

/* Mount cgroup2 on /sys/fs/cgroup if the host hasn't done so.
 * Android recovery kernels support cgroup2 but only mount it at /dev/cg2_bpf;
 * systemd needs it at /sys/fs/cgroup. Sequence: mkdir -> tmpfs anchor ->
 * cgroup2. */
void ds_cgroup_host_bootstrap(int force_cgroupv1) {
  if (force_cgroupv1)
    return;

  /* Already done */
  struct statfs sfs;
  if (statfs("/sys/fs/cgroup", &sfs) == 0 &&
      (unsigned long)sfs.f_type == (unsigned long)CGROUP2_SUPER_MAGIC)
    return;

  /* No probe_cgroup2_mount(): mkdtemp fails on ramfs roots (no /tmp).
   * The mount() calls below self-report failure via errno. */
  if (grep_file("/proc/filesystems", "cgroup2") <= 0) {
    ds_log("[CGROUP] cgroup2 not in /proc/filesystems, skipping bootstrap.");
    return;
  }

  if (access("/sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("/sys/fs/cgroup", 0755) != 0) {
      ds_error("[CGROUP] Failed to create /sys/fs/cgroup: %s", strerror(errno));
      return;
    }
  }

  /* tmpfs anchor needed: cgroup2 can't layer directly on ramfs */
  if (statfs("/sys/fs/cgroup", &sfs) == 0 &&
      (unsigned long)sfs.f_type != (unsigned long)TMPFS_MAGIC &&
      (unsigned long)sfs.f_type != (unsigned long)CGROUP2_SUPER_MAGIC) {
    if (mount("none", "/sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") != 0) {
      ds_error("[CGROUP] Failed to mount tmpfs on /sys/fs/cgroup: %s",
               strerror(errno));
      return;
    }
    ds_log("[CGROUP] Mounted tmpfs anchor on /sys/fs/cgroup.");
  }

  if (mount("none", "/sys/fs/cgroup", "cgroup2",
            MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
    ds_error("Failed to mount cgroup2 on /sys/fs/cgroup: %s", strerror(errno));
    return;
  }
  ds_log("Auto-mounted cgroup2 on /sys/fs/cgroup.");
}

/* Without a cgroup namespace (kernels before 4.6) a v1 mount shows the whole
 * host hierarchy, and the container is not its root. OpenRC moves every
 * service it starts to the root of each hierarchy, "/sys/fs/cgroup/<ctrl>/
 * tasks", which on the host is outside the container's cgroup: the memory
 * limit then covers init and getty and nothing else. This is LXC 3's mixed
 * mount for such kernels: the hierarchy's root is a read-only tmpfs directory
 * with only the container's own cgroup bound into it, writable. The root has
 * no tasks file, so OpenRC's move is a no-op, and the path the container sees
 * in /proc/self/cgroup still resolves, which systemd needs. We were joined to
 * the container's cgroup before the namespaces were unshared, so that file
 * says where it is. Returns 0 when done, -1 to mount the hierarchy instead. */
static int v1_mount_mixed(const char *mp, const char *ctrl) {
  char host[PATH_MAX], own[PATH_MAX] = "", line[512];
  if (access("/proc/self/ns/cgroup", F_OK) == 0 ||
      !find_host_cgroup_mount(ctrl, host, sizeof(host)))
    return -1;

  FILE *f = fopen("/proc/self/cgroup", "re");
  if (!f)
    return -1;
  while (fgets(line, sizeof(line), f)) {
    char *ctrls = strchr(line, ':'), *path = ctrls ? strchr(ctrls + 1, ':') : 0;
    if (!path)
      continue;
    *path++ = '\0';
    path[strcspn(path, "\n")] = '\0';
    for (char *t = strtok(ctrls + 1, ","); t; t = strtok(NULL, ","))
      if (strcmp(t, ctrl) == 0 && strcmp(path, "/") != 0)
        snprintf(own, sizeof(own), "%s", path);
  }
  fclose(f);
  if (!own[0])
    return -1;

  char src[PATH_MAX], dst[PATH_MAX];
  snprintf(src, sizeof(src), "%s%s", host, own);
  snprintf(dst, sizeof(dst), "%s%s", mp, own);
  unsigned long fl = MS_NOSUID | MS_NODEV | MS_NOEXEC;
  /* Root goes read-only before the child mount goes in: a bind of the root
   * onto itself is not recursive and would hide a child already there. */
  if (mkdir_p(dst, 0755) < 0 || mount(mp, mp, NULL, MS_BIND, NULL) < 0 ||
      mount(NULL, mp, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | fl, NULL) < 0 ||
      mount(src, dst, NULL, MS_BIND, NULL) < 0) {
    ds_warn("[CGROUP] %s: cannot bind %s into the container: %s", ctrl, src,
            strerror(errno));
    return -1;
  }
  ds_log("[CGROUP] v1 mounted: %s (no cgroup namespace, %s only)", ctrl, own);
  return 0;
}

/* Runs inside the container. Every hierarchy already exists on the host by
 * now (ds_cgroup_v1_setup), so with a cgroup namespace each of these mounts
 * comes up rooted at the container's own cgroup. */
static void mount_v1_controller(const char *name, void *arg) {
  (void)arg;
  char mp[PATH_MAX];
  snprintf(mp, sizeof(mp), "sys/fs/cgroup/%s", name);
  if (access(mp, F_OK) == 0)
    return; /* already set up or co-mounted */

  if (mkdir(mp, 0755) < 0 && errno != EEXIST)
    return;

  if (v1_mount_mixed(mp, name) == 0)
    return;

  if (mount("cgroup", mp, "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC, name) !=
      0) {
    ds_log("[CGROUP] v1 controller '%s' unavailable: %s", name,
           strerror(errno));
    rmdir(mp);
  } else {
    ds_log("[CGROUP] v1 mounted: %s", name);
  }
}

/**
 * Ported LXC-style Cgroup Setup:
 * 1. Discover host hierarchies from /proc/self/mountinfo.
 * 2. If Cgroup Namespace is active (Linux 4.6+), mount hierarchies directly.
 * 3. Otherwise (Legacy), bind-mount the container's subset from the host.
 */
int setup_cgroups(int is_systemd, int force_cgroupv1) {
  ds_cgroup_host_bootstrap(force_cgroupv1);

  if (access("sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("sys/fs/cgroup", 0755) < 0)
      return -1;
  }

  /* Mount tmpfs as the cgroup base */
  if (domount("none", "sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") < 0)
    return -1;

  int v2_active = ds_cgroup_host_is_v2() && !force_cgroupv1;
  int systemd_setup_done = 0;

  if (v2_active) {
    /* Always mount a fresh cgroup2 hierarchy within the container's
     * cgroup namespace. Isolation is handled by the kernel namespace. */
    if (mount("cgroup2", "sys/fs/cgroup", "cgroup2",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0) {
      systemd_setup_done = 1;
    } else {
      ds_error("Failed to mount cgroup2: %s", strerror(errno));
    }
  } else {
    /* V1 PATH (force_cgroupv1): Synthesize fresh mounts for all controllers. */
    each_v1_controller(mount_v1_controller, NULL);
    systemd_setup_done = 1; /* handled via systemd named cgroup below */
  }

  /* Ensure a systemd cgroup hierarchy exists for systemd containers.
   * On v1 this is a named cgroup; on v2 systemd uses the unified root. */
  if (is_systemd && !v2_active) {
    if (access("sys/fs/cgroup/systemd", F_OK) != 0) {
      mkdir("sys/fs/cgroup/systemd", 0755);
      if (v1_mount_mixed("sys/fs/cgroup/systemd", "name=systemd") != 0 &&
          mount("cgroup", "sys/fs/cgroup/systemd", "cgroup",
                MS_NOSUID | MS_NODEV | MS_NOEXEC, "none,name=systemd") < 0) {
        ds_error("Failed to mount systemd cgroup: %s", strerror(errno));
        return -1;
      }
    }
    systemd_setup_done = 1;
  }

  if (is_systemd && !systemd_setup_done) {
    ds_error("Systemd cgroup setup failed. Systemd containers cannot boot.");
    return -1;
  }

  return 0;
}
