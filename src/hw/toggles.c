/*
 * Droidspaces v7 - Per-feature hardware toggles
 *
 * --hw-access is all or nothing.  These toggles split it into the pieces a
 * container usually needs, each one carrying its own /dev nodes and its own
 * /sys write access, so nothing else opens up with it:
 *
 *   --dev-input      /dev/input and /dev/uinput
 *   --dev-usb        /dev/bus/usb, ttyUSB, ttyACM, hidraw
 *   --dev-sensors    IIO / sensor nodes
 *   --dev-block=LIST only the listed block devices (+ CAP_SYS_RAWIO)
 *   --firmware       host firmware search path (see start.c)
 *
 * Every toggle works on top of the isolated tmpfs /dev, like --gpu.  They do
 * nothing when --hw-access is on, which already exposes everything.
 *
 * /sys is visible read-only in every mode.  A toggle only adds RW where the
 * hardware has to be written to (authorize, power, brightness...).  sysfs
 * entries are symlinks into /sys/devices, so a class directory alone is not
 * enough: the real device directory behind each link is pinned RW as well.
 * A pin only covers what exists at boot (plus anything that appears below a
 * pinned directory, such as devices behind a USB root hub).
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <dirent.h>
#include <sys/statvfs.h>

#include "internal.h"

#ifndef ST_NODEV
#define ST_NODEV 4
#endif

#define HW_MAX_DEPTH 3
#define HW_MAX_PINS 256

/* Mirrored nodes carry the same unified group as the GPU ones, so a non-root
 * container user is added to one group for all of them. */
#define HW_NODE_GID DS_GPU_UNIFIED_GID

int ds_hw_toggles_active(const struct ds_config *cfg) {
  return !cfg->hw_access && (cfg->dev_input || cfg->dev_usb ||
                             cfg->dev_sensors || cfg->dev_block[0]);
}

int ds_hw_any_toggle(const struct ds_config *cfg) {
  return cfg->dev_input || cfg->dev_usb || cfg->dev_sensors ||
         cfg->dev_block[0] || cfg->hw_firmware;
}

/*
 * ds_dev_block_validate()
 *
 * A --dev-block list is comma separated absolute paths under /dev.  Anything
 * else is refused up front, since each entry becomes a raw disk node.
 */
int ds_dev_block_validate(const char *list) {
  if (!list || !list[0])
    return 0; /* empty clears the list */
  if (strlen(list) >= sizeof(((struct ds_config *)0)->dev_block))
    return -1;

  const char *p = list;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char tok[PATH_MAX];
    if (len <= 5 || len >= sizeof(tok))
      return -1;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (strncmp(tok, "/dev/", 5) != 0 || strstr(tok, "..") ||
        tok[len - 1] == '/' || strchr(tok, ' '))
      return -1;
    if (!comma)
      break;
    p = comma + 1;
  }
  return 0;
}

/* ---------------------------------------------------------------------- */
/* /dev                                                                   */
/* ---------------------------------------------------------------------- */

static void hw_make_parent(const char *tgt) {
  char parent[PATH_MAX];
  snprintf(parent, sizeof(parent), "%s", tgt);
  char *slash = strrchr(parent, '/');
  if (slash && slash != parent) {
    *slash = '\0';
    mkdir_p(parent, 0755);
  }
}

/* Copy one host node (char or block) into the isolated /dev as tgt. */
static int hw_mknod_copy(const char *tag, const char *host_path,
                         const char *tgt, mode_t type) {
  struct stat host_st;
  if (stat(host_path, &host_st) < 0) {
    /* An optional node (uinput, sensors) that this host lacks is not an
     * error; a block device the user asked for by name is. */
    if (type == S_IFBLK)
      ds_warn("[%s] %s: %s", tag, host_path, strerror(errno));
    return 0;
  }
  if ((host_st.st_mode & S_IFMT) != type) {
    if (type == S_IFBLK)
      ds_warn("[%s] %s is not a block device, skipping", tag, host_path);
    return 0;
  }

  struct stat t;
  if (lstat(tgt, &t) == 0)
    return 0; /* already there (listed twice, or a static + scan overlap) */

  hw_make_parent(tgt);
  if (mknod(tgt, type | 0660, host_st.st_rdev) < 0) {
    ds_warn("[%s] mknod %s (%d:%d) failed: %s", tag, tgt,
            (int)major(host_st.st_rdev), (int)minor(host_st.st_rdev),
            strerror(errno));
    return 0;
  }
  if (chown(tgt, 0, HW_NODE_GID) < 0)
    ds_warn("[%s] chown %s: %s", tag, tgt, strerror(errno));
  chmod(tgt, 0660);

  ds_log("[%s] Mirrored node: %-30s (%d:%d)", tag, tgt,
         (int)major(host_st.st_rdev), (int)minor(host_st.st_rdev));
  return 1;
}

