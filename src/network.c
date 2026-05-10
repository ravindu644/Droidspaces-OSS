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

/* ---------------------------------------------------------------------------
 * Upstream routing globals - shared by android routing setup and monitor
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
 * Android default network detection via kernel ip rules
 *
 * Android's netd inserts exactly one rule of the form:
 *   "<prio>: from all fwmark 0x0/0xffff iif lo lookup <iface>"
 * for the active default internet network. This rule is the kernel's
 * ground truth - it's set atomically when the default network changes
 * (wifi ↔ mobile data ↔ handoff).
 *
 * IMS/MMS-only interfaces (rmnet_data1, rmnet_data6 etc) NEVER appear
 * in this rule - they use explicit fwmarks (0xd0064, 0xd0066 etc).
 * Only the true internet interface that Qualcomm IPA/MTK CCCI has
 * hardware reply sessions for will appear here.
 *
 * Returns 1 and fills iface_out (IFNAMSIZ) on success, 0 on failure.
 * ---------------------------------------------------------------------------*/
static int ds_net_get_android_default_iface_from_rules(char *iface_out) {
  FILE *fp = popen("ip rule show 2>/dev/null", "r");
  if (!fp)
    return 0;

  char line[512];
  int found = 0;

  while (fgets(line, sizeof(line), fp) && !found) {
    /* Match lines containing both "fwmark 0x0/0xffff" and "iif lo" */
    if (!strstr(line, "fwmark 0x0/0xffff"))
      continue;
    if (!strstr(line, "iif lo"))
      continue;

    char *lookup = strstr(line, "lookup ");
    if (!lookup)
      continue;
    lookup += 7;

    /* Copy interface name until whitespace/newline */
    size_t i = 0;
    while (lookup[i] && lookup[i] != ' ' && lookup[i] != '\n' &&
           lookup[i] != '\r' && lookup[i] != '\t' && i < (size_t)(IFNAMSIZ - 1))
      i++;
    if (i == 0)
      continue;

    char iface[IFNAMSIZ];
    memcpy(iface, lookup, i);
    iface[i] = '\0';

    /* Skip non-real interfaces */
    if (strcmp(iface, "dummy0") == 0 || strcmp(iface, "lo") == 0 ||
        strncmp(iface, "ds-", 3) == 0)
      continue;

    safe_strncpy(iface_out, iface, IFNAMSIZ);
    found = 1;
  }

  pclose(fp);
  return found;
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
  if (cfg && cfg->static_nat_ip[0])
    safe_strncpy(hs->ip_str, cfg->static_nat_ip, sizeof(hs->ip_str));
  else
    veth_peer_ip(init_pid, hs->ip_str,
                 sizeof(hs->ip_str)); /* last-resort fallback */
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
      free_config_binds(&other);
      free_config_env_vars(&other);
      free_config_unknown_lines(&other);
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
 * Iterates the user-declared upstream interfaces in priority order, finds the
 * first one that is RUNNING and has an IPv4 default route, then injects
 * low-priority ip rules to direct container traffic through that table.
 *
 * This replaces the old auto-detection approach which was unreliable on
 * Android MTK/Qualcomm because both wlan0 and mobile-data interfaces can
 * have simultaneous default routes in per-interface tables; only the policy
 * rules distinguish which is active, and parsing those rules was fragile.
 * ---------------------------------------------------------------------------*/

/* Forward declaration - defined later in this file after route monitor globals
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
    ds_warn("[NET] Android routing: no upstream interface is active yet - "
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

  /* 7. Android policy routing - uses the user-declared upstream interfaces */
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

    /* Always bind to veth_host for DHCP sniffing.
     * Even if bridged, AF_PACKET on the slave sees traffic before the bridge.
     */
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

  /* 2. Bring eth0 UP - the container's DHCP client configures IP and route */
  ds_nl_link_up(ctx, "eth0");

  ds_nl_close(ctx);
  ds_log("[NET] Child: eth0 UP - awaiting DHCP lease from monitor");
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

  /* IPv6 is only enabled in host mode unless explicitly disabled */
  int ipv6_enabled = (cfg->net_mode == DS_NET_HOST && !cfg->disable_ipv6);
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

  /* 3. resolv.conf - always use dns_server_content which ds_get_dns_servers()
   * already populated with either the user's --dns servers or the compiled-in
   * defaults (1.1.1.1 / 8.8.8.8). */
  mkdir("/run/resolvconf", 0755);
  write_file("/run/resolvconf/resolv.conf", cfg->dns_server_content);

  /* Link /etc/resolv.conf */
  unlink("/etc/resolv.conf");
  if (symlink("/run/resolvconf/resolv.conf", "/etc/resolv.conf") < 0) {
    ds_warn("Failed to link /etc/resolv.conf: %s", strerror(errno));
  }

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
 * Upstream Route Monitor
 *
 * Watches LINK state and IPv4 address changes on the user-declared upstream
 * interfaces. When any change is detected that involves one of those
 * interfaces, it re-scans to find which is currently active (RUNNING + has
 * a default route) and atomically updates the policy rule.
 *
 * Event triggers:
 *   RTM_NEWLINK / RTM_DELLINK - interface state change (UP/RUNNING/DOWN)
 *   RTM_NEWADDR / RTM_DELADDR - IPv4 address assigned or removed
 *
 * 30-second heartbeat covers devices with broken netlink notifications.
 * ---------------------------------------------------------------------------*/

/* Scan upstream interfaces in priority order; return the first that is
 * RUNNING and has an IPv4 default route in any table.
 *
 * Entries without wildcards are checked directly (fast path).
 * Entries containing '*' or '?' are expanded via a full RTM_GETLINK dump
 * and matched with fnmatch() - this handles dynamic interface names like
 * "*rmnet_data*" or "v4-rmnet_data*" on Qualcomm/CLAT devices where the
 * interface number changes on every reboot.
 *
 * Each wildcard pattern slot remembers the interface it last resolved to.
 * A discovery log fires only when the resolved name changes - this handles
 * cleanup (dead interfaces are overwritten), prevents log spam on heartbeat
 * reprobes, and uses zero dynamic allocation. */

/* Per-pattern "last wildcard match" tracker.  Index matches g_upstream_ifaces.
 * An empty string means "never matched yet". */
static char g_last_wildcard_match[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];

/* Last interface returned by the ip rule probe.  Suppresses the
 * "[NET] Android default network:" log line on every 1.5s heartbeat -
 * only log when the interface actually changes. */
static char g_last_iprule_iface[IFNAMSIZ];

/* Set to 1 when we've already warned about ip rule probe failure.
 * Cleared when the probe succeeds again, so the next failure after
 * a working period logs once more. */
static int g_iprule_fail_warned;

static int find_active_upstream(ds_nl_ctx_t *ctx, char *iface_out,
                                int *table_out) {
  /* On Android, use the kernel ip rule "fwmark 0x0/0xffff iif lo lookup
   * <iface>" as the primary detection method.  This rule is set by netd
   * for the active default internet network only - IMS/MMS-only interfaces
   * (rmnet_data1, rmnet_data6 etc) never appear here.  This also ensures
   * we pick the interface Qualcomm IPA / MTK CCCI has hardware reply
   * sessions for, which is required for NAT'd forwarded traffic to work.
   *
   * This replaces the old priority-list-first approach which would pick
   * the first RUNNING rmnet_data* with a default route - often an IMS
   * interface that is RUNNING and has routes but whose IPA sessions only
   * serve locally-originated Android traffic, not forwarded container
   * packets. */
  if (is_android()) {
    char default_iface[IFNAMSIZ] = {0};
    if (ds_net_get_android_default_iface_from_rules(default_iface) &&
        default_iface[0] && iface_is_running(default_iface)) {
      int tbl = 0;
      if (ds_nl_get_iface_table(ctx, default_iface, &tbl) == 0) {
        if (strcmp(g_last_iprule_iface, default_iface) != 0) {
          ds_log("[NET] Android default network: %s (table %d) [ip rule]",
                 default_iface, tbl);
          safe_strncpy(g_last_iprule_iface, default_iface,
                       sizeof(g_last_iprule_iface));
        }
        g_iprule_fail_warned = 0; /* reset so next failure logs once */
        if (iface_out)
          safe_strncpy(iface_out, default_iface, IFNAMSIZ);
        if (table_out)
          *table_out = tbl;
        return 0;
      }
    }
    if (!g_iprule_fail_warned) {
      ds_warn("[NET] ip rule probe failed - falling back to priority list");
      g_iprule_fail_warned = 1;
    }
    g_last_iprule_iface[0] = '\0';
  }

  /* Non-Android path (or Android fallback): scan the user-declared
   * upstream interface list in priority order. */
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
    /* No upstream active yet - leave current rule in place */
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
  /* RTMGRP_LINK     - interface state changes (IFF_RUNNING, link up/down)
   * RTMGRP_IPV4_IFADDR - IPv4 address add/remove on upstream interfaces */
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
