/*
 * Droidspaces v7 - Resource Visibility Virtualization
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <time.h>

#include "virt/internal.h"

/* Compute container CPU count from quota/period, capped at host CPUs. */
int container_cpus(struct ds_config *cfg) {
  int host = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (host < 1)
    host = 1;
  if (cfg->cpu_quota <= 0 || cfg->cpu_period <= 0)
    return host;
  int n = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
  if (n < 1)
    n = 1;
  if (n > host)
    n = host;
  return n;
}

/* /proc/cpuinfo - truncated to container_cpus() entries */
char *gen_cpuinfo(struct ds_config *cfg, size_t *out_len) {
  int max_cpus = container_cpus(cfg);
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (!f)
    return NULL;

  size_t cap = 65536;
  char *buf = malloc(cap);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t off = 0;
  int cur_cpu = -1;
  char line[4096];

  while (fgets(line, sizeof(line), f)) {
    int id;
    if (sscanf(line, "processor : %d", &id) == 1)
      cur_cpu = id;
    if (cur_cpu >= max_cpus)
      break;
    size_t len = strlen(line);
    if (off + len + 1 >= cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb;
    }
    memcpy(buf + off, line, len);
    off += len;
  }
  fclose(f);
  buf[off] = '\0';
  *out_len = off;
  return buf;
}

static double container_start_time_secs(pid_t pid);

/* Seconds since the container's init started. */
static double container_uptime_secs(struct ds_config *cfg) {
  struct timespec boot;
  clock_gettime(CLOCK_BOOTTIME, &boot);
  double boottime = (double)boot.tv_sec + (double)boot.tv_nsec / 1e9;

  double up = -1.0;
  if (cfg->container_pid > 0) {
    double proc_start = container_start_time_secs(cfg->container_pid);
    if (proc_start > 0.0)
      up = boottime - proc_start;
  }
  if (up < 0.0) {
    up = boottime - ((double)cfg->start_time.tv_sec +
                     (double)cfg->start_time.tv_nsec / 1e9);
  }
  return up < 0.0 ? 0.0 : up;
}

/* /proc/stat for a container with a CPU quota: max_cpus cpuN lines whose
 * figures are the container's own.
 *
 * The cpu lines of the host cannot be used, not even a subset of them. The
 * container's threads run on every real core, so "the first N host CPUs" only
 * shows what those N cores happen to be doing, and a container pinned at its
 * quota reads as nearly idle. This is what LXCFS's cpuview fixes, and the
 * result here is the same: busy time is the cgroup's, idle is what is left of
 * the time the quota allowed, so a container at its quota reads 100% however
 * many real cores it was spread over. Like LXCFS, only user, system and idle
 * are reported and the rest of the file is the host's.
 *
 * LXCFS needs a per-cgroup history to turn deltas into counters. We do not:
 * the cgroup is created for this boot, so its totals already start at zero
 * and only grow.
 *
 * Without cgroup accounting for the container, fall back to the host's first
 * max_cpus lines, as LXCFS does too. */
#define STAT_CPU_FMT "cpu%s %llu 0 %llu %llu 0 0 0 0 0 0\n"

