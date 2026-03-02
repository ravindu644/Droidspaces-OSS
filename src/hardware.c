/*
 * Droidspaces — Hardware Access Module
 *
 * This module manages GPU acceleration and hardware device nodes. To keep your
 * system stable, we exclusively use "render nodes" (/dev/dri/renderD*) for GPU
 * access.
 *
 * Why? Because render nodes allow multiple processes (like Droidspaces and your
 * host's X11/Wayland) to share the GPU safely. "Card nodes" (/dev/dri/card*)
 * are avoided because they require exclusive control (DRM master), and trying
 * to share them often leads to driver hangs or kernel panics on desktop Linux.
 *
 * This approach gives you full OpenGL, Vulkan, and video acceleration while
 * ensuring your host system stays rock solid. It's the same industry standard
 * used by major container projects like Docker and Podman.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <dirent.h>

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

/*
 * is_dangerous_node()
 *
 * Checks if a device node name is "dangerous" (part of the host display stack
 * or a privileged DRM master node) and should be blocked from container access.
 */
int is_dangerous_node(const char *name) {
  /* Tier 1: DRM card nodes and control nodes */
  if ((strncmp(name, "card", 4) == 0 &&
       (name[4] == '\0' || isdigit(name[4]))) ||
      (strncmp(name, "controlD", 8) == 0 &&
       (name[8] == '\0' || isdigit(name[8])))) {
    return 1;
  }

  /* Tier 2: NVIDIA Proprietary Master & Modeset Nodes */
  if (strcmp(name, "nvidiactl") == 0 || strcmp(name, "nvidia-modeset") == 0)
    return 1;
  /* Block raw GPU nodes /dev/nvidia0, nvidia1, etc. */
  if (strncmp(name, "nvidia", 6) == 0 && isdigit(name[6]))
    return 1;
  /* Block NVIDIA capability nodes */
  if (strncmp(name, "nvidia-cap", 10) == 0)
    return 1;

  /* Tier 3 & 4: VGA Arbiter and Framebuffers */
  if (strcmp(name, "vga_arbiter") == 0)
    return 1;
  if (strncmp(name, "fb", 2) == 0 && isdigit(name[2]))
    return 1;

  /* Tier 5: Host TTY nodes
   *
   * SAFE (pass through - legitimate dev/embedded devices):
   *   ttyUSB*  USB-to-serial adapters (FTDI, CH340, CP2102, PL2303)
   *   ttyACM*  USB CDC ACM (Arduino, ESP32-C3/S2, Pi Pico, STM32, Heimdall)
   *   ttyAMA*  ARM AMBA UART (Raspberry Pi GPIO serial, ARM SoC hardware UART)
   *   ttyTHS*  NVIDIA Tegra high-speed UART (Jetson boards)
   *   ttymxc*  NXP i.MX UART (embedded SBCs)
   *
   * DANGEROUS (block - host console/modem paths):
   *   ttyN     VT masters: DRM Master handover risk on VT switch
   *   ttyS*    x86 hardware serial console (COM1/COM2)
   *   ttyGS*   USB gadget serial (host-side gadget controller)
   *   ttyHSL*  Qualcomm high-speed UART (modem console)
   *   ttyMSM*  Qualcomm MSM serial console
   *
   * Unknown tty* nodes fall through as safe (dev-friendly default). */
  if (strncmp(name, "tty", 3) == 0) {
    /* Safe: check before any block rule */
    if (strncmp(name, "ttyUSB", 6) == 0 || strncmp(name, "ttyACM", 6) == 0 ||
        strncmp(name, "ttyAMA", 6) == 0 || strncmp(name, "ttyTHS", 6) == 0 ||
        strncmp(name, "ttymxc", 6) == 0)
      return 0;
    /* Dangerous: VT masters */
    if (isdigit(name[3]))
      return 1;
    /* Dangerous: explicit host console and modem prefixes */
    if (strncmp(name, "ttyS", 4) == 0 ||   /* x86 hardware serial console */
        strncmp(name, "ttyGS", 5) == 0 ||  /* USB gadget serial            */
        strncmp(name, "ttyHSL", 6) == 0 || /* Qualcomm HS-UART             */
        strncmp(name, "ttyMSM", 6) == 0)   /* Qualcomm MSM console         */
      return 1;
    /* Unknown tty* falls through - safe default for future devices */
  }

  /* Tier 6: MediaTek Modem & Legacy BSD PTYs */
  if (strncmp(name, "ccci", 4) == 0 || strncmp(name, "umts_", 5) == 0)
    return 1;
  if (strncmp(name, "pty", 3) == 0) /* BSD PTY masters */
    return 1;

  /* Tier 7: Input Injection & RF Kill */
  if (strcmp(name, "uinput") == 0 || strcmp(name, "rfkill") == 0)
    return 1;

  /* Tier 8: TEE, Connectivity & Power Management (Android/MTK) */
  if (strncmp(name, "tz", 2) == 0 || strncmp(name, "trusty", 6) == 0 ||
      strncmp(name, "gz_", 3) == 0 || strncmp(name, "tee", 3) == 0)
    return 1; /* TrustZone / TEE / Secure OS */
  if (strncmp(name, "conn", 4) == 0 || strcmp(name, "mtk_sec") == 0)
    return 1; /* MediaTek Connectivity & Security */
  if (strncasecmp(name, "mt_pmic", 7) == 0)
    return 1; /* Power Management IC */
  if (strcmp(name, "tuihw") == 0 || strcmp(name, "wlan") == 0)
    return 1;

  /* Tier 9: Legacy Clutter */
  if (strncmp(name, "ram", 3) == 0 && isdigit(name[3]))
    return 1; /* Legacy RAM disks */

  /* Tier 10: Core virtualized nodes (should be unlinked and recreated) */
  if (strcmp(name, "console") == 0 || strcmp(name, "tty") == 0 ||
      strcmp(name, "full") == 0 || strcmp(name, "null") == 0 ||
      strcmp(name, "zero") == 0 || strcmp(name, "random") == 0 ||
      strcmp(name, "urandom") == 0 || strcmp(name, "ptmx") == 0 ||
      strcmp(name, "initctl") == 0)
    return 1;

  /* Systemic Hardening (Phase 12) */
  /* Tier 10: Direct Host Access */
  if (strcmp(name, "mem") == 0 || strcmp(name, "kmem") == 0 ||
      strcmp(name, "port") == 0)
    return 1;
  /* Tier 11: DisplayPort Aux */
  if (strncmp(name, "drm_dp_aux", 10) == 0)
    return 1;
  /* Tier 12: Virtual Consoles */
  if (strncmp(name, "vcs", 3) == 0)
    return 1;
  /* Tier 13: Watchdogs */
  if (strncmp(name, "watchdog", 8) == 0)
    return 1;
  /* Tier 14: DMA/Memory Gaps */
  if (strcmp(name, "udmabuf") == 0 || strcmp(name, "snapshot") == 0)
    return 1;
  /* Tier 15: TPM (KVM preserved for VMs) */
  if (strncmp(name, "tpm", 3) == 0)
    return 1;
  /* Tier 16: MTK STP Combo Chip Bus (BT/GPS/WiFi transport) */
  if (strncmp(name, "stp", 3) == 0)
    return 1;

  /* Tier 17: MTK Audio IPI / SCP IPC - known exploitable attack surface */
  if (strcmp(name, "audio_ipi") == 0 || strcmp(name, "scp_audio_ipi") == 0 ||
      strcmp(name, "vow") == 0 || strcmp(name, "vcp") == 0)
    return 1;

  /* Tier 18: eMMC Replay-Protected Memory Block - stores DRM/boot keys */
  if (strncmp(name, "rpmb", 4) == 0)
    return 1;

  /* Tier 19: MTK Multimedia Profiler + Event Tracer (CMDQ-class IOCTL risk) */
  if (strcmp(name, "mmp") == 0 || strcmp(name, "met") == 0)
    return 1;

  /* Tier 20: MTK Co-Processor Firmware IPC Channels */
  if (strcmp(name, "mcupm") == 0 || strcmp(name, "sspm") == 0 ||
      strcmp(name, "scp") == 0)
    return 1;

  /* Tier 21: MTK AED kernel exception daemon nodes */
  if (strncmp(name, "aed", 3) == 0 && (name[3] == '\0' || isdigit(name[3])))
    return 1;

  /* Tier 22: Persistent RAM log writer (survives reboots, destroys host
   * diagnostics) */
  if (strncmp(name, "pmsg", 4) == 0)
    return 1;

  /* Tier 23: MTK Display Pipeline Sync (display-critical fence driver) */
  if (strcmp(name, "mdp_sync") == 0 || strcmp(name, "fmt_sync") == 0 ||
      strcmp(name, "mtk_mdp") == 0 || strcmp(name, "mml_pq") == 0 ||
      strcmp(name, "sec_display_debug") == 0)
    return 1;

  /* Tier 24: GPS co-processor shared memory + power control */
  if (strcmp(name, "gps_emi") == 0 || strcmp(name, "gps_pwr") == 0)
    return 1;

  /* Tier 25: Secure elements, biometrics, DRM key nodes */
  if (strcmp(name, "goodix_fp") == 0 || strcmp(name, "k250a") == 0 ||
      strcmp(name, "drm_wv") == 0 || strcmp(name, "sec-nfc") == 0)
    return 1;

  /* Tier 26: MTK debug/tracing nodes */
  if (strcmp(name, "eara-io") == 0 || strcmp(name, "RT_Monitor") == 0)
    return 1;
  if (strncmp(name, "wmt", 3) == 0) /* wmtdetect, wmtWifi, wmtNfc, etc. */
    return 1;

  /* Tier 27: MTK firmware log exporters */
  if (strncmp(name, "fw_log_", 7) == 0 || strcmp(name, "sa_log_wifi") == 0)
    return 1;

  /* Tier 28: MTK Network Offload & USB IP Accelerators
   * sipa_*: bypasses netfilter at hardware offload layer.
   * mddp: MTK Distributed Data Path offload control. */
  if (strncmp(name, "sipa_", 5) == 0 || strcmp(name, "mddp") == 0 ||
      strcmp(name, "usip") == 0)
    return 1;

  return 0;
}

