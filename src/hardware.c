/*
 * Droidspaces v6 - Hardware Access Module
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
 * Unified GPU group bridge.
 *
 * Every hardware node mirrored into the container is chowned to this group.
 * root is added to it in setup_gpu_groups() so GPU access works regardless
 * of which host GID the node originally carried (GID 44 for video, GID 1006
 * for render, etc.). Users can be added manually with usermod.
 *
 * GID 786 is well outside the standard Debian/Ubuntu range (0-999 system,
 * 1000+ users) and has never been assigned to a named group in the FHS.
 */
#define DS_GPU_GROUP_NAME "droidspaces-gpu"
#define DS_GPU_UNIFIED_GID 786

/*
 * Shared GPU/hardware device lists.
 *
 * Both scan_host_gpu_gids() and mirror_gpu_nodes() iterate these tables.
 * Add new devices here once; both functions pick them up automatically.
 */

/* Dynamic directories: { host_dir, prefix_or_NULL } */
static const struct {
  const char *dir;
  const char *prefix;
} gpu_scan_dirs[] = {
    {"/dev/dri", "renderD"},    {"/dev", "nvidia"}, {"/dev", "video"},
    {"/dev/nvidia-caps", NULL}, {"/dev", "mali"},   {"/dev", "kgsl"},
    {"/dev/dma_heap", NULL},    {NULL, NULL}, /* sentinel */
};

