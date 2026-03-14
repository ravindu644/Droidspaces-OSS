/*
 * Droidspaces v5 — High-performance Container Runtime
 *
 * Network configuration: DNS, host-side setup, rootfs-side setup,
 * veth pair management, and network cleanup.
 *
 * All link/addr/route management uses the pure-C RTNETLINK API
 * (ds_netlink.c). All iptables management uses the raw socket API
 * (ds_iptables.c). No external binary dependencies for core networking.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <arpa/inet.h>
#include <fnmatch.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------*/

/* Derive the host-side veth name from a container init PID */
static void veth_host_name(pid_t pid, char *buf, size_t sz) {
  snprintf(buf, sz, "ds-v%d", (int)(pid % 100000));
}

/* Derive the peer (container-side) veth name from a container init PID */
static void veth_peer_name(pid_t pid, char *buf, size_t sz) {
  snprintf(buf, sz, "ds-p%d", (int)(pid % 100000));
}

/* Derive a deterministic IP from a PID (avoids sequential collisions) */
static void veth_peer_ip(pid_t pid, char *buf, size_t sz) {
  /* Multiplicative hash to spread sequential PIDs across the /16 subnet.
   *
   * The /16 space gives us 256 third-octets (172.28.x.y) each with 254
   * usable host addresses, for 65534 total.
   *
   * octet3: 0–255, but we skip 0 (network row) → range 1–254 (254 rows)
   * octet4: 0–255, but we skip 0 (net) and 255 (bcast) → range 1–254
   *
   * We also reserve 172.28.0.x entirely for gateway/infrastructure:
   * octet3 starts at 1 so the first container gets 172.28.1.x, keeping
   * 172.28.0.1 (DS_NAT_GW_IP) unambiguously the gateway in every row. */
  uint32_t hash = (uint32_t)pid;
  hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
  int octet3 = (int)(((hash >> 8) % 254) + 1);
  int octet4 = (int)((hash % 254) + 1);
  snprintf(buf, sz, "172.28.%d.%d/%d", octet3, octet4, DS_NAT_PREFIX);
}

/* ---------------------------------------------------------------------------
 * Upstream routing globals — shared by android routing setup and monitor
 * ---------------------------------------------------------------------------*/

static char g_upstream_ifaces[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];
static int g_upstream_count = 0;
static int g_current_gw_table = 0;
static pthread_mutex_t g_gw_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_route_monitor_sock = -1;
static volatile sig_atomic_t g_stop_monitor = 0;

/* Returns 1 if ifname exists and is both UP and RUNNING.
 * On Android, the active data interface has IFF_RUNNING set; an interface
 * that is physically present but not carrying data loses IFF_RUNNING. */
static int iface_is_running(const char *ifname) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return 0;
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  safe_strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
  int ret = 0;
  if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
    ret = (ifr.ifr_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING);
  close(fd);
  return ret;
}

/* Returns 1 if `pattern` contains a glob wildcard character ('*' or '?').
 * Used to decide whether fnmatch() or strcmp() is appropriate. */
static int is_wildcard_pattern(const char *pattern) {
  return strchr(pattern, '*') != NULL || strchr(pattern, '?') != NULL;
}

/* Returns 1 if `iface` matches `pattern`.
 * If pattern is a plain name, falls back to strcmp for speed. */
static int iface_matches_pattern(const char *pattern, const char *iface) {
  if (is_wildcard_pattern(pattern))
    return fnmatch(pattern, iface, 0) == 0;
  return strcmp(pattern, iface) == 0;
}

/* ---------------------------------------------------------------------------
 * Public helper: populate a ds_net_handshake from a container init PID
 * ---------------------------------------------------------------------------*/

void ds_net_derive_handshake(pid_t init_pid, struct ds_net_handshake *hs) {
  veth_peer_name(init_pid, hs->peer_name, sizeof(hs->peer_name));
  veth_peer_ip(init_pid, hs->ip_str, sizeof(hs->ip_str));
}

/* ---------------------------------------------------------------------------
 * Host-side networking setup (before container boot)
 * ---------------------------------------------------------------------------*/

int ds_get_dns_servers(const char *custom_dns, char *out, size_t size) {
  out[0] = '\0';
  int count = 0;

  /* 0. Try custom DNS if provided */
  if (custom_dns && custom_dns[0]) {
    char buf[1024];
    safe_strncpy(buf, custom_dns, sizeof(buf));
    char *saveptr;
    char *token = strtok_r(buf, ", ", &saveptr);
    while (token && (size_t)strlen(out) < size - 32) {
      char line[128];
      snprintf(line, sizeof(line), "nameserver %s\n", token);
      size_t current_len = strlen(out);
      snprintf(out + current_len, size - current_len, "%s", line);
      count++;
      token = strtok_r(NULL, ", ", &saveptr);
    }
  }

  /* 1. Global stable fallbacks (defined in droidspace.h) */
  if (count == 0) {
    int n = snprintf(out, size, "nameserver %s\nnameserver %s\n",
                     DS_DNS_DEFAULT_1, DS_DNS_DEFAULT_2);
    if (n > 0 && (size_t)n < size)
      count = 2;
  }

  return count;
}

