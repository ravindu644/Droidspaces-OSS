/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/capability.h>
#include <sys/prctl.h>

static int ds_hex_value(unsigned char c) {
  if (c >= '0' && c <= '9')
    return (int)(c - '0');
  if (c >= 'a' && c <= 'f')
    return (int)(c - 'a') + 10;
  if (c >= 'A' && c <= 'F')
    return (int)(c - 'A') + 10;
  return -1;
}

static int ds_file_contains_bytes(const char *path, const char *needle) {
  unsigned char buffer[4096 + 64];
  const size_t needle_len = strlen(needle);
  size_t carry = 0;
  int found = 0;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0 || needle_len == 0 || needle_len > 64) {
    if (fd >= 0)
      close(fd);
    return 0;
  }

  for (;;) {
    ssize_t n = read(fd, buffer + carry, sizeof(buffer) - carry);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;

    size_t total = carry + (size_t)n;
    for (size_t i = 0; i + needle_len <= total; i++) {
      if (memcmp(buffer + i, needle, needle_len) == 0) {
        found = 1;
        break;
      }
    }
    if (found)
      break;

    carry = total < needle_len - 1 ? total : needle_len - 1;
    memmove(buffer, buffer + total - carry, carry);
  }

  close(fd);
  return found;
}

static int ds_networkd_supports_foreign_nexthops(void) {
  static const char setting[] = "ManageForeignNextHops";
  return ds_file_contains_bytes("usr/lib/systemd/systemd-networkd", setting) ||
         ds_file_contains_bytes("lib/systemd/systemd-networkd", setting);
}

/* Build a stable DHCP identity for direct-L2 links.
 *
 * ipvlan shares its lower device's MAC, so ClientIdentifier=mac would make
 * every ipvlan container (and Android itself) look like the same DHCP client.
 * Use the container UUID as a DUID-UUID instead.  Legacy configs without a
 * valid UUID get a deterministic fallback derived from the container name;
 * the identity must never change merely because the container restarts.
 *
 * The DHCP hostname gets a stable numeric suffix so cloned/common hostnames
 * remain distinguishable without causing a fresh lease on every boot. */
static void ds_build_direct_dhcp_identity(const struct ds_config *cfg,
                                          char duid_raw[48], uint32_t *iaid,
                                          char dhcp_hostname[64],
                                          char ipv6_ra_token[46]) {
  uint8_t id[16] = {0};
  int valid_uuid = cfg->uuid[0] != '\0' && strlen(cfg->uuid) == DS_UUID_LEN;

  if (valid_uuid) {
    for (size_t i = 0; i < sizeof(id); i++) {
      int hi = ds_hex_value((unsigned char)cfg->uuid[i * 2]);
      int lo = ds_hex_value((unsigned char)cfg->uuid[i * 2 + 1]);
      if (hi < 0 || lo < 0) {
        valid_uuid = 0;
        break;
      }
      id[i] = (uint8_t)((hi << 4) | lo);
    }
  }

  if (!valid_uuid) {
    const char *seed = cfg->container_name[0] ? cfg->container_name
                                              : cfg->hostname;
    uint64_t h1 = UINT64_C(1469598103934665603);
    uint64_t h2 = UINT64_C(1099511628211) ^ UINT64_C(0x9e3779b97f4a7c15);
    for (const unsigned char *p = (const unsigned char *)seed; *p; p++) {
      h1 ^= *p;
      h1 *= UINT64_C(1099511628211);
      h2 ^= (uint64_t)(*p + 0x9dU);
      h2 *= UINT64_C(14029467366897019727);
    }
    for (size_t i = 0; i < 8; i++) {
      id[i] = (uint8_t)(h1 >> (i * 8));
      id[i + 8] = (uint8_t)(h2 >> (i * 8));
    }
  }

  snprintf(duid_raw, 48,
           "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
           "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
           id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8],
           id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
  snprintf(ipv6_ra_token, 46,
           "prefixstable,"
           "%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x",
           id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8],
           id[9], id[10], id[11], id[12], id[13], id[14], id[15]);

  *iaid = ((uint32_t)id[12] << 24) | ((uint32_t)id[13] << 16) |
          ((uint32_t)id[14] << 8) | (uint32_t)id[15];
  if (*iaid == 0)
    *iaid = 1;

  uint32_t suffix = 2166136261U;
  for (size_t i = 0; i < sizeof(id); i++) {
    suffix ^= id[i];
    suffix *= 16777619U;
  }
  suffix %= 100000U;

  const char *source = cfg->hostname[0] ? cfg->hostname : cfg->container_name;
  size_t used = 0;
  const size_t base_limit = 57; /* '-' + five digits + NUL => max 63 chars */
  for (const unsigned char *p = (const unsigned char *)source;
       *p && used < base_limit; p++) {
    unsigned char c = (unsigned char)tolower(*p);
    if (isalnum(c)) {
      dhcp_hostname[used++] = (char)c;
    } else if (used > 0 && dhcp_hostname[used - 1] != '-') {
      dhcp_hostname[used++] = '-';
    }
  }
  while (used > 0 && dhcp_hostname[used - 1] == '-')
    used--;
  if (used == 0) {
    memcpy(dhcp_hostname, "droidspace", sizeof("droidspace") - 1);
    used = sizeof("droidspace") - 1;
  }
  snprintf(dhcp_hostname + used, 64 - used, "-%05u", suffix);
}