/* Static paths: individual nodes that don't fit a directory scan */
static const char *gpu_static_devices[] = {
    /* Android IPC (Critical for Android containers/hosts) */
    "/dev/binder",
    "/dev/vndbinder",
    "/dev/hwbinder",

    /* Legacy Android Memory Allocators */
    "/dev/ion",
    "/dev/ashmem",

    /* ARM Mali / Adreno aliases */
    "/dev/mali",
    "/dev/genlock",

    /* AMD ROCm Compute */
    "/dev/kfd",

    /* PowerVR */
    "/dev/pvrsrvkm",
    "/dev/pvr_sync",

    /* Tegra */
    "/dev/nvhost-ctrl",
    "/dev/nvhost-gpu",
    "/dev/nvhost-ctrl-gpu",
    "/dev/nvhost-as-gpu",
    "/dev/nvhost-dbg-gpu",
    "/dev/nvhost-prof-gpu",
    "/dev/nvhost-tsg",
    "/dev/nvhost-tsg-gpu",
    "/dev/nvhost-vic",
    "/dev/nvhost-nvdec",
    "/dev/nvhost-nvdec1",
    "/dev/nvhost-nvenc",
    "/dev/nvhost-msenc",
    "/dev/nvmap",

    /* WSL2 */
    "/dev/dxg",

    /* Async Sync */
    "/dev/sw_sync",

    NULL, /* sentinel */
};

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
    /* Everything else is dangerous.
     * Android kernels register hundreds of tty* nodes for virtual UARTs,
     * modem channels (ttyCMIPC*), AT command interfaces (ttyC_AT), and
     * arbitrary vendor UART drivers (ttya*..ttyz*, ttyC*, ttyb*, etc.).
     * The old "unknown = safe" default was the bug - on Android the correct
     * default is BLOCKED.  Explicit safe entries above still pass through. */
    return 1;
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
  if (strstr(name, "watchdog") != NULL)
    return 1;

  /* Tier 13.5: Qualcomm RPC & Secure Interfaces */
  if (strstr(name, "qseecom") != NULL || strstr(name, "smcinvoke") != NULL ||
      strstr(name, "adsprpc") != NULL)
    return 1;

  /* Tier 14: DMA/Memory Gaps */
  if (strcmp(name, "udmabuf") == 0 || strcmp(name, "snapshot") == 0)
    return 1;
  /* Tier 15: TPM */
  if (strncmp(name, "tpm", 3) == 0)
    return 1;
  /* Tier 16: MTK STP Combo Chip Bus (BT/GPS/WiFi transport) */
  if (strncmp(name, "stp", 3) == 0)
    return 1;

  /* Tier 16.5: Qualcomm / Modem Connectivity Loopholes */
  if (strncmp(name, "rmnet_", 6) == 0 || strncmp(name, "ipa", 3) == 0 ||
      strncmp(name, "at_usb", 6) == 0 || strncmp(name, "at_mdm", 6) == 0 ||
      strncmp(name, "wwan_", 5) == 0 || strncmp(name, "btfmslim", 8) == 0 ||
      strncmp(name, "btpower", 7) == 0 || strncmp(name, "smd", 3) == 0 ||
      strncmp(name, "apr_", 4) == 0 || strstr(name, "aud_") != NULL ||
      strstr(name, "icnss_") != NULL)
    return 1;

  /* Tier 16.6: Hypervisor Consoles & Virtio Loopbacks */
  if (strncmp(name, "hvc", 3) == 0 || strncmp(name, "gh_", 3) == 0)
    return 1;

  /* Tier 17: MTK Audio IPI / SCP IPC - known exploitable attack surface */
  if (strcmp(name, "audio_ipi") == 0 || strcmp(name, "scp_audio_ipi") == 0 ||
      strcmp(name, "vow") == 0 || strcmp(name, "vcp") == 0)
    return 1;

  /* Tier 17.5: Qualcomm SoC Tracing & DSP Debug */
  if (strncmp(name, "coresight", 9) == 0 ||
      strncmp(name, "remoteproc", 10) == 0 || strncmp(name, "rpmsg_", 6) == 0 ||
      strcmp(name, "cvp") == 0 || strncmp(name, "rdbg_", 5) == 0 ||
      strcmp(name, "dcc_sram") == 0 || strcmp(name, "spec_sync") == 0 ||
      strcmp(name, "synx_device") == 0)
    return 1;

  /* Tier 17.6: Android-Specific Compatibility Nodes (Anbox, etc.) */
  if (strncmp(name, "anbox-", 6) == 0 || strcmp(name, "android_ssusbcon") == 0)
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

  /* Tier 26: MTK debug/tracing nodes and QCOM/Other misc */
  if (strcmp(name, "eara-io") == 0 || strcmp(name, "RT_Monitor") == 0 ||
      strcmp(name, "stats") == 0)
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

  /* Tier 29: Direct Bus Access (Exynos/Samsung)
   * gpiochip*: Raw GPIO control of motherboard pins.
   * i2c-*: Raw I2C bus access to CMOS sensors, power chips, and touchscreens.
   * iio:device*: Industrial I/O for raw ADC/Sensor data. */
  if (strncmp(name, "gpiochip", 8) == 0 || strncmp(name, "i2c-", 4) == 0 ||
      strncmp(name, "iio:device", 10) == 0)
    return 1;

  /* Tier 30: Performance & Clock Scaling
   * Cluster/GPU/Memory frequency overrides allow host sabotage. */
  if (strncmp(name, "cluster", 7) == 0 || strncmp(name, "gpu_freq", 8) == 0 ||
      strncmp(name, "cpu_online_", 11) == 0 ||
      strcmp(name, "memory_bandwidth") == 0 ||
      strstr(name, "msm_audio_ion") != NULL ||
      strstr(name, "msm_hdcp") != NULL || strstr(name, "msm_sps") != NULL)
    return 1;

  /* Tier 31: Exynos Modem & Multi-PDP
   * NR (5G) and Multi-PDP packet bridges for Samsung modems. */
  if (strncmp(name, "nr_", 3) == 0 || strncmp(name, "multipdp", 8) == 0 ||
      strncmp(name, "modem_boot", 10) == 0 || strcmp(name, "radio0") == 0)
    return 1;

  /* Tier 32: Sensor Hub & DSPs
   * BBD: Big Brother Daemon (Exynos sensor hub).
   * SSP: Samsung Sensor Processor. */
  if (strncmp(name, "bbd_", 4) == 0 || strncmp(name, "ssp_", 4) == 0 ||
      strcmp(name, "ssp_sensorhub") == 0)
    return 1;

  /* Tier 33: Samsung Specific Hardware (Payment/Security)
   * MST: Samsung Pay Magnetic Secure Transmission.
   * QBT: Samsung Ultrasonic Fingerprint (Qualcomm/Samsung hybrid).
   * DEK: Data Encryption Keys. */
  if (strcmp(name, "mst_ctrl") == 0 || strncmp(name, "qbt", 3) == 0 ||
      strncmp(name, "dek_", 4) == 0)
    return 1;

  /* Tier 34: Throughput & Latency Monitoring
   * Removes dozens of performance tracking nodes from /dev listing. */
  if (strstr(name, "throughput") != NULL || strstr(name, "latency") != NULL)
    return 1;

  /* Tier 35: Exynos Multimedia & Misc Logic
   * FIMG2D/G2D: Graphics accelerators that don't use DRM/RenderNodes.
   * Vertex10: Proprietary hardware logic. */
  if (strcmp(name, "fimg2d") == 0 || strcmp(name, "fmp") == 0 ||
      strcmp(name, "g2d") == 0 || strcmp(name, "vertex10") == 0 ||
      strcmp(name, "self_display") == 0)
    return 1;

  /* Tier 36: Misc Samsung Utility Nodes */
  if (strcmp(name, "ccic_misc") == 0 || strcmp(name, "hqm_event") == 0)
    return 1;

  /* Tier 37: Exynos/Samsung specific */
  if (strstr(name, "multipdp") != NULL || strncmp(name, "ttyBCM", 6) == 0)
    return 1; /* Catch dymmy/dummy and Broadcom consoles */
  if (strcmp(name, "s5p-smem") == 0 || strncmp(name, "als_", 4) == 0)
    return 1; /* Shared memory and raw sensors */
  if (strstr(name, "throughput") != NULL)
    return 1; /* Global throughput monitoring cleanup */

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
    ds_log("[GPU] GPU device %-30s → GID %d", path, (int)gid);
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

  /* 1. Dynamic directories */
  for (int i = 0; gpu_scan_dirs[i].dir != NULL; i++)
    scan_gpu_dir(gpu_scan_dirs[i].dir, gpu_scan_dirs[i].prefix, gids, &count,
                 max_gids);

  /* 2. Static individual nodes */
  for (int i = 0; gpu_static_devices[i] != NULL; i++)
    add_gpu_gid(gpu_static_devices[i], gids, &count, max_gids);

  if (count > 0)
    ds_log("[GPU] Discovered %d unique GPU/Hardware group(s)", count);

  return count;
}