/*
 * add_gpu_gid()
 *
 * Helper to stat a device and add its group ID if it's unique.
 */
static void add_gpu_gid(const char *path, gid_t *gids, int *count,
                        int max_gids) {
  /* Defense in Depth: Always check against the dangerous node blocklist */
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  if (is_dangerous_node(name)) {
    return;
  }

  struct stat st;
  if (stat(path, &st) < 0)
    return;

  gid_t gid = st.st_gid;
  if (gid == 0)
    return;

  for (int i = 0; i < *count; i++) {
    if (gids[i] == gid)
      return;
  }

  if (*count < max_gids) {
    gids[(*count)++] = gid;
    ds_log("GPU device %-30s → GID %d", path, (int)gid);
  }
}

/*
 * scan_gpu_dir()
 *
 * Dynamically scan a directory for device nodes matching a prefix.
 */
static void scan_gpu_dir(const char *dir_path, const char *prefix, gid_t *gids,
                         int *count, int max_gids) {
  DIR *dir = opendir(dir_path);
  if (!dir)
    return;

  struct dirent *entry;
  char full_path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    /* Skip . and .. */
    if (entry->d_name[0] == '.')
      continue;

    /* Restricted to character devices only */
    if (entry->d_type != DT_CHR && entry->d_type != DT_UNKNOWN)
      continue;

    if (prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;

    /* Defense in Depth: Skip dangerous nodes during GID scanning */
    if (is_dangerous_node(entry->d_name)) {
      continue;
    }

    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
    add_gpu_gid(full_path, gids, count, max_gids);
  }

  closedir(dir);
}

