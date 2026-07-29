/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Live container status for `show --format` and `info --format`.
 *
 * Everything is read from the host side. Kernel 3.10 compatible.
 *
 *   uptime  field 22 (starttime) of /proc/<init_pid>/stat subtracted from
 *           /proc/uptime.
 *   memory  any process whose ns/pid link matches the container init's
 *           namespace belongs to it. Sum VmRSS.
 *   cpu     same walk, sum utime+stime jiffies, two samples 250 ms apart,
 *           divided by the host CPU delta. Per-mille avoids integer floor
 *           on sub-1% values.
 *   ip/ip6  setns() into the container's netns and getifaddrs(), no `ip`
 *           binary needed inside the rootfs.
 *
 * One walk pair serves every container at once, so the app's heartbeat costs
 * the same for ten containers as for one. */

#include "droidspace.h"
#include <ifaddrs.h>
#include <time.h>

static void parse_pretty_name(FILE *fp, char *buf, size_t size) {
  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
      char *val = line + 12;
      size_t len = strlen(val);
      while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '"'))
        val[--len] = '\0';
      if (val[0] == '"') {
        val++;
        len--;
      }
      if (len >= size)
        len = size - 1;
      snprintf(buf, size, "%.*s", (int)len, val);
      return;
    }
  }
}

void get_os_pretty(const char *osrelease_path, char *buf, size_t size) {
  if (!buf || size == 0)
    return;
  buf[0] = '\0';

  FILE *fp = fopen(osrelease_path, "r");
  if (!fp)
    return;

  parse_pretty_name(fp, buf, size);
  fclose(fp);
}

/* Append one address to a comma separated list. Returns 0 once it is full. */
static int addr_append(char *buf, size_t size, size_t *pos, const char *ip) {
  int w = snprintf(buf + *pos, size - *pos, "%s%s", *pos ? ", " : "", ip);
  if (w < 0 || (size_t)w >= size - *pos) {
    buf[*pos] = '\0';
    return 0;
  }
  *pos += (size_t)w;
  return 1;
}

/* Addresses of the container's netns, comma separated: IPv4 without loopback,
 * IPv6 without loopback and link-local. Host-mode containers share our netns,
 * so setns() is a no-op there. */
static void netns_addrs(pid_t pid, char *v4, size_t v4_size, char *v6,
                        size_t v6_size) {
  v4[0] = v6[0] = '\0';
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/ns/net", (int)pid);
  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  int ns_fd = open(path, O_RDONLY | O_CLOEXEC);
  if (self_fd >= 0 && ns_fd >= 0 && setns(ns_fd, CLONE_NEWNET) == 0) {
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) == 0) {
      size_t pos4 = 0, pos6 = 0;
      int room4 = 1, room6 = 1;
      for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
        char ip[INET6_ADDRSTRLEN];
        if (!p->ifa_addr)
          continue;
        if (p->ifa_addr->sa_family == AF_INET && room4) {
          inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, ip,
                    sizeof(ip));
          if (strncmp(ip, "127.", 4) != 0)
            room4 = addr_append(v4, v4_size, &pos4, ip);
        } else if (p->ifa_addr->sa_family == AF_INET6 && room6) {
          struct in6_addr *a = &((struct sockaddr_in6 *)p->ifa_addr)->sin6_addr;
          if (IN6_IS_ADDR_LOOPBACK(a) || IN6_IS_ADDR_LINKLOCAL(a))
            continue;
          inet_ntop(AF_INET6, a, ip, sizeof(ip));
          room6 = addr_append(v6, v6_size, &pos6, ip);
        }
      }
      freeifaddrs(ifa);
    }
    setns(self_fd, CLONE_NEWNET);
  }
  if (self_fd >= 0)
    close(self_fd);
  if (ns_fd >= 0)
    close(ns_fd);
}

/* utime + stime, fields 14 and 15 of /proc/<pid>/stat. */
static long long proc_cpu_ticks(const char *pid_name) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%s/stat", pid_name);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  long long utime = 0, stime = 0;
  for (int i = 1; i <= 13; i++)
    if (fscanf(f, "%*s") == EOF)
      break;
  if (fscanf(f, "%lld %lld", &utime, &stime) != 2)
    utime = stime = 0;
  fclose(f);
  return utime + stime;
}

static long proc_rss_kb(const char *pid_name) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%s/status", pid_name);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  long rss = 0;
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      if (sscanf(line + 6, "%ld", &rss) != 1)
        rss = 0;
      break;
    }
  }
  fclose(f);
  return rss;
}

static long long host_cpu_ticks(void) {
  FILE *f = fopen("/proc/stat", "r");
  if (!f)
    return 0;
  long long u, n, s, i, iow, irq, sirq, total = 0;
  if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld", &u, &n, &s, &i, &iow,
             &irq, &sirq) == 7)
    total = u + n + s + i + iow + irq + sirq;
  fclose(f);
  return total;
}

