/*
 * Droidspaces v6 - High-performance Container Runtime
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
  snprintf(buf, sz, "ds-v%d", (int)pid);
}

/* Derive the peer (container-side) veth name from a container init PID */
static void veth_peer_name(pid_t pid, char *buf, size_t sz) {
  snprintf(buf, sz, "ds-p%d", (int)pid);
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

static uint32_t ds_net_hash_string(const char *s) {
  uint32_t h = 5381;
  if (!s)
    return h;
  while (*s)
    h = ((h << 5) + h) ^ (unsigned char)*s++;
  return h;
}

/* Derive a stable, locally-administered unicast MAC from the container name.
 * The same name always yields the same address, so a gateway (e.g. OpenWrt)
 * sees one persistent host across restarts instead of a fresh random MAC -
 * and therefore one DHCP lease / LuCI entry - every boot. */
static void ds_container_mac(const char *name, uint8_t mac[6]) {
  uint32_t h1 = ds_net_hash_string(name);
  char salted[300];
  snprintf(salted, sizeof(salted), "ds-mac:%s", name ? name : "");
  uint32_t h2 = ds_net_hash_string(salted);
  mac[0] = 0x02; /* locally administered (bit1), unicast (bit0 clear) */
  mac[1] = (uint8_t)(h1 >> 24);
  mac[2] = (uint8_t)(h1 >> 16);
  mac[3] = (uint8_t)(h1 >> 8);
  mac[4] = (uint8_t)(h1);
  mac[5] = (uint8_t)(h2);
}

static void gateway_hash_key(struct ds_config *cfg, char *buf, size_t sz) {
  const char *gw =
      (cfg && cfg->gateway_container[0]) ? cfg->gateway_container : "gateway";
  const char *net = (cfg && cfg->gateway_net[0]) ? cfg->gateway_net : "lan";
  snprintf(buf, sz, "%s:%s", gw, net);
}

static void gateway_veth_names(struct ds_config *cfg, char *host, size_t hsz,
                               char *peer, size_t psz) {
  char key[384];
  gateway_hash_key(cfg, key, sizeof(key));
  uint32_t h = ds_net_hash_string(key);
  snprintf(host, hsz, "ds-g%08x", h);
  snprintf(peer, psz, "ds-h%08x", h);
}

static int gateway_ifname_component_ok(const char *s) {
  if (!s || !s[0])
    return 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (!(isalnum(*p) || *p == '_' || *p == '-'))
      return 0;
  }
  return 1;
}

static void gateway_bridge_name(struct ds_config *cfg, char *buf, size_t sz) {
  if (cfg && cfg->gateway_bridge[0]) {
    safe_strncpy(buf, cfg->gateway_bridge, sz);
    return;
  }

  const char *net = (cfg && cfg->gateway_net[0]) ? cfg->gateway_net : "lan";
  char clean[10] = {0};
  size_t j = 0;
  for (size_t i = 0; net[i] && j < sizeof(clean) - 1; i++) {
    unsigned char c = (unsigned char)net[i];
    if (isalnum(c) || c == '_' || c == '-')
      clean[j++] = (char)c;
  }
  if (j == 0)
    safe_strncpy(clean, "lan", sizeof(clean));
  snprintf(buf, sz, "ds-%s", clean);
}

static const char *gateway_lan_ifname(struct ds_config *cfg) {
  if (cfg && cfg->gateway_lan_ifname[0])
    return cfg->gateway_lan_ifname;
  return "eth1";
}

static int ds_netns_rename_up(const char *netns_path, const char *old_name,
                              const char *new_name) {
  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return -errno;

  int target_fd = open(netns_path, O_RDONLY | O_CLOEXEC);
  if (target_fd < 0) {
    int e = -errno;
    close(self_fd);
    return e;
  }

  int ret = 0;
  if (setns(target_fd, CLONE_NEWNET) < 0) {
    ret = -errno;
    goto out_restore;
  }

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ret = -errno;
    goto out_restore;
  }

  if (new_name && new_name[0] && ds_nl_link_exists(ctx, new_name)) {
    ds_log("[NET] Gateway: interface %s already exists inside gateway",
           new_name);
    ds_nl_link_up(ctx, new_name);
  } else if (old_name && old_name[0] && new_name && new_name[0] &&
             strcmp(old_name, new_name) != 0) {
    if (ds_nl_rename(ctx, old_name, new_name) < 0) {
      ds_warn("[NET] Gateway: failed to rename %s to %s", old_name, new_name);
      ret = -1;
    } else {
      ds_nl_link_up(ctx, new_name);
    }
  } else if (old_name && old_name[0]) {
    ds_nl_link_up(ctx, old_name);
  }

  ds_nl_close(ctx);

out_restore:
  if (setns(self_fd, CLONE_NEWNET) < 0 && ret == 0)
    ret = -errno;
  close(target_fd);
  close(self_fd);
  return ret;
}

/* ---------------------------------------------------------------------------
 * Uplink routing globals - shared by android routing setup and monitor
 * ---------------------------------------------------------------------------*/

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

/* ---------------------------------------------------------------------------
 * Built-in uplink classification (no user configuration)
 *
 * k_uplink_patterns: the only interface families that terminate internet
 * access on Android, highest precedence first - Wi-Fi STA, ethernet
 * adapters, CLAT (464xlat IPv4-over-IPv6), Qualcomm mobile data, MTK
 * mobile data.
 *
 * k_uplink_excludes: interfaces that must NEVER be picked as an uplink.
 * swlan, ap, usb, rndis and ncm devices are created by Android to SHARE
 * its connectivity downstream (hotspot / USB tethering - Android acts as
 * the router, these face clients, not the internet).  tun/ppp are VPN
 * tunnels which container traffic intentionally bypasses (see the
 * DS_RULE_PRIO_* rationale in droidspace.h).  The rest are loopback,
 * placeholders, and our own bridge/veth devices.
 * ---------------------------------------------------------------------------*/

static const char *const k_uplink_patterns[] = {
    "wlan*", "eth*", "v4-*", "rmnet*", "*ccmni*",
};

