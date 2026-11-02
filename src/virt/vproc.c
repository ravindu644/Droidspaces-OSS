/*
 * Droidspaces v7 - Resource Visibility Virtualization
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "virt/internal.h"

#define VPROC_PATH "/run/droidspaces/vproc"
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Internal helpers */

/* In-place overwrite: preserves the bind-mount inode (rename(2) breaks it).
 * Hardened with O_NOFOLLOW and filesystem type checks to prevent attacks. */
static int write_inplace(const char *path, const char *buf, size_t len) {
  /* Use O_NOFOLLOW to prevent the container from tricking us with symlinks */
  int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return -1;

  /* Security check: must be a regular file */
  struct stat st;
  if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
    close(fd);
    return -1;
  }

  /* Security check: must be on tmpfs (the container's /run/droidspaces/vproc)
   */
  struct statfs sfs;
  if (fstatfs(fd, &sfs) < 0 || sfs.f_type != TMPFS_MAGIC) {
    close(fd);
    return -1;
  }

  ssize_t w = write_all(fd, buf, len);
  if (w == (ssize_t)len) {
    /* Ignore ftruncate failure as we already wrote the data;
     * it might fail if the file is already the correct size. */
    if (ftruncate(fd, (off_t)len) < 0) {
      /* ignore */
    }
  }
  close(fd);
  return (w == (ssize_t)len) ? 0 : -1;
}

/* Public API */

unsigned long ds_get_pid_ns_inode(pid_t pid) {
  char path[64];
  struct stat st;
  snprintf(path, sizeof(path), "/proc/%d/ns/pid", (int)pid);
  return (stat(path, &st) == 0) ? (unsigned long)st.st_ino : 0UL;
}

/* Write content to a tmpfs file, then bind-mount it over the real proc entry.
 * Called from inside the container (after pivot_root, before exec). */
static void bind_vfile(const char *vpath, const char *target,
                       const char *content) {
  if (write_file(vpath, content) < 0)
    return;
  /* Ensure the target exists as a regular file for bind_mount.  O_NOFOLLOW so a
   * symlink planted at the final component is not followed (bind_mount also
   * rejects a symlinked target as defense-in-depth). */
  if (access(target, F_OK) != 0) {
    int fd = open(target, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0444);
    if (fd >= 0)
      close(fd);
  }
  if (bind_mount(vpath, target) < 0)
    ds_warn("[VIRT] bind_mount %s -> %s failed (continuing)", vpath, target);
}

enum { V_MEM = 1, V_CPU = 2, V_SWAPCAP = 4, V_LIVE = 8 };

/* The proc views, each wanted only under the limit it reflects. V_LIVE ones
 * are rewritten by the monitor every 500 ms; cpuinfo and swaps do not change
 * after boot. */
static const struct {
  const char *name;
  char *(*gen)(struct ds_config *, size_t *);
  int flags;
} vproc_files[] = {
    {"meminfo", gen_meminfo, V_MEM | V_LIVE},
    {"cpuinfo", gen_cpuinfo, V_CPU},
    {"stat", gen_stat, V_CPU | V_LIVE},
    {"uptime", gen_uptime, V_LIVE},
    {"loadavg", gen_loadavg, V_LIVE},
    {"swaps", gen_swaps, V_MEM | V_SWAPCAP},
};

static int vproc_wanted(const struct ds_config *cfg, int flags) {
  return !((flags & V_MEM) && cfg->memory_limit <= 0) &&
         !((flags & V_CPU) && cfg->cpu_quota <= 0) &&
         !((flags & V_SWAPCAP) && cfg->swap_uncapped);
}