static int hw_mirror_chr(const char *tag, const char *host_path,
                         const char *dev_dir) {
  if (strncmp(host_path, "/dev/", 5) != 0)
    return 0;
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/%s", dev_dir, host_path + 5);
  return hw_mknod_copy(tag, host_path, tgt, S_IFCHR);
}

/* Mirror every char node of host_dir whose name starts with prefix (NULL =
 * all), descending into subdirectories.  Symlinks are skipped. */
static int hw_mirror_tree(const char *tag, const char *host_dir,
                          const char *prefix, const char *dev_dir, int depth) {
  DIR *d = opendir(host_dir);
  if (!d)
    return 0;

  int count = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    if (prefix && strncmp(de->d_name, prefix, strlen(prefix)) != 0)
      continue;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", host_dir, de->d_name);

    struct stat st;
    if (lstat(path, &st) < 0)
      continue;
    if (S_ISDIR(st.st_mode)) {
      if (depth < HW_MAX_DEPTH)
        count += hw_mirror_tree(tag, path, NULL, dev_dir, depth + 1);
    } else if (S_ISCHR(st.st_mode)) {
      count += hw_mirror_chr(tag, path, dev_dir);
    }
  }
  closedir(d);
  return count;
}

/*
 * hw_expose_dir()
 *
 * Bind the host directory over the same path in the container /dev, so a
 * device plugged in later shows up too.  Nodes keep the host's owner and mode
 * (a chown here would change the host's own nodes).  Falls back to a one-shot
 * mknod copy when the host mount is nodev or the bind fails.
 */
static int hw_expose_dir(const char *tag, const char *host_dir,
                         const char *dev_dir) {
  struct stat st;
  if (stat(host_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
    ds_log("[%s] %s not present on this host, skipping", tag, host_dir);
    return 0;
  }

  struct statvfs vfs;
  int nodev = statvfs(host_dir, &vfs) == 0 && (vfs.f_flag & ST_NODEV);

  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/%s", dev_dir, host_dir + 5);

  if (!nodev) {
    mkdir_p(tgt, 0755);
    if (mount(host_dir, tgt, NULL, MS_BIND | MS_REC, NULL) == 0) {
      ds_log("[%s] Bound host directory (live): %s", tag, tgt);
      return 1;
    }
    ds_warn("[%s] bind %s: %s, falling back to a node copy", tag, host_dir,
            strerror(errno));
  } else {
    ds_log("[%s] %s sits on a nodev mount, copying nodes instead", tag,
           host_dir);
  }

  int n = hw_mirror_tree(tag, host_dir, NULL, dev_dir, 0);
  ds_log("[%s] Mirrored %d node(s) from %s (hotplug will not show up)", tag, n,
         host_dir);
  return n;
}

static void setup_dev_input(const char *dev_dir) {
  hw_expose_dir("INPUT", "/dev/input", dev_dir);
  hw_mirror_chr("INPUT", "/dev/uinput", dev_dir);
}

static void setup_dev_usb(const char *dev_dir) {
  hw_expose_dir("USB", "/dev/bus/usb", dev_dir);
  static const char *const prefixes[] = {"ttyUSB", "ttyACM", "hidraw", NULL};
  for (int i = 0; prefixes[i]; i++)
    hw_mirror_tree("USB", "/dev", prefixes[i], dev_dir, HW_MAX_DEPTH);
}

static void setup_dev_sensors(const char *dev_dir) {
  int n = hw_mirror_tree("SENSORS", "/dev", "iio:device", dev_dir, HW_MAX_DEPTH);
  n += hw_mirror_chr("SENSORS", "/dev/sensors", dev_dir);
  if (n == 0)
    ds_log("[SENSORS] No sensor nodes on the host /dev. Android sensors "
           "usually go through the HAL over binder, not /dev.");
}

static void setup_dev_block(const char *dev_dir, const char *list) {
  const char *p = list;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char host[PATH_MAX];
    if (len > 0 && len < sizeof(host)) {
      memcpy(host, p, len);
      host[len] = '\0';

      /* /dev/block/mmcblk1p1 lands as /dev/mmcblk1p1, the name a Linux
       * userland expects. */
      const char *base = strrchr(host, '/');
      base = base ? base + 1 : host;
      char tgt[PATH_MAX * 2];
      if (snprintf(tgt, sizeof(tgt), "%s/%s", dev_dir, base) <
          (int)sizeof(tgt))
        hw_mknod_copy("BLOCK", host, tgt, S_IFBLK);
    }
    if (!comma)
      break;
    p = comma + 1;
  }
}

/*
 * ds_hw_toggles_setup_dev()
 *
 * Called from boot/init.c right after setup_dev(), before pivot_root, while
 * the host's /dev is still reachable.  dev_dir is the container's isolated
 * tmpfs /dev.
 */
