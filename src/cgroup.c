/*
 * Droidspaces v5 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Data structure for host cgroup hierarchy info */
struct host_cgroup {
  char mountpoint[PATH_MAX];
  char controllers[256];
  int version;
};

/* Find the container's cgroup path for a given controller by reading
 * /proc/self/cgroup. If controller is NULL, it looks for the v2 (unified)
 * hierarchy. */
static int find_self_cgroup_path(const char *controller, char *buf,
                                 size_t size) {
  FILE *f = fopen("/proc/self/cgroup", "re");
  if (!f)
    return -1;

  char line[1024];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char *col1 = strchr(line, ':');
    if (!col1)
      continue;
    char *col2 = strchr(col1 + 1, ':');
    if (!col2)
      continue;

    *col2 = '\0';
    char *subsys = col1 + 1;
    char *path = col2 + 1;

    /* Nuke newline at the end of path */
    char *newline = strchr(path, '\n');
    if (newline)
      *newline = '\0';

    if (controller == NULL) {
      /* Cgroup v2 (unified) is identified by an empty controller list */
      if (subsys[0] == '\0') {
        safe_strncpy(buf, path, size);
        found = 1;
        break;
      }
    } else {
      /* For v1, check if controller is present in the hierarchy's subsystem
       * list. (e.g. "cpu,cpuacct") */
      if (strstr(subsys, controller)) {
        safe_strncpy(buf, path, size);
        found = 1;
        break;
      }
    }
  }
  fclose(f);
  return found ? 0 : -1;
}

/* Parse /proc/self/mountinfo to discover how the host has mounted cgroups.
 * This is the same approach LXC uses to be "data-driven" rather than guessing.
 */
static int get_host_cgroups(struct host_cgroup *out, int max) {
  FILE *f = fopen("/proc/self/mountinfo", "re");
  if (!f)
    return 0;

  char line[2048];
  int count = 0;
  while (fgets(line, sizeof(line), f) && count < max) {
    /* mountinfo format: mountID parentID devID root mountPoint mountOptions
     * [optionalFields] - fsType mountSource superOptions */
    char *dash = strstr(line, " - ");
    if (!dash)
      continue;

    char fstype[64];
    if (sscanf(dash + 3, "%63s", fstype) != 1)
      continue;

    if (strcmp(fstype, "cgroup") != 0 && strcmp(fstype, "cgroup2") != 0)
      continue;

    /* Extract mount point (field 5) */
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
    safe_strncpy(out[count].mountpoint, p, sizeof(out[count].mountpoint));

    out[count].version = (strcmp(fstype, "cgroup2") == 0) ? 2 : 1;

    /* Extract controllers/options from superOptions (last field) */
    if (out[count].version == 1) {
      char *super_opts = strchr(dash + 3 + strlen(fstype) + 1, ' ');
      if (super_opts) {
        super_opts++; /* skip space to mountSource */
        super_opts =
            strchr(super_opts, ' '); /* skip mountSource to superOptions */
        if (super_opts) {
          super_opts++;
          char *newline = strchr(super_opts, '\n');
          if (newline)
            *newline = '\0';

          /* Strip 'rw,' or 'ro,' prefix (generic mount flags) */
          if (strncmp(super_opts, "rw,", 3) == 0)
            super_opts += 3;
          else if (strncmp(super_opts, "ro,", 3) == 0)
            super_opts += 3;
          else if (strcmp(super_opts, "rw") == 0 ||
                   strcmp(super_opts, "ro") == 0)
            super_opts = "";

          safe_strncpy(out[count].controllers, super_opts,
                       sizeof(out[count].controllers));
        }
      }
    } else {
      safe_strncpy(out[count].controllers, "unified",
                   sizeof(out[count].controllers));
    }

    /* Verify we are not looking at a mount inside Droidspaces itself
     * (e.g. if we are restarting) */
    if (!strstr(out[count].mountpoint, "/Droidspaces/")) {
      count++;
    }
  }
  fclose(f);
  return count;
}

/* Detect if we are in a virtualized cgroup namespace.
 * In a namespace, /proc/self/cgroup will show "/" for the path. */
static int is_cgroup_ns_active(void) {
  FILE *f = fopen("/proc/self/cgroup", "re");
  if (!f)
    return 0;

  char line[1024];
  int is_ns = 1;
  while (fgets(line, sizeof(line), f)) {
    char *col2 = strrchr(line, ':');
    if (col2) {
      /* Remove newline */
      char *nl = strchr(col2, '\n');
      if (nl)
        *nl = '\0';

      if (strcmp(col2 + 1, "/") != 0) {
        is_ns = 0;
        break;
      }
    }
  }
  fclose(f);
  return is_ns;
}

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

