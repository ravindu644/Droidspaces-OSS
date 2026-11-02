/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "cgroup/internal.h"

/* Count the tasks under one cgroup2 dir, children included: all of them in
 * *total, the ones that count towards a load average in *running. */
static void count_tasks(const char *dir, int *running, int *total) {
  char path[PATH_MAX + 64];
  snprintf(path, sizeof(path), "%s/cgroup.procs", dir);
  FILE *f = fopen(path, "re");
  int pid;
  while (f && fscanf(f, "%d", &pid) == 1) {
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *td = opendir(path);
    struct dirent *te;
    while (td && (te = readdir(td)) != NULL) {
      char buf[512];
      if (te->d_name[0] == '.')
        continue;
      snprintf(path, sizeof(path), "/proc/%d/task/%s/stat", pid, te->d_name);
      if (read_file(path, buf, sizeof(buf)) <= 0)
        continue;
      /* "pid (comm) S ...": comm may hold anything, so find the last ')' */
      char *st = strrchr(buf, ')');
      (*total)++;
      /* Running, or in uninterruptible sleep, the kernel's own definition */
      if (st && (st[2] == 'R' || st[2] == 'D'))
        (*running)++;
    }
    if (td)
      closedir(td);
  }
  if (f)
    fclose(f);

  DIR *d = opendir(dir);
  struct dirent *de;
  while (d && (de = readdir(d)) != NULL) {
    if (de->d_type != DT_DIR || de->d_name[0] == '.')
      continue;
    snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
    count_tasks(path, running, total);
  }
  if (d)
    closedir(d);
}

/* How many tasks the container has, and how many of them are runnable.
 * Returns -1 when it has no cgroup2 dir to walk. */
int ds_cgroup_count_tasks(const char *container_name, int *running,
                          int *total) {
  char safe_name[256], dir[PATH_MAX];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(dir, sizeof(dir), "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (access(dir, F_OK) != 0)
    return -1;
  *running = *total = 0;
  count_tasks(dir, running, total);
  return 0;
}

/* The container's CPU time since it booted, split into user and system, in
 * microseconds. From cpu.stat in its cgroup2 dir, or on kernels before 4.15,
 * which have no such file, from cpuacct.stat in its v1 cpuacct cgroup.
 * Returns 0, or -1 when the kernel keeps no such figure for the container. */
int ds_cgroup_cpu_times(const char *container_name, long long *user_us,
                        long long *system_us) {
  char dir[PATH_MAX], path[PATH_MAX + 64], buf[512], safe_name[256];

  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/cpu.stat",
           safe_name);
  if (read_file(path, buf, sizeof(buf)) > 0) {
    char *u = strstr(buf, "user_usec "), *s = strstr(buf, "system_usec ");
    if (u && s) {
      *user_us = strtoll(u + 10, NULL, 10);
      *system_us = strtoll(s + 12, NULL, 10);
      return 0;
    }
  }

  if (ds_cgroup_ctrl_dir("cpuacct", container_name, dir, sizeof(dir)) != 1)
    return -1;
  snprintf(path, sizeof(path), "%s/cpuacct.stat", dir);
  long long u = 0, s = 0;
  long hz = ds_clk_tck();
  /* "user N\nsystem N", in clock ticks */
  if (read_file(path, buf, sizeof(buf)) <= 0 || hz <= 0 ||
      sscanf(buf, "user %lld system %lld", &u, &s) != 2)
    return -1;
  *user_us = u * 1000000 / hz;
  *system_us = s * 1000000 / hz;
  return 0;
}

int ds_cgroup_get_usage(const char *container_name, long long *mem,
                        long long *file_cache, long long *cpu_us,
                        long long *pids) {
  if (mem)
    *mem = -1;
  if (file_cache)
    *file_cache = 0;
  if (cpu_us)
    *cpu_us = -1;
  if (pids)
    *pids = -1;

  char dir[PATH_MAX], path[PATH_MAX + 64], buf[256];
  int ver;

  /* parse_cgroup_ll() reports "max" (unlimited) as -1, not 0. */
  if (mem &&
      (ver = ds_cgroup_ctrl_dir("memory", container_name, dir, sizeof(dir)))) {
    snprintf(path, sizeof(path), "%s/%s", dir,
             ver == 1 ? "memory.usage_in_bytes" : "memory.current");
    if (read_file(path, buf, sizeof(buf)) > 0)
      *mem = parse_cgroup_ll(buf);

    /* The cgroup is also charged for file cache, which the kernel drops on
     * its own under pressure. Report it apart so callers can show what the
     * programs hold, the way free(1) does. */
    char stat[4096];
    snprintf(path, sizeof(path), "%s/memory.stat", dir);
    if (file_cache && read_file(path, stat, sizeof(stat)) > 0) {
      const char *keys[] = {
          ver == 1 ? "\ntotal_active_file " : "\nactive_file ",
          ver == 1 ? "\ntotal_inactive_file " : "\ninactive_file "};
      *file_cache = 0;
      for (int i = 0; i < 2; i++) {
        char *p = strstr(stat, keys[i]);
        if (p)
          *file_cache += strtoll(p + strlen(keys[i]), NULL, 10);
      }
    }
  }
  if (pids && ds_cgroup_ctrl_dir("pids", container_name, dir, sizeof(dir))) {
    snprintf(path, sizeof(path), "%s/pids.current", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *pids = parse_cgroup_ll(buf);
  }
  if (cpu_us) {
    /* cgroup2 keeps cpu.stat even without the cpu controller, and the
     * container is always in our cgroup2 dir when there is one. */
    char safe_name[256];
    sanitize_container_name(container_name, safe_name, sizeof(safe_name));
    snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/cpu.stat",
             safe_name);
    if (read_file(path, buf, sizeof(buf)) > 0) {
      char *p = strstr(buf, "usage_usec ");
      if (p)
        *cpu_us = parse_cgroup_ll(p + 11);
    } else if (ds_cgroup_ctrl_dir("cpuacct", container_name, dir,
                                  sizeof(dir)) == 1) {
      /* Kernels before 4.15: the v1 cpuacct cgroup, in nanoseconds */
      snprintf(path, sizeof(path), "%s/cpuacct.usage", dir);
      if (read_file(path, buf, sizeof(buf)) > 0 &&
          (*cpu_us = parse_cgroup_ll(buf)) >= 0)
        *cpu_us /= 1000;
    }
  }
  return 0;
}
