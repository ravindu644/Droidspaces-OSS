/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "container/internal.h"

/* A root container init whose root, as the host sees it, is on the image
 * mounted at mpath, or 0. The device is the loop device we attached, so no
 * other runtime's init and no guest can claim it. */
static pid_t init_on_mount(const char *mpath, const pid_t *pids, size_t count) {
  struct stat m;
  if (stat(mpath, &m) < 0)
    return 0;
  for (size_t i = 0; i < count; i++) {
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/proc/%d/root", (int)pids[i]);
    if (stat(path, &st) == 0 && st.st_dev == m.st_dev &&
        is_container_init(pids[i]) && proc_uid(pids[i]) == 0)
      return pids[i];
  }
  return 0;
}

/* One monitor's answer: what its container booted with, and its init pid. A
 * host config or pidfile lost while the container runs comes back from that,
 * exactly as the host had it. Nothing is taken from inside the container:
 * root there controls its /run. */
static void restore_from_monitor(struct ds_config *booted, pid_t pid,
                                 void *arg) {
  int *restored = arg;

  const char *name = booted->container_name;
  char path[PATH_MAX], pidfile[PATH_MAX];
  pid_t live;
  if (ds_config_host_path(name, path, sizeof(path)) < 0 ||
      resolve_pidfile_from_name(name, pidfile, sizeof(pidfile)) < 0)
    return;
  if (access(path, F_OK) == 0 && read_and_validate_pid(pidfile, &live) == 0)
    return;

  /* A start, stop or app edit of this name may be writing the same files:
   * leave it to them this time. */
  int lock = ds_container_lock(name, 0);
  if (lock < 0)
    return;
  int done = 0;
  if (access(path, F_OK) != 0 && ds_config_save_by_name(name, booted) == 0) {
    ds_log("Recovery: restored the configuration of '%s' from its monitor",
           name);
    /* The monitor has the env file's name, not its variables */
    if (booted->env_file[0] && access(booted->env_file, F_OK) != 0)
      ds_warn("Recovery: env file %s of '%s' is gone; its variables are not "
              "restored",
              booted->env_file, name);
    done = 1;
  }
  /* The monitor's answer is only taken while this pid is its live init */
  if (read_and_validate_pid(pidfile, &live) != 0) {
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    done |= write_file_atomic(pidfile, pid_str) == 0;
  }
  ds_container_unlock(lock);
  *restored += done;
}

int scan_containers(void) {
  ds_log("Scanning system for untracked Droidspaces containers...");

  pid_t *pids;
  size_t count;
  if (collect_pids(&pids, &count) < 0)
    return -1;

  /* 1. Every running container's monitor, found from the kernel's socket
   * table the way LXC finds running containers, never from a guest. */
  int recovered_found = 0;
  ds_cmd_for_each(restore_from_monitor, &recovered_found);

  /* An init whose monitor died is not managed any more. The host's process
   * tree says so, no guest file: its parent is an intermediate whose own
   * parent is not a [ds-monitor], or is our PID 1. Nothing about it is
   * rebuilt from inside it, it is only reported. */
  int unmanaged = 0;
  for (size_t i = 0; i < count; i++) {
    pid_t pid = pids[i];
    if (pid <= 1 || !is_unmanaged_init(pid))
      continue;
    /* It is PID 1 of its namespace: only SIGKILL reaches it from here */
    ds_warn("Container init PID %d has no monitor and is not managed. Stop "
            "it with: kill -KILL %d",
            pid, pid);
    unmanaged++;
  }

  /* 2. Prune pidfiles nobody owns any more */
  DIR *d = opendir(get_pids_dir());
  if (d) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      if (!is_pid_file(ent->d_name))
        continue;
      char pf[PATH_MAX];
      snprintf(pf, sizeof(pf), "%s/%s", get_pids_dir(), ent->d_name);

      pid_t p;
      if (read_and_validate_pid(pf, &p) < 0 && p == 0) {
        char name[256];
        get_container_name_from_pidfile(ent->d_name, name, sizeof(name));
        prune_stale_pidfile(name, pf);
      }
    }
    closedir(d);
  }

  /* 3. Scan for orphaned loop mounts in /mnt/Droidspaces */
  int orphaned_found = 0;
  DIR *md = opendir(DS_IMG_MOUNT_ROOT_UNIVERSAL);
  if (md) {
    struct dirent *ent;
    while ((ent = readdir(md)) != NULL) {
      if (ent->d_name[0] == '.')
        continue;

      char mpath[PATH_MAX];
      snprintf(mpath, sizeof(mpath), "%s/%s", DS_IMG_MOUNT_ROOT_UNIVERSAL,
               ent->d_name);

      /* The directory is named after its container. A live pidfile means the
       * container is running: a monitor from a build before the lifecycle lock
       * holds no lock at all, so the lock probe below alone would not protect
       * it. Unmounted is not the same as orphaned either: a start mounts here
       * before it writes the pidfile, and a restart keeps the mount while no
       * instance is running. Both hold the lifecycle lock. */
      char pf[PATH_MAX];
      pid_t p;
      if (resolve_pidfile_from_name(ent->d_name, pf, sizeof(pf)) == 0 &&
          read_and_validate_pid(pf, &p) == 0)
        continue;
      /* An init still running from this image keeps it. One with neither
       * monitor nor intermediate left is found only this way. */
      int mounted = is_mountpoint(mpath);
      pid_t user = mounted ? init_on_mount(mpath, pids, count) : 0;
      if (user > 0 && !is_valid_container_pid(user)) {
        ds_warn("Container init PID %d still runs from %s with no monitor. "
                "Stop it with: kill -KILL %d",
                user, mpath, user);
        unmanaged++;
      }
      /* From the host a volatile init's root does not say which image it
       * runs from, so while any unmanaged init runs no mount is touched. */
      if (user > 0 || (mounted && unmanaged))
        continue;
      int lock_fd = ds_container_lock_orphan(ent->d_name);
      if (lock_fd < 0)
        continue;

      if (mounted) {
        ds_warn("Found orphaned mount: %s, cleaning up...", mpath);
        unmount_rootfs_img(mpath, 0);
        orphaned_found++;
      } else {
        rmdir(mpath);
      }
      ds_container_unlock(lock_fd);
    }
    closedir(md);
  }

  free(pids);

  if (recovered_found == 0 && orphaned_found == 0 && unmanaged == 0)
    ds_log("No untracked resources found.");
  else
    ds_log("Scan complete: synchronized %d container(s), cleaned %d orphaned "
           "mount(s).",
           recovered_found, orphaned_found);

  return 0;
}
