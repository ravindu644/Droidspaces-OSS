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

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------*/

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
    (void)ftruncate(fd, (off_t)len);
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

/* Read a cgroup v2 file from the container's delegated slice. */
static long long read_cg_ll(const char *container_name, const char *file) {
  char safe_name[256];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  char path[PATH_MAX];
  char buf[64];
  snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/%s", safe_name,
           file);
  if (read_file(path, buf, sizeof(buf)) <= 0)
    return -1;
  if (strncmp(buf, "max", 3) == 0)
    return -1; /* unlimited */
  char *end;
  long long v = strtoll(buf, &end, 10);
  return (end == buf) ? -1 : v;
}

/* ---------------------------------------------------------------------------
 * Per-resource content generators
 * Each returns a malloc'd buffer + length. Caller must free().
 * ---------------------------------------------------------------------------*/

/* /proc/meminfo - virtualized when memory_limit > 0 */
static char *gen_meminfo(struct ds_config *cfg, size_t *out_len) {
  long long mem_limit = cfg->memory_limit; /* bytes */
  long long mem_used = read_cg_ll(cfg->container_name, "memory.current");
  if (mem_used < 0)
    mem_used = 0;

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

  /* Read memory.stat for accurate anon/file/slab breakdown */
  long long cg_anon = -1, cg_file = -1, cg_slab = -1;
  {
    char safe_name[256];
    sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));
    char path[PATH_MAX], sbuf[4096];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/memory.stat",
             safe_name);
    if (read_file(path, sbuf, sizeof(sbuf)) > 0) {
      char *p;
      if ((p = strstr(sbuf, "anon ")))
        sscanf(p + 5, "%lld", &cg_anon);
      if ((p = strstr(sbuf, "file ")))
        sscanf(p + 5, "%lld", &cg_file);
      if ((p = strstr(sbuf, "slab ")))
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
        /* MemAvailable = MemTotal - actual_cgroup_usage.
         * Do NOT add cg_file here: host page cache can be huge and pushes
         * MemAvailable >= MemTotal, triggering fastfetch/free's fallback
         * guard (memAvailable >= memTotal) which reads raw host fields and
         * produces completely wrong numbers (e.g. 16 EiB used).
         * Simple and correct: what the cgroup hasn't consumed is available. */
        val = lim_kb - mem_used / 1024;
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

/* /proc/stat - recomputed aggregate + only max_cpus cpuN lines */
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

  /* Pass 1: accumulate aggregate from allowed cpuN lines */
  unsigned long long su = 0, sn = 0, ss = 0, si = 0, sio = 0, sir = 0,
                     ssoft = 0, sst = 0, sgu = 0, sgn = 0;
  while (fgets(line, sizeof(line), f)) {
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
    if (off + 1024 >= cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb;
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

/* /proc/uptime - per-container uptime from cfg->start_time */
static char *gen_uptime(struct ds_config *cfg, size_t *out_len) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double up = (double)(now.tv_sec - cfg->start_time.tv_sec) +
              (double)(now.tv_nsec - cfg->start_time.tv_nsec) / 1e9;
  if (up < 0.0)
    up = 0.0;

  /* Scale idle time proportionally to container CPU count */
  double host_idle = 0.0;
  {
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
      double hu;
      if (fscanf(f, "%lf %lf", &hu, &host_idle) != 2)
        host_idle = 0.0;
      fclose(f);
    }
  }
  int hcpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
  int ccpus = container_cpus(cfg);
  double idle = host_idle * ((double)ccpus / (double)hcpus);
  if (idle > up * ccpus)
    idle = up * ccpus;

  char *buf = malloc(1024);
  if (!buf)
    return NULL;
  *out_len = (size_t)snprintf(buf, 1024, "%.2f %.2f\n", up, idle);
  return buf;
}

/* /proc/loadavg - CPU-ratio scaled */
static char *gen_loadavg(struct ds_config *cfg, size_t *out_len) {
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

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

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
  /* Ensure the target exists as a regular file for bind_mount */
  if (access(target, F_OK) != 0) {
    int fd = open(target, O_WRONLY | O_CREAT | O_CLOEXEC, 0444);
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

int ds_virtualize_init(struct ds_config *cfg) {
  int has_mem = (cfg->memory_limit > 0);
  int has_cpu = (cfg->cpu_quota > 0);

  if (!has_mem && !has_cpu)
    return 0; /* nothing to virtualize */

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
      {"stat", gen_stat, has_cpu},       {"uptime", gen_uptime, has_cpu},
      {"loadavg", gen_loadavg, has_cpu},
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
        char vcpu[PATH_MAX], realcpu[PATH_MAX];
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
        char vpath[PATH_MAX];
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

  ds_log("[VIRT] Resource virtualization active (mem=%d cpu=%d)", has_mem,
         has_cpu);
  return 0;
}

void ds_virtualize_update(struct ds_config *cfg) {
  if (!cfg->container_pid || cfg->container_pid <= 0)
    return;
  if (!cfg->memory_limit && !cfg->cpu_quota)
    return;

  /* PID-recycling guard: verify container identity before touching its fs */
  if (cfg->ns_inode && ds_get_pid_ns_inode(cfg->container_pid) != cfg->ns_inode)
    return;

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
      {"uptime", gen_uptime, has_cpu},
      {"loadavg", gen_loadavg, has_cpu},
  };

  for (size_t i = 0; i < ARRAY_SIZE(dyn); i++) {
    if (!dyn[i].enabled)
      continue;
    size_t len = 0;
    char *buf = dyn[i].gen(cfg, &len);
    if (!buf)
      continue;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%d/root/" VPROC_PATH "/%s",
             (int)cfg->container_pid, dyn[i].name);

    /* Only update if the vfile exists (i.e. init ran successfully) */
    struct stat st;
    if (stat(path, &st) == 0)
      write_inplace(path, buf, len);
    free(buf);
  }
}