/*
 * scan_host_gpu_gids()
 *
 * Scan known GPU device paths on the HOST and collect unique non-root GIDs.
 * Uses dynamic discovery where possible to avoid hardcoded path fragility.
 * Must be called BEFORE pivot_root while /dev still refers to the host.
 *
 * Returns: number of unique GIDs found (0 = no GPU devices)
 */
int scan_host_gpu_gids(gid_t *gids, int max_gids) {
  int count = 0;

  /* 1. Dynamic Scanning (Render Nodes, NVIDIA, V4L2, etc.) */
  scan_gpu_dir("/dev/dri", "renderD", gids, &count, max_gids);
  scan_gpu_dir("/dev", "nvidia", gids, &count, max_gids);
  scan_gpu_dir("/dev", "video", gids, &count, max_gids);
  scan_gpu_dir("/dev/nvidia-caps", NULL, gids, &count, max_gids);
  scan_gpu_dir("/dev", "mali", gids, &count, max_gids);
  scan_gpu_dir("/dev", "kgsl", gids, &count, max_gids);
  scan_gpu_dir("/dev/dma_heap", NULL, gids, &count, max_gids);

  /* 2. Static Comprehensive List (IPC, Compute, Tegra/PVR specific) */
  const char *static_devices[] = {
      /* Android IPC (Critical for Android containers/hosts) */
      "/dev/binder", "/dev/vndbinder", "/dev/hwbinder",

      /* Legacy Android Memory Allocators */
      "/dev/ion", "/dev/ashmem",

      /* ARM Mali / Adreno aliases */
      "/dev/mali", "/dev/genlock",

      /* AMD ROCm Compute */
      "/dev/kfd",

      /* PowerVR (IMG GPU) */
      "/dev/pvrsrvkm", "/dev/pvr_sync",

      /* Tegra (Comprehensive stack) */
      "/dev/nvhost-ctrl", "/dev/nvhost-gpu", "/dev/nvhost-ctrl-gpu",
      "/dev/nvhost-as-gpu", "/dev/nvhost-dbg-gpu", "/dev/nvhost-prof-gpu",
      "/dev/nvhost-tsg", "/dev/nvhost-tsg-gpu", "/dev/nvhost-vic",
      "/dev/nvhost-nvdec", "/dev/nvhost-nvdec1", "/dev/nvhost-nvenc",
      "/dev/nvhost-msenc", "/dev/nvmap",

      /* WSL2 (DirectX Proxy) */
      "/dev/dxg",

      /* Async Sync */
      "/dev/sw_sync",

      NULL};

  for (int i = 0; static_devices[i] != NULL; i++) {
    add_gpu_gid(static_devices[i], gids, &count, max_gids);
  }

  if (count > 0) {
    ds_log("Discovered %d unique GPU/Hardware group(s)", count);
  }

  return count;
}