/* Per-boot network-service policy.  /run overrides stale rootfs drop-ins, so
 * old images immediately learn about ipvlan/macvlan without being re-extracted.
 * Host/none and direct-L2 static must keep guest managers stopped; NAT,
 * gateway, and direct-L2 DHCP let the guest own eth0 configuration. */
static void ds_write_guest_network_policy(const struct ds_config *cfg) {
  if (!cfg)
    return;

  int direct_dhcp =
      (cfg->net_mode == DS_NET_IPVLAN || cfg->net_mode == DS_NET_MACVLAN) &&
      cfg->net_ipam == DS_NET_IPAM_DHCP;
  int allow_guest = cfg->net_mode == DS_NET_NAT ||
                    cfg->net_mode == DS_NET_GATEWAY || direct_dhcp;

  mkdir("run/systemd", 0755);
  mkdir("run/systemd/system", 0755);
  const char *units[] = {"NetworkManager.service", "dhcpcd.service",
                         "systemd-networkd.service",
                         "systemd-resolved.service", NULL};
  const char *allow = "[Service]\nExecCondition=\n";
  const char *block = "[Service]\nExecCondition=\nExecCondition=/bin/false\n";
  for (size_t i = 0; units[i]; i++) {
    char dir[PATH_MAX];
    char file[PATH_MAX];
    snprintf(dir, sizeof(dir), "run/systemd/system/%s.d", units[i]);
    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
      ds_warn("[NET] Cannot create systemd network policy directory %s: %s",
              dir, strerror(errno));
      continue;
    }
    snprintf(file, sizeof(file), "%s/zz-droidspaces-netmode.conf", dir);
    if (write_file(file, allow_guest ? allow : block) < 0)
      ds_warn("[NET] Cannot write systemd network policy %s", file);
  }

  /* dshost0 is a runtime-owned point-to-point control link.  The backend
   * assigns both ends before init starts; guest managers must leave it alone
   * while continuing to manage the direct-L2 eth0 normally. */
  if (cfg->host_access == DS_HOST_ACCESS_PTP) {
    mkdir("run/systemd/network", 0755);
    if (write_file("run/systemd/network/05-droidspaces-host-access.network",
                   "[Match]\nName=dshost0\n\n[Link]\nUnmanaged=yes\n") < 0)
      ds_warn("[NET] Cannot write dshost0 systemd-networkd policy");

    mkdir("run/NetworkManager", 0755);
    mkdir("run/NetworkManager/conf.d", 0755);
    if (write_file("run/NetworkManager/conf.d/"
                   "05-droidspaces-host-access.conf",
                   "[keyfile]\nunmanaged-devices=interface-name:dshost0\n") < 0)
      ds_warn("[NET] Cannot write dshost0 NetworkManager policy");
  }

  if (direct_dhcp) {
    mkdir("run/systemd/network", 0755);

    /* systemd-networkd 257+ represents RA gateways as kernel nexthop objects.
     * Android kernels commonly lack the nexthop netlink API (RTM_NEWNEXTHOP),
     * which makes IPv6 RA fail with EOPNOTSUPP.  Keep the traditional direct
     * gateway route representation when networkd advertises the compatibility
     * setting; older releases remain untouched. */
    if (cfg->net_mode == DS_NET_IPVLAN && !cfg->disable_ipv6 &&
        ds_networkd_supports_foreign_nexthops()) {
      mkdir("run/systemd/networkd.conf.d", 0755);
      if (write_file("run/systemd/networkd.conf.d/"
                     "zz-droidspaces-kernel-compat.conf",
                     "[Network]\nManageForeignNextHops=no\n") < 0)
        ds_warn("[NET] Cannot write systemd-networkd kernel compatibility "
                "configuration");
    }

    char network[2048];
    char duid_raw[48];
    char dhcp_hostname[64];
    char ipv6_ra_token[46];
    char client_identity[256];
    char ipv6_ra[128];
    uint32_t iaid;
    ds_build_direct_dhcp_identity(cfg, duid_raw, &iaid, dhcp_hostname,
                                  ipv6_ra_token);

    if (cfg->net_mode == DS_NET_IPVLAN) {
      snprintf(client_identity, sizeof(client_identity),
               "ClientIdentifier=duid\n"
               "DUIDType=uuid\n"
               "DUIDRawData=%s\n"
               "IAID=%u\n",
               duid_raw, iaid);
    } else {
      snprintf(client_identity, sizeof(client_identity),
               "ClientIdentifier=mac\n");
    }

    const char *ipv6_link = cfg->disable_ipv6
                                ? "IPv6AcceptRA=no\nLinkLocalAddressing=no\n"
                                : "IPv6AcceptRA=yes\n"
                                  "LinkLocalAddressing=ipv6\n";
    if (cfg->disable_ipv6) {
      ipv6_ra[0] = '\0';
    } else if (cfg->net_mode == DS_NET_IPVLAN) {
      snprintf(ipv6_ra, sizeof(ipv6_ra),
               "\n[IPv6AcceptRA]\nToken=%s\nUseDNS=yes\n", ipv6_ra_token);
    } else {
      snprintf(ipv6_ra, sizeof(ipv6_ra),
               "\n[IPv6AcceptRA]\nUseDNS=yes\n");
    }
    snprintf(network, sizeof(network),
             "[Match]\n"
             "Name=eth0\n\n"
             "[Network]\n"
             "DHCP=ipv4\n"
             "%s\n"
             "[DHCPv4]\n"
             "%s"
             "SendHostname=yes\n"
             "Hostname=%s\n"
             "UseDNS=yes\n"
             "UseDomains=yes\n"
             "RequestBroadcast=yes\n"
             "RouteMetric=100\n"
             "%s",
             ipv6_link, client_identity, dhcp_hostname, ipv6_ra);
    if (write_file("run/systemd/network/10-droidspaces-eth0.network",
                   network) < 0)
      ds_warn("[NET] Cannot write direct-L2 systemd-networkd configuration");
    else
      ds_log("[NET] DHCP identity: %s (%s)", dhcp_hostname,
             cfg->net_mode == DS_NET_IPVLAN ? "DUID-UUID" : "MAC");
  }

  ds_log("[NET] Guest network services: %s (mode=%d, ipam=%s)",
         allow_guest ? "enabled" : "blocked", cfg->net_mode,
         cfg->net_ipam == DS_NET_IPAM_STATIC ? "static" : "dhcp");
}