int fix_networking_host(struct ds_config *cfg) {
  ds_log("Configuring host-side networking for %s...", cfg->container_name);

  /* Enable IPv4 forwarding */
  write_file("/proc/sys/net/ipv4/ip_forward", "1");

  /* Re-enable IPv6 globally unless the user disabled it.
   * No-op on fresh boots where IPv6 was never disabled. */
  if (!cfg->disable_ipv6) {
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "0");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "0");
  }

  /* Get DNS and store it in the config struct to be used after pivot_root */
  cfg->dns_server_content[0] = '\0';
  int count = ds_get_dns_servers(cfg->dns_servers, cfg->dns_server_content,
                                 sizeof(cfg->dns_server_content));

  if (cfg->dns_servers[0])
    ds_log("Setting up %d custom DNS servers...", count);

  return 0;
}

/* ---------------------------------------------------------------------------
 * Android-specific policy routing
 *
 * Iterates the user-declared upstream interfaces in priority order, finds the
 * first one that is RUNNING and has an IPv4 default route, then injects
 * low-priority ip rules to direct container traffic through that table.
 *
 * This replaces the old auto-detection approach which was unreliable on
 * Android MTK/Qualcomm because both wlan0 and mobile-data interfaces can
 * have simultaneous default routes in per-interface tables; only the policy
 * rules distinguish which is active, and parsing those rules was fragile.
 * ---------------------------------------------------------------------------*/

/* Forward declaration — defined later in this file after route monitor globals
 */
static int find_active_upstream(ds_nl_ctx_t *ctx, char *iface_out,
                                int *table_out);

static void ds_net_setup_android_routing(ds_nl_ctx_t *ctx,
                                         const char ifaces[][IFNAMSIZ],
                                         int iface_count) {
  /* Temporarily populate the globals so find_active_upstream() can use them.
   * setup_veth_host_side() copies these right after this call anyway. */
  for (int _i = 0; _i < iface_count && _i < DS_MAX_UPSTREAM_IFACES; _i++)
    safe_strncpy(g_upstream_ifaces[_i], ifaces[_i], IFNAMSIZ);
  g_upstream_count = iface_count;

  char active_iface[IFNAMSIZ] = {0};
  int gw_table = 0;
  find_active_upstream(ctx, active_iface, &gw_table);

  uint32_t subnet_be, mask_be;
  parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
  uint8_t prefix = DS_NAT_PREFIX;

  /* DS_RULE_PRIO_TO_SUBNET (6090): inbound traffic to our subnet always
   * resolves via main table.  Install this even if no upstream is active
   * yet - the monitor will handle the FROM rule once an interface comes up.
   *
   * Priority 6090 is:
   *   • above Android's VPN rule range (10000–22000) -> checked FIRST, so
   *     reply-to-container traffic is never hijacked by a VPN's catch-all rule
   *   • above OEM reserved low-priority rules (typically < 1000) */
  int ret = ds_nl_add_rule4(ctx, 0, 0, subnet_be, prefix, RT_TABLE_MAIN,
                            DS_RULE_PRIO_TO_SUBNET);
  if (ret < 0)
    ds_warn("[NET] Android routing: failed to add 'to subnet' rule (%d)",
            DS_RULE_PRIO_TO_SUBNET);

  if (!active_iface[0]) {
    ds_warn("[NET] Android routing: no upstream interface is active yet — "
            "route monitor will install rule when one comes up");
    return;
  }

  ds_log("[NET] Android routing: active upstream %s → table %d", active_iface,
         gw_table);

  /* DS_RULE_PRIO_FROM_SUBNET (6100): traffic from our subnet → upstream
   * internet table.  Also above Android's VPN range so container-originated
   * traffic always routes through the physical uplink, not through any VPN
   * tunnel (the container has its own isolation layer). */
  ret = ds_nl_add_rule4(ctx, subnet_be, prefix, 0, 0, gw_table,
                        DS_RULE_PRIO_FROM_SUBNET);
  if (ret == 0) {
    ds_log("[NET] Android routing: rule from %s lookup table %d (prio %d)",
           DS_DEFAULT_SUBNET, gw_table, DS_RULE_PRIO_FROM_SUBNET);
    /* Seed the monitor's current table so it knows the baseline */
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = gw_table;
    pthread_mutex_unlock(&g_gw_mutex);
  } else {
    ds_warn("[NET] Android routing: ds_nl_add_rule4 failed (ret=%d)", ret);
  }
}

/* ---------------------------------------------------------------------------
 * TX checksum disable (Samsung/MTK kernel workaround)
 * ---------------------------------------------------------------------------*/

int ds_net_disable_tx_checksum(const char *ifname) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return -errno;

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  safe_strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

  struct ethtool_value eval;
  eval.cmd = ETHTOOL_STXCSUM;
  eval.data = 0; /* Disable */
  ifr.ifr_data = (caddr_t)&eval;

  int ret = ioctl(fd, SIOCETHTOOL, &ifr);
  close(fd);
  return (ret < 0) ? -errno : 0;
}

