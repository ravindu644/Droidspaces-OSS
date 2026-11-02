/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <sys/auxv.h>
#include <time.h>

/* PID collection - read numeric entries from /proc */

int collect_pids(pid_t **pids_out, size_t *count_out) {
  if (!pids_out || !count_out)
    return -1;

  *pids_out = NULL;
  *count_out = 0;

  DIR *d = opendir("/proc");
  if (!d)
    return -1;

  size_t cap = 256;
  size_t count = 0;

  pid_t *pids = malloc(cap * sizeof(pid_t));
  if (!pids) {
    closedir(d);
    return -1;
  }

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {

    /* Do NOT trust ent->d_type.
       Some filesystems (including Android /proc) return DT_UNKNOWN. */

    char *end;
    errno = 0;
    long val = strtol(ent->d_name, &end, 10);

    /* Must be a pure positive number */
    if (errno != 0 || *end != '\0' || val <= 0)
      continue;

    if (count >= cap) {
      cap *= 2;
      pid_t *tmp = realloc(pids, cap * sizeof(pid_t));
      if (!tmp) {
        free(pids);
        closedir(d);
        return -1;
      }
      pids = tmp;
    }

    pids[count++] = (pid_t)val;
  }

  closedir(d);

  *pids_out = pids;
  *count_out = count;
  return 0;
}

/* /proc path helpers */

int build_proc_root_path(pid_t pid, const char *suffix, char *buf,
                         size_t size) {
  int r;
  if (suffix && suffix[0])
    r = snprintf(buf, size, DS_PROC_ROOT_FMT "%s", pid, suffix);
  else
    r = snprintf(buf, size, DS_PROC_ROOT_FMT, pid);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* Open path, absolute as the container sees it, under pid's root without
 * following a symlink at any step. Through /proc/<pid>/root an absolute
 * symlink resolves against our root, the host's, and ".." climbs out of the
 * container's, so root inside the container could otherwise point any step
 * of a host write at a host file. O_NOFOLLOW alone guards only the last
 * step. -1 with errno set. */
int ds_open_in_root(pid_t pid, const char *path, int flags) {
  char root[64], buf[PATH_MAX];
  if (snprintf(buf, sizeof(buf), "%s", path) >= (int)sizeof(buf)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  snprintf(root, sizeof(root), DS_PROC_ROOT_FMT, pid);
  int dir = open(root, O_PATH | O_DIRECTORY | O_CLOEXEC);
  char *save, *part = strtok_r(buf, "/", &save);
  errno = dir < 0 ? errno : EINVAL;
  while (dir >= 0 && part) {
    char *next = strtok_r(NULL, "/", &save);
    int fd = -1;
    if (strcmp(part, "..") == 0)
      errno = EPERM;
    else
      fd = openat(dir, part,
                  next ? O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
                       : flags | O_NOFOLLOW | O_CLOEXEC);
    int err = errno;
    close(dir);
    errno = err;
    if (!next || fd < 0)
      return fd;
    dir = fd;
    part = next;
  }
  if (dir >= 0)
    close(dir);
  return -1;
}

/* System helpers */

int get_kernel_version(int *major, int *minor) {
  struct utsname uts;
  if (uname(&uts) < 0)
    return -1;

  if (sscanf(uts.release, "%d.%d", major, minor) != 2)
    return -1;

  return 0;
}

void check_kernel_recommendation(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) < 0)
    return;

  if (major < DS_RECOMMENDED_KERNEL_MAJOR ||
      (major == DS_RECOMMENDED_KERNEL_MAJOR &&
       minor < DS_RECOMMENDED_KERNEL_MINOR)) {
    ds_warn("Your kernel (%d.%d) is below recommended %d.%d - "
            "some functions might be unstable.",
            major, minor, DS_RECOMMENDED_KERNEL_MAJOR,
            DS_RECOMMENDED_KERNEL_MINOR);
    fflush(stdout);
  }
}

/* USER_HZ as the kernel announces it in the auxv. musl hardcodes
 * sysconf(_SC_CLK_TCK) to 100 and never looks, and a vendor kernel built
 * with USER_HZ=250 then puts every /proc tick count off by 2.5x. */
long ds_clk_tck(void) {
  long hz = (long)getauxval(AT_CLKTCK);
  return hz > 0 ? hz : 100;
}

/* Field n of /proc/<pid>/stat, numbered as proc(5) does, read into buf, or
 * NULL. Counted from the last ')' because comm may hold spaces: the
 * Termux:X11 server's is "termux-x11 com.", which threw a plain whitespace
 * split off by one. */
const char *proc_stat_field(pid_t pid, int n, char *buf, size_t size) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
  if (read_file(path, buf, size) < 0)
    return NULL;
  const char *p = strrchr(buf, ')');
  for (int field = 2; p && field < n; field++)
    p = strchr(p + 1, ' ');
  return p ? p + 1 : NULL;
}

/* Field 4 of /proc/<pid>/stat, the parent pid, 0 if unreadable. */
pid_t proc_ppid(pid_t pid) {
  char stat[512];
  const char *p = proc_stat_field(pid, 4, stat, sizeof(stat));
  return p ? (pid_t)atoi(p) : 0;
}

/* The real uid on the Uid: line of status, or -1. Not st_uid of /proc/<pid>,
 * which the kernel reports as 0 for any process that made itself
 * non-dumpable. */
uid_t proc_uid(pid_t pid) {
  char path[64], line[256];
  snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
  FILE *f = fopen(path, "re");
  unsigned long uid = (unsigned long)-1;
  while (f && fgets(line, sizeof(line), f))
    if (sscanf(line, "Uid: %lu", &uid) == 1)
      break;
  if (f)
    fclose(f);
  return (uid_t)uid;
}

/* Field 22: when the process started, in clock ticks since boot, or 0. */
unsigned long long proc_starttime(pid_t pid) {
  char buf[1024];
  const char *f = proc_stat_field(pid, 22, buf, sizeof(buf));
  return f ? strtoull(f, NULL, 10) : 0;
}

/* Seconds since the container's init started, or -1. Its start is in ticks
 * since boot, the CLOCK_BOOTTIME reference. */
long ds_get_container_uptime(pid_t pid) {
  unsigned long long start_ticks = pid > 0 ? proc_starttime(pid) : 0;
  if (start_ticks == 0)
    return -1;
  struct timespec now;
  clock_gettime(CLOCK_BOOTTIME, &now);
  double up = (double)now.tv_sec + (double)now.tv_nsec / 1e9 -
              (double)start_ticks / (double)ds_clk_tck();
  return up < 0 ? 0 : (long)up;
}