void ds_hw_toggles_setup_dev(const char *dev_dir, const struct ds_config *cfg) {
  if (!ds_hw_toggles_active(cfg))
    return;

  if (cfg->dev_input)
    setup_dev_input(dev_dir);
  if (cfg->dev_usb)
    setup_dev_usb(dev_dir);
  if (cfg->dev_sensors)
    setup_dev_sensors(dev_dir);
  if (cfg->dev_block[0])
    setup_dev_block(dev_dir, cfg->dev_block);
}

/* ---------------------------------------------------------------------- */
/* /sys                                                                   */
/* ---------------------------------------------------------------------- */

static char g_sysroot[PATH_MAX];
static char g_pinned[HW_MAX_PINS][PATH_MAX];
static int g_npinned;

/* Pin an already resolved path of the container's sysfs as its own RW mount. */
static void sys_pin_abs(const char *tag, const char *rp) {
  size_t n = strlen(g_sysroot);
  if (strncmp(rp, g_sysroot, n) != 0 || rp[n] != '/')
    return; /* resolved outside the container's sysfs */

  for (int i = 0; i < g_npinned; i++)
    if (strcmp(g_pinned[i], rp) == 0)
      return;
  if (g_npinned >= HW_MAX_PINS) {
    ds_warn("[%s] too many /sys pins, skipping %s", tag, rp + n);
    return;
  }

  if (mount(rp, rp, NULL, MS_BIND | MS_REC, NULL) < 0) {
    ds_warn("[%s] pin %s: %s", tag, rp + n, strerror(errno));
    return;
  }
  safe_strncpy(g_pinned[g_npinned++], rp, PATH_MAX);
  ds_log("[%s] /sys writable: %s", tag, rp + n);
}

static void sys_pin_rel(const char *tag, const char *rel) {
  char p[PATH_MAX], rp[PATH_MAX];
  snprintf(p, sizeof(p), "sys/%s", rel);
  if (realpath(p, rp))
    sys_pin_abs(tag, rp);
}

/* Pin the real directory behind every entry of sys/<rel_dir> whose name starts
 * with prefix (NULL = all). */
static void sys_pin_targets(const char *tag, const char *rel_dir,
                            const char *prefix) {
  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "sys/%s", rel_dir);
  DIR *d = opendir(dir);
  if (!d)
    return;

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    if (prefix && strncmp(de->d_name, prefix, strlen(prefix)) != 0)
      continue;
    char p[PATH_MAX * 2], rp[PATH_MAX];
    if (snprintf(p, sizeof(p), "%s/%s", dir, de->d_name) >= (int)sizeof(p))
      continue;
    if (realpath(p, rp))
      sys_pin_abs(tag, rp);
  }
  closedir(d);
}

static void sys_pin_block(const char *list) {
  const char *p = list;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char host[PATH_MAX];
    if (len > 0 && len < sizeof(host)) {
      memcpy(host, p, len);
      host[len] = '\0';

      struct stat st;
      if (stat(host, &st) == 0 && S_ISBLK(st.st_mode)) {
        char link[64], rp[PATH_MAX], part[PATH_MAX + 16];
        snprintf(link, sizeof(link), "sys/dev/block/%u:%u",
                 (unsigned)major(st.st_rdev), (unsigned)minor(st.st_rdev));
        if (realpath(link, rp)) {
          /* A partition: the queue/ and power/ knobs live on the disk. */
          snprintf(part, sizeof(part), "%s/partition", rp);
          if (access(part, F_OK) == 0) {
            char parent[PATH_MAX];
            safe_strncpy(parent, rp, sizeof(parent));
            char *slash = strrchr(parent, '/');
            if (slash) {
              *slash = '\0';
              sys_pin_abs("BLOCK", parent);
            }
          }
          sys_pin_abs("BLOCK", rp);
        }
      }
    }
    if (!comma)
      break;
    p = comma + 1;
  }
}

/*
 * ds_hw_toggles_setup_sys()
 *
 * Called from boot/init.c while the container's sysfs is still writable and
 * before it is remounted read-only.  Pins survive that remount, the same way
 * the net pin in init.c does.  Relative paths: cwd is the new rootfs.
 */
void ds_hw_toggles_setup_sys(const struct ds_config *cfg) {
  if (!ds_hw_toggles_active(cfg))
    return;
  if (!realpath("sys", g_sysroot))
    return;
  g_npinned = 0;

  if (cfg->dev_input) {
    sys_pin_rel("INPUT", "class/input");
    sys_pin_targets("INPUT", "class/input", NULL);
  }
  if (cfg->dev_usb) {
    sys_pin_rel("USB", "bus/usb");
    /* Root hubs: everything plugged in below them stays writable too. */
    sys_pin_targets("USB", "bus/usb/devices", "usb");
  }
  if (cfg->dev_sensors) {
    sys_pin_rel("SENSORS", "bus/iio");
    sys_pin_targets("SENSORS", "bus/iio/devices", NULL);
  }
  if (cfg->dev_block[0])
    sys_pin_block(cfg->dev_block);
}
