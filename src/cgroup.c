/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/**
 * Ported LXC-style Cgroup Setup:
 * 1. Discover host hierarchies from /proc/self/mountinfo.
 * 2. If Cgroup Namespace is active (Linux 4.6+), mount hierarchies directly.
 * 3. Otherwise (Legacy), bind-mount the container's subset from the host.
 */
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif

/* Returns 1 if the kernel's cgroupv2 controllers are sufficiently complete
 * for systemd. The cpu/io/memory v2 controllers only became usable in 5.2.
 * On kernels like Android 4.14, cgroup2 mounts SUCCEED but the controllers
 * are absent - systemd probes them and falls apart. */
int ds_cgroup_v2_usable(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) != 0)
    return 0; /* unknown kernel - assume unusable, safe default */
  return (major > 5 || (major == 5 && minor >= 2));
}

/* Scan mountinfo for any host cgroup2 mount (e.g. /dev/cg2_bpf on Android).
 * Returns 1 and fills 'buf' if found. */
static int find_host_cgroup2_mount(char *buf, size_t size) {
  FILE *f = fopen("/proc/self/mountinfo", "re");
  if (!f)
    return 0;

  char line[2048];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char *dash = strstr(line, " - ");
    if (!dash)
      continue;
    char fstype[16];
    if (sscanf(dash + 3, "%15s", fstype) != 1)
      continue;
    if (strcmp(fstype, "cgroup2") != 0)
      continue;

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

    /* Skip Droidspaces-internal mounts to avoid false positives on restart */
    if (strstr(p, "/Droidspaces/"))
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
int ds_cgroup_host_is_v2(void) { return find_host_cgroup2_mount(NULL, 0); }

/* Returns 1 if the kernel supports cgroup2 (check /proc/filesystems). */
int ds_cgroup_kernel_supports_v2(void) {
  return (grep_file("/proc/filesystems", "cgroup2") > 0);
}

/* Mount cgroup2 on /sys/fs/cgroup if the host hasn't done so.
 * Android recovery kernels support cgroup2 but only mount it at /dev/cg2_bpf;
 * systemd needs it at /sys/fs/cgroup. Sequence: mkdir -> tmpfs anchor ->
 * cgroup2. */
void ds_cgroup_host_bootstrap(int force_cgroupv1) {
  if (force_cgroupv1)
    return;

  /* Already done */
  struct statfs sfs;
  if (statfs("/sys/fs/cgroup", &sfs) == 0 &&
      (unsigned long)sfs.f_type == (unsigned long)CGROUP2_SUPER_MAGIC)
    return;

  /* No probe_cgroup2_mount(): mkdtemp fails on ramfs roots (no /tmp).
   * The mount() calls below self-report failure via errno. */
  if (grep_file("/proc/filesystems", "cgroup2") <= 0) {
    ds_log("[CGROUP] cgroup2 not in /proc/filesystems, skipping bootstrap.");
    return;
  }

  if (access("/sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("/sys/fs/cgroup", 0755) != 0) {
      ds_error("[CGROUP] Failed to create /sys/fs/cgroup: %s", strerror(errno));
      return;
    }
  }

  /* tmpfs anchor needed: cgroup2 can't layer directly on ramfs */
  if (statfs("/sys/fs/cgroup", &sfs) == 0 &&
      (unsigned long)sfs.f_type != (unsigned long)TMPFS_MAGIC &&
      (unsigned long)sfs.f_type != (unsigned long)CGROUP2_SUPER_MAGIC) {
    if (mount("none", "/sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") != 0) {
      ds_error("[CGROUP] Failed to mount tmpfs on /sys/fs/cgroup: %s",
               strerror(errno));
      return;
    }
    ds_log("[CGROUP] Mounted tmpfs anchor on /sys/fs/cgroup.");
  }

  if (mount("none", "/sys/fs/cgroup", "cgroup2",
            MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
    ds_error("Failed to mount cgroup2 on /sys/fs/cgroup: %s", strerror(errno));
    return;
  }
  ds_log("Auto-mounted cgroup2 on /sys/fs/cgroup.");
}


/*
 * mount_v1_controllers - unified v1 controller setup, works on every host.
 *
 * Iterates /proc/cgroups (kernel truth) for every enabled controller.
 * For each one:
 *   1. If already present in sys/fs/cgroup/ -> skip (idempotent).
 *   2. If found in the host map AND in_ns  -> fresh namespace mount.
 *   3. If found in the host map, no ns     -> bind-mount from host path.
 *   4. Not in map (pure-v2, hybrid gap)    -> synthesize a fresh v1 mount.
 *      This is safe: the kernel accepts a fresh v1 mount for any subsystem
 *      that isn't already bound to an active v2 hierarchy with live tasks.
 *
 * Co-mounted hierarchies (e.g. net_cls,net_prio on the same host mount)
 * get symlinks for their secondary names, matching historical behaviour.
 */
static void mount_v1_controllers(void) {
  FILE *f = fopen("/proc/cgroups", "re");
  if (!f)
    return;

  unsigned long flags = MS_NOSUID | MS_NODEV | MS_NOEXEC;
  char line[256];
  if (!fgets(line, sizeof(line), f)) { /* skip header */
    fclose(f);
    return;
  }

  while (fgets(line, sizeof(line), f)) {
    char name[64];
    int hier, ncg, enabled;
    if (sscanf(line, "%63s %d %d %d", name, &hier, &ncg, &enabled) != 4)
      continue;
    if (!enabled)
      continue;

    char mp[PATH_MAX];
    snprintf(mp, sizeof(mp), "sys/fs/cgroup/%s", name);
    if (access(mp, F_OK) == 0)
      continue; /* already set up or co-mounted */

    if (mkdir(mp, 0755) < 0 && errno != EEXIST)
      continue;

    /* Always synthesize a fresh v1 mount for every subsystem.
     * The kernel translates this to the container's isolated cgroupns root. */
    if (mount("cgroup", mp, "cgroup", flags, name) != 0) {
      ds_log("[CGROUP] v1 controller '%s' unavailable: %s", name,
             strerror(errno));
      rmdir(mp);
    } else {
      ds_log("[CGROUP] v1 mounted: %s", name);
    }
  }
  fclose(f);
}

int setup_cgroups(int is_systemd, int force_cgroupv1) {
  ds_cgroup_host_bootstrap(force_cgroupv1);

  if (access("sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("sys/fs/cgroup", 0755) < 0)
      return -1;
  }

  /* Mount tmpfs as the cgroup base */
  if (domount("none", "sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") < 0)
    return -1;

  int v2_active = ds_cgroup_host_is_v2() && !force_cgroupv1;
  int systemd_setup_done = 0;

  if (v2_active) {
    /* Always mount a fresh cgroup2 hierarchy within the container's
     * cgroup namespace. Isolation is handled by the kernel namespace. */
    if (mount("cgroup2", "sys/fs/cgroup", "cgroup2",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0) {
      systemd_setup_done = 1;
    } else {
      ds_error("Failed to mount cgroup2: %s", strerror(errno));
    }
  } else {
    /* V1 PATH (force_cgroupv1): Synthesize fresh mounts for all controllers. */
    mount_v1_controllers();
    systemd_setup_done = 1; /* handled via systemd named cgroup below */
  }

  /* Ensure a systemd cgroup hierarchy exists for systemd containers.
   * On v1 this is a named cgroup; on v2 systemd uses the unified root. */
  if (is_systemd && !v2_active) {
    if (access("sys/fs/cgroup/systemd", F_OK) != 0) {
      mkdir("sys/fs/cgroup/systemd", 0755);
      if (mount("cgroup", "sys/fs/cgroup/systemd", "cgroup",
                MS_NOSUID | MS_NODEV | MS_NOEXEC, "none,name=systemd") < 0) {
        ds_error("Failed to mount systemd cgroup: %s", strerror(errno));
        return -1;
      }
    }
    systemd_setup_done = 1;
  }

  if (is_systemd && !systemd_setup_done) {
    ds_error("Systemd cgroup setup failed. Systemd containers cannot boot.");
    return -1;
  }

  return 0;
}

/**
 * Move a process (usually self) into the same cgroup hierarchy as target_pid.
 * This is used by 'enter' to ensure the process is physically inside the
 * container's cgroup subtree on the host, which is required for D-Bus/logind
 * inside the container to correctly move the process into session scopes.
 */
int ds_cgroup_attach(pid_t target_pid) {
  /* On unified v2 hosts, we only need to write to the root v2 hierarchy.
   * On hybrid hosts, we might need to iterate through mounted v1 controllers.
   * For simplicity and correctness, we scan sys/fs/cgroup for directories. */
  DIR *d = opendir("/sys/fs/cgroup");
  if (!d)
    return -1;

  char target_cgroup[PATH_MAX];
  snprintf(target_cgroup, sizeof(target_cgroup), "/proc/%d/cgroup", target_pid);

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;

    char cg_root[PATH_MAX];
    snprintf(cg_root, sizeof(cg_root), "/sys/fs/cgroup/%s", de->d_name);
    struct stat st;
    if (stat(cg_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
      /* Might be the unified root at /sys/fs/cgroup itself */
      if (strcmp(de->d_name, "cgroup.procs") == 0) {
        safe_strncpy(cg_root, "/sys/fs/cgroup", sizeof(cg_root));
      } else {
        continue;
      }
    }

    /* 1. Discover where target_pid lives in this hierarchy */
    FILE *f = fopen(target_cgroup, "re");
    if (!f)
      continue;

    char line[1024], subpath[PATH_MAX] = {0};
    while (fgets(line, sizeof(line), f)) {
      char *col1 = strchr(line, ':');
      if (!col1)
        continue;
      char *col2 = strchr(col1 + 1, ':');
      if (!col2)
        continue;

      char *subsys = col1 + 1;
      char *path = col2 + 1;
      *col2 = '\0';

      int match = 0;
      if (strcmp(de->d_name, "cgroup.procs") == 0 && subsys[0] == '\0') {
        match = 1; /* V2 match */
      } else if (strstr(subsys, de->d_name)) {
        match = 1; /* V1 match */
      }

      if (match) {
        char *nl = strchr(path, '\n');
        if (nl)
          *nl = '\0';
        safe_strncpy(subpath, path, sizeof(subpath));
        break;
      }
    }
    fclose(f);

    if (subpath[0] == '\0')
      continue;

    /* 2. Create leaf and move self - same logic as before but uses local sysfs */
    char leaf_dir[PATH_MAX];
    snprintf(leaf_dir, sizeof(leaf_dir), "%s%s/ds-enter-%d", cg_root, subpath,
             (int)getpid());

    if (mkdir_p(leaf_dir, 0755) < 0 && errno != EEXIST)
      continue;

    char procs_path[PATH_MAX];
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", leaf_dir);
    int pfd = open(procs_path, O_WRONLY | O_CLOEXEC);
    if (pfd >= 0) {
      char pid_s[32];
      int len = snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
      write(pfd, pid_s, len);
      close(pfd);
    }

    if (strcmp(de->d_name, "cgroup.procs") == 0) break;
  }
  closedir(d);
  return 0;
}

/* ---------------------------------------------------------------------------
 * ds_cgroup_detach
 *
 * Removes the ds-enter-<pid> leaf cgroup directories that ds_cgroup_attach()
 * created for a single enter/run session.  Must be called by the parent after
 * waitpid() so the leaf is guaranteed to be empty.
 * ---------------------------------------------------------------------------*/
void ds_cgroup_detach(pid_t child_pid, const char *container_name) {
  /* After child exits, we clean up the leaf cgroups we created in attach.
   * Since we create them in every mounted hierarchy under /sys/fs/cgroup,
   * we just iterate that dir again. */
  (void)container_name; /* reserved for future per-layout targeting */

  DIR *d = opendir("/sys/fs/cgroup");
  if (!d)
    return;

  char enter_suffix[64];
  snprintf(enter_suffix, sizeof(enter_suffix), "/ds-enter-%d", (int)child_pid);

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;

    char cg_root[PATH_MAX];
    snprintf(cg_root, sizeof(cg_root), "/sys/fs/cgroup/%s", de->d_name);
    struct stat st;
    if (stat(cg_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
      if (strcmp(de->d_name, "cgroup.procs") == 0)
        safe_strncpy(cg_root, "/sys/fs/cgroup", sizeof(cg_root));
      else
        continue;
    }

    /* We need to recursively find and rmdir the ds-enter leaf.
     * For simplicity, since we know it's a descendant of /droidspaces/,
     * we can just walk the container's subtree. */
    char ds_dir[PATH_MAX];
    snprintf(ds_dir, sizeof(ds_dir), "%s/droidspaces", cg_root);

    DIR *top = opendir(ds_dir);
    if (!top) {
      /* Try directly in root (no container name nesting) */
      char direct[PATH_MAX];
      snprintf(direct, sizeof(direct), "%s%s", cg_root, enter_suffix);
      rmdir(direct);
      continue;
    }

    struct dirent *inner;
    while ((inner = readdir(top)) != NULL) {
      if (inner->d_name[0] == '.')
        continue;
      char leaf[PATH_MAX];
      snprintf(leaf, sizeof(leaf), "%s/%s%s", ds_dir, inner->d_name, enter_suffix);
      rmdir(leaf);
    }
    closedir(top);
    if (strcmp(de->d_name, "cgroup.procs") == 0) break;
  }
  closedir(d);
}

/* ---------------------------------------------------------------------------
 * ds_cgroup_cleanup_container
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
 * (all rmdir calls are silently ignored on ENOENT).
 * ---------------------------------------------------------------------------*/

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
static void rmdir_cgroup_tree(const char *path) {
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
    if (read_file(events_path, buf, sizeof(buf)) > 0) {
      if (strstr(buf, "populated 0"))
        break;
    }
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

void ds_cgroup_cleanup_container(const char *container_name) {
  if (!container_name || !container_name[0])
    return;

  char safe_name[256];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));

  DIR *d = opendir("/sys/fs/cgroup");
  if (!d)
    return;

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;

    char cg_path[PATH_MAX];
    snprintf(cg_path, sizeof(cg_path), "/sys/fs/cgroup/%s/droidspaces/%s",
             de->d_name, safe_name);

    /* Handle unified V2 where droidspaces/ is at root */
    if (strcmp(de->d_name, "cgroup.procs") == 0)
      snprintf(cg_path, sizeof(cg_path), "/sys/fs/cgroup/droidspaces/%s",
               safe_name);

    if (access(cg_path, F_OK) != 0)
      continue;
    rmdir_cgroup_tree(cg_path);
    if (strcmp(de->d_name, "cgroup.procs") == 0) break;
  }
  closedir(d);
}

static int ds_host_supports_v2_cached = -1;

void print_cgroup_status(struct ds_config *cfg) {
  if (cfg->force_cgroupv1) {
    ds_warn("Using legacy Cgroup V1 hierarchy (forced by --force-cgroupv1)");
    return;
  }

  if (ds_host_supports_v2_cached == -1)
    ds_host_supports_v2_cached = ds_cgroup_kernel_supports_v2();

  if (!ds_host_supports_v2_cached) {
    ds_warn("Host does not support Cgroup V2 (falling back to legacy V1)");
  }
}