static long host_mem_total_kb(void) {
  FILE *f = fopen("/proc/meminfo", "r");
  if (!f)
    return 0;
  long total = 0;
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "MemTotal:", 9) == 0) {
      if (sscanf(line + 9, "%ld", &total) != 1)
        total = 0;
      break;
    }
  }
  fclose(f);
  return total;
}

/* One pass over /proc. Every process whose PID namespace matches entry i
 * adds its CPU ticks to cpu[i], and its RSS to st[i] when want_rss is set. */
static void walk_proc(struct ds_status *st, char (*ns)[64], int n,
                      long long *cpu, int want_rss) {
  DIR *d = opendir("/proc");
  if (!d)
    return;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] < '1' || de->d_name[0] > '9')
      continue;
    char path[PATH_MAX], link[64];
    snprintf(path, sizeof(path), "/proc/%s/ns/pid", de->d_name);
    ssize_t r = readlink(path, link, sizeof(link) - 1);
    if (r <= 0)
      continue;
    link[r] = '\0';
    int i = 0;
    while (i < n && strcmp(link, ns[i]) != 0)
      i++;
    if (i == n)
      continue;
    cpu[i] += proc_cpu_ticks(de->d_name);
    if (want_rss)
      st[i].ram_used_kb += proc_rss_kb(de->d_name);
  }
  closedir(d);
}

long ds_collect_status(struct ds_status *st, int n) {
  long ram_total = host_mem_total_kb();
  if (n <= 0)
    return ram_total;

  char (*ns)[64] = calloc((size_t)n, sizeof(*ns));
  long long *t1 = calloc((size_t)n, sizeof(*t1));
  long long *t2 = calloc((size_t)n, sizeof(*t2));
  if (!ns || !t1 || !t2) {
    free(ns);
    free(t1);
    free(t2);
    return ram_total;
  }

  for (int i = 0; i < n; i++) {
    char path[PATH_MAX];
    st[i].ram_used_kb = 0;
    st[i].cpu_permill = 0;
    st[i].uptime_sec = ds_get_container_uptime(st[i].pid);
    st[i].os[0] = '\0';
    if (build_proc_root_path(st[i].pid, "/etc/os-release", path,
                             sizeof(path)) == 0)
      get_os_pretty(path, st[i].os, sizeof(st[i].os));
    netns_addrs(st[i].pid, st[i].ip, sizeof(st[i].ip), st[i].ip6,
                sizeof(st[i].ip6));
    /* An unreadable link leaves ns[i] empty, which no process can match. */
    snprintf(path, sizeof(path), "/proc/%d/ns/pid", (int)st[i].pid);
    ssize_t r = readlink(path, ns[i], sizeof(ns[i]) - 1);
    ns[i][r > 0 ? r : 0] = '\0';
  }

  walk_proc(st, ns, n, t1, 1);
  long long host1 = host_cpu_ticks();

  /* 250ms measurement window - short enough for a responsive UI,
   * long enough for a meaningful CPU delta (1 jiffie = 10ms at HZ=100,
   * so 250ms gives 25-jiffie resolution = ~0.4% minimum granularity). */
  struct timespec ts = {0, 250000000L};
  nanosleep(&ts, NULL);

  walk_proc(st, ns, n, t2, 0);
  long long host2 = host_cpu_ticks();

  long long delta_host = host2 - host1;
  for (int i = 0; i < n; i++) {
    long long delta = t2[i] - t1[i];
    if (delta < 0)
      delta = 0;
    long permill = delta_host > 0 ? (long)(delta * 1000 / delta_host) : 0;
    st[i].cpu_permill = permill > 1000 ? 1000 : permill;
  }

  free(ns);
  free(t1);
  free(t2);
  return ram_total;
}

/* JSON output. We only ever write flat objects of strings and integers, so
 * these three functions are the whole encoder. */

void ds_json_str(const char *key, const char *val, int *first) {
  printf("%s\"%s\":\"", *first ? "" : ",", key);
  *first = 0;
  for (const unsigned char *p = (const unsigned char *)val; val && *p; p++) {
    if (*p == '"' || *p == '\\')
      printf("\\%c", *p);
    else if (*p < 0x20)
      printf("\\u%04x", *p);
    else
      putchar(*p);
  }
  putchar('"');
}

void ds_json_int(const char *key, long long val, int *first) {
  printf("%s\"%s\":%lld", *first ? "" : ",", key, val);
  *first = 0;
}

void ds_json_status(const struct ds_status *st, int *first) {
  char uptime[128];
  ds_format_uptime(st->uptime_sec, uptime, sizeof(uptime));
  ds_json_str("name", st->name, first);
  ds_json_int("pid", st->pid, first);
  ds_json_str("os", st->os, first);
  ds_json_str("hostname", st->hostname, first);
  ds_json_str("ip", st->ip, first);
  ds_json_str("ip6", st->ip6, first);
  ds_json_str("anland_sock", st->anland_sock, first);
  ds_json_int("uptime_sec", st->uptime_sec, first);
  ds_json_str("uptime", uptime, first);
  ds_json_int("ram_used_kb", st->ram_used_kb, first);
  ds_json_int("cpu_permill", st->cpu_permill, first);
}
