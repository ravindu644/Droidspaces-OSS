/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "cgroup/internal.h"

static int ctrl_supported_v2(const char *cg_path, const char *name);

/* Where we mount the v1 hierarchies the host left unmounted. */
void v1_own_mount_dir(char *buf, size_t size) {
  snprintf(buf, size, "%s/cgroup/", get_workspace_dir());
}

/* Mounts under a Droidspaces path belong to a container's rootfs and must not
 * be mistaken for host hierarchies on restart. Our own v1 mounts are the
 * exception. */
int is_container_mount(const char *mp) {
  char own[PATH_MAX];
  v1_own_mount_dir(own, sizeof(own));
  return strstr(mp, "/Droidspaces/") && strncmp(mp, own, strlen(own)) != 0;
}

/* Scan mountinfo for a host cgroup mount: the cgroup2 one when ctrl is NULL
 * (e.g. /dev/cg2_bpf on Android), else the v1 hierarchy that owns ctrl,
 * wherever it is mounted (Android: /dev/memcg, /dev/cpuctl).
 * Returns 1 and fills 'buf' if found. */
int find_host_cgroup_mount(const char *ctrl, char *buf, size_t size) {
  FILE *f = fopen("/proc/self/mountinfo", "re");
  if (!f)
    return 0;

  char line[2048];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char *dash = strstr(line, " - ");
    if (!dash)
      continue;
    char fstype[16], opts[256] = "";
    if (sscanf(dash + 3, "%15s %*s %255s", fstype, opts) < 1)
      continue;
    if (strcmp(fstype, ctrl ? "cgroup" : "cgroup2") != 0)
      continue;
    if (ctrl) {
      for (char *c = opts; *c; c++)
        if (*c == ',')
          *c = ' ';
      if (!ctrl_in_list(opts, ctrl))
        continue;
    }

    /* Extract mountpoint (field 5) */
    char *p = line;
    for (int i = 0; i < 4; i++) {
      p = strchr(p, ' ');
      if (!p)
        break;
      p++;
    }
    if (!p)
      continue;
    char *mp_end = strchr(p, ' ');
    if (!mp_end)
      continue;
    *mp_end = '\0';

    if (is_container_mount(p))
      continue;

    if (buf)
      safe_strncpy(buf, p, size);
    found = 1;
    break;
  }
  fclose(f);
  return found;
}

/* Scans mountinfo to find any cgroup2 mount - covers /dev/cg2_bpf (Android)
 * and /sys/fs/cgroup placed by ds_cgroup_host_bootstrap(). */
int ds_cgroup_host_is_v2(void) { return find_host_cgroup_mount(NULL, NULL, 0); }

/* Calls fn for every controller the kernel has enabled (/proc/cgroups). */
void each_v1_controller(void (*fn)(const char *name, void *arg), void *arg) {
  FILE *f = fopen("/proc/cgroups", "re");
  if (!f)
    return;

  char line[256];
  if (!fgets(line, sizeof(line), f)) { /* skip header */
    fclose(f);
    return;
  }

  while (fgets(line, sizeof(line), f)) {
    char name[64];
    int hier, ncg, enabled;
    if (sscanf(line, "%63s %d %d %d", name, &hier, &ncg, &enabled) == 4 &&
        enabled)
      fn(name, arg);
  }
  fclose(f);
}

struct ctrl_query {
  const char *name;
  int found;
};

static void match_controller(const char *name, void *arg) {
  struct ctrl_query *q = arg;
  if (strcmp(name, q->name) == 0)
    q->found = 1;
}

/* Whether the running kernel has a controller at all, on any hierarchy.
 * Limits follow the controller wherever it is bound, so this is the whole
 * question for "can this kernel limit X". /proc/cgroups only answers it for
 * v1: since the kernel grew CONFIG_MEMCG_V1 and CONFIG_CPUSETS_V1, a
 * controller built without its v1 half is left out of that file although
 * cgroup2 has it in full. So ask the cgroup2 root first, wherever the host
 * mounted it, and fall back to /proc/cgroups for what a v1 hierarchy holds. */
int ds_cgroup_has_controller(const char *name) {
  char mnt[PATH_MAX];
  if (find_host_cgroup_mount(NULL, mnt, sizeof(mnt)) &&
      ctrl_supported_v2(mnt, name))
    return 1;

  struct ctrl_query q = {name, 0};
  each_v1_controller(match_controller, &q);
  return q.found;
}