/* ---------------------------------------------------------------------------
 * setup_veth_host_side
 *
 * Called from the Monitor process AFTER receiving the "ready" signal from the
 * container init (via net_ready_pipe).
 *
 * Steps:
 *   1. Create or reuse bridge ds-br0 with IP 172.28.0.1/16
 *   2. iptables: MASQUERADE + FORWARD ACCEPT + INPUT ACCEPT + MSS clamp
 *   3. Create veth pair (ds-vXXXXX / ds-pXXXXX)
 *   4. Disable TX checksum on host veth (Samsung/MTK workaround)
 *   5. Attach host veth to bridge, bring up
 *   6. Move peer veth into container's network namespace
 *   7. Android policy routing
 * ---------------------------------------------------------------------------*/

int setup_veth_host_side(struct ds_config *cfg, pid_t child_pid) {
  char veth_host[IFNAMSIZ], veth_peer[IFNAMSIZ];
  veth_host_name(child_pid, veth_host, sizeof(veth_host));
  veth_peer_name(child_pid, veth_peer, sizeof(veth_peer));

  ds_log("Setting up host-side NAT networking for %s (PID %d)...",
         cfg->container_name, (int)child_pid);

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[NET] Failed to open RTNETLINK socket");
    return -1;
  }

  /* Clean up stale interfaces from previous runs */
  ds_log("[DEBUG] Cleaning up any stale interfaces: %s, %s", veth_host,
         veth_peer);
  ds_nl_del_link(ctx, veth_host);

  /* 1. Ensure bridge exists (SKIP for bridgeless fallback) */
  if (!cfg->net_bridgeless) {
    if (!ds_nl_link_exists(ctx, DS_NAT_BRIDGE)) {
      ds_log("[DEBUG] Creating bridge %s...", DS_NAT_BRIDGE);
      if (ds_nl_create_bridge(ctx, DS_NAT_BRIDGE) < 0)
        ds_warn("[DEBUG] Failed to create bridge %s", DS_NAT_BRIDGE);

      if (ds_nl_add_addr4(ctx, DS_NAT_BRIDGE, inet_addr(DS_NAT_GW_IP),
                          DS_NAT_PREFIX) < 0)
        ds_warn("[DEBUG] Failed to add IP to %s", DS_NAT_BRIDGE);

      if (ds_nl_link_up(ctx, DS_NAT_BRIDGE) < 0)
        ds_warn("[DEBUG] Failed to bring up %s", DS_NAT_BRIDGE);

      /* Disable ICMP redirects on the bridge. */
      write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/accept_redirects",
                 "0");
      write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/send_redirects",
                 "0");
    } else {
      ds_log("[DEBUG] Bridge %s already exists.", DS_NAT_BRIDGE);
      write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/accept_redirects",
                 "0");
      write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/send_redirects",
                 "0");
    }
  } else {
    ds_log("[NET] Bridgeless Fallback: skipping bridge creation.");
  }

  /* Late-stage hardening: sysctl for bridge */
  if (cfg->net_mode == DS_NET_NAT) {
    ds_log("[DEBUG] Applying late-stage hardening for Android NAT...");
    if (!cfg->net_bridgeless) {
      write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/rp_filter", "0");
      if (access("/proc/sys/net/bridge", F_OK) == 0) {
        write_file("/proc/sys/net/bridge/bridge-nf-call-iptables", "0");
        write_file("/proc/sys/net/bridge/bridge-nf-call-ip6tables", "0");
      }
      ds_ipt_ensure_input_accept(DS_NAT_BRIDGE);
    } else {
      write_file("/proc/sys/net/ipv4/conf/all/rp_filter", "0");
      write_file("/proc/sys/net/ipv4/conf/default/rp_filter", "0");
      /* In bridgeless mode, we must accept input from the veth itself */
      ds_ipt_ensure_input_accept(veth_host);
    }
  }

  /* 2. iptables rules */
  if (ds_ipt_ensure_masquerade(DS_DEFAULT_SUBNET) < 0)
    ds_warn("[NET] MASQUERADE rule failed");
  if (!cfg->net_bridgeless) {
    if (ds_ipt_ensure_forward_accept(DS_NAT_BRIDGE) < 0)
      ds_warn("[NET] FORWARD ACCEPT failed");
  } else {
    if (ds_ipt_ensure_forward_accept(veth_host) < 0)
      ds_warn("[NET] FORWARD ACCEPT failed");
  }
  ds_ipt_ensure_mss_clamp();

  /* 3. Create veth pair */
  ds_log("[DEBUG] Creating veth pair %s <-> %s...", veth_host, veth_peer);
  if (ds_nl_create_veth(ctx, veth_host, veth_peer) < 0) {
    ds_warn("[NET] Failed to create veth pair (%s, %s)", veth_host, veth_peer);
    ds_nl_close(ctx);
    return -1;
  }

  /* 4. Disable TX checksum on host veth */
  ds_net_disable_tx_checksum(veth_host);

  /* 5. Set master or assign IP directly for PTP */
  if (!cfg->net_bridgeless) {
    if (ds_nl_set_master(ctx, veth_host, DS_NAT_BRIDGE) < 0)
      ds_warn("[NET] Failed to attach %s to %s", veth_host, DS_NAT_BRIDGE);
  } else {
    /* Bridgeless Fallback: Assign GW IP to veth_host directly */
    if (ds_nl_add_addr4(ctx, veth_host, inet_addr(DS_NAT_GW_IP), 32) < 0)
      ds_warn("[NET] Bridgeless: Failed to add IP to %s", veth_host);

    /* Interface must be UP before routes can be added on some kernels */
    if (ds_nl_link_up(ctx, veth_host) < 0)
      ds_warn("[NET] Failed to bring up %s", veth_host);

    /* Add route for container IP to this veth */
    char peer_ip_cidr[32];
    veth_peer_ip(child_pid, peer_ip_cidr, sizeof(peer_ip_cidr));
    uint32_t peer_ip, peer_mask;
    parse_cidr(peer_ip_cidr, &peer_ip, &peer_mask);

    if (ds_nl_add_route4(ctx, peer_ip, 32, 0,
                         ds_nl_get_ifindex(ctx, veth_host)) < 0)
      ds_warn("[NET] Bridgeless: Failed to add route for %s", peer_ip_cidr);
  }

  /* Ensure veth_host is UP (redundant if bridgeless but safe) */
  if (ds_nl_link_up(ctx, veth_host) < 0)
    ds_warn("[NET] Failed to bring up %s", veth_host);

  /* Disable ICMP redirects on the host veth. */
  {
    char sysctl_path[128];
    snprintf(sysctl_path, sizeof(sysctl_path),
             "/proc/sys/net/ipv4/conf/%s/accept_redirects", veth_host);
    write_file(sysctl_path, "0");
  }

  /* 6. Move peer veth into container's network namespace */
  char netns_path[PATH_MAX];
  snprintf(netns_path, sizeof(netns_path), "/proc/%d/ns/net", child_pid);

  /* No retry loop needed; init has already signaled readiness */
  int netns_fd = open(netns_path, O_RDONLY | O_CLOEXEC);
  if (netns_fd < 0) {
    ds_warn("[NET] Failed to open container netns %s: %s", netns_path,
            strerror(errno));
    ds_nl_close(ctx);
    return -1;
  }

  /* Read peer MAC now — after ds_nl_move_to_netns the interface is inside the
   * container netns and invisible to host-side ioctl(SIOCGIFHWADDR). */
  uint8_t peer_mac[6] = {0};
  {
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
      memset(&ifr, 0, sizeof(ifr));
      strncpy(ifr.ifr_name, veth_peer, IFNAMSIZ - 1);
      if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0)
        memcpy(peer_mac, ifr.ifr_hwaddr.sa_data, 6);
      else
        ds_warn("Failed to read peer MAC for %s: %s", veth_peer,
                strerror(errno));
      close(fd);
    }
  }

  ds_log("[DEBUG] Moving %s into netns of PID %d using FD %d...", veth_peer,
         (int)child_pid, netns_fd);
  int r = ds_nl_move_to_netns(ctx, veth_peer, netns_fd);
  close(netns_fd);

  if (r < 0) {
    ds_warn("[NET] Failed to move %s into container netns (ret=%d)", veth_peer,
            r);
    ds_nl_close(ctx);
    return -1;
  }
  ds_log("[DEBUG] Successfully moved %s to PID %d", veth_peer, (int)child_pid);

  /* 7. Android policy routing — uses the user-declared upstream interfaces */
  if (is_android()) {
    ds_net_setup_android_routing(ctx,
                                 (const char (*)[IFNAMSIZ])cfg->upstream_ifaces,
                                 cfg->upstream_iface_count);
  }

  /* Cache the upstream list so ds_net_start_route_monitor() can access it.
   * setup_veth_host_side() always runs before ds_net_start_route_monitor(). */
  for (int _i = 0;
       _i < cfg->upstream_iface_count && _i < DS_MAX_UPSTREAM_IFACES; _i++)
    safe_strncpy(g_upstream_ifaces[_i], cfg->upstream_ifaces[_i], IFNAMSIZ);
  g_upstream_count = cfg->upstream_iface_count;

  ds_nl_close(ctx);

  /* 8. Start embedded DHCP server so the container's DHCP client acquires
   * the same deterministic IP that veth_peer_ip() computed for routing.
   *
   * Binding interface depends on topology:
   *   Bridge mode    — bind to ds-br0.  veth_host is a bridge slave; the
   *                    kernel delivers frames from the container upward to
   *                    the bridge interface, not the slave.  A socket bound
   *                    to the slave never sees the DHCP DISCOVERs.
   *   Bridgeless mode — bind to veth_host directly (point-to-point veth,
   *                    no bridge in the path). */
  {
    char peer_ip_cidr[32];
    veth_peer_ip(child_pid, peer_ip_cidr, sizeof(peer_ip_cidr));
    uint32_t offer_ip = 0, dummy_mask = 0;
    parse_cidr(peer_ip_cidr, &offer_ip, &dummy_mask);
    const char *dhcp_iface = cfg->net_bridgeless ? veth_host : DS_NAT_BRIDGE;
    ds_dhcp_server_start(cfg, dhcp_iface, offer_ip, inet_addr(DS_NAT_GW_IP),
                         peer_mac);

    /* Store the container IP string for port forward cleanup later */
    struct in_addr offer_addr;
    offer_addr.s_addr = offer_ip;
    inet_ntop(AF_INET, &offer_addr, cfg->nat_container_ip,
              sizeof(cfg->nat_container_ip));

    /* Install DNAT + FORWARD rules for any --port mappings */
    if (cfg->port_forward_count > 0)
      ds_ipt_add_portforwards(cfg, cfg->nat_container_ip);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * setup_veth_child_side_named
 *
 * Called from internal_boot() INSIDE the container's new network namespace.
 * ---------------------------------------------------------------------------*/

int setup_veth_child_side_named(struct ds_config *cfg, const char *peer_name,
                                const char *ip_str) {
  (void)cfg;
  (void)ip_str; /* IP is now assigned by the container's own DHCP client */
  ds_log("[DEBUG] Child: Configuring isolated networking. Local PID: %d, "
         "Peer: %s",
         (int)getpid(), peer_name ? peer_name : "(null)");

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[DEBUG] Child: Failed to open netlink socket");
    return -1;
  }

  /* 0. Rename interface to eth0 */
  if (peer_name && peer_name[0] && strcmp(peer_name, "eth0") != 0) {
    ds_log("[DEBUG] Renaming %s to eth0...", peer_name);
    if (ds_nl_rename(ctx, peer_name, "eth0") < 0)
      ds_warn("[DEBUG] Failed to rename %s to eth0.", peer_name);
  }

  /* 1. Loopback */
  ds_nl_link_up(ctx, "lo");

  /* 2. Bring eth0 UP — the container's DHCP client configures IP and route */
  ds_nl_link_up(ctx, "eth0");

  ds_nl_close(ctx);
  ds_log("[NET] Child: eth0 UP — awaiting DHCP lease from monitor");
  return 0;
}