/*
 * ds_apply_capability_hardening()
 *
 * Drops dangerous capabilities from the bounding set to reduce the container's
 * attack surface.
 *
 * In Standard Mode (hw_access=0), we drop several sensitive capabilities.
 * In Hardware Mode (hw_access=1), we preserve most to ensure full
 * low-level hardware access (USB, Serial, Bluetooth, Flashing).
 */
void ds_apply_capability_hardening(int hw_access, int privileged_mask) {
  if (privileged_mask & DS_PRIV_NOCAPS) {
    ds_log("[SEC] --privileged=nocaps: skipping capability drops.");
    return;
  }
  /* Universal drops - even in hardware mode, there's no legitimate use
   * for CAP_SYS_MODULE inside a container (kernel module loading).
   * CAP_SYS_BOOT is intentionally preserved - it is required for in-container
   * reboot(2) to work inside a PID namespace without rebooting the host. */
  int universal_drops[] = {CAP_SYS_MODULE, -1};
  int total_dropped = 0;

  for (int i = 0; universal_drops[i] != -1; i++) {
    if (prctl(PR_CAPBSET_DROP, universal_drops[i], 0, 0, 0) < 0) {
      if (errno != EINVAL) {
        ds_warn("[SEC] Failed to drop universal cap %d: %s", universal_drops[i],
                strerror(errno));
      }
    } else {
      total_dropped++;
    }
  }

  if (hw_access) {
    ds_log("[SEC] Hardware Mode: preserved bounding set (dropped %d universal "
           "caps).",
           total_dropped);
    return;
  }

  /* Standard Hardening Tier: drop capabilities that affect host stability
   * or allow escaping the container's isolation. */
  int caps_to_drop[] = {
      CAP_SYS_RAWIO,       /* Raw hardware access (I/O ports, memory) */
      CAP_SYS_PTRACE,      /* Process tracing/injection across namespaces */
      CAP_SYS_PACCT,       /* Process accounting */
      CAP_MAC_ADMIN,       /* Mandatory Access Control policy modification */
      CAP_MAC_OVERRIDE,    /* Bypass MAC policies */
      CAP_WAKE_ALARM,      /* Affect host power management / wakeups */
      CAP_BLOCK_SUSPEND,   /* Affect host power management / sleep */
      CAP_AUDIT_READ,      /* Read kernel audit logs */
      CAP_DAC_READ_SEARCH, /* Bypass file read/directory search permissions -
                            * the other half of the Shocker escape: combined
                            * with open_by_handle_at it allows reading any
                            * file on the host outside the mount namespace. */
      -1};

  for (int i = 0; caps_to_drop[i] != -1; i++) {
    if (prctl(PR_CAPBSET_DROP, caps_to_drop[i], 0, 0, 0) < 0) {
      if (errno != EINVAL) {
        ds_warn("[SEC] Failed to drop cap %d: %s", caps_to_drop[i],
                strerror(errno));
      }
    } else {
      total_dropped++;
    }
  }

  ds_log("[SEC] Bounding set hardened (dropped %d caps).", total_dropped);
}

