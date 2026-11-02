/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/* Build this boot's cgroups from nothing and write the limits into them.
 *
 * The caller is the monitor and it stays outside: the intermediate joins with
 * ds_cgroup_join(). These are LXC's rules, and each one closes a hole we had.
 * A supervisor sitting inside the cgroup blocks its removal, a cgroup that
 * outlives a restart is reused with its old limits, and a limit that is no
 * longer configured is never written back to "max". So: nothing of ours
 * inside, nothing reused, limits only ever written to a new cgroup. */
void ds_cgroup_setup(struct ds_config *cfg) {
  ds_cgroup_cleanup_container(cfg->container_name);

  if (cfg->force_cgroupv1 || !ds_cgroup_host_is_v2())
    v1_setup(cfg);

  /* Every container gets a cgroup2 dir when the host has cgroup2, a v1 one
   * too, as LXC places its payload in every hierarchy. A controller can sit
   * on either side whatever the container's own view is: once a cgroup2
   * container has enabled pids below, it can no longer be mounted as v1, and
   * a v1 container with nowhere to stand on cgroup2 would lose its limit.
   * This comes after v1_setup(), which takes the controllers nobody holds
   * yet, so only what cgroup2 still owns is enabled here. A v1 container
   * never mounts cgroup2 and does not see any of this. */
  if (access("/sys/fs/cgroup/cgroup.procs", F_OK) == 0) {
    /* A controller only appears in a child cgroup once the parent's
     * subtree_control enables it, so walk the two levels above ours:
     * /sys/fs/cgroup -> /sys/fs/cgroup/droidspaces */
    char enable[64] = "", buf[256], dir[PATH_MAX], safe_name[256];
    if (read_file("/sys/fs/cgroup/cgroup.controllers", buf, sizeof(buf)) > 0)
      snprintf(enable, sizeof(enable), "%s%s%s",
               cfg->memory_limit && ctrl_in_list(buf, "memory") ? "+memory "
                                                                : "",
               cfg->cpu_quota && ctrl_in_list(buf, "cpu") ? "+cpu " : "",
               cfg->pids_limit && ctrl_in_list(buf, "pids") ? "+pids" : "");

    mkdir("/sys/fs/cgroup/droidspaces", 0755);
    if (enable[0]) {
      if (write_file("/sys/fs/cgroup/cgroup.subtree_control", enable) < 0)
        ds_warn("[CGROUP] subtree_control (root): %s", strerror(errno));
      if (write_file("/sys/fs/cgroup/droidspaces/cgroup.subtree_control",
                     enable) < 0)
        ds_warn("[CGROUP] subtree_control (droidspaces): %s", strerror(errno));
    }

    sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));
    snprintf(dir, sizeof(dir), "/sys/fs/cgroup/droidspaces/%s", safe_name);
    mkdir_fresh(dir);
  }

  ds_cgroup_apply_limits(cfg);
}

/* Move the caller into the container's cgroups: every v1 one it has (all of
 * them on a v1 container, the ones holding a limit on a cgroup2 one) and,
 * under 'leaf', its cgroup2 dir. */
static int cgroup_join(const char *container_name, const char *leaf) {
  v1_each(container_name, V1_JOIN);

  char safe_name[256], path[PATH_MAX], pid_s[32];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (access(path, F_OK) != 0)
    return 0;

  if (leaf) {
    strncat(path, leaf, sizeof(path) - strlen(path) - 1);
    mkdir(path, 0755);
  }
  strncat(path, "/cgroup.procs", sizeof(path) - strlen(path) - 1);
  snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
  return write_file(path, pid_s);
}

/* The per-boot intermediate calls this before it unshares, so that init and
 * the cgroup namespace both start at the root of the container's cgroups. */
void ds_cgroup_join(const char *container_name) {
  if (cgroup_join(container_name, NULL) < 0)
    ds_warn("[CGROUP] Could not join the container's cgroup: %s",
            strerror(errno));
}

/* Put the calling process inside the container's cgroups before it setns()es
 * in, the way lxc-attach does. logind inside the container can only move a
 * session into its scope if the process is already under its root, and a
 * session left outside would also run outside the limits.
 *
 * The target is always a cgroup we created for the container, never the one
 * its init sits in now: systemd moves PID 1 into init.scope, and nothing else
 * belongs in there.
 *
 * On cgroup2, systemd enables controllers on the container root, which then
 * cannot hold processes, so every session shares one leaf beside init.scope
 * (LXC's ".lxc"). It goes away with the container's tree at stop. The name
 * has no leading dot because rmdir_cgroup_tree() skips dot entries.
 * ponytail: no ds-enter-N retry on EBUSY like LXC has, that only hits if the
 * container makes child cgroups inside our leaf. */