/* Compatibility wrapper */

/* ---------------------------------------------------------------------------
 * Rootfs-side networking setup (inside container, after pivot_root)
 * ---------------------------------------------------------------------------*/

int fix_networking_rootfs(struct ds_config *cfg) {
  /* 1. Hostname */
  if (cfg->hostname[0]) {
    if (sethostname(cfg->hostname, strlen(cfg->hostname)) < 0) {
      ds_warn("Failed to set hostname to %s: %s", cfg->hostname,
              strerror(errno));
    }
    /* Persist to /etc/hostname */
    char hn_buf[256 + 2];
    snprintf(hn_buf, sizeof(hn_buf), "%.256s\n", cfg->hostname);
    write_file("/etc/hostname", hn_buf);
  }

  /* 2. /etc/hosts */
  char hosts_content[1024];
  const char *hostname = (cfg->hostname[0]) ? cfg->hostname : "localhost";

  /* Only strip IPv6 hosts entries when IPv6 is explicitly disabled */
  if (!cfg->disable_ipv6 || cfg->net_mode != DS_NET_NAT) {
    snprintf(hosts_content, sizeof(hosts_content),
             "127.0.0.1\tlocalhost\n"
             "127.0.1.1\t%s\n"
             "::1\t\tlocalhost ip6-localhost ip6-loopback\n"
             "ff02::1\t\tip6-allnodes\n"
             "ff02::2\t\tip6-allrouters\n",
             hostname);
  } else {
    snprintf(hosts_content, sizeof(hosts_content),
             "127.0.0.1\tlocalhost\n"
             "127.0.1.1\t%s\n",
             hostname);
  }

  write_file("/etc/hosts", hosts_content);

  /* 3. resolv.conf (from in-memory config passed via cfg struct) */
  mkdir("/run/resolvconf", 0755);
  if (cfg->dns_server_content[0]) {
    write_file("/run/resolvconf/resolv.conf", cfg->dns_server_content);
  } else {
    /* Fallback if DNS content is empty */
    char dns_fallback[256];
    snprintf(dns_fallback, sizeof(dns_fallback),
             "nameserver %s\nnameserver %s\n", DS_DNS_DEFAULT_1,
             DS_DNS_DEFAULT_2);
    write_file("/run/resolvconf/resolv.conf", dns_fallback);
  }

  /* Link /etc/resolv.conf */
  unlink("/etc/resolv.conf");
  if (symlink("/run/resolvconf/resolv.conf", "/etc/resolv.conf") < 0) {
    ds_warn("Failed to link /etc/resolv.conf: %s", strerror(errno));
  }

  if (cfg->disable_ipv6 && cfg->net_mode != DS_NET_HOST) {
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "1");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "1");
  } else if (cfg->disable_ipv6 && cfg->net_mode == DS_NET_HOST) {
    /* In host mode, disabling IPv6 affects the host's netns. Warn and apply. */
    ds_warn("--disable-ipv6 in host mode disables IPv6 on the host "
            "network namespace.");
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "1");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "1");
  }

  /* 5. unprivileged ICMP sockets: new network namespaces reset
   * ping_group_range to "1 0". Allow all GIDs so ping works without
   * CAP_NET_RAW. */
  write_file("/proc/sys/net/ipv4/ping_group_range", "0 2147483647");

  /* 6. Android Network Groups */
  if (is_android()) {
    const char *etc_group = "/etc/group";
    if (access(etc_group, F_OK) == 0) {
      if (!grep_file(etc_group, "aid_inet")) {
        FILE *fg = fopen(etc_group, "ae");
        if (fg) {
          fprintf(
              fg,
              "aid_inet:x:3003:\naid_net_raw:x:3004:\naid_net_admin:x:3005:\n");
          fclose(fg);
        }
      }
    }

    /* Add root to groups if usermod exists */
    if (access("/usr/sbin/usermod", X_OK) == 0 ||
        access("/sbin/usermod", X_OK) == 0) {
      if (!grep_file("/etc/group", "aid_inet:x:3003:root") &&
          !grep_file("/etc/group", "aid_inet:*:3003:root")) {
        char *args[] = {"usermod", "-a", "-G", "aid_inet,aid_net_raw",
                        "root",    NULL};
        run_command_quiet(args);
      }
    }
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Runtime introspection
 * ---------------------------------------------------------------------------*/

int detect_ipv6_in_container(pid_t pid) {
  char path[PATH_MAX];
  build_proc_root_path(pid, "/proc/sys/net/ipv6/conf/all/disable_ipv6", path,
                       sizeof(path));

  char buf[16];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;

  /* 0 means enabled, 1 means disabled */
  return (buf[0] == '0') ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Upstream Route Monitor
 *
 * Watches LINK state and IPv4 address changes on the user-declared upstream
 * interfaces. When any change is detected that involves one of those
 * interfaces, it re-scans to find which is currently active (RUNNING + has
 * a default route) and atomically updates the policy rule.
 *
 * Event triggers:
 *   RTM_NEWLINK / RTM_DELLINK — interface state change (UP/RUNNING/DOWN)
 *   RTM_NEWADDR / RTM_DELADDR — IPv4 address assigned or removed
 *
 * 30-second heartbeat covers devices with broken netlink notifications.
 * ---------------------------------------------------------------------------*/

/* Scan upstream interfaces in priority order; return the first that is
 * RUNNING and has an IPv4 default route in any table.
 *
 * Entries without wildcards are checked directly (fast path).
 * Entries containing '*' or '?' are expanded via a full RTM_GETLINK dump
 * and matched with fnmatch() — this handles dynamic interface names like
 * "*rmnet_data*" or "v4-rmnet_data*" on Qualcomm/CLAT devices where the
 * interface number changes on every reboot.
 *
 * Each wildcard pattern slot remembers the interface it last resolved to.
 * A discovery log fires only when the resolved name changes — this handles
 * cleanup (dead interfaces are overwritten), prevents log spam on heartbeat
 * reprobes, and uses zero dynamic allocation. */

/* Per-pattern "last wildcard match" tracker.  Index matches g_upstream_ifaces.
 * An empty string means "never matched yet". */
static char g_last_wildcard_match[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];

static int find_active_upstream(ds_nl_ctx_t *ctx, char *iface_out,
                                int *table_out) {
  for (int i = 0; i < g_upstream_count; i++) {
    const char *pattern = g_upstream_ifaces[i];

    if (!is_wildcard_pattern(pattern)) {
      /* Fast path: literal name */
      if (!iface_is_running(pattern))
        continue;
      int tbl = 0;
      if (ds_nl_get_iface_table(ctx, pattern, &tbl) == 0) {
        if (iface_out)
          safe_strncpy(iface_out, pattern, IFNAMSIZ);
        if (table_out)
          *table_out = tbl;
        return 0;
      }
    } else {
      /* Wildcard path: enumerate all real interfaces and fnmatch */
      char all_ifaces[64][IFNAMSIZ];
      int all_count = ds_nl_list_ifaces(ctx, all_ifaces, 64);
      for (int j = 0; j < all_count; j++) {
        if (!iface_matches_pattern(pattern, all_ifaces[j]))
          continue;
        /* Skip our own bridge/veth and loopback */
        if (strncmp(all_ifaces[j], "ds-", 3) == 0)
          continue;
        if (strcmp(all_ifaces[j], "lo") == 0)
          continue;
        if (!iface_is_running(all_ifaces[j]))
          continue;
        int tbl = 0;
        if (ds_nl_get_iface_table(ctx, all_ifaces[j], &tbl) == 0) {
          /* Log only when the resolved interface changes for this pattern */
          if (strcmp(g_last_wildcard_match[i], all_ifaces[j]) != 0) {
            ds_log("[NET] Wildcard '%s' matched active interface '%s' "
                   "(table %d)",
                   pattern, all_ifaces[j], tbl);
            safe_strncpy(g_last_wildcard_match[i], all_ifaces[j], IFNAMSIZ);
          }
          if (iface_out)
            safe_strncpy(iface_out, all_ifaces[j], IFNAMSIZ);
          if (table_out)
            *table_out = tbl;
          return 0;
        }
      }
    }
  }
  return -ENOENT;
}

/* Re-probe which upstream is active and update the ip rule if needed. */
static void do_upstream_reprobe(void) {
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return;

  char new_iface[IFNAMSIZ] = {0};
  int new_table = 0;

  if (find_active_upstream(ctx, new_iface, &new_table) != 0) {
    /* No upstream active yet — leave current rule in place */
    ds_nl_close(ctx);
    return;
  }

  pthread_mutex_lock(&g_gw_mutex);
  int old_table = g_current_gw_table;
  pthread_mutex_unlock(&g_gw_mutex);

  if (new_table == old_table) {
    ds_nl_close(ctx);
    return;
  }

  ds_log("[NET] Route monitor: upstream switch table %d → %d (%s)", old_table,
         new_table, new_iface);

  uint32_t subnet_be, mask_be;
  parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
  (void)mask_be;

  if (old_table > 0)
    ds_nl_del_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, old_table,
                    DS_RULE_PRIO_FROM_SUBNET);

  if (ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, new_table,
                      DS_RULE_PRIO_FROM_SUBNET) == 0) {
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = new_table;
    pthread_mutex_unlock(&g_gw_mutex);
    ds_log("[NET] Route monitor: rule updated → from %s lookup %d (prio %d)",
           DS_DEFAULT_SUBNET, new_table, DS_RULE_PRIO_FROM_SUBNET);

    /* Android's netd aggressively disables ip_forward when interfaces go down
     * or when tethering is halted. Re-enable it whenever we find a valid active
     * upstream to prevent the container's internet from falling into a black
     * hole.
     */
    if (is_android()) {
      write_file("/proc/sys/net/ipv4/ip_forward", "1");
    }
  } else {
    ds_warn("[NET] Route monitor: failed to install new rule for table %d",
            new_table);
  }

  ds_nl_close(ctx);
}

