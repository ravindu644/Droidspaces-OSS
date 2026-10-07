/*
 * Droidspaces v6 - Resource Visibility Virtualization
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "virtualize.h"
#include <time.h>

#define VPROC_PATH "/run/droidspaces/vproc"
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Internal helpers */

/* In-place overwrite: preserves the bind-mount inode (rename(2) breaks it).
 * Hardened with O_NOFOLLOW and filesystem type checks to prevent attacks. */
static int write_inplace(const char *path, const char *buf, size_t len) {
  /* Use O_NOFOLLOW to prevent the container from tricking us with symlinks */
  int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return -1;

  /* Security check: must be a regular file */
  struct stat st;
  if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
    close(fd);
    return -1;
  }

  /* Security check: must be on tmpfs (the container's /run/droidspaces/vproc)
   */
  struct statfs sfs;
  if (fstatfs(fd, &sfs) < 0 || sfs.f_type != TMPFS_MAGIC) {
    close(fd);
    return -1;
  }

  ssize_t w = write_all(fd, buf, len);
  if (w == (ssize_t)len) {
    /* Ignore ftruncate failure as we already wrote the data;
     * it might fail if the file is already the correct size. */
    if (ftruncate(fd, (off_t)len) < 0) {
      /* ignore */
    }
  }
  close(fd);
  return (w == (ssize_t)len) ? 0 : -1;
}

/* Compute container CPU count from quota/period, capped at host CPUs. */
static int container_cpus(struct ds_config *cfg) {
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

/* Per-resource content generators
 * Each returns a malloc'd buffer + length. Caller must free(). */

/* /proc/meminfo - virtualized when memory_limit > 0 */
static char *gen_meminfo(struct ds_config *cfg, size_t *out_len) {
  long long mem_limit = cfg->memory_limit; /* bytes */
  long long mem_used = -1, file_cache = 0;
  ds_cgroup_get_usage(cfg->container_name, &mem_used, &file_cache, NULL, NULL);
  if (mem_used < 0)
    mem_used = 0;
  if (file_cache > mem_used)
    file_cache = mem_used;

  FILE *f = fopen("/proc/meminfo", "r");
  if (!f)
    return NULL;

  /* Get host MemTotal (kB) for ratio calculation */
  long long host_total_kb = 0;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, "MemTotal: %lld", &host_total_kb) == 1)
      break;
  }
  rewind(f);

  double ratio = 1.0;
  if (mem_limit > 0 && host_total_kb > 0)
    ratio = (double)mem_limit / ((double)host_total_kb * 1024.0);

  /* Read memory.stat for accurate anon/file/slab breakdown. v1 names the
   * first two differently and has no slab line. */
  long long cg_anon = -1, cg_file = -1, cg_slab = -1;
  {
    char dir[PATH_MAX], path[PATH_MAX + 64], sbuf[4096];
    int v1 = ds_cgroup_ctrl_dir("memory", cfg->container_name, dir,
                                sizeof(dir)) == 1;
    const char *k_anon = v1 ? "total_rss " : "anon ";
    const char *k_file = v1 ? "total_cache " : "file ";
    snprintf(path, sizeof(path), "%s/memory.stat", dir);
    if (read_file(path, sbuf, sizeof(sbuf)) > 0) {
      char *p;
      if ((p = strstr(sbuf, k_anon)))
        sscanf(p + strlen(k_anon), "%lld", &cg_anon);
      if ((p = strstr(sbuf, k_file)))
        sscanf(p + strlen(k_file), "%lld", &cg_file);
      if (!v1 && (p = strstr(sbuf, "slab ")))
        sscanf(p + 5, "%lld", &cg_slab);
    }
  }

  size_t cap = 16384;
  char *buf = malloc(cap);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t off = 0;

  while (fgets(line, sizeof(line), f)) {
    if (off + 512 >= cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb;
    }

    char key[128];
    long long val;
    int has_kb = (strstr(line, " kB") != NULL);

    if (sscanf(line, "%127[^:]: %lld", key, &val) == 2 && mem_limit > 0) {
      long long lim_kb = mem_limit / 1024;

      if (!strcmp(key, "MemTotal"))
        val = lim_kb;
      else if (!strcmp(key, "MemFree"))
        val = (mem_limit - mem_used) / 1024 > 0 ? (mem_limit - mem_used) / 1024
                                                : 0;
      else if (!strcmp(key, "MemAvailable")) {
        /* What the cgroup has not consumed, plus its own file cache, which
         * the kernel gives back under pressure. free and fastfetch report
         * "used" as MemTotal - MemAvailable, so without the cache term they
         * count every file the container has read as used memory.
         * It must be the cgroup's cache, never the host's Cached, and it must
         * stay below MemTotal: at memAvailable >= memTotal those tools fall
         * back to raw fields and print nonsense (e.g. 16 EiB used). */
        val = lim_kb - (mem_used - file_cache) / 1024;
        if (val >= lim_kb)
          val = lim_kb - 1;
        if (val < 0)
          val = 0;
      } else if (!strcmp(key, "SwapTotal") || !strcmp(key, "SwapFree"))
        val = 0;
      else if (!strcmp(key, "AnonPages") && cg_anon >= 0)
        val = cg_anon / 1024;
      else if ((!strcmp(key, "Cached") || !strcmp(key, "Mapped")) &&
               cg_file >= 0)
        val = cg_file / 1024;
      else if (!strcmp(key, "Slab") && cg_slab >= 0)
        val = cg_slab / 1024;
      else
        val = (long long)(val * ratio);

      /* Cap all fields at MemTotal to avoid nonsensical values */
      if (has_kb && val > lim_kb)
        val = lim_kb;

      char fkey[130];
      snprintf(fkey, sizeof(fkey), "%s:", key);
      int n = snprintf(buf + off, cap - off, "%-16s%11lld kB\n", fkey, val);
      if (n > 0)
        off += (size_t)n;
      continue;
    }

    size_t len = strlen(line);
    if (off + len < cap) {
      memcpy(buf + off, line, len);
      off += len;
    }
  }
  fclose(f);
  buf[off] = '\0';
  *out_len = off;
  return buf;
}