/*
 * mirror_gpu_node()
 *
 * For a single host GPU device path: if the node is absent or wrongly a
 * directory in the container's /dev, fix it with mknod().
 *
 * Background: on Android, /dev is a plain tmpfs populated by ueventd - NOT
 * the kernel's devtmpfs.  So GPU nodes like /dev/kgsl-3d0, /dev/mali0 and
 * /dev/dri/renderD128 exist in ueventd's tmpfs but are absent (or appear as
 * empty directories) when we mount a fresh devtmpfs inside the container.
 * scan_host_gpu_gids() already detected those host nodes; here we just make
 * sure a matching character device node is present in the container /dev.
 */
static void mirror_gpu_node(const char *host_path, const char *dev_path) {
  /* host_path must be rooted under /dev/ */
  if (strncmp(host_path, "/dev/", 5) != 0)
    return;

  /* Trusted GPU list gate: never mirror dangerous/sensitive nodes.
   * This check lives here - not only in callers - so every code path
   * (dynamic dir scan AND static list) is covered by one consistent rule. */
  const char *node_name = strrchr(host_path, '/');
  node_name = node_name ? node_name + 1 : host_path;
  if (is_dangerous_node(node_name))
    return;

  /* Host node must be a character device.  Applies to BOTH root-owned
   * (gid=0) and group-owned nodes - we do not filter by ownership here.
   * add_gpu_gid() skips gid=0 because there is nothing to add to the
   * group list, but mirroring must still happen so the node is physically
   * present in devtmpfs regardless of who owns it. */
  struct stat host_st;
  if (stat(host_path, &host_st) < 0)
    return;
  if (!S_ISCHR(host_st.st_mode))
    return;

  /* Build the container-side target path */
  const char *rel = host_path + 5; /* strip leading "/dev/" */
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/%s", dev_path, rel);

  /* Ensure the parent directory exists (handles /dev/dri/renderD128 etc.) */
  char parent[PATH_MAX];
  snprintf(parent, sizeof(parent), "%s", tgt);
  char *slash = strrchr(parent, '/');
  if (slash && slash != parent) {
    *slash = '\0';
    mkdir(parent, 0755); /* best-effort - already exists is fine */
  }

  /* Check current state of the target */
  struct stat tgt_st;
  if (lstat(tgt, &tgt_st) == 0) {
    if (S_ISCHR(tgt_st.st_mode)) {
      /* devtmpfs already has a proper node. Re-own it to the unified group
       * so the container user can access it regardless of the host GID. */
      if (chown(tgt, 0, DS_GPU_UNIFIED_GID) < 0)
        ds_warn("[GPU] chown (pre-existing) %s → unified group: %s", tgt,
                strerror(errno));
      chmod(tgt, 0660);
      return;
    }

    /* devtmpfs created an empty directory placeholder instead of a node
     * (the /dev/kgsl-3d0 case seen in the screenshot).  Nuke it. */
    if (S_ISDIR(tgt_st.st_mode)) {
      if (rmdir(tgt) < 0) {
        ds_warn("[GPU] Cannot remove stale directory %s: %s", tgt,
                strerror(errno));
        return;
      }
    } else {
      unlink(tgt);
    }
  }

  /* Create the node with the same major:minor and permissions as the host */
  mode_t mode = S_IFCHR | (host_st.st_mode & 0666);
  if (mknod(tgt, mode, host_st.st_rdev) < 0) {
    ds_warn("[GPU] mknod %s (%d:%d) failed: %s", tgt,
            (int)major(host_st.st_rdev), (int)minor(host_st.st_rdev),
            strerror(errno));
    return;
  }

  /* Force ownership to the unified GPU group so UID 1000 can access
   * the node regardless of which GID the host assigned to it. */
  if (chown(tgt, 0, DS_GPU_UNIFIED_GID) < 0)
    ds_warn("[GPU] chown %s → unified group: %s", tgt, strerror(errno));
  chmod(tgt, 0660);

  ds_log("[GPU] Mirrored missing node: %-30s (%d:%d)", tgt,
         (int)major(host_st.st_rdev), (int)minor(host_st.st_rdev));
}

