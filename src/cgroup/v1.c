/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

#include "cgroup/internal.h"

/* LXC's cpuset1_initialize: a new cpuset cgroup starts with no cpus and no
 * mems and refuses every attach until it has both. Copy the root's into our
 * parent and let children clone from there. Android mounts cpuset with
 * noprefix, hence both spellings. Fails quietly on every other hierarchy. */
static void v1_seed_cpuset(const char *mnt) {
  static const char *const files[] = {"cpuset.cpus", "cpuset.mems", "cpus",
                                      "mems", NULL};
  char path[PATH_MAX + 64], buf[256];
  for (int i = 0; files[i]; i++) {
    snprintf(path, sizeof(path), "%s/%s", mnt, files[i]);
    if (read_file(path, buf, sizeof(buf)) <= 0)
      continue;
    snprintf(path, sizeof(path), "%s/droidspaces/%s", mnt, files[i]);
    if (write_file(path, buf) < 0) {
    }
  }
  snprintf(path, sizeof(path), "%s/droidspaces/cgroup.clone_children", mnt);
  if (write_file(path, "1") < 0) {
  }
}

/* A container's cgroup must be made by the start that uses it, as in LXC:
 * limits are only ever written to a new cgroup, so one that is already there
 * still carries whatever an earlier run set. ds_cgroup_setup() removes
 * leftovers first, so EEXIST here means that removal failed. Other errors
 * are not worth a line: schedtune on 4.14 has a hard cap on group count. */
void mkdir_fresh(const char *dir) {
  if (mkdir(dir, 0755) < 0 && errno == EEXIST)
    ds_warn("[CGROUP] %s is left over from an earlier run and could not be "
            "removed, its old limits may still apply.",
            dir);
}

/* Apply 'op' to this container's cgroup in every v1 hierarchy mounted on the
 * host. Only V1_CREATE makes one; the rest act on those that exist, which for
 * a cgroup2 container is just the hierarchies holding a limit. */
void v1_each(const char *container_name, int op) {
  FILE *f = fopen("/proc/self/mountinfo", "re");
  if (!f)
    return;

  char safe_name[256], pid_s[32], line[2048];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());

  while (fgets(line, sizeof(line), f)) {
    char mnt[PATH_MAX], fstype[16];
    char *dash = strstr(line, " - ");
    if (!dash || sscanf(dash + 3, "%15s", fstype) != 1 ||
        strcmp(fstype, "cgroup") != 0 ||
        sscanf(line, "%*s %*s %*s %*s %4095s", mnt) != 1 ||
        is_container_mount(mnt))
      continue;

    char dir[PATH_MAX + 512], path[PATH_MAX + 640];
    snprintf(dir, sizeof(dir), "%s/droidspaces", mnt);
    if (op == V1_CREATE) {
      /* Not fatal: schedtune on 4.14 has a hard cap on group count */
      if (mkdir(dir, 0755) == 0)
        v1_seed_cpuset(mnt);
    }
    snprintf(dir, sizeof(dir), "%s/droidspaces/%s", mnt, safe_name);
    if (op == V1_CREATE) {
      mkdir_fresh(dir);
      /* Kernels before 5.x account memory flat by default, and then a limit
       * here would not cover the cgroups systemd creates below us. Fails
       * quietly on every other hierarchy. */
      snprintf(path, sizeof(path), "%s/memory.use_hierarchy", dir);
      if (write_file(path, "1") < 0) {
      }
      continue;
    }
    if (access(dir, F_OK) != 0)
      continue;

    if (op == V1_REMOVE) {
      rmdir_cgroup_tree(dir);
      continue;
    }
    snprintf(path, sizeof(path), "%s/cgroup.procs", dir);
    if (write_file(path, pid_s) < 0) {
    }
  }
  fclose(f);
}

/* v1 only lets the initial cgroup namespace create a hierarchy, so whatever
 * the host left unmounted (Android: pids, devices, name=systemd) is mounted
 * out here, before the container is namespaced. It stays mounted on purpose:
 * attach and cleanup find it through mountinfo. */
static void v1_mount_missing(const char *word, const char *data) {
  if (find_host_cgroup_mount(word, NULL, 0))
    return;
  char mp[PATH_MAX];
  v1_own_mount_dir(mp, sizeof(mp));
  strncat(mp, word, sizeof(mp) - strlen(mp) - 1);
  mkdir_p(mp, 0755);
  if (mount("cgroup", mp, "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC, data) !=
      0)
    rmdir(mp);
}

static void v1_mount_missing_controller(const char *name, void *arg) {
  (void)arg;
  v1_mount_missing(name, name);
}

/* Give a v1 container its own cgroup in every v1 hierarchy, name=systemd
 * included, the way LXC lays it out. Without it the container's systemd sits
 * at the host root of each hierarchy and litters /dev/memcg and friends with
 * its slices. */
void v1_setup(struct ds_config *cfg) {
  each_v1_controller(v1_mount_missing_controller, NULL);
  v1_mount_missing("name=systemd", "none,name=systemd");
  v1_each(cfg->container_name, V1_CREATE);
}