void print_cgroup_status(struct ds_config *cfg) {
  if (cfg->force_cgroupv1)
    ds_warn("Using legacy Cgroup V1 hierarchy (forced by --force-cgroupv1)");
  else if (grep_file("/proc/filesystems", "cgroup2") <= 0)
    ds_warn("Host does not support Cgroup V2 (falling back to legacy V1)");
}

/* Returns 1 if 'name' appears in a space/newline-separated controller list. */
int ctrl_in_list(const char *list, const char *name) {
  const char *p = list;
  size_t nlen = strlen(name);
  while (*p) {
    while (*p == ' ' || *p == '\n')
      p++;
    if (strncmp(p, name, nlen) == 0 &&
        (p[nlen] == ' ' || p[nlen] == '\n' || p[nlen] == '\0'))
      return 1;
    while (*p && *p != ' ' && *p != '\n')
      p++;
  }
  return 0;
}

/* Check controller availability before touching any cgroup files. */
static int ctrl_supported_v2(const char *cg_path, const char *name) {
  if (strlen(cg_path) > PATH_MAX - 32)
    return 0;
  char buf[256];
  char path[PATH_MAX + 64];
  snprintf(path, sizeof(path), "%s/cgroup.controllers", cg_path);
  if (read_file(path, buf, sizeof(buf)) <= 0)
    return 0;
  return ctrl_in_list(buf, name);
}

/* Helper: parse a cgroup integer file that may contain "max" (unlimited).
 * Returns the parsed value, or -1 on error/unlimited. */
long long parse_cgroup_ll(const char *buf) {
  if (strncmp(buf, "max", 3) == 0)
    return -1; /* unlimited */
  char *end;
  errno = 0;
  long long v = strtoll(buf, &end, 10);
  if (errno || end == buf)
    return -1;
  return v;
}

/* Where this container's knobs for one controller live. A controller belongs
 * to a single hierarchy, and Android binds memory and cpu to v1 ones
 * (/dev/memcg, /dev/cpuctl), which takes them out of cgroup2 for good. So
 * look in our cgroup2 dir first, then in whatever v1 hierarchy owns the
 * controller. Returns 2, 1, or 0 when it is nowhere.
 * ponytail: rescans mountinfo on every v1 lookup. Cache the mountpoint if the
 * virtualize tick ever shows up in a profile. */
int ds_cgroup_ctrl_dir(const char *ctrl, const char *container_name, char *dir,
                       size_t size) {
  char safe_name[256], mnt[PATH_MAX];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(dir, size, "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (ctrl_supported_v2(dir, ctrl))
    return 2;
  if (!find_host_cgroup_mount(ctrl, mnt, sizeof(mnt)))
    return 0;
  snprintf(dir, size, "%s/droidspaces/%s", mnt, safe_name);
  return 1;
}

/* Whether the kernel accounts swap per cgroup, without which no swap cap can
 * be set. v1 has the memsw files at its root. A cgroup2 root never carries
 * memory files, so ask a cgroup one level down that has memory enabled: it
 * has swap.max exactly when swap is accounted. With none to ask, say yes; a
 * cap that then fails is warned about at start and treated as allowed swap.
 * ponytail: one level deep, systemd's slices always answer it. */
int ds_cgroup_has_swap_accounting(void) {
  char mnt[PATH_MAX], path[PATH_MAX * 2];
  if (find_host_cgroup_mount("memory", mnt, sizeof(mnt))) {
    snprintf(path, sizeof(path), "%s/memory.memsw.limit_in_bytes", mnt);
    return access(path, F_OK) == 0;
  }
  if (!find_host_cgroup_mount(NULL, mnt, sizeof(mnt)))
    return 0;
  DIR *d = opendir(mnt);
  if (!d)
    return 0;
  int answer = 1;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    snprintf(path, sizeof(path), "%s/%s/memory.max", mnt, de->d_name);
    if (access(path, F_OK) != 0)
      continue;
    snprintf(path, sizeof(path), "%s/%s/memory.swap.max", mnt, de->d_name);
    answer = access(path, F_OK) == 0;
    break;
  }
  closedir(d);
  return answer;
}