int internal_boot(struct ds_config *cfg) {
  /* Defensive check: ensure configuration is valid */
  if (!cfg) {
    ds_error("internal_boot received NULL configuration.");
    return -1;
  }

  /* Pre-open the container log file before namespace isolation / pivot_root.
   * The FD survives mount namespace changes, ensuring all post-pivot logs
   * (X11 bridge, bind mounts, init exec) are captured in the host log. */
  ds_open_container_log(cfg);

  /* NAT child-side handshake
   *
   * This block runs BEFORE any mount operations, while /proc still points to
   * the HOST /proc.  That means /proc/<our_pid>/ns/net is accessible to the
   * monitor and it can move the veth peer into our netns.
   *
   * Protocol:
   *   1. Close pipe ends we don't use.
   *   2. Write "R" (READY) on net_ready_pipe[1] → monitor unblocks.
   *   3. Block on net_done_pipe[0] until monitor finishes veth setup.
   *   4. Call setup_veth_child_side_named() with the received handshake.
   *
   * For DS_NET_NONE: we still do the pipe exchange (monitor sends an empty
   * handshake) so loopback is still configured.  No veth is created.
   */
  if (cfg->net_mode != DS_NET_HOST) {
    ds_log("[NET] Child: net_mode=%d - starting handshake with monitor",
           cfg->net_mode);

    /* We write to net_ready, read from net_done */
    if (cfg->net_ready_pipe[0] >= 0)
      close(cfg->net_ready_pipe[0]);
    if (cfg->net_done_pipe[1] >= 0)
      close(cfg->net_done_pipe[1]);

    /* Signal monitor: we are alive and in our new netns */
    char rdy = 'R';
    if (cfg->net_ready_pipe[1] >= 0) {
      if (write(cfg->net_ready_pipe[1], &rdy, 1) < 0)
        ds_warn("[NET] Child: write READY failed: %s", strerror(errno));
      close(cfg->net_ready_pipe[1]);
      cfg->net_ready_pipe[1] = -1;
    }

    /* Wait for monitor to complete host-side setup */
    struct ds_net_handshake hs;
    memset(&hs, 0, sizeof(hs));
    if (cfg->net_done_pipe[0] >= 0) {
      ssize_t nr = read(cfg->net_done_pipe[0], &hs, sizeof(hs));
      close(cfg->net_done_pipe[0]);
      cfg->net_done_pipe[0] = -1;
      if (nr != (ssize_t)sizeof(hs)) {
        ds_warn("[NET] Child: incomplete handshake (read %zd, expected %zu)",
                nr, sizeof(hs));
      } else if (cfg->net_mode == DS_NET_GATEWAY) {
        ds_log("[NET] Child: handshake received (gateway mode)");
      } else {
        ds_log("[NET] Child: handshake received: peer=%s ip=%s", hs.peer_name,
               hs.ip_str);
      }
    }

    if ((cfg->net_mode == DS_NET_IPVLAN ||
         cfg->net_mode == DS_NET_MACVLAN) &&
        hs.status < 0)
      ds_die("Direct L2 network setup failed: %s", strerror(-hs.status));

    /* Configure our side of the veth (or just loopback for DS_NET_NONE) */
    if (cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_GATEWAY) {
      setup_veth_child_side_named(cfg, hs.peer_name, hs.ip_str);
    } else if (cfg->net_mode == DS_NET_IPVLAN ||
               cfg->net_mode == DS_NET_MACVLAN) {
      if (setup_parent_link_child_side(cfg, hs.peer_name) < 0)
        ds_die("Failed to configure direct L2 interface inside container");
    } else {
      /* DS_NET_NONE: just bring up loopback */
      ds_nl_ctx_t *nlctx = ds_nl_open();
      if (nlctx) {
        ds_nl_link_up(nlctx, "lo");
        ds_nl_close(nlctx);
      }
    }

    ds_log("[NET] Child: handshake complete");
  }

  /* 0. Boot Guard: Ensure name is present and unique.
   * This is a critical security check to prevent anonymous or conflicting
   * containers from booting, even if the CLI checks were bypassed. */
  if (!cfg->container_name[0]) {
    ds_error("CRITICAL: Boot aborted — container name is empty.");
    goto boot_fail;
  }

  pid_t existing_pid = 0;
  if (is_container_running(cfg, &existing_pid)) {
    /* If we find ourselves in the pidfile, it's not a conflict, it's just us
     * being tracked early (which is fine). */
    if (existing_pid != getpid()) {
      ds_error(
          "CRITICAL: Boot aborted — name '%s' is already in use by PID %d.",
          cfg->container_name, existing_pid);
      goto boot_fail;
    }
  }

  /* 1. Isolated mount namespace */
  if (unshare(CLONE_NEWNS) < 0) {
    ds_error("Failed to unshare mount namespace: %s", strerror(errno));
    goto boot_fail;
  }

  /* 2. Make all mounts private to avoid leaking to host.
   * We ALWAYS start with MS_PRIVATE because MS_SHARED breaks pivot_root/MS_MOVE
   * fallbacks on some kernels (e.g. Android rootfs). We will switch to
   * MS_SHARED after the rootfs relocation if requested. */
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
    ds_error("Failed to make / private: %s", strerror(errno));
    goto boot_fail;
  }

  /* Init family was classified before fork in start_rootfs().
   * Boot-time setup only needs to know whether that family is systemd. */
  int is_systemd = (cfg->init_type == DS_INIT_SYSTEMD);

  /* 3. Setup volatile overlay INSIDE the container's mount namespace.
   * This MUST happen here (not in parent) so the overlay's connection to
   * its lowerdir (e.g. a loop-mounted image) survives mount privatization. */
  if (cfg->volatile_mode) {
    if (setup_volatile_overlay(cfg) < 0) {
      ds_error("Failed to setup volatile overlay.");
      goto boot_fail;
    }
  }

  /* 4. Bind mount rootfs to itself (required for pivot_root) */
  if (mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL) <
      0) {
    ds_error("Failed to bind mount rootfs: %s", strerror(errno));
    goto boot_fail;
  }

  /* 4b. Force the image rootfs read-write - the single unified point covering
   * fresh boot, restart, AND internal reboot.
   *
   * On shutdown/reboot OpenWRT remounts its root read-only, leaving our mount
   * point under /mnt/Droidspaces/<name> read-only too.  External and internal
   * reboots never unmount the rootfs.img (to keep restarts fast), so the next
   * boot reuses that still-read-only mount and pivot_root fails to create
   * .old_root ("Read-only file system") - the container never boots.
   *
   * Skipped for directory rootfs (a fresh self-bind in this new namespace, its
   * host backing fs already writable) and volatile mode (pivot_root targets the
   * read-write overlay, not the read-only lower image).  Safe: the guest synced
   * the fs before flipping it read-only.  A no-op when already read-write. */
  if (cfg->is_img_mount && !cfg->volatile_mode) {
    if (mount(NULL, cfg->rootfs_path, NULL,
              MS_REMOUNT | MS_NOATIME | MS_NODIRATIME, NULL) < 0)
      ds_warn("Failed to remount rootfs %s read-write: %s", cfg->rootfs_path,
              strerror(errno));
  }

  /* 5. Set working directory to rootfs (required before pivot_root) */
  if (chdir(cfg->rootfs_path) < 0) {
    ds_error("Failed to chdir to '%s': %s", cfg->rootfs_path, strerror(errno));
    goto boot_fail;
  }

  /* 6. Read UUID from sync file if not already provided (parity with v2) */
  if (cfg->uuid[0] == '\0') {
    read_file(".droidspaces-uuid", cfg->uuid, sizeof(cfg->uuid));
  }
  if (access(".droidspaces-uuid", F_OK) == 0) {
    if (unlink(".droidspaces-uuid") < 0) {
      /* This might fail if the rootfs is RO (image mount), but internal_boot
       * already skips writing it in that case. */
    }
  }

  /* 7. Pre-create standard directories in one loop to reduce syscalls */
  const char *dirs_to_create[] = {".old_root", "proc", "sys", "run", "tmp"};
  int dir_creation_failed = 0;
  for (size_t i = 0; i < sizeof(dirs_to_create) / sizeof(dirs_to_create[0]);
       i++) {
    if (mkdir(dirs_to_create[i], 0755) < 0 && errno != EEXIST) {
      ds_error("Failed to create '%s': %s", dirs_to_create[i], strerror(errno));
      /* .old_root is critical for pivot_root, track if it fails */
      if (strcmp(dirs_to_create[i], ".old_root") == 0) {
        dir_creation_failed = 1;
      }
    }
  }
  if (dir_creation_failed) {
    ds_error("Failed to create critical directory .old_root");
    goto boot_fail;
  }

  /* 8. Setup /dev (device nodes, devtmpfs) */
  if (setup_dev(".", cfg->hw_access, cfg->gpu_mode, cfg->privileged_mask) < 0) {
    ds_error("Failed to setup /dev.");
    goto boot_fail;
  }

  /* 9. Log hardware access mode (BEFORE pivot_root) */
  if (!cfg->reboot_cycle) {
    if (cfg->hw_access)
      ds_log("Setting up hardware access...");
    else if (cfg->gpu_mode)
      ds_log("Setting up GPU-only access...");
    else
      ds_log("Hardware access disabled: using isolated tmpfs...");
  }

  /* 10. Mount virtual filesystems (proc, sys) */
  if (domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount procfs: %s", strerror(errno));
    goto boot_fail;
  }

  /* Mount /sys */
  if (domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) <
      0) {
    ds_error("Failed to mount sysfs: %s", strerror(errno));
    goto boot_fail;
  }

  /* 10. Pre-create the cgroup mountpoint while /sys is still RW.
   * This allows us to mount cgroups onto it later even after /sys is RO. */
  mkdir_p("sys/fs/cgroup", 0755);

  if (cfg->hw_access && cfg->foreground && is_systemd) {
    /* DYNAMIC HARDWARE HOLES: Instead of hardcoding, we iterate through
     * everything in /sys and 'pin' subdirectories as independent RW mounts.
     * This ensures 100% hardware visibility (devices, bus, class, block, etc)
     * even after we remount the top-level /sys as RO for systemd's benefit. */
    DIR *d = opendir("sys");
    if (d) {
      struct dirent *de;
      while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
          continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "sys/%s", de->d_name);

        struct stat st;
        if (stat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
          if (mount(subpath, subpath, NULL, MS_BIND | MS_REC, NULL) < 0) {
            /* Ignore errors for files or pseudo-dirs that can't be mounted */
          }
        }
      }
      closedir(d);
    }
  } else if (!cfg->hw_access) {
    /* Hardware isolation: network only mixed mode */
    if (mkdir("sys/devices", 0755) < 0 && errno != EEXIST) {
      ds_warn("Failed to create sys/devices directory: %s", strerror(errno));
    }
    if (mkdir("sys/devices/virtual", 0755) < 0 && errno != EEXIST) {
      ds_warn("Failed to create sys/devices/virtual directory: %s",
              strerror(errno));
    }
    if (mkdir("sys/devices/virtual/net", 0755) < 0 && errno != EEXIST) {
      ds_warn("Failed to create sys/devices/virtual/net directory: %s",
              strerror(errno));
    }

    /* Fix: Instead of mounting a fresh sysfs (which creates a recursive tree),
     * we bind-mount the existing net devices path from our own sysfs mount.
     * This keeps the symlink at /sys/class/net/eth0 valid while pinning the
     * path as an independent mount point that can survive isolation and
     * provide RW access if needed. */
    if (mount("sys/devices/virtual/net", "sys/devices/virtual/net", NULL,
              MS_BIND | MS_REC, NULL) < 0) {
      ds_warn("Failed to bind-mount network devices in isolated /sys "
              "(networking may be limited)");
    }
  }

  /* Remount /sys as RO for systemd's benefit, but ONLY if we are in
   * foreground mode + systemd (where we used pinned sub-mounts) or if
   * hw_access is disabled entirely. In background mode or non-systemd
   * hw_access mode, we leave /sys RW. */
  if (!cfg->hw_access || (cfg->foreground && is_systemd)) {
    if (mount(NULL, "sys", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0) {
      ds_warn("Failed to remount /sys as read-only: %s", strerror(errno));
    }
  }

  /* 11. Setup Cgroups AFTER locking down /sys.
   * Mounting onto a directory on a RO parent is allowed for root, and it
   * ensures the sub-mount (tmpfs) is RW and independent of the parent's RO. */
  if (setup_cgroups(is_systemd, cfg->force_cgroupv1) < 0) {
    ds_error("Failed to setup container cgroups.");
    goto boot_fail;
  }

  if (domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0) {
    ds_error("Failed to mount tmpfs at /run: %s", strerror(errno));
    goto boot_fail;
  }

  /* 13. Setup /tmp: always mount a fresh isolated tmpfs.
   * The X11 socket lives in /run/.X11-unix so systemd's tmp.mount
   * cannot interfere with it. */
  if (domount("tmpfs", "tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
    ds_warn("Failed to mount tmpfs at /tmp: %s", strerror(errno));

  /* 14. Bind-mount console BEFORE pivot_root (host pts still visible). */
  if (mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL) < 0)
    ds_warn("Failed to bind mount console '%s': %s", cfg->console.name,
            strerror(errno));

  /* 15. Android-specific storage */
  if (cfg->android_storage) {
    android_setup_storage(".");
  }

  /* 16. Custom bind mounts */
  setup_custom_binds(cfg, ".");

  /* 17. pivot_root with MS_MOVE+chroot fallback for ramfs/rootfs environments
   * (e.g. Android recovery) where pivot_root(2) always returns EINVAL because
   * the kernel refuses to pivot when new_root is on the same underlying fs as
   * the current root (ramfs has no backing device, self-bind doesn't help).
   * MS_MOVE atomically relocates the new root onto / and chroot(2) locks us
   * in - exactly what switch_root(8) does internally. */
  int used_ms_move = 0;
  if (is_ramfs("/")) {
    ds_log("Detected rootfs/ramfs root - automatically falling back to "
           "MS_MOVE+chroot");
    used_ms_move = 1;
    if (mount(".", "/", NULL, MS_MOVE, NULL) < 0) {
      ds_error("MS_MOVE fallback failed: %s", strerror(errno));
      goto boot_fail;
    }
    if (chroot(".") < 0) {
      ds_error("chroot(\".\") after MS_MOVE failed: %s", strerror(errno));
      goto boot_fail;
    }
  } else if (syscall(SYS_pivot_root, ".", ".old_root") < 0) {
    ds_error("pivot_root failed: %s", strerror(errno));
    goto boot_fail;
  }

  if (chdir("/") < 0) {
    ds_error("chdir(\"/\") after pivot_root failed: %s", strerror(errno));
    goto boot_fail;
  }

  /* 17b. Apply deferred mount propagation settings.
   * Switch to MS_SHARED only after relocation is complete. */
  if (cfg->privileged_mask & DS_PRIV_SHARED) {
    if (mount(NULL, "/", NULL, MS_REC | MS_SHARED, NULL) < 0) {
      ds_warn("[SEC] Failed to apply MS_SHARED propagation: %s",
              strerror(errno));
    } else {
      ds_log("[SEC] Root mount propagation set to SHARED.");
    }
  }

  /* 18. Setup devpts (must be after pivot_root for newinstance) */
  setup_devpts(cfg->hw_access);

  /* Apply jail mask after pivot_root for correct path resolution */
  ds_apply_jail_mask(cfg->hw_access, cfg->privileged_mask);

  /* 18b. Resource Visibility Virtualization
   * Always runs: uptime/loadavg are fundamental container features.
   * CPU/RAM spoofing is selectively enabled only when cgroup limits are set. */
  if (is_mountpoint("/proc")) {
    if (ds_virtualize_init(cfg) < 0)
      ds_warn(
          "[VIRT] Initialization failed, continuing without virtualization.");
  } else {
    ds_warn("[VIRT] /proc not mounted, skipping virtualization.");
  }

  /* 19. Configure rootfs networking (hostname, resolv.conf, etc) */
  fix_networking_rootfs(cfg);

  /* 20. Setup GPU groups and X11 socket (AFTER pivot_root) */
  setup_hardware_access(cfg);

  /* Log bind mounts and boot (after hw-access logs for clean ordering) */
  if (!cfg->reboot_cycle) {
    if (cfg->bind_count > 0)
      ds_log("Setting up %d custom bind mount(s)...", cfg->bind_count);
    ds_log("Booting '%s' (init: %s)...", cfg->container_name,
           cfg->custom_init[0] ? cfg->custom_init : DS_DEFAULT_INIT);
  }

  /* 20b. Write identity markers for PID discovery (AFTER logs to ensure CLI
   * parent sees them before exiting background mode). */
  mkdir("run/droidspaces", 0755);
  if (cfg->uuid[0] != '\0') {
    char marker_path[PATH_MAX];
    snprintf(marker_path, sizeof(marker_path), "run/droidspaces/%s", cfg->uuid);
    write_file(marker_path, ""); /* empty UUID marker */
  }

  /* Save a normalized copy of the config inside /run for metadata recovery. */
  if (ds_config_save("run/droidspaces/container.config", cfg) < 0) {
    ds_warn("Boot: Failed to save internal configuration backup");
  }

  ds_write_guest_network_policy(cfg);

  write_file("run/droidspaces/name", cfg->container_name);

  if (cfg->img_mount_point[0])
    write_file("run/droidspaces/mount", cfg->img_mount_point);

  /* Legacy compatibility: write version to the marker directory root */
  write_file("run/droidspaces/version", DS_VERSION);
  if (cfg->foreground) {
    printf(C_BOLD C_WHITE "\r\n(to exit from the foreground mode, press "
                          "CTRL+ALT+Q)\r\n" C_RESET);
    fflush(stdout);
  }
  printf("\r\n");
  fflush(stdout);

  /* 21. Cleanup .old_root (skip when MS_MOVE fallback was used - there is no
   * old root mountpoint to detach in that path). */
  if (!used_ms_move) {
    if (umount2("/.old_root", MNT_DETACH) < 0)
      ds_warn("Failed to unmount .old_root: %s", strerror(errno));
    else
      rmdir("/.old_root");
  } else {
    rmdir("/.old_root");
  }

  /* 22. Set container identity for systemd/openrc */
  write_file(DS_SYSTEMD_CONTAINER_MARKER, "droidspaces");

  /* 23. Clear environment and set container defaults */
  ds_env_boot_setup(cfg);
  ds_env_save("/run/droidspaces.env", cfg);

  /* 23b. Integration with /etc/profile.d for universal sourcing */
  if (access("/etc/profile.d", F_OK) == 0) {
    const char *profile_link = "/etc/profile.d/droidspaces_env.sh";
    /* Always recreate - avoids TOCTOU and fixes stale symlinks after rootfs
     * swap */
    unlink(profile_link);
    if (symlink("/run/droidspaces.env", profile_link) < 0 && errno != EEXIST) {
      ds_warn("Failed to create profile.d symlink: %s", strerror(errno));
    }
  }

  /* 23c. Apply security hardening (capabilities)
   * Apply security hardening (capabilities and seccomp)
   * This is done at the very end to ensure all setup tasks that might need
   * privileges (like chown/chmod or mknod) are finished. */
  /* Neutralize the KSU container-escape path BEFORE seccomp is applied: the
   * ioctl is delivered through the [ksu_driver] fd that the magic reboot()
   * installs, and the seccomp filter below denies that very magic reboot.
   * Best-effort, silent no-op on non-KSU kernels. */
  ds_ksu_neutralize_root_escape();
  ds_seccomp_apply_minimal(cfg->privileged_mask, cfg->userns_allowed);
  android_seccomp_setup(is_systemd,
                        cfg->block_nested_ns &&
                            !(cfg->privileged_mask & DS_PRIV_NOSEC),
                        cfg->privileged_mask);

  ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask);

  /* 24. Redirect standard I/O to /dev/console */
  int console_fd = open("/dev/console", O_RDWR);
  if (console_fd >= 0) {
    if (ds_terminal_set_stdfds(console_fd) < 0) {
      ds_warn("Failed to redirect stdio to /dev/console");
      close(console_fd);
    } else {
      ds_terminal_make_controlling(console_fd);

      /* Set a sane default window size on the console PTY if none was set.
       * The parent's console_monitor_loop will overwrite this with the
       * real host terminal size via SIGWINCH, but we need a reasonable
       * default so early boot output (before the parent syncs) is
       * properly aligned. Without this, programs like sudo that query
       * the terminal size get {0,0} and produce misaligned output. */
      struct winsize ws;
      if (ioctl(console_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col == 0 &&
          ws.ws_row == 0) {
        ws.ws_row = 24;
        ws.ws_col = 80;
        ioctl(console_fd, TIOCSWINSZ, &ws);
      }

      /* Sticky permissions again just in case systemd's TTYReset stripped them
       */
      fchmod(console_fd, 0620);
      if (fchown(console_fd, 0, DS_DEFAULT_TTY_GID) < 0) {
        /* best-effort, ignore EPERM */
      }
      if (console_fd > 2)
        close(console_fd);
    }
  }

  /* 25. EXEC INIT */
  char *init_bin =
      cfg->custom_init[0] ? cfg->custom_init : (char *)DS_DEFAULT_INIT;
  char *init_args[16];
  int argc = 0;
  init_args[argc++] = init_bin;

  /* Tell systemd which cgroup hierarchy the container was actually set up
   * with.  We use statfs() on /sys/fs/cgroup (now the container root after
   * pivot_root) rather than guessing from kernel version.  setup_cgroups()
   * already decided the layout - we just reflect what it mounted:
   *   cgroup2fs  → unified (v2 only)  → unified_cgroup_hierarchy=1
   *   tmpfs      → legacy / hybrid    → unified_cgroup_hierarchy=0
   * This is exactly what LXC does via lxc.init.cmd. */
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif
  if (is_systemd && !cfg->custom_init[0]) {
    struct statfs _cgsfs;
    if (statfs("/sys/fs/cgroup", &_cgsfs) == 0) {
      if ((unsigned long)_cgsfs.f_type == (unsigned long)CGROUP2_SUPER_MAGIC) {
        init_args[argc++] = (char *)"systemd.unified_cgroup_hierarchy=1";
      } else {
        /* tmpfs root → legacy or hybrid layout mounted by setup_cgroups */
        init_args[argc++] = (char *)"systemd.unified_cgroup_hierarchy=0";
        init_args[argc++] =
            (char *)"systemd.legacy_systemd_cgroup_controller=1";
      }
    }
    /* statfs failure → leave systemd to probe on its own */
  }

  init_args[argc] = NULL;

  if (execve(init_bin, init_args, environ) < 0) {
    ds_error("Failed to execute %s: %s", init_bin, strerror(errno));
    ds_die("Container boot failed. Please ensure the rootfs path is correct "
           "and contains a valid %s binary.",
           init_bin);
  }

boot_fail:
  ds_close_container_log();
  return -1;
}
