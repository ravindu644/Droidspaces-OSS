/*
 * Droidspaces v7 - Resource Visibility Virtualization
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "virt/internal.h"

/* /proc/meminfo - virtualized when memory_limit > 0 */
char *gen_meminfo(struct ds_config *cfg, size_t *out_len) {
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
      int swap_line = !strcmp(key, "SwapTotal") || !strcmp(key, "SwapFree") ||
                      !strcmp(key, "SwapCached");

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
      } else if (swap_line) {
        /* Allowed swap is the host's, so show the host's */
        if (!cfg->swap_uncapped)
          val = 0;
      } else if (!strcmp(key, "AnonPages") && cg_anon >= 0)
        val = cg_anon / 1024;
      else if ((!strcmp(key, "Cached") || !strcmp(key, "Mapped")) &&
               cg_file >= 0)
        val = cg_file / 1024;
      else if (!strcmp(key, "Slab") && cg_slab >= 0)
        val = cg_slab / 1024;
      else
        val = (long long)(val * ratio);

      /* Cap all fields at MemTotal to avoid nonsensical values */
      if (has_kb && val > lim_kb && !swap_line)
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

/* /proc/swaps: a memory-limited container gets no swap unless allowed, and
 * meminfo says so, but fastfetch and swapon list the devices from here and
 * would show the host's zram filling up. A header with no devices is "no
 * swap". With swap allowed the host's file is the truth and stays. */
char *gen_swaps(struct ds_config *cfg, size_t *out_len) {
  (void)cfg;
  char *buf = strdup("Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n");
  if (buf)
    *out_len = strlen(buf);
  return buf;
}