int ds_cgroup_attach(const char *container_name) {
  return cgroup_join(container_name, "/ds-enter");
}

/* Recursive bottom-up rmdir of a cgroup subtree.  cgroup directories can
 * only be removed from the leaves upward - attempting to rmdir a non-empty
 * cgroup returns EBUSY.
 *
 * Even after all processes exit, cgroup state is destroyed asynchronously
 * by the kernel.  Child dirs enter a "dying" state that is invisible to
 * readdir() but still causes the parent's rmdir() to return EBUSY.
 *
 * We handle this with two mechanisms:
 *   1. cgroup.kill (kernel 5.14+): write "1" to kill all remaining
 *      processes in the subtree atomically, then poll cgroup.events
 *      until populated=0 before attempting rmdir.
 *   2. Retry loop: for older kernels without cgroup.kill, retry rmdir
 *      with short sleeps to let the async cleanup complete. */
void rmdir_cgroup_tree(const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    rmdir(path);
    return;
  }

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    if (de->d_type != DT_DIR)
      continue;

    char child[PATH_MAX];
    safe_strncpy(child, path, sizeof(child));
    strncat(child, "/", sizeof(child) - strlen(child) - 1);
    strncat(child, de->d_name, sizeof(child) - strlen(child) - 1);
    rmdir_cgroup_tree(child);
  }
  closedir(d);

  /* 1. cgroup.kill - available on kernel 5.14+.
   *    Writing "1" sends SIGKILL to every process in the subtree
   *    atomically, including those in dying child cgroups. */
  char kill_path[PATH_MAX];
  safe_strncpy(kill_path, path, sizeof(kill_path));
  strncat(kill_path, "/cgroup.kill", sizeof(kill_path) - strlen(kill_path) - 1);
  if (access(kill_path, W_OK) == 0) {
    int kfd = open(kill_path, O_WRONLY | O_CLOEXEC);
    if (kfd >= 0) {
      if (write(kfd, "1", 1) < 0) {
      }
      close(kfd);
    }
  }

  /* 2. Poll cgroup.events for populated=0.
   *    Bail out after ~500ms (50 × 10ms) to avoid blocking forever. */
  char events_path[PATH_MAX];
  safe_strncpy(events_path, path, sizeof(events_path));
  strncat(events_path, "/cgroup.events",
          sizeof(events_path) - strlen(events_path) - 1);
  for (int i = 0; i < 50; i++) {
    char buf[256] = {0};
    /* v1 has no cgroup.events, nothing to wait on */
    if (read_file(events_path, buf, sizeof(buf)) <= 0 ||
        strstr(buf, "populated 0"))
      break;
    usleep(10000); /* 10 ms */
  }

  /* 3. rmdir with retry - handles residual dying descendants on older
   *    kernels that lack cgroup.kill.  10 attempts × 20 ms = 200 ms max. */
  for (int attempt = 0; attempt < 10; attempt++) {
    if (rmdir(path) == 0 || errno == ENOENT)
      return;
    if (errno != EBUSY)
      return;      /* unexpected error - give up */
    usleep(20000); /* 20 ms */
  }
}

/* ds_cgroup_cleanup_container
 *
 * Removes the entire /sys/fs/cgroup/droidspaces/<container_name>/ subtree
 * that was created at container start for cgroup namespace isolation.
 *
 * The kernel requires a bottom-up rmdir walk - a cgroup directory can only
 * be removed after all its children are gone.  All container processes are
 * dead by the time cleanup_container_resources() calls this, so every leaf
 * is empty and the walk always succeeds.
 *
 * Safe to call on every stop regardless of whether the directory exists
 * (all rmdir calls are silently ignored on ENOENT). */
void ds_cgroup_cleanup_container(const char *container_name) {
  if (!container_name || !container_name[0])
    return;

  v1_each(container_name, V1_REMOVE);

  char dir[PATH_MAX];
  char safe_name[256];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(dir, sizeof(dir), "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (access(dir, F_OK) == 0)
    rmdir_cgroup_tree(dir);
}
