/*
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

int ds_cpu_parse_cores(const char *value, struct ds_config *cfg) {
  int performance = 0, efficiency = 0;
  const char *p = value;
  if (!p)
    return -1;
  while (*p) {
    if (!isdigit((unsigned char)*p))
      return -1;
    char *end;
    errno = 0;
    long count = strtol(p, &end, 10);
    if (errno || count < 1 || count > CPU_SETSIZE)
      return -1;
    int *slot;
    if (*end == 'P' || *end == 'p')
      slot = &performance;
    else if (*end == 'E' || *end == 'e')
      slot = &efficiency;
    else
      return -1;
    if (*slot)
      return -1;
    *slot = (int)count;
    p = end + 1;
    if (*p && (*p++ != ',' || !*p))
      return -1;
  }
  if (performance + efficiency > CPU_SETSIZE)
    return -1;
  cfg->cpu_p_cores = performance;
  cfg->cpu_e_cores = efficiency;
  CPU_ZERO(&cfg->cpuset_cpus);
  cfg->cpuset_invalid = 0;
  return 0;
}

static int read_list(const char *path, cpu_set_t *mask) {
  char buf[DS_CPU_LIST_LEN];
  if (read_file(path, buf, sizeof(buf)) <= 0)
    return -1;
  buf[strcspn(buf, "\r\n")] = '\0';
  CPU_ZERO(mask);
  return buf[0] ? ds_parse_cpu_list(buf, mask) : 0;
}

static int cpu_path(int cpu, const char *file, char *path, size_t size) {
  int n = snprintf(path, size, "/sys/devices/system/cpu/cpu%d/%s", cpu, file);
  return n > 0 && (size_t)n < size ? 0 : -1;
}

static int cpu_number(int cpu, const char *file) {
  char path[128], buf[64], *end;
  if (cpu_path(cpu, file, path, sizeof(path)) < 0 ||
      read_file(path, buf, sizeof(buf)) <= 0)
    return -1;
  errno = 0;
  long value = strtol(buf, &end, 10);
  if (errno || end == buf)
    return -1;
  while (isspace((unsigned char)*end))
    end++;
  return *end || value < 0 || value > INT_MAX ? -1 : (int)value;
}

/* Intel exposes hybrid classes through separate PMUs, not CPU numbering or
 * frequency. Their CPU lists contain online CPUs; siblings supply offline
 * threads belonging to a selected physical core. */
static int class_masks(const cpu_set_t *present, cpu_set_t *performance,
                       cpu_set_t *efficiency) {
  cpu_set_t common;
  if (read_list("/sys/bus/event_source/devices/cpu_core/cpus", performance) ==
          0 &&
      read_list("/sys/bus/event_source/devices/cpu_atom/cpus", efficiency) ==
          0) {
    CPU_AND(&common, performance, efficiency);
    return CPU_COUNT(&common) ? -1 : 0;
  }

  struct utsname uts;
  if (uname(&uts) < 0 ||
      (strcmp(uts.machine, "aarch64") && strncmp(uts.machine, "arm", 3)))
    return -1;

  int capacity[CPU_SETSIZE], minimum = INT_MAX, maximum = 0;
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (!CPU_ISSET(cpu, present))
      continue;
    capacity[cpu] = cpu_number(cpu, "cpu_capacity");
    if (capacity[cpu] <= 0)
      return -1;
    if (capacity[cpu] < minimum)
      minimum = capacity[cpu];
    if (capacity[cpu] > maximum)
      maximum = capacity[cpu];
  }
  if (minimum == maximum)
    return -1;
  CPU_ZERO(performance);
  CPU_ZERO(efficiency);
  /* Capacity is the scheduler's performance scale, not an energy meter.
   * Select its lowest/highest tiers; middle tiers stay available by CPU ID. */
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (!CPU_ISSET(cpu, present))
      continue;
    if (capacity[cpu] == minimum)
      CPU_SET(cpu, efficiency);
    else if (capacity[cpu] == maximum)
      CPU_SET(cpu, performance);
  }
  return 0;
}

struct cpu_core {
  int cpu;
  int package;
  int core;
  int performance;
};

static int compare_cores(const void *a, const void *b) {
  const struct cpu_core *left = a, *right = b;
  if (left->package != right->package)
    return left->package < right->package ? -1 : 1;
  if (left->core != right->core)
    return left->core < right->core ? -1 : 1;
  return (left->cpu > right->cpu) - (left->cpu < right->cpu);
}