/*
 * setup_gpu_groups()
 *
 * After pivot_root, create matching groups inside the container's /etc/group
 * and add root to each. Groups are named "gpu_<gid>" to avoid conflicts
 * with existing groups.
 *
 * Idempotent: safe to call on container restart (skips existing groups).
 */

/* Helper to check if a username exists in a comma-separated list of users */
static int has_user(const char *users, const char *username) {
  if (!users || !username)
    return 0;

  size_t len = strlen(username);
  const char *p = users;

  while ((p = strstr(p, username)) != NULL) {
    /* Check if it's a whole word match */
    int at_start = (p == users);
    int prev_comma = (p > users && *(p - 1) == ',');
    int next_comma = (*(p + len) == ',' || *(p + len) == '\0');

    if ((at_start || prev_comma) && next_comma) {
      return 1;
    }
    p++;
  }
  return 0;
}

int setup_gpu_groups(gid_t *gpu_gids, int gid_count) {
  if (gid_count <= 0)
    return 0;

  /* Check if /etc/group exists — some minimal rootfs may not have it */
  if (access("/etc/group", F_OK) != 0) {
    ds_warn("No /etc/group found, skipping GPU group setup");
    return 0;
  }

  /* We'll rewrite the group file to a temporary location */
  const char *group_path = "/etc/group";
  const char *tmp_path = "/etc/group.tmp";

  FILE *fin = fopen(group_path, "re");
  if (!fin) {
    ds_warn("Cannot read /etc/group: %s", strerror(errno));
    return -1;
  }

  FILE *fout = fopen(tmp_path, "we");
  if (!fout) {
    ds_warn("Cannot create /etc/group.tmp: %s", strerror(errno));
    fclose(fin);
    return -1;
  }

  /* Track which GIDs we found in the file */
  int *found_gids = calloc((size_t)gid_count, sizeof(int));
  if (!found_gids) {
    ds_warn("Memory allocation failed for GPU group tracking");
    fclose(fin);
    fclose(fout);
    return -1;
  }

  char line[2048];
  int modified_count = 0;

  while (fgets(line, sizeof(line), fin)) {
    /* Format: name:password:GID:user_list
     * We need to find the GID (3rd field) */
    char name[256];
    int gid_val;
    char *users_ptr = NULL;

    /* Manual parsing to be safe and extract user list pointer */
    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));

    /* Remove trailing newline for parsing */
    char *nl = strrchr(line_copy, '\n');
    if (nl)
      *nl = '\0';

    int colons = 0;
    char *p = line_copy;
    char *gid_str = NULL;

    while (*p) {
      if (*p == ':') {
        colons++;
        if (colons == 2)
          gid_str = p + 1;
        else if (colons == 3) {
          *p = '\0';
          users_ptr = p + 1;
          break;
        }
      }
      p++;
    }

    if (gid_str && users_ptr) {
      gid_val = atoi(gid_str);

      /* Check if this GID is one of our target GPU GIDs */
      int gpu_idx = -1;
      for (int i = 0; i < gid_count; i++) {
        if (gpu_gids[i] == (gid_t)gid_val) {
          gpu_idx = i;
          break;
        }
      }

      if (gpu_idx != -1) {
        found_gids[gpu_idx] = 1;

        /* Check if 'root' is already in the user list */
        if (!has_user(users_ptr, "root")) {
          /* Extract group name for logging */
          p = line_copy;
          int i = 0;
          while (*p && *p != ':' && i < 255)
            name[i++] = *p++;
          name[i] = '\0';

          /* Add root to the members list */
          if (strlen(users_ptr) > 0)
            fprintf(fout, "%.*s:%s,root\n", (int)(users_ptr - line_copy - 1),
                    line_copy, users_ptr);
          else
            fprintf(fout, "%.*s:root\n", (int)(users_ptr - line_copy - 1),
                    line_copy);

          ds_log("Added root to existing group '%s' (GID %d)", name, gid_val);
          modified_count++;
          continue; /* Skip the default fputs below */
        }
      }
    }

    /* Print original line if not modified */
    fputs(line, fout);
  }

  /* Append absolutely missing GPU groups */
  for (int i = 0; i < gid_count; i++) {
    if (!found_gids[i]) {
      fprintf(fout, "gpu_%d:x:%d:root\n", (int)gpu_gids[i], (int)gpu_gids[i]);
      ds_log("Created new GPU group gpu_%d (GID %d)", (int)gpu_gids[i],
             (int)gpu_gids[i]);
      modified_count++;
    }
  }

  fclose(fin);
  fclose(fout);
  free(found_gids);

  /* Atomic replacement */
  if (modified_count > 0) {
    if (rename(tmp_path, group_path) < 0) {
      ds_warn("Failed to update /etc/group: %s", strerror(errno));
      unlink(tmp_path);
      return -1;
    }
    ds_log("Finalized GPU group membership (Updated %d entry/entries)",
           modified_count);
  } else {
    unlink(tmp_path);
  }

  return 0;
}

