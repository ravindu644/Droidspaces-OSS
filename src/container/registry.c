/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "container/internal.h"

int is_pid_file(const char *name) {
  if (!name)
    return 0;
  size_t len = strlen(name);
  if (len < strlen(DS_EXT_PID))
    return 0;
  return (strcmp(name + len - strlen(DS_EXT_PID), DS_EXT_PID) == 0);
}

/* PID File Resolution */

int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size) {
  if (!name || !pidfile || size == 0)
    return -1;
  pidfile[0] = '\0';
  if (!validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  const char *dir = get_pids_dir();
  int r = snprintf(pidfile, size, "%.3800s/%.256s.pid", dir, safe_name);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* 1 if a live pidfile other than skip names pid */
static int pid_tracked(pid_t pid, const char *skip) {
  DIR *d = opendir(get_pids_dir());
  struct dirent *e;
  int found = 0;
  while (d && !found && (e = readdir(d)) != NULL) {
    char path[PATH_MAX];
    pid_t p = 0;
    if (!is_pid_file(e->d_name))
      continue;
    snprintf(path, sizeof(path), "%s/%s", get_pids_dir(), e->d_name);
    found = strcmp(path, skip) != 0 && read_and_validate_pid(path, &p) == 0 &&
            p == pid;
  }
  if (d)
    closedir(d);
  return found;
}

/* The init of the instance running from cfg's rootfs, or 0: a container init
 * whose root, as the host sees it, is that image's mount or that directory.
 * For when the pidfile is lost and no monitor answers: it died, it is from
 * an older build, or init outlived monitor and intermediate both. Whatever
 * its parent, a root init rooted on this container's own tree is this
 * container, unless another container's pidfile names it: two directory
 * containers may share one rootfs. Both sides are the host's
 * own view: cfg is the host config, and a guest cannot root itself on a tree it
 * cannot see. */
/* ponytail: a volatile container's root is an overlay only it can see, so
 * this misses it; scan still reports it. Match its lowerdir if that bites. */
static pid_t init_on_rootfs(const struct ds_config *cfg) {
  char mnt[PATH_MAX];
  const char *root = cfg->rootfs_path[0] ? cfg->rootfs_path : NULL;
  if (cfg->is_img_mount || cfg->rootfs_img_path[0])
    root = ds_img_mount_point(cfg->container_name, mnt, sizeof(mnt)) == 0
               ? mnt
               : NULL;
  struct stat want;
  pid_t *pids = NULL, found = 0;
  size_t count = 0;
  if (!root || stat(root, &want) < 0 || collect_pids(&pids, &count) < 0)
    return 0;
  for (size_t i = 0; i < count && !found; i++) {
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/proc/%d/root", (int)pids[i]);
    if (stat(path, &st) == 0 && st.st_dev == want.st_dev &&
        st.st_ino == want.st_ino && is_container_init(pids[i]) &&
        proc_uid(pids[i]) == 0 && !pid_tracked(pids[i], cfg->pidfile))
      found = pids[i];
  }
  free(pids);
  return found;
}

int is_container_running(struct ds_config *cfg, pid_t *pid_out) {
  if (cfg->pidfile[0] == '\0') {
    if (cfg->container_name[0] == '\0')
      return 0;
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
  }

  pid_t pid = 0;
  int ret = read_and_validate_pid(cfg->pidfile, &pid);
  if (pid_out)
    *pid_out = pid;

  if (ret == 0 && pid > 0)
    return 1;

  /* The pidfile is missing or stale. The container's monitor knows its init
   * pid for certain; if the monitor died, the init still running from this
   * container's rootfs is the one. A lost pidfile is rewritten from that,
   * never from markers inside a guest, which root there can forge. */
  pid_t mon = cfg->container_name[0] ? ds_cmd_get_pid(cfg->container_name) : 0;
  if (mon <= 0)
    mon = init_on_rootfs(cfg);
  if (mon > 0) {
    if (pid_out)
      *pid_out = mon;
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", mon);
    write_file_atomic(cfg->pidfile, pid_str);
    return 1;
  }

  return 0;
}

void get_container_name_from_pidfile(const char *pidfile, char *name,
                                     size_t size) {
  safe_strncpy(name, pidfile, size);
  char *dot = strrchr(name, '.');
  if (dot)
    *dot = '\0';
}

/* Resolve a scanned pidfile dirent into tmp_cfg->container_name / ->pidfile. */
static void resolve_scanned_container(const char *dirent_name,
                                      struct ds_config *tmp_cfg) {
  char clean_name[256];
  get_container_name_from_pidfile(dirent_name, clean_name, sizeof(clean_name));
  safe_strncpy(tmp_cfg->container_name, clean_name,
               sizeof(tmp_cfg->container_name));
  resolve_pidfile_from_name(clean_name, tmp_cfg->pidfile,
                            sizeof(tmp_cfg->pidfile));
}

/* Remove a stale container's pidfile.
 *
 * A dead PID in a pidfile is not always stale. During a restart the old init
 * is dead and the new one not written yet, and a container that just exited
 * still has a monitor about to clean it up or reboot it. Those belong to
 * whoever holds the lifecycle lock or to the monitor, so only prune what
 * nobody owns: a container whose monitor is gone (a crash, or a host reboot
 * that left the files behind). */
void prune_stale_pidfile(const char *name, const char *pidfile) {
  int lock_fd = ds_container_lock_orphan(name);
  if (lock_fd < 0)
    return;
  unlink(pidfile);
  ds_container_unlock(lock_fd);
}

/* The next running container in a Pids/ directory scan, pruning the stale
 * pidfiles it passes. 1 with cfg and pid filled in, 0 at the end. */
int next_running(DIR *d, struct ds_config *cfg, pid_t *pid) {
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (!is_pid_file(ent->d_name))
      continue;
    ds_config_init(cfg);
    resolve_scanned_container(ent->d_name, cfg);
    if (is_container_running(cfg, pid))
      return 1;
    if (*pid == 0 && access(cfg->pidfile, F_OK) == 0)
      prune_stale_pidfile(cfg->container_name, cfg->pidfile);
  }
  return 0;
}

int count_running_containers(char *first_name, size_t size) {
  DIR *d = opendir(get_pids_dir());
  if (!d)
    return 0;
  struct ds_config c;
  pid_t pid;
  int count = 0;
  while (next_running(d, &c, &pid))
    if (count++ == 0 && first_name && size > 0)
      safe_strncpy(first_name, c.container_name, size);
  closedir(d);
  return count;
}

int is_container_init(pid_t pid) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  FILE *f = fopen(path, "re");
  if (!f)
    return 0;

  char line[1024];
  int is_init = 0;
  int nspid_found = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "NSpid:", 6) == 0) {
      /* NSpid line format: "NSpid: <pid1> <pid2> ... <pidN>"
       * The last value is the PID in the innermost namespace.
       * We use a robust tokenizer to avoid issues with tabs/spaces.
       * NOTE: NSpid was added in Linux 4.1. On older kernels (e.g. 3.10),
       * this line is absent and we fall back to the ns/pid inode check. */
      nspid_found = 1;
      char *p = line + 6;
      char *last_val = NULL;
      char *saveptr;
      char *token = strtok_r(p, " \t\n\r", &saveptr);
      while (token) {
        last_val = token;
        token = strtok_r(NULL, " \t\n\r", &saveptr);
      }
      if (last_val && strcmp(last_val, "1") == 0) {
        is_init = 1;
      }
      break;
    }
  }
  fclose(f);

  if (nspid_found)
    return is_init;

  /*
   * Fallback for kernels < 4.1 (e.g. 3.10) where NSpid is absent:
   * Compare the inode of /proc/<pid>/ns/pid vs /proc/1/ns/pid.
   * Available since Linux 3.8 (namespaces(7)).
   * If inodes differ, the process lives in a different PID namespace.
   * Combined with the monitor check in is_valid_container_pid(), this is
   * sufficient to identify a container init process.
   */
  struct stat st_pid, st_host;
  char ns_path[PATH_MAX];

  snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/pid", pid);
  if (stat(ns_path, &st_pid) < 0)
    return 0;

  if (stat("/proc/1/ns/pid", &st_host) < 0)
    return 0;

  /* Different inode == different PID namespace == process is a container init
   */
  return (st_pid.st_ino != st_host.st_ino) ? 1 : 0;
}