static int core_siblings(int cpu, const cpu_set_t *present,
                         const cpu_set_t *online, const int *packages,
                         const int *ids, cpu_set_t *siblings) {
  char path[128];
  if (cpu_path(cpu, "topology/thread_siblings_list", path, sizeof(path)) < 0 ||
      read_list(path, siblings) < 0 || !CPU_ISSET(cpu, siblings)) {
    if (packages[cpu] < 0 || ids[cpu] < 0)
      return -1;
    CPU_ZERO(siblings);
    for (int other = 0; other < CPU_SETSIZE; other++)
      if (CPU_ISSET(other, present) && packages[other] == packages[cpu] &&
          ids[other] == ids[cpu])
        CPU_SET(other, siblings);
  }
  CPU_AND(siblings, siblings, present);
  /* Hotplug can remove an offline thread from thread_siblings_list. Retain
   * its physical-core membership when the package/core IDs still prove it. */
  if (packages[cpu] >= 0 && ids[cpu] >= 0)
    for (int other = 0; other < CPU_SETSIZE; other++) {
      if (!CPU_ISSET(other, present) || packages[other] != packages[cpu] ||
          ids[other] != ids[cpu] || CPU_ISSET(other, siblings))
        continue;
      if (CPU_ISSET(other, online))
        return -1;
      CPU_SET(other, siblings);
    }
  return 0;
}

int ds_cpu_resolve_cores(struct ds_config *cfg) {
  if (cfg->cpuset_invalid) {
    ds_error("Invalid saved CPU placement configuration.");
    return -1;
  }
  if (!cfg->cpu_p_cores && !cfg->cpu_e_cores)
    return 0;
  if (cfg->cpu_p_cores < 0 || cfg->cpu_p_cores > CPU_SETSIZE ||
      cfg->cpu_e_cores < 0 || cfg->cpu_e_cores > CPU_SETSIZE)
    return -1;

  cpu_set_t present, online, performance, efficiency, seen, selected;
  if (read_list("/sys/devices/system/cpu/present", &present) < 0 ||
      read_list("/sys/devices/system/cpu/online", &online) < 0 ||
      !CPU_COUNT(&present) || !CPU_COUNT(&online) ||
      class_masks(&present, &performance, &efficiency) < 0) {
    ds_error("Cannot identify P/E CPU classes on this host; select explicit "
             "CPU IDs with --cpuset-cpus instead.");
    return -1;
  }

  struct cpu_core cores[CPU_SETSIZE];
  int packages[CPU_SETSIZE], ids[CPU_SETSIZE];
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (!CPU_ISSET(cpu, &present))
      continue;
    packages[cpu] = cpu_number(cpu, "topology/physical_package_id");
    ids[cpu] = cpu_number(cpu, "topology/core_id");
  }
  size_t count = 0;
  CPU_ZERO(&seen);
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (!CPU_ISSET(cpu, &present) || !CPU_ISSET(cpu, &online) ||
        CPU_ISSET(cpu, &seen))
      continue;
    cpu_set_t siblings, active, common;
    if (core_siblings(cpu, &present, &online, packages, ids, &siblings) < 0) {
      ds_error("Cannot identify the physical core of CPU %d.", cpu);
      return -1;
    }
    CPU_AND(&common, &siblings, &seen);
    if (CPU_COUNT(&common)) {
      ds_error("Inconsistent CPU sibling topology at CPU %d.", cpu);
      return -1;
    }
    CPU_OR(&seen, &seen, &siblings);
    CPU_AND(&active, &siblings, &online);
    if (!CPU_COUNT(&active))
      continue;
    CPU_AND(&common, &active, &performance);
    int is_performance = CPU_EQUAL(&common, &active);
    if (!is_performance) {
      int performance_threads = CPU_COUNT(&common);
      CPU_AND(&common, &active, &efficiency);
      if (!CPU_EQUAL(&common, &active)) {
        if (!performance_threads && !CPU_COUNT(&common))
          continue;
        ds_error("Cannot identify one CPU class for physical core at CPU %d.",
                 cpu);
        return -1;
      }
    }
    cores[count++] =
        (struct cpu_core){cpu, packages[cpu], ids[cpu], is_performance};
  }
  qsort(cores, count, sizeof(cores[0]), compare_cores);

  int need_p = cfg->cpu_p_cores, need_e = cfg->cpu_e_cores;
  CPU_ZERO(&selected);
  for (size_t i = 0; i < count && (need_p || need_e); i++) {
    int *needed = cores[i].performance ? &need_p : &need_e;
    if (!*needed)
      continue;
    cpu_set_t siblings, active, common;
    if (core_siblings(cores[i].cpu, &present, &online, packages, ids,
                      &siblings) < 0)
      return -1;
    CPU_AND(&active, &siblings, &online);
    CPU_AND(&common, &active,
            cores[i].performance ? &performance : &efficiency);
    if (!CPU_COUNT(&active) || !CPU_EQUAL(&common, &active))
      return -1;
    CPU_OR(&selected, &selected, &siblings);
    (*needed)--;
  }
  if (need_p || need_e) {
    ds_error("Not enough online physical cores for %dP,%dE.", cfg->cpu_p_cores,
             cfg->cpu_e_cores);
    return -1;
  }
  cfg->cpuset_cpus = selected;
  cfg->cpuset_invalid = 0;
  return 0;
}