/* /proc/cpuinfo - truncated to container_cpus() entries */
static char *gen_cpuinfo(struct ds_config *cfg, size_t *out_len) {
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

static char *gen_stat(struct ds_config *cfg, size_t *out_len) {
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
  long hz = sysconf(_SC_CLK_TCK);
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

/* Read container init PID's start time from /proc/<pid>/stat field 22.
 * Returns seconds-since-boot (same reference as CLOCK_BOOTTIME).
 * Ported from lxcfs get_reaper_start_time_in_sec(). */
static double container_start_time_secs(pid_t pid) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
  FILE *f = fopen(path, "r");
  if (!f)
    return -1.0;
  unsigned long long starttime = 0;
  /* skip fields 1-21, read field 22 (starttime) */
  int r = fscanf(f,
                 "%*d %*s %*c %*d %*d %*d %*d %*d %*u " /* 1-9  */
                 "%*u %*u %*u %*u %*u %*u %*d %*d "     /* 10-17 */
                 "%*d %*d %*d %*d %llu",                /* 18-22 */
                 &starttime);
  fclose(f);
  if (r != 1 || starttime == 0)
    return -1.0;
  long ticks = sysconf(_SC_CLK_TCK);
  if (ticks <= 0)
    return -1.0;
  return (double)starttime / (double)ticks;
}

/* /proc/uptime - lxcfs-style: uptime = CLOCK_BOOTTIME - container_init_start.
 * idle = up*ncpus - cpu_busy (from cgv2 cpu.stat).
 * Falls back to cfg->start_time only if container_pid is not yet available. */
static char *gen_uptime(struct ds_config *cfg, size_t *out_len) {
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

static char *gen_loadavg(struct ds_config *cfg, size_t *out_len) {
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
static char *gen_cpu_sysfs(struct ds_config *cfg, size_t *out_len) {
  int n = container_cpus(cfg);
  char *buf = malloc(32);
  if (!buf)
    return NULL;
  *out_len = (size_t)(n == 1 ? snprintf(buf, 32, "0\n")
                             : snprintf(buf, 32, "0-%d\n", n - 1));
  return buf;
}

/* Public API */

unsigned long ds_get_pid_ns_inode(pid_t pid) {
  char path[64];
  struct stat st;
  snprintf(path, sizeof(path), "/proc/%d/ns/pid", (int)pid);
  return (stat(path, &st) == 0) ? (unsigned long)st.st_ino : 0UL;
}

/* Write content to a tmpfs file, then bind-mount it over the real proc entry.
 * Called from inside the container (after pivot_root, before exec). */
static void bind_vfile(const char *vpath, const char *target,
                       const char *content,
                       size_t len __attribute__((unused))) {
  if (write_file(vpath, content) < 0)
    return;
  /* Ensure the target exists as a regular file for bind_mount.  O_NOFOLLOW so a
   * symlink planted at the final component is not followed (bind_mount also
   * rejects a symlinked target as defense-in-depth). */
  if (access(target, F_OK) != 0) {
    int fd = open(target, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0444);
    if (fd >= 0)
      close(fd);
  }
  if (bind_mount(vpath, target) < 0)
    ds_warn("[VIRT] bind_mount %s -> %s failed (continuing)", vpath, target);
}

/* Set process CPU affinity to match the virtualized CPU count.
 * This ensures tools like nproc and htop that use sched_getaffinity()
 * see the correct number of available cores. */
static void ds_virtualize_affinity(struct ds_config *cfg) {
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
void ds_virtualize_join(pid_t init_pid) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(init_pid, sizeof(mask), &mask) == 0 &&
      sched_setaffinity(0, sizeof(mask), &mask) < 0) {
  }
}

int ds_virtualize_init(struct ds_config *cfg) {
  int has_mem = (cfg->memory_limit > 0);
  int has_cpu = (cfg->cpu_quota > 0);

  /* Apply CPU affinity masking so syscall-based tools (nproc) are fooled */
  if (has_cpu)
    ds_virtualize_affinity(cfg);

  /* Create tmpfs backing store inside container */
  if (mkdir_p(VPROC_PATH, 0755) < 0) {
    ds_warn("[VIRT] mkdir_p %s: %s", VPROC_PATH, strerror(errno));
    return -1;
  }
  if (domount("none", VPROC_PATH, "tmpfs", MS_NOSUID | MS_NODEV,
              "mode=755,size=1M") < 0) {
    ds_warn("[VIRT] tmpfs mount failed: %s", strerror(errno));
    return -1;
  }

  /* Struct-of-arrays for proc files: name, generator, condition */
  struct {
    const char *name;
    char *(*gen)(struct ds_config *, size_t *);
    int enabled;
  } proc_files[] = {
      {"meminfo", gen_meminfo, has_mem}, {"cpuinfo", gen_cpuinfo, has_cpu},
      {"stat", gen_stat, has_cpu},       {"uptime", gen_uptime, 1},
      {"loadavg", gen_loadavg, 1},
  };

  for (size_t i = 0; i < ARRAY_SIZE(proc_files); i++) {
    if (!proc_files[i].enabled)
      continue;
    size_t len = 0;
    char *buf = proc_files[i].gen(cfg, &len);
    if (!buf)
      continue;

    char vpath[PATH_MAX], target[PATH_MAX];
    snprintf(vpath, sizeof(vpath), VPROC_PATH "/%s", proc_files[i].name);
    snprintf(target, sizeof(target), "/proc/%s", proc_files[i].name);
    bind_vfile(vpath, target, buf, len);
    free(buf);
  }

  /* CPU sysfs for nproc/htop (only when cpu-limited) */
  if (has_cpu) {
    char sysfs_base[PATH_MAX];
    snprintf(sysfs_base, sizeof(sysfs_base), VPROC_PATH "/cpu_sysfs");
    if (mkdir_p(sysfs_base, 0755) == 0) {
      /* 1. Populate the virtual /sys/devices/system/cpu with masked cpuX dirs.
       * We bind-mount the real allowed cpuN directories into our virtual
       * sysfs base. This preserves all sub-files (cpufreq, topology, etc). */
      int n = container_cpus(cfg);
      for (int i = 0; i < n; i++) {
        char vcpu[PATH_MAX + 32], realcpu[PATH_MAX];
        snprintf(vcpu, sizeof(vcpu), "%s/cpu%d", sysfs_base, i);
        snprintf(realcpu, sizeof(realcpu), "/sys/devices/system/cpu/cpu%d", i);
        if (access(realcpu, F_OK) == 0) {
          if (mkdir(vcpu, 0755) == 0) {
            if (bind_mount(realcpu, vcpu) < 0)
              ds_warn("[VIRT] bind_mount %s -> %s failed", realcpu, vcpu);
          }
        }
      }

      /* 2. Write virtualized online/possible/present files into the base. */
      const char *sysfs_names[] = {"online", "possible", "present"};
      for (size_t i = 0; i < ARRAY_SIZE(sysfs_names); i++) {
        size_t len = 0;
        char *buf = gen_cpu_sysfs(cfg, &len);
        if (!buf)
          continue;
        char vpath[PATH_MAX + 32];
        snprintf(vpath, sizeof(vpath), "%s/%s", sysfs_base, sysfs_names[i]);
        write_file(vpath, buf);
        free(buf);
      }

      /* 3. Bind-mount the entire virtual directory over the real one.
       * readdir() on /sys/devices/system/cpu will now only see cpu0...cpu(N-1).
       */
      if (bind_mount(sysfs_base, "/sys/devices/system/cpu") < 0)
        ds_warn("[VIRT] Failed to mask /sys/devices/system/cpu (htop may show "
                "host cores)");
    }
  }

  ds_log("[VIRT] Resource virtualization active (mem=%d cpu=%d uptime=1 "
         "loadavg=1)",
         has_mem, has_cpu);
  return 0;
}

void ds_virtualize_update(struct ds_config *cfg) {
  if (cfg->container_pid <= 0)
    return;

  /* PID-recycling guard: verify container identity before touching its fs */
  if (cfg->ns_inode) {
    unsigned long live = ds_get_pid_ns_inode(cfg->container_pid);
    if (live != cfg->ns_inode) {
      write_monitor_debug_log(cfg->container_name,
                              "[VIRT] update skipped: ns_inode mismatch "
                              "(expected %lu, got %lu) pid=%d",
                              cfg->ns_inode, live, (int)cfg->container_pid);
      return;
    }
  }

  /* Pre-check: if the virtualization tmpfs directory is not yet
   * mounted/created, skip the update silently to avoid startup race logs and
   * redundant generation overhead. */
  char vproc_dir[PATH_MAX];
  snprintf(vproc_dir, sizeof(vproc_dir), "/proc/%d/root" VPROC_PATH,
           (int)cfg->container_pid);
  struct stat st_dir;
  if (stat(vproc_dir, &st_dir) != 0 || !S_ISDIR(st_dir.st_mode)) {
    return;
  }

  int has_mem = (cfg->memory_limit > 0);
  int has_cpu = (cfg->cpu_quota > 0);

  /* Dynamic files only (cpuinfo is static after boot, skip it) */
  struct {
    const char *name;
    char *(*gen)(struct ds_config *, size_t *);
    int enabled;
  } dyn[] = {
      {"meminfo", gen_meminfo, has_mem},
      {"stat", gen_stat, has_cpu},
      {"uptime", gen_uptime, 1},
      {"loadavg", gen_loadavg, 1},
  };

  for (size_t i = 0; i < ARRAY_SIZE(dyn); i++) {
    if (!dyn[i].enabled)
      continue;
    size_t len = 0;
    char *buf = dyn[i].gen(cfg, &len);
    if (!buf) {
      write_monitor_debug_log(cfg->container_name,
                              "[VIRT] gen_%s returned NULL", dyn[i].name);
      continue;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%d/root/" VPROC_PATH "/%s",
             (int)cfg->container_pid, dyn[i].name);

    /* The directory is there but this file is not: the guest is taking its
     * /run apart on the way down, or this one file never got set up. Nothing
     * to refresh here and nothing to report, any more than for the directory
     * itself above; the other files still get theirs. */
    struct stat st;
    if (stat(path, &st) != 0) {
      free(buf);
      continue;
    }

    if (write_inplace(path, buf, len) < 0)
      write_monitor_debug_log(cfg->container_name,
                              "[VIRT] write_inplace failed: %s (%s)", path,
                              strerror(errno));
    free(buf);
  }
}