/*
 * do_mirror_gpu_dir()
 *
 * Walk a host directory (e.g. /dev/dri, /dev/dma_heap) and call
 * mirror_gpu_node() for every entry that matches the optional prefix.
 *
 * We deliberately do NOT pre-filter by d_type here.  d_type can be
 * DT_UNKNOWN on some Android kernels/filesystems, which would silently
 * drop valid root-owned char devices before mirror_gpu_node() ever sees
 * them.  mirror_gpu_node() already does a stat()+S_ISCHR check and the
 * is_dangerous_node() gate - those are the single source of truth.
 */
static void do_mirror_gpu_dir(const char *host_dir, const char *prefix,
                              const char *dev_path) {
  DIR *dir = opendir(host_dir);
  if (!dir)
    return;

  struct dirent *entry;
  char full_path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    if (prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;

    snprintf(full_path, sizeof(full_path), "%s/%s", host_dir, entry->d_name);
    mirror_gpu_node(full_path, dev_path);
  }

  closedir(dir);
}

/*
 * mirror_gpu_nodes()
 *
 * Public entry point called from setup_dev() immediately after devtmpfs is
 * mounted.  Mirrors every GPU/hardware device node that scan_host_gpu_gids()
 * would detect, using the same scan paths so behaviour is always in sync.
 *
 * Must be called BEFORE pivot_root while the host /dev is still accessible.
 */