/*
 * setup_x11_socket()
 *
 * Bind mount X11 socket directory for GUI application support.
 * Supports both desktop Linux and Termux X11 (Android).
 *
 * Only the .X11-unix subdirectory is mounted — never the entire /tmp.
 * Binding /tmp causes "required key not available" errors on encrypted
 * Android devices due to FBE keyring conflicts.
 *
 * Non-fatal: silently returns 0 if no X11 socket is found.
 *
 */

/*
 * stop_termux_if_running()
 *
 * Checks if Termux is running and aggressively stops it to prevent
 * blocking during tmpfs mount.
 */
void stop_termux_if_running(void) {
  struct stat st;
  if (stat("/data/data/com.termux", &st) != 0) {
    return; /* Termux not installed */
  }

  /* Check if Termux is actually running */
  char *pidof_argv[] = {"pidof", "com.termux", NULL};
  if (run_command_quiet(pidof_argv) != 0) {
    return; /* Not running, nothing to do */
  }

  ds_log("Stopping Termux to prepare unified /tmp...");

  /* Method 1: Use Android Activity Manager to stop app */
  char *am_argv[] = {"am", "force-stop", "com.termux", NULL};
  int ret = run_command_quiet(am_argv);

  /* Method 2: Fallback to pkill if am fails */
  if (ret != 0) {
    char *pkill_argv[] = {"pkill", "-9", "com.termux", NULL};
    run_command_quiet(pkill_argv);
  }

  /* Give it a moment to die */
  nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 500000000}, NULL);
}

/*
 * setup_unified_tmpfs()
 *
 */