char *gen_stat(struct ds_config *cfg, size_t *out_len) {
  int max_cpus = container_cpus(cfg);
  FILE *f = fopen("/proc/stat", "r");
  if (!f)
    return NULL;

  size_t cap = 65536;
  char *buf = malloc(cap);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t off = 0;
  char line[2048];

  long long user_us = 0, system_us = 0;
  long hz = ds_clk_tck();
  int own = cfg->cpu_period > 0 && hz > 0 &&
            ds_cgroup_cpu_times(cfg->container_name, &user_us, &system_us) == 0;
  unsigned long long own_user = 0, own_system = 0, own_idle = 0;
  if (own) {
    own_user = (unsigned long long)(user_us * hz / 1000000);
    own_system = (unsigned long long)(system_us * hz / 1000000);

    /* The quota as a core count, not rounded up: half a core fully used must
     * leave no idle time on the one CPU the container sees. */
    double allowed = (double)cfg->cpu_quota / (double)cfg->cpu_period;
    if (allowed > max_cpus)
      allowed = max_cpus;
    double idle = allowed * container_uptime_secs(cfg) * (double)hz -
                  (double)(own_user + own_system);

    /* A counter must not go backwards, and the two clocks behind this one
     * are read at slightly different moments. A new init means a new boot
     * with fresh cgroups, so the floor starts over. */
    static pid_t idle_pid;
    static unsigned long long idle_floor;
    if (idle_pid != cfg->container_pid) {
      idle_pid = cfg->container_pid;
      idle_floor = 0;
    }
    own_idle = idle > 0.0 ? (unsigned long long)idle : 0;
    if (own_idle < idle_floor)
      own_idle = idle_floor;
    idle_floor = own_idle;
  }

  /* Pass 1 (fallback only): accumulate aggregate from allowed cpuN lines */
  unsigned long long su = 0, sn = 0, ss = 0, si = 0, sio = 0, sir = 0,
                     ssoft = 0, sst = 0, sgu = 0, sgn = 0;
  while (!own && fgets(line, sizeof(line), f)) {
    int id;
    if (sscanf(line, "cpu%d", &id) == 1 && id < max_cpus) {
      unsigned long long u, n, s, i, io, ir, sf, st, gu, gn;
      u = n = s = i = io = ir = sf = st = gu = gn = 0;
      sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
             &u, &n, &s, &i, &io, &ir, &sf, &st, &gu, &gn);
      su += u;
      sn += n;
      ss += s;
      si += i;
      sio += io;
      sir += ir;
      ssoft += sf;
      sst += st;
      sgu += gu;
      sgn += gn;
    }
  }
  rewind(f);

  /* Pass 2: emit with recomputed aggregate */
  int agg_done = 0;
  while (fgets(line, sizeof(line), f)) {
    /* A single /proc/stat line (intr, softirq) can be up to sizeof(line)-1
     * bytes on hosts with many IRQ vectors.  A fixed-headroom guard is not
     * enough: size the growth to the actual line length (plus slack for the
     * generated aggregate "cpu" line and the trailing NUL) before either the
     * memcpy passthrough or the snprintf below can write. */
    size_t need = strlen(line) + 512 + (size_t)max_cpus * 96;
    if (off + need >= cap) {
      while (off + need >= cap)
        cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb;
    }
    if (own) {
      if (strncmp(line, "cpu", 3) != 0) {
        size_t len = strlen(line);
        memcpy(buf + off, line, len);
        off += len;
      } else if (!agg_done) {
        /* The whole cpu block at once: the aggregate, then it split evenly
         * over the visible CPUs with the remainder on cpu0. LXCFS packs the
         * usage into them in order; the totals are the same, and this way
         * one bar is not pinned while the next sits empty. */
        int n = snprintf(buf + off, cap - off, STAT_CPU_FMT, " ", own_user,
                         own_system, own_idle);
        if (n > 0)
          off += (size_t)n;
        unsigned long long cpus = (unsigned long long)max_cpus;
        for (int k = 0; k < max_cpus; k++) {
          char id[16];
          snprintf(id, sizeof(id), "%d", k);
          n = snprintf(buf + off, cap - off, STAT_CPU_FMT, id,
                       own_user / cpus + (k ? 0 : own_user % cpus),
                       own_system / cpus + (k ? 0 : own_system % cpus),
                       own_idle / cpus + (k ? 0 : own_idle % cpus));
          if (n > 0)
            off += (size_t)n;
        }
        agg_done = 1;
      }
      continue;
    }
    if (strncmp(line, "cpu ", 4) == 0) {
      if (!agg_done) {
        int n =
            snprintf(buf + off, cap - off,
                     "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                     su, sn, ss, si, sio, sir, ssoft, sst, sgu, sgn);
        if (n > 0)
          off += (size_t)n;
        agg_done = 1;
      }
      continue;
    }
    int id;
    if (sscanf(line, "cpu%d", &id) == 1 && id >= max_cpus)
      continue;
    size_t len = strlen(line);
    memcpy(buf + off, line, len);
    off += len;
  }
  fclose(f);
  buf[off] = '\0';
  *out_len = off;
  return buf;
}

/* The container's CPU busy time in seconds, from its cgroup (lxcfs calls this
 * get_reaper_busy()). -1 when the kernel keeps no such figure for it. */
static double cg_cpu_busy_secs(const char *container_name) {
  long long usec = -1;
  ds_cgroup_get_usage(container_name, NULL, NULL, &usec, NULL);
  return usec < 0 ? -1.0 : (double)usec / 1e6;
}

/* When the container's init started, in seconds since boot (the
 * CLOCK_BOOTTIME reference), or -1. */
static double container_start_time_secs(pid_t pid) {
  unsigned long long starttime = proc_starttime(pid);
  return starttime ? (double)starttime / (double)ds_clk_tck() : -1.0;
}

/* /proc/uptime - lxcfs-style: uptime = CLOCK_BOOTTIME - container_init_start.
 * idle = up*ncpus - cpu_busy (from cgv2 cpu.stat).
 * Falls back to cfg->start_time only if container_pid is not yet available. */
char *gen_uptime(struct ds_config *cfg, size_t *out_len) {
  double up = container_uptime_secs(cfg);

  int ccpus = container_cpus(cfg);
  double busy = cg_cpu_busy_secs(cfg->container_name);
  double idle = (busy >= 0.0) ? (up * ccpus - busy) : (up * ccpus * 0.1);
  if (idle < 0.0)
    idle = 0.0;

  char *buf = malloc(64);
  if (!buf)
    return NULL;
  /* snprintf returns the length it *would* have written; clamp to the bytes
   * actually in the buffer so a consumer's write_all() cannot over-read the
   * heap if the formatted output ever exceeds 63 chars. */
  int n = snprintf(buf, 64, "%.2f %.2f\n", up, idle);
  if (n < 0) {
    free(buf);
    return NULL;
  }
  *out_len = (n < 64) ? (size_t)n : 63;
  return buf;
}

