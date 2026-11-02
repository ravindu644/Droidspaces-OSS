/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "cgroup/internal.h"

/* Write one limit where its controller lives and return 1 if it is in place.
 * A NULL file means this cgroup version needs no write for this knob. On v1
 * the cgroup is ours to create; the intermediate joins it afterwards. */
static int apply_limit(struct ds_config *cfg, const char *ctrl,
                       const char *v2_file, const char *v2_val,
                       const char *v1_file, const char *v1_val,
                       const char *kconfig) {
  char dir[PATH_MAX], path[PATH_MAX + 64];
  int ver = ds_cgroup_ctrl_dir(ctrl, cfg->container_name, dir, sizeof(dir));
  const char *file = ver == 1 ? v1_file : v2_file;
  if (!file)
    return 1;

  /* Only undo a cgroup this call made: a later knob failing must not take
   * the earlier limits in the same cgroup down with it. */
  int made = ver == 1 && access(dir, F_OK) != 0;
  if (ver == 1) {
    mkdir_p(dir, 0755);
    /* Kernels before 5.x account flat by default, and then the limit would
     * not cover the cgroups systemd creates below us. Later ones are always
     * hierarchical and ignore this. */
    snprintf(path, sizeof(path), "%s/memory.use_hierarchy", dir);
    if (!strcmp(ctrl, "memory") && write_file(path, "1") < 0) {
    }
    /* A cgroup left over from an earlier run keeps that run's memsw cap, and
     * v1 refuses a limit_in_bytes above it, so lift it before any write. */
    snprintf(path, sizeof(path), "%s/memory.memsw.limit_in_bytes", dir);
    if (!strcmp(ctrl, "memory") && write_file(path, "-1") < 0) {
    }
  }

  snprintf(path, sizeof(path), "%s/%s", dir, file);
  if (ver == 0 && ds_cgroup_has_controller(ctrl)) {
    /* The kernel has it, we just cannot get at it: not in our cgroup2 dir
     * and on no v1 hierarchy we can find. Do not blame the kernel config. */
    ds_warn("[CGROUP] The %s controller is held by a cgroup hierarchy we "
            "could not reach, limit skipped.",
            ctrl);
  } else if (access(path, F_OK) != 0) {
    ds_warn("[CGROUP] %s is not available on this kernel, limit skipped "
            "(needs %s).",
            file, kconfig);
  } else if (write_file(path, ver == 1 ? v1_val : v2_val) < 0) {
    ds_warn("[CGROUP] %s: %s", file, strerror(errno));
  } else {
    return 1;
  }
  if (made)
    rmdir(dir);
  return 0;
}

/* A limit that cannot be applied is zeroed in cfg, so the /proc
 * virtualization does not advertise a cap that is not there. */