static const char *const k_uplink_excludes[] = {
    "ds-*",   "lo",   "dummy*", "swlan*", "ap*",  "usb*",
    "rndis*", "ncm*", "p2p*",   "tun*",   "ppp*", "bt-pan",
};

static int uplink_name_excluded(const char *ifname) {
  for (size_t i = 0;
       i < sizeof(k_uplink_excludes) / sizeof(k_uplink_excludes[0]); i++) {
    if (fnmatch(k_uplink_excludes[i], ifname, 0) == 0)
      return 1;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Public helper: populate a ds_net_handshake from a container init PID
 * ---------------------------------------------------------------------------*/

void ds_net_derive_handshake(pid_t init_pid, struct ds_config *cfg,
                             struct ds_net_handshake *hs) {
  veth_peer_name(init_pid, hs->peer_name, sizeof(hs->peer_name));
  /* Use the already-resolved static IP - not the PID-hash fallback.
   * ip_str is informational on the child side (voided in
   * setup_veth_child_side_named) but the boot.c log line prints it,
   * so it should reflect the actual IP the DHCP server will offer. */
  if (cfg && cfg->net_mode == DS_NET_NAT && cfg->static_nat_ip[0])
    safe_strncpy(hs->ip_str, cfg->static_nat_ip, sizeof(hs->ip_str));
  else if (cfg && cfg->net_mode == DS_NET_NAT)
    veth_peer_ip(init_pid, hs->ip_str,
                 sizeof(hs->ip_str)); /* last-resort fallback */
  else
    hs->ip_str[0] = '\0';
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

/* ---------------------------------------------------------------------------
 * Static NAT IP: validation, collision check, and resolution
 * ---------------------------------------------------------------------------*/

int ds_net_validate_static_ip(const char *ip_str, char *errbuf,
                              size_t errsize) {
  if (!ip_str || !ip_str[0]) {
    snprintf(errbuf, errsize, "empty IP string");
    return -1;
  }

  /* Reject CIDR notation - we store plain dotted-decimal */
  if (strchr(ip_str, '/')) {
    snprintf(errbuf, errsize,
             "pass plain IP without prefix length "
             "(e.g. 172.28.5.10, not 172.28.5.10/16)");
    return -1;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip_str, &addr) != 1) {
    snprintf(errbuf, errsize, "not a valid IPv4 address");
    return -1;
  }

  int o1, o2, o3, o4;
  if (sscanf(ip_str, "%d.%d.%d.%d", &o1, &o2, &o3, &o4) != 4) {
    snprintf(errbuf, errsize, "malformed IPv4 address");
    return -1;
  }

  /* Must be inside 172.28.0.0/16 */
  if (o1 != 172 || o2 != 28) {
    snprintf(errbuf, errsize,
             "must be inside the NAT subnet " DS_DEFAULT_SUBNET " (got %s)",
             ip_str);
    return -1;
  }

  /* Octet3: reserve row 0 entirely for gateway/infrastructure (172.28.0.x) */
  if (o3 < 1 || o3 > 254) {
    snprintf(errbuf, errsize,
             "third octet %d out of range - must be 1-254 "
             "(172.28.0.x is reserved for the gateway)",
             o3);
    return -1;
  }

  /* Octet4: exclude network address (0) and broadcast (255) */
  if (o4 < 1 || o4 > 254) {
    snprintf(errbuf, errsize, "fourth octet %d out of range - must be 1-254",
             o4);
    return -1;
  }

  return 0;
}

int ds_net_check_ip_collision(const char *ip_str, const char *exclude_name) {
  char containers_dir[PATH_MAX];
  snprintf(containers_dir, sizeof(containers_dir), "%s/Containers",
           get_workspace_dir());

  /* The directory entries are sanitized names (written by
   * ds_config_save_by_name via sanitize_container_name). Sanitize exclude_name
   * the same way so the self-skip comparison is always apples-to-apples. */
  char safe_exclude[256] = {0};
  if (exclude_name && exclude_name[0])
    sanitize_container_name(exclude_name, safe_exclude, sizeof(safe_exclude));

  DIR *d = opendir(containers_dir);
  if (!d)
    return 0; /* Can't scan → assume unique */

  struct dirent *ent;
  int collision = 0;

  while ((ent = readdir(d)) != NULL && !collision) {
    if (ent->d_name[0] == '.')
      continue;
    /* Skip the container being configured so it can keep its own IP on restart
     */
    if (safe_exclude[0] && strcmp(ent->d_name, safe_exclude) == 0)
      continue;

    /* config_path must hold: containers_dir (PATH_MAX-1) + '/' +
     * ent->d_name (NAME_MAX = 255) + "/container.config" (18) + NUL.
     * A plain PATH_MAX buffer overflows that worst case - use an explicit
     * worst-case size so -Werror=format-truncation is satisfied. */
    char config_path[PATH_MAX + NAME_MAX + 32];
    snprintf(config_path, sizeof(config_path), "%s/%s/container.config",
             containers_dir, ent->d_name);

    struct ds_config other;
    memset(&other, 0, sizeof(other));
    other.net_ready_pipe[0] = other.net_ready_pipe[1] = -1;
    other.net_done_pipe[0] = other.net_done_pipe[1] = -1;

    if (ds_config_load(config_path, &other) == 0) {
      if (other.static_nat_ip[0] && strcmp(other.static_nat_ip, ip_str) == 0)
        collision = 1;
      ds_config_free(&other);
    }
  }

  closedir(d);
  return collision;
}

/* Internal: derive a unique IP via djb2(container_name), walking forward on
 * collision.  Deterministic on first boot → same row every time the same
 * container name is used, spreading containers across the /16. */
static void ds_net_auto_assign_ip(struct ds_config *cfg) {
  uint32_t hash = 5381;
  for (const char *p = cfg->container_name; *p; p++)
    hash = ((hash << 5) + hash) ^ (unsigned char)*p;

  int o3 = (int)((hash >> 8) % 254) + 1; /* 1-254 */
  int o4 = (int)(hash % 254) + 1;        /* 1-254 */

  /* Walk up to a full /16 to find an unoccupied slot */
  for (int attempts = 0; attempts < 254 * 254; attempts++) {
    char candidate[32];
    snprintf(candidate, sizeof(candidate), "172.28.%d.%d", o3, o4);

    if (ds_net_check_ip_collision(candidate, cfg->container_name) == 0) {
      safe_strncpy(cfg->static_nat_ip, candidate, sizeof(cfg->static_nat_ip));
      return;
    }

    if (++o4 > 254) {
      o4 = 1;
      if (++o3 > 254)
        o3 = 1;
    }
  }

  /* Subnet is completely exhausted - reuse hash address as last resort */
  char fallback[32];
  snprintf(fallback, sizeof(fallback), "172.28.%d.%d", o3, o4);
  safe_strncpy(cfg->static_nat_ip, fallback, sizeof(cfg->static_nat_ip));
  ds_warn("[NET] NAT subnet appears fully allocated - reusing %s as fallback",
          cfg->static_nat_ip);
}

void ds_net_resolve_static_ip(struct ds_config *cfg) {
  char errbuf[256];

  if (cfg->static_nat_ip[0]) {
    /* IP already set - from --nat-ip flag or loaded from a previous boot.
     * Re-validate in case the config was hand-edited since last boot. */
    if (ds_net_validate_static_ip(cfg->static_nat_ip, errbuf, sizeof(errbuf)) !=
        0) {
      ds_warn("[NET] static_nat_ip '%s' failed validation: %s "
              "- auto-assigning a new IP",
              cfg->static_nat_ip, errbuf);
      cfg->static_nat_ip[0] = '\0';

    } else if (ds_net_check_ip_collision(cfg->static_nat_ip,
                                         cfg->container_name)) {
      ds_warn("[NET] static_nat_ip '%s' is already assigned to another "
              "container - auto-assigning a new IP",
              cfg->static_nat_ip);
      cfg->static_nat_ip[0] = '\0';
    }
  }

  if (!cfg->static_nat_ip[0])
    ds_net_auto_assign_ip(cfg);

  ds_log("[NET] Container '%s' → static NAT IP: %s (persisted to config)",
         cfg->container_name, cfg->static_nat_ip);
}

int fix_networking_host(struct ds_config *cfg) {
  ds_log("Configuring host-side networking for %s...", cfg->container_name);

  /* Enable IPv4 forwarding */
  write_file("/proc/sys/net/ipv4/ip_forward", "1");

  /* Re-enable IPv6 globally only in host mode if not disabled */
  if (cfg->net_mode == DS_NET_HOST && !cfg->disable_ipv6) {
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
 * Detects the active uplink (the routing table netd designates as the
 * default internet network), then injects low-priority ip rules to direct
 * container traffic through that table.  Fully automatic - the route
 * monitor keeps the rule in sync across wifi <-> mobile-data handoffs.
 * ---------------------------------------------------------------------------*/

/* Forward declaration - defined later in this file after route monitor globals
 */
static int find_active_uplink(ds_nl_ctx_t *ctx, char *iface_out,
                              int *table_out);

static void ds_net_setup_android_routing(ds_nl_ctx_t *ctx) {
  char active_iface[IFNAMSIZ] = {0};
  int gw_table = 0;
  find_active_uplink(ctx, active_iface, &gw_table);

  uint32_t subnet_be, mask_be;
  parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
  uint8_t prefix = DS_NAT_PREFIX;

  /* DS_RULE_PRIO_TO_SUBNET (6090): inbound traffic to our subnet always
   * resolves via main table.  Install this even if no uplink is active
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

  /* DS_RULE_PRIO_TETHER (6095): replies from our subnet to hotspot/USB-tether
   * clients must consult netd's local_network table (which holds every
   * downstream interface's connected route and no default route) before the
   * uplink table grabs them.  Installed regardless of uplink state - tether
   * clients can reach forwarded ports even with no WAN. */
  ret = ds_nl_add_rule4(ctx, subnet_be, prefix, 0, 0,
                        DS_ANDROID_TABLE_LOCAL_NETWORK, DS_RULE_PRIO_TETHER);
  if (ret < 0)
    ds_warn("[NET] Android routing: failed to add tether-return rule (%d)",
            DS_RULE_PRIO_TETHER);

  if (!active_iface[0]) {
    ds_warn("[NET] Android routing: no active uplink detected yet - "
            "route monitor will install rule when one comes up");
    return;
  }

  ds_log("[NET] Android routing: active uplink %s → table %d", active_iface,
         gw_table);

  /* DS_RULE_PRIO_FROM_SUBNET (6100): traffic from our subnet → uplink
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
    }

    /* Always assert bridge IP/UP/Hardening even if it already exists.
     * This ensures everything is correct after host-side networking changes or
     * crashes. */
    int err = ds_nl_add_addr4(ctx, DS_NAT_BRIDGE, inet_addr(DS_NAT_GW_IP),
                              DS_NAT_PREFIX);
    if (err < 0 && err != -EEXIST && err != -ENETDOWN) {
      ds_warn("[DEBUG] Failed to add IP to %s: %d", DS_NAT_BRIDGE, err);
    }

    if (ds_nl_link_up(ctx, DS_NAT_BRIDGE) < 0)
      ds_warn("[DEBUG] Failed to bring up %s", DS_NAT_BRIDGE);

    /* Disable ICMP redirects on the bridge. */
    write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/accept_redirects",
               "0");
    write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/send_redirects", "0");
    write_file("/proc/sys/net/ipv4/conf/" DS_NAT_BRIDGE "/rp_filter", "0");
  } else {
    ds_log("[NET] Bridgeless Fallback: skipping bridge creation.");
  }

  /* Late-stage hardening: sysctl for bridge */
  if (cfg->net_mode == DS_NET_NAT) {
    ds_log("[DEBUG] Applying late-stage hardening for Android NAT...");
    if (!cfg->net_bridgeless) {
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

    /* Add host route for the container's static IP to this veth.
     * cfg->static_nat_ip is already resolved and persisted before fork. */
    struct in_addr peer_in;
    if (inet_pton(AF_INET, cfg->static_nat_ip, &peer_in) == 1) {
      if (ds_nl_add_route4(ctx, peer_in.s_addr, 32, 0,
                           ds_nl_get_ifindex(ctx, veth_host)) < 0)
        ds_warn("[NET] Bridgeless: Failed to add route for %s",
                cfg->static_nat_ip);
    } else {
      ds_warn("[NET] Bridgeless: static_nat_ip '%s' is not parseable - "
              "no host route installed",
              cfg->static_nat_ip);
    }
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

  /* 7. Android policy routing - uplink is detected automatically */
  if (is_android())
    ds_net_setup_android_routing(ctx);

  ds_nl_close(ctx);

  /* 8. Start embedded DHCP server so the container's DHCP client acquires
   * the static IP persisted in cfg->static_nat_ip.  Using a stable IP here
   * means every reboot the container gets the same address - no PREROUTING
   * rule churn, no "wrong IP" on the first DHCP renew.
   *
   * Binding interface depends on topology:
   *   Bridge mode    - bind to ds-br0.  veth_host is a bridge slave; the
   *                    kernel delivers frames from the container upward to
   *                    the bridge interface, not the slave.  A socket bound
   *                    to the slave would never see the DHCP DISCOVERs.
   *   Bridgeless mode - bind to veth_host directly (point-to-point veth,
   *                    no bridge in the path). */
  {
    struct in_addr offer_in;
    uint32_t offer_ip = 0;
    if (inet_pton(AF_INET, cfg->static_nat_ip, &offer_in) == 1) {
      offer_ip = offer_in.s_addr;
    } else {
      ds_warn("[NET] DHCP: static_nat_ip '%s' unparseable - "
              "DHCP server will offer 0.0.0.0 (container boot will fail)",
              cfg->static_nat_ip);
    }

    /* Bind to veth_host. In bridge mode the kernel also floods L2 broadcasts
     * to sibling veth ports; isolation is enforced by peer_mac filter in
     * the DHCP server loop, not by the socket bind alone. */
    const char *dhcp_iface = veth_host;
    ds_dhcp_server_start(cfg, dhcp_iface, offer_ip, inet_addr(DS_NAT_GW_IP));

    /* Store the container IP string (plain dotted-decimal) for port-forward
     * cleanup later.  static_nat_ip is already in that exact format. */
    safe_strncpy(cfg->nat_container_ip, cfg->static_nat_ip,
                 sizeof(cfg->nat_container_ip));

    /* Install DNAT + FORWARD rules for any --port mappings */
    if (cfg->port_forward_count > 0)
      ds_ipt_add_portforwards(cfg, cfg->nat_container_ip);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * setup_gateway_veth_side
 *
 * Phase-1 OpenWrt gateway mode.  Droidspaces owns only the L2 plumbing:
 *   - create/reuse a bridge-only LAN with no IP address
 *   - attach one veth peer to the designated gateway container netns
 *   - attach one veth peer to the application container netns
 *
 * No NAT, DHCP, DNS, port-forwarding, Android policy routing, or firewall
 * policy is installed here.  The gateway container is expected to run the
 * routing policy engine (OpenWrt netifd/firewall/dnsmasq/etc.).
 * ---------------------------------------------------------------------------*/

int setup_gateway_veth_side(struct ds_config *cfg, pid_t child_pid) {
  if (!cfg || !cfg->gateway_container[0]) {
    ds_warn("[NET] Gateway: no gateway container configured");
    return -1;
  }

  if (strcmp(cfg->gateway_container, cfg->container_name) == 0) {
    ds_warn("[NET] Gateway: refusing to attach container to itself");
    return -1;
  }

  const char *gw_if = gateway_lan_ifname(cfg);
  if (strlen(gw_if) >= IFNAMSIZ || !gateway_ifname_component_ok(gw_if)) {
    ds_warn("[NET] Gateway: invalid gateway interface name '%s'", gw_if);
    return -1;
  }

  struct ds_config gw_cfg;
  memset(&gw_cfg, 0, sizeof(gw_cfg));
  gw_cfg.net_ready_pipe[0] = gw_cfg.net_ready_pipe[1] = -1;
  gw_cfg.net_done_pipe[0] = gw_cfg.net_done_pipe[1] = -1;
  safe_strncpy(gw_cfg.container_name, cfg->gateway_container,
               sizeof(gw_cfg.container_name));

  /* Best effort: loading the config gives UUID fallback discovery, but the
   * fast pidfile path still works when the config file is unavailable. */
  (void)ds_config_load_by_name(cfg->gateway_container, &gw_cfg);

  pid_t gw_pid = 0;
  if (!is_container_running(&gw_cfg, &gw_pid) || gw_pid <= 0) {
    ds_warn("[NET] Gateway: container '%s' is not running",
            cfg->gateway_container);
    ds_config_free(&gw_cfg);
    return -1;
  }

  char bridge[IFNAMSIZ];
  char gw_host[IFNAMSIZ], gw_peer[IFNAMSIZ];
  char app_host[IFNAMSIZ], app_peer[IFNAMSIZ];
  gateway_bridge_name(cfg, bridge, sizeof(bridge));
  gateway_veth_names(cfg, gw_host, sizeof(gw_host), gw_peer, sizeof(gw_peer));
  veth_host_name(child_pid, app_host, sizeof(app_host));
  veth_peer_name(child_pid, app_peer, sizeof(app_peer));

  ds_log("[NET] Gateway: wiring %s to gateway %s via bridge %s",
         cfg->container_name, cfg->gateway_container, bridge);

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[NET] Gateway: failed to open RTNETLINK socket");
    ds_config_free(&gw_cfg);
    return -1;
  }

  if (!ds_nl_link_exists(ctx, bridge)) {
    ds_log("[NET] Gateway: creating delegated LAN bridge %s", bridge);
    if (ds_nl_create_bridge(ctx, bridge) < 0) {
      ds_warn("[NET] Gateway: failed to create bridge %s", bridge);
      ds_nl_close(ctx);
      ds_config_free(&gw_cfg);
      return -1;
    }
  }

  if (ds_nl_link_up(ctx, bridge) < 0)
    ds_warn("[NET] Gateway: failed to bring up bridge %s", bridge);

  /* Keep the bridge policy-neutral: no address, no DHCP, no NAT.  Disable
   * bridge netfilter calls so OpenWrt's own firewalling remains the visible
   * policy authority inside the delegated network. */
  write_file("/proc/sys/net/bridge/bridge-nf-call-iptables", "0");
  write_file("/proc/sys/net/bridge/bridge-nf-call-ip6tables", "0");

  /* Lazily attach a LAN-side interface to the gateway if it is not already
   * present.  This allows the gateway container to boot with its WAN side
   * first, then receive its LAN cable when the first client is attached. */
  if (!ds_nl_link_exists(ctx, gw_host)) {
    char gw_netns[PATH_MAX];
    snprintf(gw_netns, sizeof(gw_netns), "/proc/%d/ns/net", (int)gw_pid);

    ds_log("[NET] Gateway: creating gateway veth %s <-> %s", gw_host, gw_peer);
    if (ds_nl_create_veth(ctx, gw_host, gw_peer) < 0) {
      ds_warn("[NET] Gateway: failed to create gateway veth pair");
      ds_nl_close(ctx);
      ds_config_free(&gw_cfg);
      return -1;
    }

    ds_net_disable_tx_checksum(gw_host);
    if (ds_nl_set_master(ctx, gw_host, bridge) < 0)
      ds_warn("[NET] Gateway: failed to attach %s to %s", gw_host, bridge);
    if (ds_nl_link_up(ctx, gw_host) < 0)
      ds_warn("[NET] Gateway: failed to bring up %s", gw_host);

    int gw_netns_fd = open(gw_netns, O_RDONLY | O_CLOEXEC);
    if (gw_netns_fd < 0) {
      ds_warn("[NET] Gateway: failed to open %s: %s", gw_netns,
              strerror(errno));
      ds_nl_close(ctx);
      ds_config_free(&gw_cfg);
      return -1;
    }

    int r = ds_nl_move_to_netns(ctx, gw_peer, gw_netns_fd);
    close(gw_netns_fd);
    if (r < 0) {
      ds_warn("[NET] Gateway: failed to move %s into gateway netns", gw_peer);
      ds_nl_close(ctx);
      ds_config_free(&gw_cfg);
      return -1;
    }

    if (ds_netns_rename_up(gw_netns, gw_peer, gw_if) < 0)
      ds_warn("[NET] Gateway: moved %s but could not rename/up it as %s",
              gw_peer, gw_if);
  } else {
    if (ds_nl_set_master(ctx, gw_host, bridge) < 0)
      ds_warn("[NET] Gateway: failed to reattach %s to %s", gw_host, bridge);
    ds_nl_link_up(ctx, gw_host);
  }

  /* Wire the application container into the delegated LAN bridge. */
  ds_nl_del_link(ctx, app_host);
  ds_log("[NET] Gateway: creating app veth %s <-> %s", app_host, app_peer);
  if (ds_nl_create_veth(ctx, app_host, app_peer) < 0) {
    ds_warn("[NET] Gateway: failed to create app veth pair");
    ds_nl_close(ctx);
    ds_config_free(&gw_cfg);
    return -1;
  }

  ds_net_disable_tx_checksum(app_host);
  if (ds_nl_set_master(ctx, app_host, bridge) < 0)
    ds_warn("[NET] Gateway: failed to attach %s to %s", app_host, bridge);
  if (ds_nl_link_up(ctx, app_host) < 0)
    ds_warn("[NET] Gateway: failed to bring up %s", app_host);

  char child_netns[PATH_MAX];
  snprintf(child_netns, sizeof(child_netns), "/proc/%d/ns/net", (int)child_pid);
  int child_netns_fd = open(child_netns, O_RDONLY | O_CLOEXEC);
  if (child_netns_fd < 0) {
    ds_warn("[NET] Gateway: failed to open container netns %s: %s", child_netns,
            strerror(errno));
    ds_nl_close(ctx);
    ds_config_free(&gw_cfg);
    return -1;
  }

  int r = ds_nl_move_to_netns(ctx, app_peer, child_netns_fd);
  close(child_netns_fd);
  if (r < 0) {
    ds_warn("[NET] Gateway: failed to move %s into container netns", app_peer);
    ds_nl_close(ctx);
    ds_config_free(&gw_cfg);
    return -1;
  }

  ds_nl_close(ctx);
  ds_config_free(&gw_cfg);

  ds_log("[NET] Gateway: delegated LAN ready: %s -> %s -> %s",
         cfg->container_name, bridge, cfg->gateway_container);
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

  /* 0b. Pin eth0 to a deterministic MAC derived from the container name, so
   * an upstream gateway sees one stable host across restarts (no LuCI/lease
   * churn) instead of a new random MAC each boot. Set while down, pre-up. */
  if (cfg && cfg->container_name[0]) {
    uint8_t mac[6];
    ds_container_mac(cfg->container_name, mac);
    if (ds_nl_set_mac(ctx, "eth0", mac) < 0)
      ds_warn("[NET] Child: failed to set deterministic MAC on eth0");
    else
      ds_log("[NET] Child: eth0 MAC pinned to %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  /* 1. Loopback */
  ds_nl_link_up(ctx, "lo");

  /* 2. Bring eth0 UP - the container's DHCP client configures IP and route */
  ds_nl_link_up(ctx, "eth0");

  ds_nl_close(ctx);
  if (cfg && cfg->net_mode == DS_NET_GATEWAY)
    ds_log("[NET] Child: eth0 UP - gateway container owns DHCP/routing");
  else
    ds_log("[NET] Child: eth0 UP - awaiting DHCP lease from monitor");
  return 0;
}

/* Compatibility wrapper */

/* ---------------------------------------------------------------------------
 * /etc/resolv.conf wiring (inside container, after pivot_root)
 *
 * Single source of truth for the container's resolver. Two cases:
 *
 *   1. gateway mode + no custom --dns + systemd container:
 *      DNS is owned by the gateway's DHCP/DNS (e.g. OpenWrt). systemd-resolved
 *      (fed by the lease) publishes it at /run/systemd/resolve/resolv.conf, so
 *      we only point the symlink there - resolved owns the file. (A non-systemd
 *      gateway has no resolved, so it falls through to case 2 with default
 * DNS.)
 *
 *   2. everything else (nat/host/none, or any mode with custom --dns):
 *      Droidspaces owns DNS. Write our content (custom or default) to
 *      /run/droidspaces/resolv.conf and symlink it.
 *
 * This replaces the old /run/resolvconf duct-tape, which clobbered the distro's
 * resolver and left a dangling symlink in gateway mode.
 * ---------------------------------------------------------------------------*/
static void setup_resolv_conf(struct ds_config *cfg) {
  const char *target;

  if (cfg->net_mode == DS_NET_GATEWAY && !cfg->dns_servers[0] &&
      is_systemd_rootfs("/")) {
    target = "/run/systemd/resolve/resolv.conf";
  } else {
    mkdir("/run/droidspaces", 0755);
    write_file("/run/droidspaces/resolv.conf", cfg->dns_server_content);
    target = "/run/droidspaces/resolv.conf";
  }

  unlink("/etc/resolv.conf");
  if (symlink(target, "/etc/resolv.conf") < 0)
    ds_warn("Failed to link /etc/resolv.conf -> %s: %s", target,
            strerror(errno));
}

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

  /* IPv6 is enabled in host mode and gateway mode unless explicitly disabled.
   * Gateway mode is policy-owned by OpenWrt, so IPv6 RA/DHCPv6 should be able
   * to operate inside the application container netns. */
  int ipv6_enabled =
      ((cfg->net_mode == DS_NET_HOST || cfg->net_mode == DS_NET_GATEWAY) &&
       !cfg->disable_ipv6);
  if (ipv6_enabled) {
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

  /* 3. resolv.conf (unified resolver wiring - see setup_resolv_conf). */
  setup_resolv_conf(cfg);

  if (!ipv6_enabled) {
    if (cfg->net_mode == DS_NET_HOST) {
      /* In host mode, disabling IPv6 affects the host's netns. Warn and apply.
       */
      ds_warn("--disable-ipv6 in host mode disables IPv6 on the host "
              "network namespace.");
    }
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "1");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "1");
  }

  /* 5. unprivileged ICMP sockets: new network namespaces reset
   * ping_group_range to "1 0". Allow all GIDs so ping works without
   * CAP_NET_RAW. */
  write_file("/proc/sys/net/ipv4/ping_group_range", "0 2147483647");

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
 * Uplink Route Monitor
 *
 * Watches FIB rule, route, link, and IPv4 address changes on the host.
 * When a relevant change is detected it re-probes which uplink is currently
 * active and atomically updates the container policy rule.  Fully automatic
 * - no user-declared interface list.
 *
 * Event triggers:
 *   RTM_NEWRULE / RTM_DELRULE   - netd swaps the default-network rule
 *                                 (this IS the wifi <-> mobile handoff)
 *   RTM_NEWROUTE / RTM_DELROUTE - default route moved between tables
 *   RTM_NEWLINK / RTM_DELLINK   - interface state change (UP/RUNNING/DOWN)
 *   RTM_NEWADDR / RTM_DELADDR   - IPv4 address assigned or removed
 *
 * A 1.5s heartbeat covers devices with broken netlink notifications and
 * re-asserts ip_forward, which Android periodically resets.
 * ---------------------------------------------------------------------------*/

/* Last detected uplink interface.  Suppresses the "[NET] active uplink:"
 * log line on every heartbeat - only log when the result changes. */
static char g_last_uplink_iface[IFNAMSIZ];

/* Set to 1 when we've already warned that automatic detection found no
 * uplink.  Cleared when a probe succeeds again, so the next failure after
 * a working period logs once more. */
static int g_uplink_fail_warned;

/* Basic sanity for a probe result: a real, carrier-up interface that is
 * not loopback, not ours, and not a downstream/tether/VPN device. */
static int uplink_candidate_ok(const char *ifname) {
  if (!ifname[0])
    return 0;
  if (uplink_name_excluded(ifname))
    return 0;
  return iface_is_running(ifname);
}

static void log_uplink_change(const char *ifname, int table,
                              const char *method) {
  if (strcmp(g_last_uplink_iface, ifname) != 0) {
    ds_log("[NET] Active uplink: %s (table %d) [%s]", ifname, table, method);
    safe_strncpy(g_last_uplink_iface, ifname, sizeof(g_last_uplink_iface));
  }
  g_uplink_fail_warned = 0; /* reset so next failure logs once */
}

/* Tier 3 (last resort): scan all interfaces against the built-in uplink
 * whitelist in priority order; return the first that is RUNNING and has
 * an IPv4 default route in some table. */
static int scan_uplink_whitelist(ds_nl_ctx_t *ctx, char *iface_out,
                                 int *table_out) {
  char all_ifaces[64][IFNAMSIZ];
  int all_count = ds_nl_list_ifaces(ctx, all_ifaces, 64);

  for (size_t p = 0;
       p < sizeof(k_uplink_patterns) / sizeof(k_uplink_patterns[0]); p++) {
    for (int j = 0; j < all_count; j++) {
      if (fnmatch(k_uplink_patterns[p], all_ifaces[j], 0) != 0)
        continue;
      if (!uplink_candidate_ok(all_ifaces[j]))
        continue;
      int tbl = 0;
      if (ds_nl_get_iface_table(ctx, all_ifaces[j], &tbl) != 0)
        continue;
      if (iface_out)
        safe_strncpy(iface_out, all_ifaces[j], IFNAMSIZ);
      if (table_out)
        *table_out = tbl;
      return 0;
    }
  }
  return -ENOENT;
}

/* Find the interface/table currently providing internet access.
 * Fully automatic, three tiers, first hit wins:
 *
 *   1. Android netd default-network FIB rule (fwmark 0x0/0xffff iif lo).
 *      The kernel's ground truth - swapped atomically on every handoff,
 *      and never points at IMS/MMS-only interfaces.  Also the only
 *      interface Qualcomm IPA / MTK CCCI has hardware reply sessions
 *      for, which NAT'd forwarded traffic requires.
 *   2. The main routing table's IPv4 default route - the standard Linux
 *      case (what LXC/Docker implicitly rely on), and ROMs/chroots that
 *      populate the main table.
 *   3. Built-in whitelist scan of known uplink interface families.
 *
 * Returns 0 and fills iface_out (IFNAMSIZ) / table_out on success. */
static int find_active_uplink(ds_nl_ctx_t *ctx, char *iface_out,
                              int *table_out) {
  char name[IFNAMSIZ] = {0};
  int tbl = 0;

  /* Tier 1: netd default-network rule (Android only - the rule simply
   * does not exist elsewhere, but skip the dump cost off-Android). */
  if (is_android() && ds_nl_get_android_default(ctx, name, &tbl) == 0 &&
      uplink_candidate_ok(name)) {
    log_uplink_change(name, tbl, "netd rule");
    if (iface_out)
      safe_strncpy(iface_out, name, IFNAMSIZ);
    if (table_out)
      *table_out = tbl;
    return 0;
  }

  /* Tier 2: main-table default route (standard Linux semantics). */
  name[0] = '\0';
  if (ds_nl_get_table_default_oif(ctx, RT_TABLE_MAIN, name) == 0 &&
      uplink_candidate_ok(name)) {
    log_uplink_change(name, RT_TABLE_MAIN, "main table");
    if (iface_out)
      safe_strncpy(iface_out, name, IFNAMSIZ);
    if (table_out)
      *table_out = RT_TABLE_MAIN;
    return 0;
  }

  /* Tier 3: built-in whitelist scan. */
  name[0] = '\0';
  tbl = 0;
  if (scan_uplink_whitelist(ctx, name, &tbl) == 0) {
    log_uplink_change(name, tbl, "whitelist scan");
    if (iface_out)
      safe_strncpy(iface_out, name, IFNAMSIZ);
    if (table_out)
      *table_out = tbl;
    return 0;
  }

  if (!g_uplink_fail_warned) {
    ds_warn("[NET] Uplink detection: no active internet interface found - "
            "will keep probing");
    g_uplink_fail_warned = 1;
  }
  g_last_uplink_iface[0] = '\0';
  return -ENOENT;
}

/* Re-probe which uplink is active and update the ip rule if needed. */
static void do_uplink_reprobe(void) {
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return;

  char new_iface[IFNAMSIZ] = {0};
  int new_table = 0;

  if (find_active_uplink(ctx, new_iface, &new_table) != 0) {
    /* No uplink active yet - leave current rule in place */
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

  ds_log("[NET] Route monitor: uplink switch table %d → %d (%s)", old_table,
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

    if (is_android())
      write_file("/proc/sys/net/ipv4/ip_forward", "1");
  } else {
    ds_warn("[NET] Route monitor: failed to install new rule for table %d",
            new_table);
  }

  ds_nl_close(ctx);
}

static void *route_monitor_loop(void *arg) {
  (void)arg;

  ds_log("[NET] Uplink route monitor started (automatic detection)");

  int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0) {
    ds_warn("[NET] Route monitor: failed to open netlink socket: %s",
            strerror(errno));
    return NULL;
  }

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  /* RTMGRP_LINK        - interface state changes (IFF_RUNNING, up/down)
   * RTMGRP_IPV4_IFADDR - IPv4 address add/remove
   * RTMGRP_IPV4_ROUTE  - default route moved between tables
   * RTNLGRP_IPV4_RULE  - netd swapping the default-network FIB rule:
   *                      this IS the wifi <-> mobile handoff signal.
   *                      No legacy RTMGRP_ bitmask macro exists for rule
   *                      groups; the bit is 1 << (RTNLGRP_IPV4_RULE - 1). */
  sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
                 (1u << (RTNLGRP_IPV4_RULE - 1));

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
      do_uplink_reprobe();
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

      if (h->nlmsg_type == RTM_NEWRULE || h->nlmsg_type == RTM_DELRULE) {
        /* FIB rule change - on Android this is netd switching the default
         * network.  Always reprobe; the reprobe is idempotent so the echo
         * of our own rule updates settles in one extra no-op pass. */
        should_reprobe = 1;
      } else if (h->nlmsg_type == RTM_NEWLINK || h->nlmsg_type == RTM_DELLINK) {
        /* Any real interface changing state may affect the uplink - e.g.
         * a new rmnet_dataX popping up mid-session.  Only our own veths
         * and loopback are noise. */
        struct ifinfomsg *ifi = NLMSG_DATA(h);
        char evname[IFNAMSIZ] = {0};
        if_indextoname((unsigned int)ifi->ifi_index, evname);
        if (evname[0] && strncmp(evname, "ds-", 3) != 0 &&
            strcmp(evname, "lo") != 0)
          should_reprobe = 1;
      } else if (h->nlmsg_type == RTM_NEWADDR || h->nlmsg_type == RTM_DELADDR) {
        struct ifaddrmsg *ifa = NLMSG_DATA(h);
        if (ifa->ifa_family == AF_INET) {
          char evname[IFNAMSIZ] = {0};
          if_indextoname((unsigned int)ifa->ifa_index, evname);
          if (evname[0] && strncmp(evname, "ds-", 3) != 0 &&
              strcmp(evname, "lo") != 0)
            should_reprobe = 1;
        }
      } else if (h->nlmsg_type == RTM_NEWROUTE ||
                 h->nlmsg_type == RTM_DELROUTE) {
        /* Default route added/removed in some table.  Filter our own
         * subnet/bridge routes by output interface. */
        struct rtmsg *rtm = NLMSG_DATA(h);
        if (rtm->rtm_family == AF_INET) {
          int oif = 0;
          struct rtattr *rta = RTM_RTA(rtm);
          int rlen = (int)RTM_PAYLOAD(h);
          for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
            if (rta->rta_type == RTA_OIF)
              oif = *(int *)RTA_DATA(rta);
          }
          char evname[IFNAMSIZ] = {0};
          if (oif > 0)
            if_indextoname((unsigned int)oif, evname);
          if (!evname[0] ||
              (strncmp(evname, "ds-", 3) != 0 && strcmp(evname, "lo") != 0))
            should_reprobe = 1;
        }
      }

      if (should_reprobe)
        break;
    }

    if (should_reprobe) {
      /* Handoffs emit a burst of rule/route/addr events back-to-back.
       * Drain the socket for a short window so the burst collapses into
       * a single reprobe instead of one per event. */
      struct pollfd dp = {.fd = sock, .events = POLLIN};
      while (!g_stop_monitor && poll(&dp, 1, 200) > 0) {
        if (recv(sock, buf, sizeof(buf), 0) <= 0)
          break;
      }
      do_uplink_reprobe();
    }
  }

  pthread_mutex_lock(&g_gw_mutex);
  close(sock);
  g_route_monitor_sock = -1;
  pthread_mutex_unlock(&g_gw_mutex);

  ds_log("[NET] Uplink route monitor stopped");
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
  if (cfg->net_mode == DS_NET_GATEWAY) {
    ds_nl_ctx_t *ctx = ds_nl_open();
    if (!ctx)
      return;

    char veth_host[IFNAMSIZ] = {0};
    pid_t effective_pid =
        container_pid > 0 ? container_pid : cfg->container_pid;
    if (effective_pid > 0) {
      veth_host_name(effective_pid, veth_host, sizeof(veth_host));
      ds_nl_del_link(ctx, veth_host);
      ds_log("[NET] Gateway cleanup: removed %s", veth_host);

      /* Refcount the delegated bridge: once the last client veth has left,
       * reap the now-empty bridge so idle segments don't linger. */
      char bridge[IFNAMSIZ], gw_host[IFNAMSIZ], gw_peer[IFNAMSIZ];
      gateway_bridge_name(cfg, bridge, sizeof(bridge));
      gateway_veth_names(cfg, gw_host, sizeof(gw_host), gw_peer,
                         sizeof(gw_peer));
      int clients = ds_nl_count_bridge_members_with_prefix(ctx, bridge, "ds-v");
      if (clients > 0) {
        ds_log("[NET] Gateway cleanup: %d client(s) still on %s - keeping "
               "delegated LAN",
               clients, bridge);
      } else {
        /* Reap the empty bridge, but NEVER delete the gateway veth (gw_host):
         * its peer is the gateway container's live LAN interface (e.g. eth1),
         * and deleting either end destroys the pair - ripping the cable out of
         * a still-running gateway.  OpenWrt's netifd will not re-init a brand
         * new eth1 that reappears under it, so its DHCP/LAN stays dead until
         * the gateway is rebooted.  Leaving gw_host masterless keeps eth1
         * alive; the next client recreates the bridge and reattaches gw_host
         * (setup_gateway_veth_side else-branch).  When the gateway container
         * itself stops, its netns dies and gw_host vanishes with its peer,
         * leaving zero residue. */
        ds_nl_del_link(ctx, bridge);
        ds_log("[NET] Gateway cleanup: reaped idle delegated LAN bridge %s "
               "(kept gateway veth %s)",
               bridge, gw_host);
      }
    } else {
      ds_warn("[NET] Gateway cleanup: cannot derive veth name - no valid PID");
    }

    ds_nl_close(ctx);
    return;
  }

  if (cfg->net_mode != DS_NET_NAT)
    return;

  ds_net_stop_route_monitor();

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return;

  /* 1. Delete host-side veth - peer in dead netns is already gone */
  char veth_host[IFNAMSIZ] = {0};
  pid_t effective_pid = container_pid > 0 ? container_pid : cfg->container_pid;
  if (effective_pid <= 0) {
    ds_warn("[NET] cleanup: cannot derive veth name - no valid PID");
    /* still proceed with iptables cleanup */
  } else {
    veth_host_name(effective_pid, veth_host, sizeof(veth_host));
    ds_nl_del_link(ctx, veth_host);
  }

  /* Check how many ds-v* veths remain AFTER deleting ours.
   * Shared resources (DHCP server, MASQUERADE, FORWARD, Android
   * policy rules) must only be torn down when we are the last container.
   * Stopping them while others are running kills their networking. */
  int surviving = ds_nl_count_ifaces_with_prefix(ctx, "ds-v");
  if (surviving > 0) {
    ds_log("[NET] cleanup: %d other container(s) still running - "
           "keeping shared iptables and routing rules",
           surviving);
    ds_ipt_remove_portforwards(cfg);
    if (cfg->net_bridgeless && veth_host[0] != '\0')
      ds_ipt_remove_iface_rules(veth_host);
    ds_nl_close(ctx);
    return;
  }

  /* Last container - safe to stop shared services and remove shared rules */
  ds_dhcp_server_stop();

  /* 2. Remove Android policy rules (last container - safe to clean up) */
  if (is_android()) {
    uint32_t subnet, mask;
    parse_cidr(DS_DEFAULT_SUBNET, &subnet, &mask);

    /* Remove DS policy rules at both current and legacy priority values so
     * an upgrade from an older build that used hardcoded 90/100/200/201 still
     * cleans up completely.  del_rule4 is idempotent (ENOENT → 0). */
    int prios[] = {
        DS_RULE_PRIO_TO_SUBNET,   /* 6090 - current */
        DS_RULE_PRIO_TETHER,      /* 6095 - current */
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