static void *route_monitor_loop(void *arg) {
  (void)arg;

  /* Build a comma-separated list for the log line */
  /* DS_MAX_UPSTREAM_IFACES * (IFNAMSIZ + 1 for comma) + NUL */
  char iface_list[DS_MAX_UPSTREAM_IFACES * (IFNAMSIZ + 1) + 1];
  memset(iface_list, 0, sizeof(iface_list));
  for (int i = 0; i < g_upstream_count; i++) {
    if (i > 0)
      strncat(iface_list, ",", sizeof(iface_list) - strlen(iface_list) - 1);
    strncat(iface_list, g_upstream_ifaces[i],
            sizeof(iface_list) - strlen(iface_list) - 1);
  }
  ds_log("[NET] Upstream route monitor started (interfaces: %s)", iface_list);

  int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0) {
    ds_warn("[NET] Route monitor: failed to open netlink socket: %s",
            strerror(errno));
    return NULL;
  }

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  /* RTMGRP_LINK     — interface state changes (IFF_RUNNING, link up/down)
   * RTMGRP_IPV4_IFADDR — IPv4 address add/remove on upstream interfaces */
  sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;

  if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    ds_warn("[NET] Route monitor: failed to bind netlink socket: %s",
            strerror(errno));
    close(sock);
    return NULL;
  }

  pthread_mutex_lock(&g_gw_mutex);
  g_route_monitor_sock = sock;
  pthread_mutex_unlock(&g_gw_mutex);

  uint8_t buf[8192];
  struct pollfd pfd = {.fd = sock, .events = POLLIN};

  while (!g_stop_monitor) {
    /* Enforce IPv4 forwarding in real-time. Since Android kernels do not
     * broadcast POLLERR/inotify events for /proc/sys/ memory variables,
     * we must check it periodically. Reading a 1-byte procfs memory flag
     * takes < 1 microsecond, costing 0% CPU. */
    if (is_android() && g_current_gw_table > 0) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv4/ip_forward", val, sizeof(val)) > 0 &&
          val[0] == '0') {
        ds_log("[NET] Route monitor: ip_forward was disabled by Android, "
               "re-enabling...");
        write_file("/proc/sys/net/ipv4/ip_forward", "1\n");
      }
    }

    /* 1.5-second heartbeat: aggressively re-asserts ip_forward and covers
     * devices with broken netlink notifications. */
    int pr = poll(&pfd, 1, 1500);
    if (pr < 0) {
      if (g_stop_monitor)
        break;
      if (errno == EINTR)
        continue;
      break;
    }

    if (pr == 0) {
      do_upstream_reprobe();
      continue;
    }

    ssize_t len = recv(sock, buf, sizeof(buf), 0);
    if (len <= 0) {
      if (g_stop_monitor)
        break;
      if (len < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      break;
    }

    int should_reprobe = 0;
    struct nlmsghdr *h = (struct nlmsghdr *)buf;

    for (; NLMSG_OK(h, (uint32_t)len); h = NLMSG_NEXT(h, len)) {
      if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
        break;

      if (h->nlmsg_type == RTM_NEWLINK || h->nlmsg_type == RTM_DELLINK) {
        /* Filter: care about events on declared upstream interfaces or any
         * interface matching a wildcard pattern (e.g. "*rmnet_data*").
         * A new rmnet_dataX popping up mid-session triggers a reprobe so
         * the monitor can adopt the newly-active interface immediately. */
        struct ifinfomsg *ifi = NLMSG_DATA(h);
        char evname[IFNAMSIZ] = {0};
        if_indextoname((unsigned int)ifi->ifi_index, evname);
        if (evname[0] && strncmp(evname, "ds-", 3) != 0) {
          for (int i = 0; i < g_upstream_count; i++) {
            if (iface_matches_pattern(g_upstream_ifaces[i], evname)) {
              should_reprobe = 1;
              break;
            }
          }
        }
      } else if (h->nlmsg_type == RTM_NEWADDR || h->nlmsg_type == RTM_DELADDR) {
        struct ifaddrmsg *ifa = NLMSG_DATA(h);
        if (ifa->ifa_family == AF_INET) {
          char evname[IFNAMSIZ] = {0};
          if_indextoname((unsigned int)ifa->ifa_index, evname);
          if (evname[0] && strncmp(evname, "ds-", 3) != 0) {
            for (int i = 0; i < g_upstream_count; i++) {
              if (iface_matches_pattern(g_upstream_ifaces[i], evname)) {
                should_reprobe = 1;
                break;
              }
            }
          }
        }
      }

      if (should_reprobe)
        break;
    }

    if (should_reprobe)
      do_upstream_reprobe();
  }

  pthread_mutex_lock(&g_gw_mutex);
  close(sock);
  g_route_monitor_sock = -1;
  pthread_mutex_unlock(&g_gw_mutex);

  ds_log("[NET] Upstream route monitor stopped");
  return NULL;
}