int ds_virtualize_init(struct ds_config *cfg) {
  int has_mem = (cfg->memory_limit > 0);
  int has_cpu = (cfg->cpu_quota > 0);

  /* Apply CPU affinity masking so syscall-based tools (nproc) are fooled */
  if (has_cpu)
    ds_virtualize_affinity(cfg);

  /* Create tmpfs backing store inside container */
  if (mkdir_p(VPROC_PATH, 0755) < 0) {
    ds_warn("[VIRT] mkdir_p %s: %s", VPROC_PATH, strerror(errno));
    return -1;
  }
  if (domount("none", VPROC_PATH, "tmpfs", MS_NOSUID | MS_NODEV,
              "mode=755,size=1M") < 0) {
    ds_warn("[VIRT] tmpfs mount failed: %s", strerror(errno));
    return -1;
  }

  for (size_t i = 0; i < ARRAY_SIZE(vproc_files); i++) {
    if (!vproc_wanted(cfg, vproc_files[i].flags))
      continue;
    size_t len = 0;
    char *buf = vproc_files[i].gen(cfg, &len);
    if (!buf)
      continue;

    char vpath[PATH_MAX], target[PATH_MAX];
    snprintf(vpath, sizeof(vpath), VPROC_PATH "/%s", vproc_files[i].name);
    snprintf(target, sizeof(target), "/proc/%s", vproc_files[i].name);
    bind_vfile(vpath, target, buf);
    free(buf);
  }

  /* CPU sysfs for nproc/htop (only when cpu-limited) */
  if (has_cpu) {
    char sysfs_base[PATH_MAX];
    snprintf(sysfs_base, sizeof(sysfs_base), VPROC_PATH "/cpu_sysfs");
    if (mkdir_p(sysfs_base, 0755) == 0) {
      /* 1. Populate the virtual /sys/devices/system/cpu with masked cpuX dirs.
       * We bind-mount the real allowed cpuN directories into our virtual
       * sysfs base. This preserves all sub-files (cpufreq, topology, etc). */
      int n = container_cpus(cfg);
      for (int i = 0; i < n; i++) {
        char vcpu[PATH_MAX + 32], realcpu[PATH_MAX];
        snprintf(vcpu, sizeof(vcpu), "%s/cpu%d", sysfs_base, i);
        snprintf(realcpu, sizeof(realcpu), "/sys/devices/system/cpu/cpu%d", i);
        if (access(realcpu, F_OK) == 0) {
          if (mkdir(vcpu, 0755) == 0) {
            if (bind_mount(realcpu, vcpu) < 0)
              ds_warn("[VIRT] bind_mount %s -> %s failed", realcpu, vcpu);
          }
        }
      }

      /* 2. Write virtualized online/possible/present files into the base. */
      const char *sysfs_names[] = {"online", "possible", "present"};
      for (size_t i = 0; i < ARRAY_SIZE(sysfs_names); i++) {
        size_t len = 0;
        char *buf = gen_cpu_sysfs(cfg, &len);
        if (!buf)
          continue;
        char vpath[PATH_MAX + 32];
        snprintf(vpath, sizeof(vpath), "%s/%s", sysfs_base, sysfs_names[i]);
        write_file(vpath, buf);
        free(buf);
      }

      /* 3. Bind-mount the entire virtual directory over the real one.
       * readdir() on /sys/devices/system/cpu will now only see cpu0...cpu(N-1).
       */
      if (bind_mount(sysfs_base, "/sys/devices/system/cpu") < 0)
        ds_warn("[VIRT] Failed to mask /sys/devices/system/cpu (htop may show "
                "host cores)");
    }
  }

  ds_log("[VIRT] Resource virtualization active (mem=%d cpu=%d uptime=1 "
         "loadavg=1)",
         has_mem, has_cpu);
  return 0;
}

void ds_virtualize_update(struct ds_config *cfg) {
  if (cfg->container_pid <= 0)
    return;

  /* PID-recycling guard: verify container identity before touching its fs */
  if (cfg->ns_inode) {
    unsigned long live = ds_get_pid_ns_inode(cfg->container_pid);
    if (live != cfg->ns_inode) {
      write_monitor_debug_log(cfg->container_name,
                              "[VIRT] update skipped: ns_inode mismatch "
                              "(expected %lu, got %lu) pid=%d",
                              cfg->ns_inode, live, (int)cfg->container_pid);
      return;
    }
  }

  /* Pre-check: if the virtualization tmpfs directory is not yet
   * mounted/created, skip the update silently to avoid startup race logs and
   * redundant generation overhead. */
  char vproc_dir[PATH_MAX];
  snprintf(vproc_dir, sizeof(vproc_dir), "/proc/%d/root" VPROC_PATH,
           (int)cfg->container_pid);
  struct stat st_dir;
  if (stat(vproc_dir, &st_dir) != 0 || !S_ISDIR(st_dir.st_mode)) {
    return;
  }

  for (size_t i = 0; i < ARRAY_SIZE(vproc_files); i++) {
    if (!(vproc_files[i].flags & V_LIVE) ||
        !vproc_wanted(cfg, vproc_files[i].flags))
      continue;
    size_t len = 0;
    char *buf = vproc_files[i].gen(cfg, &len);
    if (!buf) {
      write_monitor_debug_log(cfg->container_name,
                              "[VIRT] gen_%s returned NULL",
                              vproc_files[i].name);
      continue;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%d/root/" VPROC_PATH "/%s",
             (int)cfg->container_pid, vproc_files[i].name);

    /* The directory is there but this file is not: the guest is taking its
     * /run apart on the way down, or this one file never got set up. Nothing
     * to refresh here and nothing to report, any more than for the directory
     * itself above; the other files still get theirs. */
    struct stat st;
    if (stat(path, &st) != 0) {
      free(buf);
      continue;
    }

    if (write_inplace(path, buf, len) < 0)
      write_monitor_debug_log(cfg->container_name,
                              "[VIRT] write_inplace failed: %s (%s)", path,
                              strerror(errno));
    free(buf);
  }
}