/* Returns 1 if the HOST's /sys/fs/cgroup is a pure cgroupv2 root.
 * Checked before pivot_root so /sys/fs/cgroup still refers to the host mount.
 * Public so main.c can validate --force-cgroupv1 before launch. */
int ds_cgroup_host_is_v2(void) {
  struct statfs sfs;
  if (statfs("/sys/fs/cgroup", &sfs) != 0)
    return 0;
  return (unsigned long)sfs.f_type == (unsigned long)CGROUP2_SUPER_MAGIC;
}

void ds_cgroup_host_bootstrap(int force_cgroupv1) {
  /* ANDROID RECOVERY FIX: If cgroup2 is supported but NOT mounted anywhere
   * on the host, we mount it ourselves on /sys/fs/cgroup. This prevents
   * falling back to V1 on modern kernels just because the recovery init
   * script didn't mount it. */
  if (!force_cgroupv1 && !ds_cgroup_host_is_v2() &&
      grep_file("/proc/filesystems", "cgroup2") > 0) {
    if (mkdir_p("/sys/fs/cgroup", 0755) == 0) {
      /* Try to mount a tmpfs base first (LXC-style) */
      mount("none", "/sys/fs/cgroup", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
            "mode=755,size=16M");
      if (mount("none", "/sys/fs/cgroup", "cgroup2",
                MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0) {
        ds_log("Auto-mounted Cgroup V2 on host.");
      }
    }
  }
}

/* Android non-standard v1 controller path aliases.
 * Android mounts many controllers outside /sys/fs/cgroup, and some use
 * non-standard mount-option names.  This table maps the kernel's canonical
 * controller name (from /proc/cgroups col-1) to the typical Android mountpoint
 * suffix and the mount option string the kernel expects.
 *
 * History:
 *  Android 4–8:  cpu→/dev/cpuctl, cpuset→/dev/cpuset, memory→/dev/memcg
 *  Android 9–11: blkio→/dev/blkio added; schedtune on some OEM kernels
 *  Android 12+:  cgroups.json abstraction; memory path still /dev/memcg on
 *                many devices; blkio may be at /dev/blkio or
 * /sys/fs/cgroup/blkio Pure-v2 host: none of the above exist; --force-cgroupv1
 * user synthesizes fresh v1 mounts directly from the kernel's controller table.
 */
struct cgroup_v1_alias {
  const char *kernel_name; /* subsys_name from /proc/cgroups          */
  const char *dir_suffix;  /* directory under sys/fs/cgroup/           */
  const char *mount_opt;   /* -o option for mount(2), NULL=kernel_name */
};

static const struct cgroup_v1_alias v1_aliases[] = {
    {"cpuset", "cpuset", NULL},
    {"cpu", "cpu", "cpu"},
    {"cpuacct", "cpuacct", "cpuacct"},
    {"blkio", "blkio", "blkio"},
    {"memory", "memory", "memory"},
    {"devices", "devices", "devices"},
    {"freezer", "freezer", "freezer"},
    {"net_cls", "net_cls", "net_cls"},
    {"net_prio", "net_prio", "net_prio"},
    {"pids", "pids", "pids"},
    {"perf_event", "perf_event", "perf_event"},
    {"misc", "misc", "misc"},
    /* Android OEM aliases */
    {"schedtune", "schedtune", "schedtune"},
    {NULL, NULL, NULL}};

/* Synthesize v1 cgroup mounts from /proc/cgroups when --force-cgroupv1 is
 * active but the host (pure cgroupv2 Android) has no v1 mountinfo entries.
 *
 * For each enabled controller in /proc/cgroups that isn't already mounted
 * under sys/fs/cgroup/, we attempt a fresh mount("cgroup", path, "cgroup",
 * flags, controller_name).  This is safe because:
 *   1. The kernel already has the subsystem compiled in (/proc/cgroups
 * enabled=1).
 *   2. cgroupv1 allows independent mounts of each subsystem even on a host
 *      that normally uses v2, as long as the subsystem isn't already attached
 *      to a live v2 hierarchy with active processes (rare in Android
 * userspace).
 *   3. We mount into the container's private tmpfs base, so the host is
 *      not affected.
 *
 * Returns the number of controllers successfully mounted.
 */
static int ds_cgroup_v1_synthesize(void) {
  FILE *f = fopen("/proc/cgroups", "re");
  if (!f)
    return 0;

  unsigned long flags = MS_NOSUID | MS_NODEV | MS_NOEXEC;
  int mounted = 0;
  char line[256];

  /* Skip header line */
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return 0;
  }

  while (fgets(line, sizeof(line), f)) {
    char name[64];
    int hierarchy, num_cgroups, enabled;

    /* Format: subsys_name  hierarchy  num_cgroups  enabled */
    if (sscanf(line, "%63s %d %d %d", name, &hierarchy, &num_cgroups,
               &enabled) != 4)
      continue;

    if (!enabled)
      continue;

    /* Skip name=systemd - handled separately */
    if (strcmp(name, "name") == 0)
      continue;

    /* Find alias entry */
    const struct cgroup_v1_alias *alias = NULL;
    for (int i = 0; v1_aliases[i].kernel_name != NULL; i++) {
      if (strcmp(v1_aliases[i].kernel_name, name) == 0) {
        alias = &v1_aliases[i];
        break;
      }
    }

    /* Use kernel name directly if no explicit alias */
    const char *dir = alias ? alias->dir_suffix : name;
    const char *opt =
        alias ? (alias->mount_opt ? alias->mount_opt : name) : name;

    char mp[PATH_MAX];
    snprintf(mp, sizeof(mp), "sys/fs/cgroup/%s", dir);

    /* Already mounted by the main loop (e.g. from a real mountinfo entry) */
    if (access(mp, F_OK) == 0)
      continue;

    if (mkdir(mp, 0755) < 0 && errno != EEXIST)
      continue;

    if (mount("cgroup", mp, "cgroup", flags, opt) == 0) {
      ds_log("[CGROUP] mounted %s at %s", opt, mp);
      mounted++;
    } else {
      /* Some controllers (e.g. schedtune) are OEM-only and will fail on
       * mainline kernels.  Log at debug level and remove the empty dir. */
      ds_log("[CGROUP] skipped %s: %s", opt, strerror(errno));
      rmdir(mp);
    }
  }

  fclose(f);
  return mounted;
}