/* /proc/loadavg: the container's own load average.
 *
 * The kernel's figure counts every runnable task on the machine, so inside a
 * container it says how busy the phone is, not the container. LXCFS keeps a
 * per-container one by sampling the cgroup's runnable tasks every five
 * seconds and feeding them through the kernel's own decay, and this does the
 * same from the monitor's heartbeat. It starts at 0.00 on every boot.
 *
 * Without a cgroup to count, fall back to the host's load scaled by the
 * share of CPUs the container sees. */

/* The kernel's fixed-point load average, kernel/sched/loadavg.c */
#define LOAD_FSHIFT 11
#define LOAD_FIXED_1 (1UL << LOAD_FSHIFT)
#define LOAD_SAMPLE_SECS 5

static unsigned long calc_load(unsigned long load, unsigned long exp,
                               unsigned long active) {
  unsigned long newload = load * exp + active * (LOAD_FIXED_1 - exp);
  if (active >= load)
    newload += LOAD_FIXED_1 - 1;
  return newload / LOAD_FIXED_1;
}

static char *gen_own_loadavg(struct ds_config *cfg, size_t *out_len) {
  /* Decay factors for 1, 5 and 15 minutes at a five second sample */
  static const unsigned long exps[3] = {1884, 2014, 2037};
  static unsigned long avg[3];
  static int running, total;
  static pid_t boot_pid;
  static time_t sampled;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (boot_pid != cfg->container_pid) {
    /* A new init is a new boot: the load starts over */
    boot_pid = cfg->container_pid;
    avg[0] = avg[1] = avg[2] = 0;
    sampled = 0;
  }
  if (!sampled || now.tv_sec - sampled >= LOAD_SAMPLE_SECS) {
    if (ds_cgroup_count_tasks(cfg->container_name, &running, &total) < 0)
      return NULL;
    /* The first sample only seeds the task counts */
    for (int i = 0; sampled && i < 3; i++)
      avg[i] =
          calc_load(avg[i], exps[i], (unsigned long)running * LOAD_FIXED_1);
    sampled = now.tv_sec;
  }

  char *buf = malloc(128);
  if (!buf)
    return NULL;
  unsigned long v[3];
  for (int i = 0; i < 3; i++)
    v[i] = avg[i] + LOAD_FIXED_1 / 200; /* round to two decimals */
  int n = snprintf(
      buf, 128, "%lu.%02lu %lu.%02lu %lu.%02lu %d/%d 0\n", v[0] >> LOAD_FSHIFT,
      ((v[0] & (LOAD_FIXED_1 - 1)) * 100) >> LOAD_FSHIFT, v[1] >> LOAD_FSHIFT,
      ((v[1] & (LOAD_FIXED_1 - 1)) * 100) >> LOAD_FSHIFT, v[2] >> LOAD_FSHIFT,
      ((v[2] & (LOAD_FIXED_1 - 1)) * 100) >> LOAD_FSHIFT, running, total);
  *out_len = (n > 0 && n < 128) ? (size_t)n : 0;
  return buf;
}

char *gen_loadavg(struct ds_config *cfg, size_t *out_len) {
  char *own = gen_own_loadavg(cfg, out_len);
  if (own)
    return own;

  FILE *f = fopen("/proc/loadavg", "r");
  if (!f)
    return NULL;
  double l1, l5, l15;
  int run, tot;
  if (fscanf(f, "%lf %lf %lf %d/%d %*d", &l1, &l5, &l15, &run, &tot) != 5) {
    fclose(f);
    return NULL;
  }
  fclose(f);

  int hcpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
  int ccpus = container_cpus(cfg);
  double r = (double)ccpus / (double)hcpus;

  int srun = (int)(run * r);
  if (srun < 1 && run > 0)
    srun = 1;
  int stot = (int)(tot * r);
  if (stot < 1)
    stot = 1;

  char *buf = malloc(1024);
  if (!buf)
    return NULL;
  *out_len = (size_t)snprintf(buf, 1024, "%.2f %.2f %.2f %d/%d 0\n", l1 * r,
                              l5 * r, l15 * r, srun, stot);
  return buf;
}

/* /sys/devices/system/cpu/{online,possible,present} - for nproc */
char *gen_cpu_sysfs(struct ds_config *cfg, size_t *out_len) {
  int n = container_cpus(cfg);
  char *buf = malloc(32);
  if (!buf)
    return NULL;
  *out_len = (size_t)(n == 1 ? snprintf(buf, 32, "0\n")
                             : snprintf(buf, 32, "0-%d\n", n - 1));
  return buf;
}