void ds_cgroup_apply_limits(struct ds_config *cfg) {
  char val[64], quota[32], period[32];

  if (cfg->memory_limit) {
    snprintf(val, sizeof(val), "%lld", cfg->memory_limit);
    if (!apply_limit(cfg, "memory", "memory.max", val, "memory.limit_in_bytes",
                     val,
                     "CONFIG_MEMCG, and no cgroup_disable=memory on the "
                     "kernel command line"))
      cfg->memory_limit = 0;
    /* Unless the user allowed swap, a container at its limit would spill
     * into the host's swap, which on Android is zram, so it keeps eating RAM
     * and kswapd CPU until the whole phone crawls. Cap it at its own limit
     * and let its OOM killer act instead. v2's swap.max=0 is absolute. v1 has
     * no swap-only knob: RAM+swap equal to RAM leaves no room for swap at the
     * limit, though global reclaim may still move some pages out within it.
     * Per-cgroup swappiness would stop that, but Android 5.15 accepts the
     * write and ignores it. A cap the kernel cannot provide counts as allowed
     * swap, so the virtualization does not report a cap that is not there.
     * That goes in a runtime field, allow_swap stays what the user saved.
     * Allowed swap needs no write: the memory write above already lifted a
     * leftover v1 memsw cap. */
    cfg->swap_uncapped =
        !cfg->memory_limit || cfg->allow_swap ||
        !apply_limit(cfg, "memory", "memory.swap.max", "0",
                     "memory.memsw.limit_in_bytes", val,
                     "CONFIG_MEMCG_SWAP with swap accounting on, swapaccount=1 "
                     "on the kernel command line before 5.8");
  }
  if (cfg->cpu_quota) {
    long long per = cfg->cpu_period > 0 ? cfg->cpu_period : 100000;
    snprintf(val, sizeof(val), "%lld %lld", cfg->cpu_quota, per);
    snprintf(quota, sizeof(quota), "%lld", cfg->cpu_quota);
    snprintf(period, sizeof(period), "%lld", per);
    /* v1 splits cpu.max in two files, period first */
    if (!apply_limit(cfg, "cpu", NULL, NULL, "cpu.cfs_period_us", period,
                     "CONFIG_CFS_BANDWIDTH, and no cgroup_disable=cpu on the "
                     "kernel command line") ||
        !apply_limit(cfg, "cpu", "cpu.max", val, "cpu.cfs_quota_us", quota,
                     "CONFIG_CFS_BANDWIDTH, and no cgroup_disable=cpu on the "
                     "kernel command line"))
      cfg->cpu_quota = 0;

    /* cgroup2 only reports a cgroup's CPU time in cpu.stat from 4.15 on. On
     * older kernels the figure lives in the v1 cpuacct hierarchy, so give the
     * container a cgroup there too. The join and the cleanup take every v1
     * cgroup of ours that exists, this one included. */
    char acct[PATH_MAX];
    if (cfg->cpu_quota && ds_cgroup_ctrl_dir("cpuacct", cfg->container_name,
                                             acct, sizeof(acct)) == 1)
      mkdir_p(acct, 0755);
  }
  if (cfg->pids_limit) {
    /* The command line refuses a value this low. A config file can still
     * carry one, and a container that cannot boot helps nobody, so raise it
     * here, where it is said once per boot and not on every config load. */
    if (cfg->pids_limit < DS_MIN_PIDS_LIMIT) {
      ds_warn("[CGROUP] pids_limit %lld is too low to boot under, using %d.",
              cfg->pids_limit, DS_MIN_PIDS_LIMIT);
      cfg->pids_limit = DS_MIN_PIDS_LIMIT;
    }
    snprintf(val, sizeof(val), "%lld", cfg->pids_limit);
    if (!apply_limit(
            cfg, "pids", "pids.max", val, "pids.max", val,
            "CONFIG_CGROUP_PIDS, and no cgroup_disable=pids on the kernel "
            "command line"))
      cfg->pids_limit = 0;
  }
}

/* The limits actually in force, read back from the cgroup files. The config
 * only says what was asked for, and a limit the kernel could not provide was
 * dropped at start. 0 means unlimited. */
void ds_cgroup_get_limits(const char *container_name, long long *mem,
                          long long *cpu_quota, long long *cpu_period,
                          long long *pids, int *swap_capped) {
  char dir[PATH_MAX], path[PATH_MAX + 64], buf[256];
  int ver;
  *mem = *cpu_quota = *cpu_period = *pids = 0;
  *swap_capped = 0;

  if ((ver = ds_cgroup_ctrl_dir("memory", container_name, dir, sizeof(dir)))) {
    snprintf(path, sizeof(path), "%s/%s", dir,
             ver == 1 ? "memory.limit_in_bytes" : "memory.max");
    if (read_file(path, buf, sizeof(buf)) > 0)
      *mem = parse_cgroup_ll(buf);
    /* Swap is capped when v2 swap.max is 0, or v1 RAM+swap equals RAM */
    snprintf(path, sizeof(path), "%s/%s", dir,
             ver == 1 ? "memory.memsw.limit_in_bytes" : "memory.swap.max");
    if (read_file(path, buf, sizeof(buf)) > 0) {
      long long sw = parse_cgroup_ll(buf);
      *swap_capped = ver == 2 ? sw == 0 : sw == *mem;
    }
  }
  if ((ver = ds_cgroup_ctrl_dir("cpu", container_name, dir, sizeof(dir))) ==
      2) {
    snprintf(path, sizeof(path), "%s/cpu.max", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      sscanf(buf, "%lld %lld", cpu_quota, cpu_period); /* "max" parses none */
  } else if (ver == 1) {
    snprintf(path, sizeof(path), "%s/cpu.cfs_quota_us", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *cpu_quota = parse_cgroup_ll(buf);
    snprintf(path, sizeof(path), "%s/cpu.cfs_period_us", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *cpu_period = parse_cgroup_ll(buf);
  }
  if (ds_cgroup_ctrl_dir("pids", container_name, dir, sizeof(dir))) {
    snprintf(path, sizeof(path), "%s/pids.max", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *pids = parse_cgroup_ll(buf);
  }

  /* Unlimited reads back as "max" or -1 depending on the file, and v1
   * memory spells it as a page-rounded LLONG_MAX */
  if (*mem < 0 || *mem > LLONG_MAX / 2)
    *mem = *swap_capped = 0;
  if (*cpu_quota <= 0)
    *cpu_quota = *cpu_period = 0;
  else if (*cpu_period <= 0)
    *cpu_period = 100000; /* the kernel's default period */
  if (*pids < 0)
    *pids = 0;
}
