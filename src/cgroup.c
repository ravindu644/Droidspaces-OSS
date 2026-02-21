/*
 * Droidspaces v4 — High-performance Container Runtime
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
      strcpy(out[count].controllers, "unified");
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
        ds_log("Cgroup NS check failed for line: %s (path: '%s')", line,
               col2 + 1);
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
int setup_cgroups(void) {
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

  int is_pure_v2 = 0;

  for (int i = 0; i < n; i++) {
    char container_mp[PATH_MAX];
    const char *suffix = NULL;

    if (strcmp(hosts[i].mountpoint, "/sys/fs/cgroup") == 0) {
      suffix = "";
      is_pure_v2 = (hosts[i].version == 2);
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

    snprintf(container_mp, sizeof(container_mp), "sys/fs/cgroup/%s", suffix);
    if (suffix[0] != '\0') {
      mkdir(container_mp, 0755);
    }

    int in_ns = is_cgroup_ns_active();

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
        /* Success - skip bind mount logic */
        goto symlink_v1;
      }
    }

    /* LEGACY PATH: Manual bind-mount isolation (for kernels < 4.6) */
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
      memset(host_full_subpath, 0, sizeof(host_full_subpath));
      strncpy(host_full_subpath, hosts[i].mountpoint,
              sizeof(host_full_subpath) - 1);
      strncat(host_full_subpath, self_path,
              sizeof(host_full_subpath) - strlen(host_full_subpath) - 1);

      unsigned long flags = MS_BIND | MS_REC | MS_NOSUID | MS_NODEV | MS_NOEXEC;
      domount(host_full_subpath, container_mp, NULL, flags, NULL);
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
              symlink(suffix, link_path);
            }
          }
          tok = strtok_r(NULL, ",", &saveptr);
        }
        free(it);
      }
    }
  }

  /* Final isolation: Remount /sys/fs/cgroup as Read-Only.
   * CRITICAL: Skip if it's a pure v2 hierarchy, as systemd needs write access
   * to create scopes at the root. On v1/hybrid, the base is tmpfs and safe to
   * RO. */
  if (!is_pure_v2) {
    mount(NULL, "sys/fs/cgroup", NULL,
          MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
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
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  for (int i = 0; i < n; i++) {
    const char *ctrl = (hosts[i].version == 2) ? NULL : hosts[i].controllers;
    char first_ctrl[64];

    if (hosts[i].version == 1 && ctrl) {
      if (sscanf(ctrl, "%63[^,]", first_ctrl) == 1)
        ctrl = first_ctrl;
    }

    /* 1. Discover where target_pid is for this hierarchy */
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
        break;
      }
    }
    fclose(f);

    if (subpath[0] == '\0')
      continue;

    /* 2. Move self (or caller) into that cgroup on the host */
    char tasks_path[PATH_MAX];
    safe_strncpy(tasks_path, hosts[i].mountpoint, sizeof(tasks_path));
    strncat(tasks_path, "/", sizeof(tasks_path) - strlen(tasks_path) - 1);
    strncat(tasks_path, subpath, sizeof(tasks_path) - strlen(tasks_path) - 1);
    strncat(tasks_path, (hosts[i].version == 2) ? "/cgroup.procs" : "/tasks",
            sizeof(tasks_path) - strlen(tasks_path) - 1);

    int fd = open(tasks_path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
      char pid_s[32];
      int len = snprintf(pid_s, sizeof(pid_s), "%d", getpid());
      if (write(fd, pid_s, len) < 0) {
        /* Ignore EPERM if we're already there or restricted,
         * but log other errors. */
        if (errno != EPERM) {
          ds_warn("Failed to attach to cgroup %s: %s", tasks_path,
                  strerror(errno));
        }
      }
      close(fd);
    }
  }

  return 0;
}