void mirror_gpu_nodes(const char *dev_path) {
  /* Dynamic directories */
  for (int i = 0; gpu_scan_dirs[i].dir != NULL; i++)
    do_mirror_gpu_dir(gpu_scan_dirs[i].dir, gpu_scan_dirs[i].prefix, dev_path);

  /* Static individual nodes */
  for (int i = 0; gpu_static_devices[i] != NULL; i++)
    mirror_gpu_node(gpu_static_devices[i], dev_path);
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

int setup_gpu_groups(void) {
  /* Check if /etc/group exists - some minimal rootfs may not have it */
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

  char line[2048];
  int modified_count = 0;
  int unified_group_found = 0; /* set when DS_GPU_GROUP_NAME is seen */

  while (fgets(line, sizeof(line), fin)) {
    /* Format: name:password:GID:user_list */
    char *users_ptr = NULL;

    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));

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
      int gid_val = atoi(gid_str);

      /* If the unified group already exists, ensure root is a member */
      if (gid_val == DS_GPU_UNIFIED_GID) {
        unified_group_found = 1;

        if (!has_user(users_ptr, "root")) {
          char new_members[2048];
          safe_strncpy(new_members, users_ptr, sizeof(new_members));
          if (strlen(new_members) > 0)
            strncat(new_members, ",root",
                    sizeof(new_members) - strlen(new_members) - 1);
          else
            safe_strncpy(new_members, "root", sizeof(new_members));

          fprintf(fout, "%.*s:%s\n", (int)(users_ptr - line_copy - 1),
                  line_copy, new_members);
          ds_log("[GPU] Updated " DS_GPU_GROUP_NAME " (GID %d) members: %s",
                 DS_GPU_UNIFIED_GID, new_members);
          modified_count++;
          continue;
        }
        /* root already present - fall through to fputs */
      }
    }

    /* Print original line if not modified */
    fputs(line, fout);
  }

  /* Create the unified group if it wasn't already in the file */
  if (!unified_group_found) {
    fprintf(fout, DS_GPU_GROUP_NAME ":x:%d:root\n", DS_GPU_UNIFIED_GID);
    ds_log("[GPU] Created unified group " DS_GPU_GROUP_NAME " (GID %d)",
           DS_GPU_UNIFIED_GID);
    modified_count++;
  }

  fclose(fin);
  fclose(fout);

  /* Atomic replacement */
  if (modified_count > 0) {
    if (rename(tmp_path, group_path) < 0) {
      ds_warn("Failed to update /etc/group: %s", strerror(errno));
      unlink(tmp_path);
      return -1;
    }
    ds_log("[GPU] Finalized GPU group membership (Updated %d entry/entries)",
           modified_count);
  } else {
    unlink(tmp_path);
  }

  return 0;
}

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

  /* OPTIMIZATION: Check if the unified bridge is already mounted.
   * If the Termux /usr/tmp is already a tmpfs, it means the bridge is
   * already established and we don't need to kill Termux to re-mount it. */
  struct statfs fs;
  if (statfs(DS_TERMUX_TMP_DIR, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    return; /* Bridge already exists, skip force-stop */
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
  snprintf(mount_opts, sizeof(mount_opts), "mode=1777,uid=%d,gid=%d",
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

  if (!is_android()) {
    /* Desktop Linux path - X11 socket always mounted in both hw/non-hw modes
     */
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
  if (!cfg->termux_x11)
    return 0;

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
int setup_hardware_access(struct ds_config *cfg) {
  /* 1. Create GPU groups inside the container.
   *    hw_access: full hardware passthrough - always set up GPU groups.
   *    gpu_mode:  isolated tmpfs with GPU nodes mirrored in - also needs the
   *               unified droidspaces-gpu group so the container user can
   *               actually open those nodes. */
  if (cfg->hw_access || cfg->gpu_mode)
    setup_gpu_groups();

  /* 2. Mount X11 socket for GUI applications (always attempt on Linux, check
   * flag on Android) */
  setup_x11_and_virgl_sockets(cfg);

  return 0;
}