void ds_net_stop_route_monitor(void) {
  g_stop_monitor = 1;
  pthread_mutex_lock(&g_gw_mutex);
  if (g_route_monitor_sock >= 0)
    shutdown(g_route_monitor_sock, SHUT_RDWR);
  pthread_mutex_unlock(&g_gw_mutex);
}

void ds_net_start_route_monitor(void) {
  if (!is_android())
    return;

  if (g_upstream_count == 0) {
    ds_warn("[NET] Route monitor: no upstream interfaces defined, skipping");
    return;
  }

  g_stop_monitor = 0;

  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&tid, &attr, route_monitor_loop, NULL) != 0)
    ds_warn("[NET] Failed to start route monitor thread: %s", strerror(errno));

  pthread_attr_destroy(&attr);
}

/* ---------------------------------------------------------------------------
 * Network cleanup (called on container stop)
 * ---------------------------------------------------------------------------*/

void ds_net_cleanup(struct ds_config *cfg, pid_t container_pid) {
  if (cfg->net_mode != DS_NET_NAT)
    return;

  ds_net_stop_route_monitor();

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return;

  /* 1. Delete host-side veth — peer in dead netns is already gone */
  char veth_host[IFNAMSIZ] = {0};
  pid_t effective_pid = container_pid > 0 ? container_pid : cfg->container_pid;
  if (effective_pid <= 0) {
    ds_warn("[NET] cleanup: cannot derive veth name — no valid PID");
    /* still proceed with iptables cleanup */
  } else {
    veth_host_name(effective_pid, veth_host, sizeof(veth_host));
    ds_dhcp_server_stop();
    ds_nl_del_link(ctx, veth_host);
  }

  /* Check how many ds-v* veths remain AFTER deleting ours.
   * Shared rules (MASQUERADE, FORWARD, Android policy) must only be removed
   * when we are the last container — removing them while others are running
   * would kill their networking immediately. */
  int surviving = ds_nl_count_ifaces_with_prefix(ctx, "ds-v");
  if (surviving > 0) {
    ds_log("[NET] cleanup: %d other container(s) still running — "
           "keeping shared iptables and routing rules",
           surviving);
    ds_ipt_remove_portforwards(cfg);
    if (cfg->net_bridgeless && veth_host[0] != '\0')
      ds_ipt_remove_iface_rules(veth_host);
    ds_nl_close(ctx);
    return;
  }

  /* 2. Remove Android policy rules (last container — safe to clean up) */
  if (is_android()) {
    uint32_t subnet, mask;
    parse_cidr(DS_DEFAULT_SUBNET, &subnet, &mask);

    /* Remove DS policy rules at both current and legacy priority values so
     * an upgrade from an older build that used hardcoded 90/100/200/201 still
     * cleans up completely.  del_rule4 is idempotent (ENOENT → 0). */
    int prios[] = {
        DS_RULE_PRIO_TO_SUBNET,   /* 6090 - current */
        DS_RULE_PRIO_FROM_SUBNET, /* 6100 - current */
        90,
        100,
        200,
        201 /* legacy - pre-VPN-fix builds */
    };
    for (size_t i = 0; i < sizeof(prios) / sizeof(prios[0]); i++) {
      ds_nl_del_rule4(ctx, 0, 0, subnet, DS_NAT_PREFIX, 0, prios[i]);
      ds_nl_del_rule4(ctx, subnet, DS_NAT_PREFIX, 0, 0, 0, prios[i]);
    }
  }

  ds_nl_close(ctx);

  /* 3. Remove iptables rules */
  if (cfg->net_bridgeless && veth_host[0] != '\0') {
    ds_ipt_remove_iface_rules(veth_host);
  }
  ds_ipt_remove_portforwards(cfg);
  ds_ipt_remove_ds_rules();
}