/*
 * ds_feature_needs - generic "is this feature still needed?" scanner.
 *
 * Reads the int field at cfg_flag_offset inside ds_config to decide
 * whether the associated global daemon/server should stay up.
 *
 * Returns:
 *   1: at least one running container has the feature enabled
 *   0: feature containers exist but none are currently running
 *  -1: no containers with the feature are installed
 *
 * A new feature needs no change to this scanner, only a check_*_needs()
 * wrapper below that passes offsetof(struct ds_config, <flag>): the daemon
 * helpers take a callback with no arguments.
 */
int ds_feature_needs(size_t cfg_flag_offset) {
  /* Phase 1: any running container with the feature? */
  DIR *pd = opendir(get_pids_dir());
  if (!pd)
    return -1;

  struct dirent *ent;
  while ((ent = readdir(pd)) != NULL) {
    if (!is_pid_file(ent->d_name))
      continue;
    char name[256];
    get_container_name_from_pidfile(ent->d_name, name, sizeof(name));
    struct ds_config tmp = {0};
    if (ds_config_peek(name, &tmp) == 0) {
      int flag = *(int *)((char *)&tmp + cfg_flag_offset);
      pid_t pid;
      int running = flag && is_container_running(&tmp, &pid);
      ds_config_free(&tmp);
      if (running) {
        closedir(pd);
        return 1;
      }
    }
  }
  closedir(pd);

  /* Phase 2: feature installed in any container at all? */
  char containers_path[PATH_MAX];
  snprintf(containers_path, sizeof(containers_path), "%s/Containers",
           get_workspace_dir());
  DIR *cd = opendir(containers_path);
  if (!cd)
    return -1;

  int installed = 0;
  while ((ent = readdir(cd)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;
    struct ds_config tmp = {0};
    if (ds_config_peek(ent->d_name, &tmp) == 0) {
      int flag = *(int *)((char *)&tmp + cfg_flag_offset);
      if (flag) {
        installed = 1;
        ds_config_free(&tmp);
        break;
      }
      ds_config_free(&tmp);
    }
  }
  closedir(cd);
  return installed ? 0 : -1;
}

/* Compat wrappers - thin shims over ds_feature_needs() */
int check_selinux_permissive_needs(void) {
  return ds_feature_needs(offsetof(struct ds_config, selinux_permissive));
}
int check_x11_needs(void) {
  return ds_feature_needs(offsetof(struct ds_config, x11));
}
int check_virgl_needs(void) {
  return ds_feature_needs(offsetof(struct ds_config, virgl));
}
int check_pulse_needs(void) {
  return ds_feature_needs(offsetof(struct ds_config, pulseaudio));
}

/* PID file helpers */

int read_and_validate_pid(const char *pidfile, pid_t *pid_out) {
  if (pid_out)
    *pid_out = 0;

  char buf[64];
  if (read_file(pidfile, buf, sizeof(buf)) < 0) {
    /* If file is gone or empty, count as stopped without error logging */
    return -1;
  }

  char *end;
  long val = strtol(buf, &end, 10);
  if (*end != '\0' || val <= 0) {
    /* Stale/invalid data in pidfile */
    ds_error("Invalid/stale PID in %s: '%s'", pidfile, buf);
    return -1;
  }

  /* check if process exists. Atomic check: if kill fails with ESRCH, we KNOW
   * it's dead without racing between exist checking and acting. */
  if (kill((pid_t)val, 0) < 0) {
    if (errno == ESRCH) {
      return -1;
    }
    /* Permissive check: if EPERM, it exists but we can't signal it.
     * Likely still running if it was ours. */
  }

  /*
   * Crucial Fix: Distinguish between "process is gone" (-1) and
   * "process exists but is not a Droidspaces container" (-2).
   * Pruning logic must ONLY nuke files when the process is truly gone.
   */
  if (!is_valid_container_pid((pid_t)val)) {
    if (pid_out)
      *pid_out = (pid_t)val; /* Return the PID so caller knows it exists */
    return -2;
  }

  if (pid_out)
    *pid_out = (pid_t)val;
  return 0;
}

/* 1 if p is a Droidspaces monitor: named [ds-monitor], root, and in our pid
 * namespace. A guest can name its processes anything, but none of them is in
 * ours. The intermediate between a monitor and its init inherits the name. */
int is_ds_monitor(pid_t p) {
  char path[64], comm[32] = "";
  if (p <= 1 || !ds_peer_in_pidns(p) || proc_uid(p) != 0)
    return 0;
  snprintf(path, sizeof(path), "/proc/%d/comm", (int)p);
  return read_file(path, comm, sizeof(comm)) >= 0 &&
         strcmp(comm, "[ds-monitor]") == 0;
}

int is_valid_container_pid(pid_t pid) {
  /* A Droidspaces container init is PID 1 of its namespace whose parent is
   * its intermediate: monitor, intermediate, init. The intermediate carries
   * the monitor's name and waits on init for as long as it lives, even if
   * the monitor is killed, so a container stays running (its image mounted,
   * its name taken) for as long as it really runs. That is the host's own
   * process tree, which no guest can forge, where a marker inside the guest
   * was the guest's to delete or plant. A monitor from an older build is
   * named the same. With monitor and intermediate both killed, init is an
   * orphan nothing tells apart from another runtime's, and like LXC with
   * lxc-start gone we lose track of it; scan still finds it by its image
   * mount. */
  return is_container_init(pid) && proc_uid(pid) == 0 &&
         is_ds_monitor(proc_ppid(pid));
}

int is_unmanaged_init(pid_t pid) {
  return is_valid_container_pid(pid) &&
         !is_ds_monitor(proc_ppid(proc_ppid(pid)));
}
