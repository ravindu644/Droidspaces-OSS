/*
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/* Android's v1 cpuset mount uses noprefix, so its files are cpus and mems. */
static void cpuset_path(const char *dir, const char *file, char *path,
                        size_t size) {
  snprintf(path, size, "%s/cpuset.%s", dir, file);
  if (access(path, F_OK) != 0)
    snprintf(path, size, "%s/%s", dir, file);
}

static int read_mask(const char *path, cpu_set_t *mask) {
  char buf[DS_CPU_LIST_LEN];
  if (read_file(path, buf, sizeof(buf)) <= 0)
    return -1;
  buf[strcspn(buf, "\r\n")] = '\0';
  return ds_parse_cpu_list(buf, mask);
}

int ds_cgroup_validate_cpuset(const struct ds_config *cfg) {
  if (cfg->cpuset_invalid) {
    ds_error("Invalid cpuset_cpus: expected CPU IDs/ranges, e.g. 0-3,6.");
    return -1;
  }
  if (!CPU_COUNT(&cfg->cpuset_cpus))
    return 0;
  cpu_set_t available, common;
  if (!ds_cgroup_has_controller("cpuset")) {
    ds_error("CPU pinning requires CONFIG_CPUSETS and an enabled cpuset "
             "controller.");
    return -1;
  }
  if (read_mask("/sys/devices/system/cpu/present", &available) < 0) {
    ds_error("Cannot read the host's CPU list for CPU pinning.");
    return -1;
  }
  CPU_AND(&common, &cfg->cpuset_cpus, &available);
  if (!CPU_EQUAL(&common, &cfg->cpuset_cpus)) {
    ds_error("cpuset_cpus includes CPUs absent from this host.");
    return -1;
  }
  if (read_mask("/sys/devices/system/cpu/online", &available) < 0) {
    ds_error("Cannot read online CPUs for CPU pinning.");
    return -1;
  }
  CPU_AND(&common, &cfg->cpuset_cpus, &available);
  if (CPU_COUNT(&common) == 0) {
    ds_error("cpuset_cpus has no online CPU.");
    return -1;
  }
  return 0;
}

int ds_cgroup_get_cpuset(const char *name, char *buf, size_t size) {
  char dir[PATH_MAX], path[PATH_MAX + 64];
  int ver = ds_cgroup_ctrl_dir("cpuset", name, dir, sizeof(dir));
  buf[0] = '\0';
  if (!ver)
    return -1;
  cpuset_path(dir, ver == 2 ? "cpus.effective" : "effective_cpus", path,
              sizeof(path));
  if (ver == 1 && access(path, F_OK) != 0)
    cpuset_path(dir, "cpus", path, sizeof(path));
  if (read_file(path, buf, size) <= 0)
    return -1;
  buf[strcspn(buf, "\r\n")] = '\0';
  return 0;
}

int apply_cpuset(const struct ds_config *cfg) {
  if (ds_cgroup_validate_cpuset(cfg) < 0)
    return -1;
  if (!CPU_COUNT(&cfg->cpuset_cpus))
    return 0;
  char dir[PATH_MAX], path[PATH_MAX + 64], buf[DS_CPU_LIST_LEN];
  int ver = ds_cgroup_ctrl_dir("cpuset", cfg->container_name, dir, sizeof(dir));
  if (!ver) {
    ds_error("The cpuset controller is not reachable for CPU pinning.");
    return -1;
  }
  if (ver == 1) {
    char mnt[PATH_MAX], parent[PATH_MAX + 16];
    if (!find_host_cgroup_mount("cpuset", mnt, sizeof(mnt)))
      return -1;
    snprintf(parent, sizeof(parent), "%s/droidspaces", mnt);
    if (mkdir(parent, 0755) == 0)
      v1_seed_cpuset(mnt);
    if (access(dir, F_OK) != 0)
      mkdir_fresh(dir);
    /* Even a hybrid container needs a nonempty memory-node set before it
     * can join v1 cpuset. NUMA placement stays inherited from our parent. */
    cpuset_path(parent, "mems", path, sizeof(path));
    if (read_file(path, buf, sizeof(buf)) <= 0)
      goto fail;
    cpuset_path(dir, "mems", path, sizeof(path));
    if (write_file(path, buf) < 0)
      goto fail;
  }
  cpuset_path(dir, "cpus", path, sizeof(path));
  if (ds_format_cpu_list(&cfg->cpuset_cpus, buf, sizeof(buf)) < 0 ||
      write_file(path, buf) < 0)
    goto fail;

  /* v2 falls back to the parent's CPUs when none of the requested ones can
   * be granted. A successful write alone is not proof that pinning worked. */
  cpu_set_t effective, common;
  if (ds_cgroup_get_cpuset(cfg->container_name, buf, sizeof(buf)) < 0 ||
      ds_parse_cpu_list(buf, &effective) < 0) {
    errno = EINVAL;
    goto fail;
  }
  CPU_AND(&common, &cfg->cpuset_cpus, &effective);
  if (!CPU_EQUAL(&common, &effective)) {
    errno = EINVAL;
    goto fail;
  }
  return 0;
fail:
  ds_error("Cannot apply CPU pinning: %s", strerror(errno));
  return -1;
}

/* The generic v1 join is best effort for optional controllers. An explicit
 * pinning request must not start init or an attach session outside cpuset. */
int join_cpuset(const struct ds_config *cfg) {
  if (!CPU_COUNT(&cfg->cpuset_cpus))
    return 0;
  char dir[PATH_MAX], path[PATH_MAX + 64], pid[32];
  int ver = ds_cgroup_ctrl_dir("cpuset", cfg->container_name, dir, sizeof(dir));
  if (ver == 2)
    return 0; /* The shared cgroup2 join checks its write. */
  if (ver == 1) {
    snprintf(path, sizeof(path), "%s/cgroup.procs", dir);
    snprintf(pid, sizeof(pid), "%d", (int)getpid());
    if (write_file(path, pid) == 0)
      return 0;
  }
  ds_error("Cannot join the container's cpuset cgroup.");
  return -1;
}