int setup_unified_tmpfs(void) {
  const char *termux_tmp = DS_TERMUX_TMP_DIR;
  struct stat st;
  struct statfs fs;

  /* Check if Termux exists */
  if (stat("/data/data/com.termux", &st) != 0) {
    return 0; /* Non-fatal: Termux not installed */
  }

  /* Ensure tmp directory exists */
  mkdir_p(termux_tmp, 0755);

  /* Already mounted? Just ensure ownership is correct. */
  if (statfs(termux_tmp, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    if (chown(termux_tmp, st.st_uid, st.st_gid) < 0) {
      ds_warn("Failed to chown %s: %s", termux_tmp, strerror(errno));
    }
    chmod(termux_tmp, 01777);
    return 0;
  }

  /* Detect Termux SELinux context (including categories) */
  char context[256] = {0};
  if (get_selinux_context("/data/data/com.termux", context, sizeof(context)) <
      0) {
    safe_strncpy(context, "u:object_r:app_data_file:s0", sizeof(context));
  }

  /* Mount tmpfs with proper permissions and ownership */
  char mount_opts[256];
  snprintf(mount_opts, sizeof(mount_opts), "size=256M,mode=1777,uid=%d,gid=%d",
           (int)st.st_uid, (int)st.st_gid);

  if (mount("tmpfs", termux_tmp, "tmpfs", MS_NOSUID | MS_NODEV, mount_opts) !=
      0) {
    ds_warn("Failed to create unified /tmp: %s", strerror(errno));
    return -1;
  }

  /* Explicitly apply SELinux context to the mount point */
  if (set_selinux_context(termux_tmp, context) < 0) {
    ds_warn("Failed to apply SELinux context to unified /tmp: %s",
            strerror(errno));
  }

  return 0;
}

/*
 * cleanup_unified_tmpfs()
 *
 */
void cleanup_unified_tmpfs(void) {
  struct statfs fs;

  /* Only unmount if a tmpfs is actually present at the target path */
  if (statfs(DS_TERMUX_TMP_DIR, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    umount2(DS_TERMUX_TMP_DIR, MNT_DETACH);
  }
}

/*
 * setup_x11_and_virgl_sockets()
 *
 */
int setup_x11_and_virgl_sockets(struct ds_config *cfg) {
  (void)cfg;

  if (!is_android()) {
    /* Desktop Linux path */
    const char *x11_source = DS_X11_PATH_DESKTOP;
    if (access(x11_source, F_OK) == 0) {
      mkdir_p("/tmp", 01777);
      mkdir_p(DS_X11_CONTAINER_DIR, 01777);
      if (mount(x11_source, DS_X11_CONTAINER_DIR, NULL, MS_BIND | MS_REC,
                NULL) < 0) {
        ds_warn("Failed to bind mount X11 socket: %s", strerror(errno));
        return -1;
      }
      ds_log("Bridged host's X11 unix socket with container");
    } else {
      ds_warn("X11 support skipped: No host X11 socket detected");
    }
    return 0;
  }

  /* Android path: bridge Termux /tmp into container's /tmp */
  const char *bridge_source =
      DS_TERMUX_TMP_OLDROOT; /* FIX: Use explicit macro */
  const char *container_tmp = "/tmp";

  /* Verify source exists */
  if (access(bridge_source, F_OK) != 0) {
    ds_warn("Termux not installed - X11/VirGL socket bridge unavailable");
    return 0; /* Non-fatal */
  }

  ds_log("Bridging Termux and container for X11/VirGL sockets...");

  /* Ensure container /tmp exists and is clean */
  mkdir_p(container_tmp, 01777);

  /* Bind mount entire Termux high-speed tmpfs to container /tmp.
   * This ensures /tmp IS a mountpoint and unified with Termux. */
  if (mount(bridge_source, container_tmp, NULL, MS_BIND, NULL) != 0) {
    ds_warn("Failed to bridge /tmp socket layer: %s", strerror(errno));
  }

  /* Ensure permissions are correct */
  chmod(container_tmp, 01777);

  return 0;
}

/*
 * setup_hardware_access()
 *
 * Top-level entry point called from boot.c AFTER pivot_root.
 * Orchestrates GPU group creation and X11 socket mounting.
 *
 * All operations are non-fatal: failures produce warnings but don't
 * prevent the container from booting.
 *
 */
int setup_hardware_access(struct ds_config *cfg, gid_t *gpu_gids,
                          int gid_count) {
  if (!cfg->hw_access && !cfg->termux_x11)
    return 0;

  /* 1. Create GPU groups inside the container */
  if (cfg->hw_access)
    setup_gpu_groups(gpu_gids, gid_count);

  /* 2. Mount X11 socket for GUI applications */
  if (cfg->hw_access || cfg->termux_x11)
    setup_x11_and_virgl_sockets(cfg);

  return 0;
}