int setup_cgroups(int is_systemd, int force_cgroupv1) {
  /* Ensure host has cgroups mounted before we try to setup container subset.
   * Inside the container namespace, this is mostly a no-op if host is already
   * setup, but ensures private view is consistent. */
  ds_cgroup_host_bootstrap(force_cgroupv1);

  if (access("sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("sys/fs/cgroup", 0755) < 0)
      return -1;
  }

  /* 1. Mount tmpfs as the cgroup base */
  if (domount("none", "sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") < 0)
    return -1;

  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  int in_ns = is_cgroup_ns_active();

  /* CGROUP SELECTION LOGIC:
   * 1. If --force-cgroupv1 is set -> strictly use V1.
   * 2. Otherwise, if the host has V2 mounted -> use V2.
   * 3. Fallback to V1.
   * This allows forcing V2 even on older kernels (like 4.14) if supported
   * by the host, while giving an escape hatch to V1. */
  int v2_active = ds_cgroup_host_is_v2() && !force_cgroupv1;
  int systemd_setup_done = 0;

  for (int i = 0; i < n; i++) {
    /* STRICT SEPARATION:
     * We don't mix V1 and V2 hierarchies in the container. */
    if (v2_active && hosts[i].version == 1)
      continue;
    if (!v2_active && hosts[i].version == 2)
      continue;

    char container_mp[PATH_MAX];
    const char *suffix = NULL;

    if (strcmp(hosts[i].mountpoint, "/sys/fs/cgroup") == 0) {
      suffix = "";
    } else {
      suffix = strstr(hosts[i].mountpoint, "/sys/fs/cgroup/");
      if (suffix) {
        suffix += 15; /* skip "/sys/fs/cgroup/" */
      } else {
        suffix = strrchr(hosts[i].mountpoint, '/');
        if (suffix)
          suffix++;
        else
          suffix = hosts[i].controllers;
      }
    }

    /* Track if this is the systemd hierarchy */
    int is_systemd_hierarchy = (strcmp(suffix, "systemd") == 0 ||
                                strstr(hosts[i].controllers, "name=systemd"));

    snprintf(container_mp, sizeof(container_mp), "sys/fs/cgroup/%s", suffix);
    if (suffix[0] != '\0') {
      mkdir(container_mp, 0755);
    }

    if (in_ns) {
      /* MODERN PATH: Use Cgroup Namespace support.
       * Mounting the cgroup filesystem directly inside the namespace
       * automatically gives us the container-isolated root. */
      unsigned long flags = MS_NOSUID | MS_NODEV | MS_NOEXEC;
      const char *fstype = (hosts[i].version == 2) ? "cgroup2" : "cgroup";
      const char *opts = (hosts[i].version == 2) ? NULL : hosts[i].controllers;

      /* For v1, we MUST specify at least one controller. If the parser failed
       * to find them in mountinfo (common on Android), fallback to the
       * directory name (suffix). */
      if (hosts[i].version == 1 && (opts == NULL || opts[0] == '\0')) {
        opts = suffix;
      }

      /* ANDROID FIX: Map known directory names to actual controller names
       * expected by the kernel. */
      const char *actual_opts = opts;
      if (hosts[i].version == 1) {
        if (strcmp(opts, "memcg") == 0)
          actual_opts = "memory";
        else if (strcmp(opts, "acct") == 0)
          actual_opts = "cpuacct";
      }

      if (mount("cgroup", container_mp, fstype, flags, actual_opts) == 0) {
        if (is_systemd_hierarchy || hosts[i].version == 2)
          systemd_setup_done = 1;
        goto symlink_v1;
      }
    }

    /* LEGACY PATH: Manual bind-mount isolation */
    char self_path[PATH_MAX];
    const char *ctrl_for_lookup =
        (hosts[i].version == 2) ? NULL : hosts[i].controllers;
    char first_ctrl[64];

    if (ctrl_for_lookup) {
      if (sscanf(ctrl_for_lookup, "%63[^,]", first_ctrl) == 1) {
        ctrl_for_lookup = first_ctrl;
      }
    }

    if (find_self_cgroup_path(ctrl_for_lookup, self_path, sizeof(self_path)) ==
        0) {
      char host_full_subpath[PATH_MAX * 2];
      safe_strncpy(host_full_subpath, hosts[i].mountpoint,
                   sizeof(host_full_subpath));
      strncat(host_full_subpath, self_path,
              sizeof(host_full_subpath) - strlen(host_full_subpath) - 1);

      /* Some ROMs mount cgroup controllers at non-standard locations
       * (e.g. blkio at /dev/blkio instead of /sys/fs/cgroup/blkio).
       * If the resolved subpath doesn't exist, fall back to the hierarchy
       * root so the bind-mount still succeeds. */
      if (access(host_full_subpath, F_OK) != 0) {
        ds_log("[DEBUG] cgroup subpath %s not found, falling back to %s",
               host_full_subpath, hosts[i].mountpoint);
        safe_strncpy(host_full_subpath, hosts[i].mountpoint,
                     sizeof(host_full_subpath));
      }

      unsigned long flags = MS_BIND | MS_REC | MS_NOSUID | MS_NODEV | MS_NOEXEC;
      if (domount_silent(host_full_subpath, container_mp, NULL, flags, NULL) ==
          0) {
        if (is_systemd_hierarchy || hosts[i].version == 2)
          systemd_setup_done = 1;
      } else {
        ds_log("[DEBUG] cgroup bind-mount %s -> %s failed: %s",
               host_full_subpath, container_mp, strerror(errno));
      }
    }

  symlink_v1:
    /* Create symlinks for secondary names in comounted v1 hierarchies */
    if (hosts[i].version == 1 && strchr(hosts[i].controllers, ',')) {
      char *tok, *saveptr;
      char *it = strdup(hosts[i].controllers);
      if (it) {
        tok = strtok_r(it, ",", &saveptr);
        while (tok) {
          char link_path[PATH_MAX];
          snprintf(link_path, sizeof(link_path), "sys/fs/cgroup/%s", tok);
          if (strcmp(tok, suffix) != 0) {
            if (access(link_path, F_OK) != 0) {
              if (symlink(suffix, link_path) < 0) {
                ds_warn("Failed to create cgroup symlink %s -> %s: %s",
                        link_path, suffix, strerror(errno));
              }
            }
          }
          tok = strtok_r(NULL, ",", &saveptr);
        }
        free(it);
      }
    }
  }

  /* 2. PURE-V2 HOST + --force-cgroupv1 FALLBACK:
   * When the host is a pure cgroupv2 Android device and the user requested
   * --force-cgroupv1, the mountinfo scan above yields zero v1 entries.
   * Android never mounts v1 controllers in this configuration, so we must
   * synthesize them directly from /proc/cgroups.
   *
   * We detect this condition by checking: forced v1 requested AND the host
   * is pure v2 AND no v1 hierarchy was successfully set up yet. */
  if (force_cgroupv1 && ds_cgroup_host_is_v2()) {
    /* Count how many v1 mounts we actually produced from the main loop */
    int v1_found = 0;
    for (int i = 0; i < n; i++) {
      if (hosts[i].version == 1)
        v1_found++;
    }
    if (v1_found == 0) {
      ds_log("[CGROUP] pure-v2 host with --force-cgroupv1: synthesizing v1 "
             "controllers from /proc/cgroups");
      int synth = ds_cgroup_v1_synthesize();
      ds_log("[CGROUP] synthesized %d v1 controller(s)", synth);
    }
  }

  /* 3. FORCED SYSTEMD SUPPORT: If we are booting a systemd rootfs but no
   * systemd hierarchy was found on the host, we MUST create one manually.
   * On modern kernels (or if V2 is active), we skip this because systemd
   * will use the unified hierarchy. */
  if (is_systemd && !systemd_setup_done && !v2_active) {
    mkdir("sys/fs/cgroup/systemd", 0755);
    if (mount("cgroup", "sys/fs/cgroup/systemd", "cgroup",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "none,name=systemd") < 0) {
      ds_error("Failed to mount systemd cgroup: %s", strerror(errno));
      return -1;
    }
    systemd_setup_done = 1;
  }

  /* If it's a systemd container and we still don't have a systemd cgroup, fail
   * early. */
  if (is_systemd && !systemd_setup_done) {
    ds_error("Systemd cgroup setup failed. Systemd containers cannot boot.");
    return -1;
  }

  /* Final isolation: Both Pure V2 and Legacy V1 environments stay Read-Write.
   * - Systemd-v2 needs write access to the root to manage scopes.
   * - Systemd-v1/OpenRC needs write access to create controller symlinks.
   * The "Hybrid-RO" middle-ground is now removed. */

  return 0;
}

/**
 * Move a process (usually self) into the same cgroup hierarchy as target_pid.
 * This is used by 'enter' to ensure the process is physically inside the
 * container's cgroup subtree on the host, which is required for D-Bus/logind
 * inside the container to correctly move the process into session scopes.
 */
int ds_cgroup_attach(pid_t target_pid) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  for (int i = 0; i < n; i++) {
    const char *ctrl = (hosts[i].version == 2) ? NULL : hosts[i].controllers;
    char first_ctrl[64];

    if (hosts[i].version == 1 && ctrl) {
      if (sscanf(ctrl, "%63[^,]", first_ctrl) == 1)
        ctrl = first_ctrl;
    }

    /* 1. Discover where target_pid lives in this hierarchy */
    char proc_path[PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/cgroup", target_pid);

    FILE *f = fopen(proc_path, "re");
    if (!f)
      continue;

    char line[1024];
    char subpath[PATH_MAX] = {0};
    while (fgets(line, sizeof(line), f)) {
      char *col1 = strchr(line, ':');
      if (!col1)
        continue;
      char *col2 = strchr(col1 + 1, ':');
      if (!col2)
        continue;

      char *subsys = col1 + 1;
      *col2 = '\0';
      char *path = col2 + 1;

      int match = 0;
      if (hosts[i].version == 2 && subsys[0] == '\0') {
        match = 1;
      } else if (hosts[i].version == 1 && ctrl && strstr(subsys, ctrl)) {
        match = 1;
      }

      if (match) {
        char *nl = strchr(path, '\n');
        if (nl)
          *nl = '\0';
        safe_strncpy(subpath, path, sizeof(subpath));

        /* Professional refinement: if the path ends in a systemd management
         * unit (.scope, .service, .slice), strip that component. This ensures
         * the 'ds-enter-PID' cgroup is created as a peer to 'init.scope'
         * (the container root) rather than being nested inside it. This is
         * cleaner for systemd's accounting and avoids "non-leaf" V2 errors. */
        char *last_slash = strrchr(subpath, '/');
        if (last_slash && last_slash != subpath) {
          if (strstr(last_slash, ".scope") || strstr(last_slash, ".service") ||
              strstr(last_slash, ".slice")) {
            *last_slash = '\0';
          }
        }
        break;
      }
    }
    fclose(f);

    if (subpath[0] == '\0')
      continue;

    /* 2. Create a fresh leaf cgroup under init's path.
     *
     * Writing directly to init's cgroup.procs fails with EPERM on cgroupv1
     * legacy kernels (and for systemd-managed scopes on v2): the cgroup is
     * either non-leaf or systemd holds a delegation lock on it.  The correct
     * approach - which is exactly what lxc-attach uses - is to mkdir a new
     * child cgroup under the target's subtree and write into THAT.  We own
     * the new directory so the write always succeeds, and the process appears
     * in the hierarchy as a proper descendant of init's cgroup rather than
     * leaking to the cgroup root ("/"). */
    /* Build: <mountpoint>/<subpath>/ds-enter-<pid>
     * subpath always starts with '/' so we skip the extra separator.
     * Use strncat chains - snprintf of two PATH_MAX strings into one
     * PATH_MAX buffer triggers -Wformat-truncation=2 at compile time. */
    char leaf_dir[PATH_MAX];
    char enter_suffix[32];
    safe_strncpy(leaf_dir, hosts[i].mountpoint, sizeof(leaf_dir));
    /* subpath begins with '/', append directly - no extra '/' needed. */
    strncat(leaf_dir, subpath, sizeof(leaf_dir) - strlen(leaf_dir) - 1);
    snprintf(enter_suffix, sizeof(enter_suffix), "/ds-enter-%d", (int)getpid());
    strncat(leaf_dir, enter_suffix, sizeof(leaf_dir) - strlen(leaf_dir) - 1);

    if (mkdir(leaf_dir, 0755) < 0 && errno != EEXIST) {
      continue;
    }

    /* 3. Move self into the leaf via cgroup.procs (moves whole process,
     *    not just the calling thread - unlike the legacy /tasks interface). */
    char procs_path[PATH_MAX];
    safe_strncpy(procs_path, leaf_dir, sizeof(procs_path));
    strncat(procs_path, "/cgroup.procs",
            sizeof(procs_path) - strlen(procs_path) - 1);

    int fd = open(procs_path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }

    char pid_s[32];
    int len = snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
    if (write(fd, pid_s, len) < 0) {
    }
    close(fd);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * ds_cgroup_detach
 *
 * Removes the ds-enter-<pid> leaf cgroup directories that ds_cgroup_attach()
 * created for a single enter/run session.  Must be called by the parent after
 * waitpid() so the leaf is guaranteed to be empty.
 * ---------------------------------------------------------------------------*/
void ds_cgroup_detach(pid_t child_pid) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  char enter_suffix[32];
  snprintf(enter_suffix, sizeof(enter_suffix), "/ds-enter-%d", (int)child_pid);

  for (int i = 0; i < n; i++) {
    char ds_dir[PATH_MAX];
    safe_strncpy(ds_dir, hosts[i].mountpoint, sizeof(ds_dir));
    strncat(ds_dir, "/droidspaces", sizeof(ds_dir) - strlen(ds_dir) - 1);

    DIR *top = opendir(ds_dir);
    if (!top) {
      char direct[PATH_MAX];
      safe_strncpy(direct, hosts[i].mountpoint, sizeof(direct));
      strncat(direct, enter_suffix, sizeof(direct) - strlen(direct) - 1);
      rmdir(direct);
      continue;
    }

    struct dirent *de;
    while ((de = readdir(top)) != NULL) {
      if (de->d_name[0] == '.')
        continue;
      char leaf[PATH_MAX];
      safe_strncpy(leaf, ds_dir, sizeof(leaf));
      strncat(leaf, "/", sizeof(leaf) - strlen(leaf) - 1);
      strncat(leaf, de->d_name, sizeof(leaf) - strlen(leaf) - 1);
      strncat(leaf, enter_suffix, sizeof(leaf) - strlen(leaf) - 1);
      rmdir(leaf);
    }
    closedir(top);
  }
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

  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  char safe_name[256];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));

  for (int i = 0; i < n; i++) {
    char cg_path[PATH_MAX];
    safe_strncpy(cg_path, hosts[i].mountpoint, sizeof(cg_path));
    strncat(cg_path, "/droidspaces/", sizeof(cg_path) - strlen(cg_path) - 1);
    strncat(cg_path, safe_name, sizeof(cg_path) - strlen(cg_path) - 1);

    if (access(cg_path, F_OK) != 0)
      continue; /* nothing to clean on this hierarchy */
    rmdir_cgroup_tree(cg_path);
  }
}
