/*
 * Droidspaces v7 - Resource Visibility Virtualization
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/* Set process CPU affinity to match the virtualized CPU count.
 * This ensures tools like nproc and htop that use sched_getaffinity()
 * see the correct number of available cores. */
void ds_virtualize_affinity(struct ds_config *cfg) {
  int n = container_cpus(cfg);
  int host = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (n >= host || n <= 0)
    return;

  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) < 0)
    return;

  int count = 0;
  cpu_set_t new_mask;
  CPU_ZERO(&new_mask);

  for (int i = 0; i < CPU_SETSIZE && count < n; i++) {
    if (CPU_ISSET(i, &mask)) {
      CPU_SET(i, &new_mask);
      count++;
    }
  }

  if (count > 0) {
    if (sched_setaffinity(0, sizeof(cpu_set_t), &new_mask) < 0) {
      /* Silently ignore if we can't set affinity (e.g. restrictive seccomp
       * on host), but it usually works since we are root here. */
    }
  }
}

/* A session entering a running container takes the CPU set its init has.
 * The limited set is applied to init at boot and inherited by everything it
 * forks, but an enter or run session is forked on the host and only joins the
 * namespaces, so without this it would see, and run on, every host CPU while
 * the rest of the container sees one. Call before setns(): init_pid is a host
 * PID. */
/* Qualcomm kernels with core_ctl drop isolated CPUs from sched_getaffinity(),
 * and which CPUs are isolated changes by the second. Copying that pins a
 * session to whichever of init's CPUs happened to be awake, or leaves it on
 * every host CPU when none were. Cpus_allowed_list is the mask init was given.
 */
static int task_cpus_allowed(pid_t pid, cpu_set_t *mask) {
  char path[64], line[1024];
  snprintf(path, sizeof(path), DS_PROC_STATUS_FMT, (int)pid);
  FILE *f = fopen(path, "re");
  if (!f)
    return -1;

  int found = 0;
  while (!found && fgets(line, sizeof(line), f)) {
    if (strncmp(line, "Cpus_allowed_list:", 18) != 0)
      continue;
    found = 1;
    /* "0-1,4,6-7" */
    for (char *p = line + 18; *p;) {
      char *end;
      long a = strtol(p, &end, 10);
      if (end == p) {
        p++;
        continue;
      }
      long b = a;
      if (*end == '-')
        b = strtol(end + 1, &end, 10);
      for (long i = a; i >= 0 && i <= b && i < CPU_SETSIZE; i++)
        CPU_SET((int)i, mask);
      p = end;
    }
  }
  fclose(f);
  return (found && CPU_COUNT(mask) > 0) ? 0 : -1;
}

void ds_virtualize_join(pid_t init_pid) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (task_cpus_allowed(init_pid, &mask) < 0 &&
      sched_getaffinity(init_pid, sizeof(mask), &mask) < 0)
    return;
  if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
  }
}
