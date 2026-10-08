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
#include <sys/file.h>
#include <sys/ioctl.h>

/* Internal helpers */

/* Host-side veth prefix for an application container.  NAT containers use
 * "ds-v"; gateway clients use "ds-c" so the NAT-mode "last container" refcount
 * (which scans for "ds-v") never miscounts a gateway client as a live NAT
 * container and so keeps shared NAT iptables/route rules alive forever. */
static const char *app_veth_host_prefix(const struct ds_config *cfg) {
  return (cfg && cfg->net_mode == DS_NET_GATEWAY) ? "ds-c" : "ds-v";
}

static uint32_t ds_net_hash_string(const char *s);

/* Derive the host-side veth name (mode-aware). A NAT veth is named after the
 * container's init PID. A gateway client's is named after the container: if
 * an instance dies without cleanup, its cable can outlive it on the bridge
 * with the same MAC as its successor's, and a name that does not change is
 * what lets the next start find that leftover and replace it. */
static void veth_host_name(const struct ds_config *cfg, pid_t pid, char *buf,
                           size_t sz) {
  if (cfg && cfg->net_mode == DS_NET_GATEWAY)
    snprintf(buf, sz, "%s%08x", app_veth_host_prefix(cfg),
             ds_net_hash_string(cfg->container_name));
  else
    snprintf(buf, sz, "%s%d", app_veth_host_prefix(cfg), (int)pid);
}

/* Derive the peer (container-side) veth name from a container init PID.
 * Gateway clients use "ds-q" to match the distinct host-side prefix. */
static void veth_peer_name(const struct ds_config *cfg, pid_t pid, char *buf,
                           size_t sz) {
  const char *p = (cfg && cfg->net_mode == DS_NET_GATEWAY) ? "ds-q" : "ds-p";
  snprintf(buf, sz, "%s%d", p, (int)pid);
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

/* Derive a stable, locally-administered unicast MAC from a key string, salted
 * with a domain prefix so the two callers never collide on the same key.
 *
 *   "ds-mac:"   <container name>  - the container's own eth0.  A gateway (e.g.
 *                                   OpenWrt) then sees one persistent host
 *                                   across restarts - one DHCP lease / LuCI
 *                                   entry - instead of a fresh random MAC.
 *   "ds-gwmac:" <segment key>     - a gateway LAN-side veth (becomes e.g. eth1
 *                                   inside OpenWrt).  Keeps the same MAC across
 *                                   every (re)plug so netifd sees one
 *                                   persistent device, not a new one each time.
 */
static void ds_derive_mac(const char *key, const char *salt_prefix,
                          uint8_t mac[6]) {
  uint32_t h1 = ds_net_hash_string(key);
  char salted[512];
  snprintf(salted, sizeof(salted), "%s%s", salt_prefix, key ? key : "");
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

/* Open a container's network namespace for the host side to plug cables
 * into or act in. Fails closed when the namespace turns out to be our own: a
 * pid that was recycled, or an init that died before its monitor noticed,
 * would otherwise have the host create an "eth0" in the host's own namespace,
 * or wire and query itself. */
static int open_container_netns(const char *netns_path) {
  int fd = open(netns_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat target, self;
  if (fstat(fd, &target) == 0 && stat("/proc/self/ns/net", &self) == 0 &&
      target.st_ino == self.st_ino && target.st_dev == self.st_dev) {
    ds_warn("[NET] %s is the host's own network namespace - refusing to use it",
            netns_path);
    close(fd);
    errno = EPERM;
    return -1;
  }
  return fd;
}

static int ds_netns_rename_up(const char *netns_path, const char *old_name,
                              const char *new_name) {
  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return -errno;

  int target_fd = open_container_netns(netns_path);
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
    /* Already present under its final name inside the gateway - the common,
     * healthy case: the atomic move+rename created it, or this is an idempotent
     * re-entry.  Just make sure it is up; the caller logs "uplink ready". */
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

/* Return 1 if interface `ifname` exists inside the netns at `netns_path`, else
 * 0.  Used to tell a healthy gateway cable (its peer really lives inside the
 * CURRENT gateway netns) apart from a stale host-side veth whose peer is
 * stranded in a zombie netns - one kept alive past container stop by a leftover
 * process (e.g. tailscaled).  On any error it returns 0, i.e. "not present", so
 * the caller rebuilds the cable: the safe default. */
static int netns_has_link(const char *netns_path, const char *ifname) {
  if (!netns_path || !ifname || !ifname[0])
    return 0;

  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return 0;
  int target_fd = open_container_netns(netns_path);
  if (target_fd < 0) {
    close(self_fd);
    return 0;
  }

  int present = 0;
  if (setns(target_fd, CLONE_NEWNET) == 0) {
    ds_nl_ctx_t *ctx = ds_nl_open();
    if (ctx) {
      present = ds_nl_link_exists(ctx, ifname) ? 1 : 0;
      ds_nl_close(ctx);
    }
    if (setns(self_fd, CLONE_NEWNET) < 0)
      ds_warn("[NET] Gateway: failed to restore netns after liveness check: %s",
              strerror(errno));
  }

  close(target_fd);
  close(self_fd);
  return present;
}

/* Uplink routing globals - shared by android routing setup and monitor */

static int g_current_gw_table = 0;
static int g_current_gw6_table = 0; /* same, for the uplink's IPv6 routes */
static pthread_mutex_t g_gw_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_route_monitor_sock = -1;
static volatile sig_atomic_t g_stop_monitor = 0;
static pthread_t g_route_monitor_tid;
static int g_route_monitor_started = 0; /* loop active; guarded by g_gw_mutex */
static int g_route_monitor_needs_join = 0; /* thread created, not yet joined;
                                              guarded by g_gw_mutex */
/* Set once any container installs localhost port-forward DNAT rules.
 * Sticky - never cleared, since route_localnet=1 is harmless to leave on
 * and other containers may still depend on it. */
static volatile sig_atomic_t g_local_forward_active = 0;

/* User-pinned upstream interfaces (--upstream).  When non-empty, the uplink is
 * resolved ONLY from this list (priority order, literals + wildcards) and all
 * automatic detection is disabled - an explicit manual override.  Copied from
 * cfg in setup_veth_host_side(), before routing setup and the monitor read it.
 */
static char g_upstream_ifaces[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];
static int g_upstream_count = 0;

/* Shape of the host-side rules this monitor installed, captured in
 * setup_veth_host_side() before the monitor thread starts and only rewritten
 * from that same path on a container reboot.  The monitor replays exactly
 * these when Android's netd flushes the tables out from under a running
 * container - see install_netfilter_rules(). */
static int g_host_bridgeless = 0;
static char g_host_veth[IFNAMSIZ];
static struct ds_port_forward g_host_port_forwards[DS_MAX_PORT_FORWARDS];
static int g_host_port_forward_count = 0;
static char g_host_container_ip[INET_ADDRSTRLEN];
static int g_host_nat6 = 0; /* NAT66 is set up, replay its rules too */
static pid_t g_host_init_pid = 0;
static int g_host_dns_managed = 0; /* resolv.conf holds our default list */

/* Returns 1 if ifname exists and is both UP and RUNNING.
 * On Android, the active data interface has IFF_RUNNING set; an interface
 * that is physically present but not carrying data loses IFF_RUNNING. */
static int iface_is_running(const char *ifname) {
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
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

/* Built-in uplink classification (no user configuration)
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
 * placeholders, and our own bridge/veth devices. */

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

/* Public helper: populate a ds_net_handshake from a container init PID */

void ds_net_derive_handshake(pid_t init_pid, struct ds_config *cfg,
                             struct ds_net_handshake *hs) {
  veth_peer_name(cfg, init_pid, hs->peer_name, sizeof(hs->peer_name));
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

/* Host-side networking setup (before container boot) */

/* The default resolvers in the two orders an uplink can call for. Same lines,
 * so same length: sync_resolv_order() swaps one for the other in place. */
#define DS_RESOLV_CONF "/run/droidspaces/resolv.conf"
#define DNS_LINE(ip) "nameserver " ip "\n"
static const char k_dns_v4_first[] = DNS_LINE(DS_DNS_DEFAULT_1)
    DNS_LINE(DS_DNS_DEFAULT_2) DNS_LINE(DS_DNS_DEFAULT_6);
static const char k_dns_v6_first[] = DNS_LINE(DS_DNS_DEFAULT_6)
    DNS_LINE(DS_DNS_DEFAULT_1) DNS_LINE(DS_DNS_DEFAULT_2);

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
  if (count == 0 && size >= sizeof(k_dns_v4_first)) {
    memcpy(out, k_dns_v4_first, sizeof(k_dns_v4_first));
    count = 3;
  }

  return count;
}

/* Static NAT IP: validation, collision check, and resolution */

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
  uint32_t hash = ds_net_hash_string(cfg->container_name);

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

/* Android-specific policy routing
 *
 * Detects the active uplink (the routing table netd designates as the
 * default internet network), then injects low-priority ip rules to direct
 * container traffic through that table.  Fully automatic - the route
 * monitor keeps the rule in sync across wifi <-> mobile-data handoffs. */

/* Forward declaration - defined later in this file after route monitor globals
 */
static int find_active_uplink(ds_nl_ctx_t *ctx, char *iface_out,
                              int *table_out);

/* Install (or reinstall) the container policy rules.  Single source of truth
 * for the FIB rule set: container start calls it once, and the route monitor
 * calls it on every cycle, because Android's netd flushes the whole rule table
 * when it restarts and strands every running container (see the monitor's
 * reconciliation notes).  ds_nl_add_rule4() maps EEXIST to success, so
 * re-running this is a silent no-op once the rules are in place - which is why
 * it stays quiet and lets the caller report failures.
 *
 * DS_RULE_PRIO_TO_SUBNET (6090): inbound traffic to our subnet always resolves
 * via the main table.  Installed even with no uplink - without it the host
 * cannot reach the container at all.
 *
 * DS_RULE_PRIO_TETHER (6095): replies from our subnet to hotspot/USB-tether
 * clients must consult netd's local_network table (which holds every
 * downstream interface's connected route and no default route) before the
 * uplink table grabs them.  Also uplink-independent, so tether clients can
 * reach forwarded ports even with no WAN.
 *
 * DS_RULE_PRIO_FROM_SUBNET (6100): traffic from our subnet → the uplink's
 * internet table, so it only goes in once an uplink has been selected.
 *
 * All three sit above Android's VPN rule range (10000–22000), so they are
 * checked FIRST and container traffic is never hijacked by a VPN catch-all,
 * and above the OEM reserved low-priority rules (typically < 1000).
 *
 * Returns 0 when every applicable rule is in place, -1 if any add failed. */
static int install_policy_rules(ds_nl_ctx_t *ctx) {
  uint32_t subnet_be, mask_be;
  parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
  (void)mask_be;

  int ret = 0;

  if (ds_nl_add_rule4(ctx, 0, 0, subnet_be, DS_NAT_PREFIX, RT_TABLE_MAIN,
                      DS_RULE_PRIO_TO_SUBNET) < 0)
    ret = -1;

  if (ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0,
                      DS_ANDROID_TABLE_LOCAL_NETWORK, DS_RULE_PRIO_TETHER) < 0)
    ret = -1;

  pthread_mutex_lock(&g_gw_mutex);
  int gw_table = g_current_gw_table;
  pthread_mutex_unlock(&g_gw_mutex);

  if (gw_table > 0 && ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0,
                                      gw_table, DS_RULE_PRIO_FROM_SUBNET) < 0)
    ret = -1;

  /* The same TO and FROM pair for the NAT66 prefix. No tether rule: hotspot
   * clients reach forwarded ports over IPv4, and --port is IPv4 only. */
  if (g_host_nat6) {
    struct in6_addr net6;
    inet_pton(AF_INET6, DS_NAT6_PREFIX, &net6);

    pthread_mutex_lock(&g_gw_mutex);
    int gw6_table = g_current_gw6_table;
    pthread_mutex_unlock(&g_gw_mutex);

    if (ds_nl_rule6(ctx, 1, &net6, DS_NAT6_PREFIX_LEN, 0, RT_TABLE_MAIN,
                    DS_RULE_PRIO_TO_SUBNET) < 0)
      ret = -1;
    if (gw6_table > 0 && ds_nl_rule6(ctx, 1, &net6, DS_NAT6_PREFIX_LEN, 1,
                                     gw6_table, DS_RULE_PRIO_FROM_SUBNET) < 0)
      ret = -1;
  }

  return ret;
}

/* Point the IPv6 FROM rule at the table of the uplink IPv4 just settled on.
 * On an IPv6-only carrier the IPv4 uplink is the CLAT device v4-rmnetX, whose
 * IPv6 routes live in the table of the real rmnetX underneath, so the prefix
 * is stripped. An uplink with no IPv6 route (or iface == NULL) clears the
 * rule, and the container simply keeps using IPv4. */
static void sync_uplink6(ds_nl_ctx_t *ctx, const char *iface) {
  if (!g_host_nat6)
    return;

  int table = 0;
  if (iface) {
    if (strncmp(iface, "v4-", 3) == 0)
      iface += 3;
    ds_nl_get_iface_table(ctx, AF_INET6, iface, &table);
  }

  pthread_mutex_lock(&g_gw_mutex);
  int old = g_current_gw6_table;
  g_current_gw6_table = table;
  pthread_mutex_unlock(&g_gw_mutex);
  if (old == table)
    return;

  struct in6_addr net6;
  inet_pton(AF_INET6, DS_NAT6_PREFIX, &net6);
  if (old > 0)
    ds_nl_rule6(ctx, 0, &net6, DS_NAT6_PREFIX_LEN, 1, old,
                DS_RULE_PRIO_FROM_SUBNET);
  if (table > 0)
    ds_nl_rule6(ctx, 1, &net6, DS_NAT6_PREFIX_LEN, 1, table,
                DS_RULE_PRIO_FROM_SUBNET);
  ds_log("[NET] IPv6 uplink table %d → %d (from %s, prio %d)", old, table,
         DS_NAT6_SUBNET, DS_RULE_PRIO_FROM_SUBNET);
}

/* glibc asks the nameservers in file order and waits out a timeout on every
 * one it cannot reach. On an IPv6-only uplink the two IPv4 defaults in front
 * cost each lookup several seconds, and the IPv6 one in front would do the
 * same on an IPv4-only network. So the default list follows the uplink.
 *
 * The file lives inside the container, whose root can put anything at that
 * path. We only ever swap one of our two lists for the other: a file holding
 * anything else, a hand edit included, is left alone, and so is whatever is
 * not a regular file on tmpfs. */
static void sync_resolv_order(int v6_first) {
  if (!g_host_dns_managed)
    return;

  const char *want = v6_first ? k_dns_v6_first : k_dns_v4_first;
  const char *stale = v6_first ? k_dns_v4_first : k_dns_v6_first;
  size_t len = sizeof(k_dns_v4_first) - 1;

  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/root" DS_RESOLV_CONF,
           (int)g_host_init_pid);
  int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return;

  /* One byte more than a list, so a longer file can never compare equal. */
  char cur[sizeof(k_dns_v4_first) + 1] = {0};
  struct stat st;
  struct statfs sfs;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && fstatfs(fd, &sfs) == 0 &&
      sfs.f_type == TMPFS_MAGIC && read(fd, cur, sizeof(cur) - 1) >= 0 &&
      strcmp(cur, stale) == 0 && pwrite(fd, want, len, 0) == (ssize_t)len)
    ds_log("[NET] Resolver order: IPv%d first", v6_first ? 6 : 4);
  close(fd);
}

static void ds_net_setup_android_routing(ds_nl_ctx_t *ctx) {
  char active_iface[IFNAMSIZ] = {0};
  int gw_table = 0;

  /* No uplink yet: find_active_uplink() already logged the single "no WAN"
   * line.  The uplink-independent rules still go in below; the route monitor
   * adds the FROM rule once an interface appears. */
  if (find_active_uplink(ctx, active_iface, &gw_table) == 0) {
    /* Table 0 is an IPv6-only uplink: no IPv4 rule, sync_uplink6() logs. */
    if (gw_table > 0)
      ds_log("[NET] Android routing: active uplink %s → table %d, rule from %s "
             "lookup table %d (prio %d)",
             active_iface, gw_table, DS_DEFAULT_SUBNET, gw_table,
             DS_RULE_PRIO_FROM_SUBNET);
    /* Seed the monitor's current table so it knows the baseline */
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = gw_table;
    pthread_mutex_unlock(&g_gw_mutex);
    sync_uplink6(ctx, active_iface);
  }

  if (install_policy_rules(ctx) < 0)
    ds_warn("[NET] Android routing: one or more policy rules failed to install "
            "- container reachability may be degraded");
}

/* TX checksum disable (Samsung/MTK kernel workaround) */

int ds_net_disable_tx_checksum(const char *ifname) {
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
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

/* install_netfilter_rules
 *
 * Install (or reinstall) the host-side netfilter rules for this container's
 * topology.  Counterpart to install_policy_rules(): the single source of truth
 * for the iptables side, called once at container start and again by the route
 * monitor after netd flushes the tables.  Every helper is idempotent.
 *
 * Reads the g_host_* snapshot rather than cfg, so the monitor thread can call
 * it without reaching into a struct the reboot path rewrites in place. */

/* Interface the filter rules match on: bridge mode filters on the bridge,
 * bridgeless PTP filters on the veth itself.  Shared by the installer and the
 * monitor's presence probe so the two can never disagree about what to look
 * for.  Empty until setup_veth_host_side() has taken its snapshot. */
static const char *host_filter_iface(void) {
  return g_host_bridgeless ? g_host_veth : DS_NAT_BRIDGE;
}

/* One family's rule set. Returns the MASQUERADE result, the one rule without
 * which that family has no connectivity at all. */
static int install_family_rules(int family, const char *subnet) {
  const char *iface = host_filter_iface();
  const char *label = (family == AF_INET6) ? "IPv6" : "IPv4";

  if (iface[0]) {
    ds_ipt_ensure_input_accept(family, iface);
    if (ds_ipt_ensure_forward_accept(family, iface) < 0)
      ds_warn("[NET] %s FORWARD ACCEPT failed", label);
  }

  int masq = ds_ipt_ensure_masquerade(family, subnet);
  if (masq < 0)
    ds_warn("[NET] %s MASQUERADE rule failed", label);

  ds_ipt_ensure_mss_clamp(family);
  return masq;
}

static void install_netfilter_rules(void) {
  install_family_rules(AF_INET, DS_DEFAULT_SUBNET);
  if (g_host_nat6)
    install_family_rules(AF_INET6, DS_NAT6_SUBNET);

  if (g_host_port_forward_count > 0 && g_host_container_ip[0])
    ds_ipt_add_portforwards(g_host_port_forwards, g_host_port_forward_count,
                            g_host_container_ip);
}

/* Turning on IPv6 forwarding makes the kernel ignore Router Advertisements on
 * every interface left at accept_ra=1. A host that got its own address by
 * SLAAC would then lose its default route when the current one expires. 2
 * means "accept even while forwarding". Android already runs its uplinks at 2,
 * and if forwarding is already on, whoever enabled it has dealt with this. */
static void keep_host_slaac(void) {
  char val[4] = {0};
  if (read_file("/proc/sys/net/ipv6/conf/all/forwarding", val, sizeof(val)) >
          0 &&
      val[0] == '1')
    return;

  DIR *d = opendir("/proc/sys/net/ipv6/conf");
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.' || !strcmp(e->d_name, "all") ||
        !strcmp(e->d_name, "lo") || !strncmp(e->d_name, "ds-", 3))
      continue;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/accept_ra",
             e->d_name);
    if (read_file(path, val, sizeof(val)) > 0 && val[0] == '1')
      write_file(path, "2");
  }
  closedir(d);
}

/* NAT66 host side: the IPv6 mirror of steps 1, 2 and 5 below, modelled on
 * LXC's lxc-net (ULA on the bridge, forwarding, one MASQUERADE rule, RAs).
 *
 * Runs once the bridge or the bridgeless veth is up. Fills *prefix with the
 * /64 this container should be advertised and returns 1, or returns 0 when
 * IPv6 stays off: the user disabled it, the kernel has no IPv6, or it has no
 * ip6tables nat table. In that last case the container keeps a link-local
 * address only and everything goes over IPv4 as before. */
static int setup_nat6_host(ds_nl_ctx_t *ctx, struct ds_config *cfg,
                           struct in6_addr *prefix) {
  g_host_nat6 = 0;
  if (cfg->disable_ipv6 ||
      access("/proc/sys/net/ipv6/conf/all/forwarding", F_OK) != 0)
    return 0;

  /* Not a warning: stock GKI kernels have no IPv6 nat table, and telling
   * every one of those users on every start would be noise. `droidspaces
   * check` reports it. */
  if (!ds_ipt6_available()) {
    ds_log("[NET] IPv6 NAT unavailable (the kernel lacks CONFIG_IP6_NF_NAT or "
           "CONFIG_IP6_NF_TARGET_MASQUERADE) - container stays IPv4 only");
    return 0;
  }

  /* Our end is a router: it must have IPv6 on even if the host default is
   * off, and it must never autoconfigure itself from a container's RA.
   *
   * accept_ra_rt_table only exists on Android kernels, where its default of
   * -1000 makes the kernel file an interface's IPv6 connected routes in a
   * private table (1000 + ifindex) instead of main. 0 means main, which is
   * where our policy rule sends traffic for the container prefix. */
  const char *iface = host_filter_iface();
  static const char *const knobs[][2] = {{"disable_ipv6", "0"},
                                         {"accept_ra", "0"},
                                         {"autoconf", "0"},
                                         {"accept_ra_rt_table", "0"}};
  for (size_t i = 0; i < sizeof(knobs) / sizeof(knobs[0]); i++) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/%s", iface,
             knobs[i][0]);
    write_file(path, knobs[i][1]);
  }

  struct in6_addr gw_ll;
  inet_pton(AF_INET6, DS_NAT6_GW_LL, &gw_ll);
  inet_pton(AF_INET6, DS_NAT6_PREFIX, prefix);

  int err = ds_nl_add_addr6(ctx, iface, &gw_ll, 64);
  if (cfg->net_bridgeless) {
    /* No shared segment, so each container gets its own /64 routed at its
     * veth. Reusing the last two octets of the IPv4 address means the IPv4
     * allocator's uniqueness check covers this prefix too. */
    struct in_addr v4;
    if (inet_pton(AF_INET, cfg->static_nat_ip, &v4) != 1)
      return 0;
    memcpy(&prefix->s6_addr[6], (uint8_t *)&v4 + 2, 2);
  } else if (err == 0) {
    struct in6_addr gw = *prefix;
    gw.s6_addr[15] = 1;
    err = ds_nl_add_addr6(ctx, iface, &gw, 64);
  }

  /* Route the prefix in the main table ourselves, in both modes. Bridgeless
   * has no address in the prefix, so nothing else would create the route.
   * Bridge mode gets one from the kernel, but on Android it may sit in that
   * private per-interface table: a bridge that existed before the sysctl
   * above was written keeps its route there. Replies to the container are
   * looked up in main, and without this they die as "network unreachable"
   * while outbound packets and IPv4 carry on working. */
  if (err == 0)
    err = ds_nl_add_route6(ctx, prefix, 64, ds_nl_get_ifindex(ctx, iface));
  if (err < 0) {
    ds_warn("[NET] IPv6 setup on %s failed (%d) - container stays IPv4 only",
            iface, err);
    return 0;
  }

  /* Fail closed: without MASQUERADE the container would be handed an address
   * and a default route that lead nowhere, and every IPv6 connection would
   * hang until it timed out instead of falling back to IPv4 at once. A kernel
   * with the nat table but no CONFIG_IP6_NF_TARGET_MASQUERADE lands here. */
  if (install_family_rules(AF_INET6, DS_NAT6_SUBNET) < 0)
    return 0;

  keep_host_slaac();
  write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1");
  g_host_nat6 = 1;

  char pfx[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, prefix, pfx, sizeof(pfx));
  ds_log("[NET] IPv6 NAT enabled: advertising %s/64 via %s", pfx,
         DS_NAT6_GW_LL);
  return 1;
}

/* setup_veth_host_side
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
 *   7. Android policy routing */

int setup_veth_host_side(struct ds_config *cfg, pid_t child_pid) {
  char veth_host[IFNAMSIZ], veth_peer[IFNAMSIZ];
  veth_host_name(cfg, child_pid, veth_host, sizeof(veth_host));
  veth_peer_name(cfg, child_pid, veth_peer, sizeof(veth_peer));

  ds_log("Setting up host-side NAT networking for %s (PID %d)...",
         cfg->container_name, (int)child_pid);

  /* On a reboot cycle the snapshot below still describes the previous one.
   * Keep its veth name: nobody else is going to delete it. */
  char prev_veth[IFNAMSIZ];
  int prev_bridgeless = g_host_bridgeless;
  safe_strncpy(prev_veth, g_host_veth, sizeof(prev_veth));

  /* Snapshot everything install_netfilter_rules() needs, before anything else
   * touches the tables.  The monitor thread replays these to rebuild the rules
   * after a netd flush and must not read cfg directly - the reboot path
   * rewrites *cfg in place.  static_nat_ip is already resolved and persisted
   * before the fork, and is exactly what nat_container_ip is set to below. */
  g_host_bridgeless = cfg->net_bridgeless;
  g_host_init_pid = child_pid;
  g_host_dns_managed = cfg->net_mode == DS_NET_NAT && !cfg->dns_servers[0];
  safe_strncpy(g_host_veth, veth_host, sizeof(g_host_veth));
  g_host_port_forward_count = cfg->port_forward_count;
  memcpy(g_host_port_forwards, cfg->port_forwards,
         sizeof(g_host_port_forwards));
  safe_strncpy(g_host_container_ip, cfg->static_nat_ip,
               sizeof(g_host_container_ip));

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[NET] Failed to open RTNETLINK socket");
    return -1;
  }

  /* Clean up stale interfaces from previous runs */
  ds_log("[DEBUG] Cleaning up any stale interfaces: %s, %s", veth_host,
         veth_peer);
  ds_nl_del_link(ctx, veth_host);

  /* The previous boot cycle's veth. The kernel only removes it once the old
   * network namespace is freed, and a guest that was killed with TCP
   * connections open keeps that namespace alive for minutes while its
   * orphaned sockets retransmit into a bridge that now sends the replies to
   * the new instance. Until then the leftover counts as another running
   * container, so the last stop would keep the shared rules in place. */
  if (prev_veth[0] && strcmp(prev_veth, veth_host) != 0) {
    ds_nl_del_link(ctx, prev_veth);
    if (prev_bridgeless) {
      ds_ipt_remove_iface_rules(AF_INET, prev_veth);
      ds_ipt_remove_iface_rules(AF_INET6, prev_veth);
    }
  }

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
    } else {
      write_file("/proc/sys/net/ipv4/conf/all/rp_filter", "0");
      write_file("/proc/sys/net/ipv4/conf/default/rp_filter", "0");
    }
  }

  /* 2. iptables rules - the same set the route monitor reinstalls whenever
   * netd flushes the tables out from under us. */
  install_netfilter_rules();

  /* 3. Create the veth pair with the peer born inside the container's netns.
   * Creating it here and moving it afterwards cost a second netdev
   * unregister and register, about 50 ms of RCU waits, inside init's wait
   * for DONE. init has already signalled readiness, so its netns exists. */
  char netns_path[PATH_MAX];
  snprintf(netns_path, sizeof(netns_path), "/proc/%d/ns/net", child_pid);
  int netns_fd = open_container_netns(netns_path);
  if (netns_fd < 0) {
    ds_warn("[NET] Failed to open container netns %s: %s", netns_path,
            strerror(errno));
    ds_nl_close(ctx);
    return -1;
  }
  ds_log("[DEBUG] Creating veth pair %s <-> %s (peer in netns of PID %d)...",
         veth_host, veth_peer, (int)child_pid);
  int created = ds_nl_create_veth_in(ctx, veth_host, veth_peer, netns_fd, NULL);
  close(netns_fd);
  if (created < 0) {
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

  /* 5b. IPv6. Before the Android routing step, which adds the IPv6 policy
   * rules only when this succeeded. */
  struct in6_addr ra_prefix;
  int nat6 = setup_nat6_host(ctx, cfg, &ra_prefix);

  /* Cache the user-pinned upstream list (if any) into the globals that the
   * routing setup and the route monitor read.  Empty list = auto-detect.  This
   * runs before ds_net_setup_android_routing() and
   * ds_net_start_route_monitor(), which both consult g_upstream_*.  The count
   * is already capped at DS_MAX_UPSTREAM_IFACES by the parsers. */
  g_upstream_count = cfg->upstream_iface_count;
  for (int _i = 0; _i < g_upstream_count; _i++)
    safe_strncpy(g_upstream_ifaces[_i], cfg->upstream_ifaces[_i], IFNAMSIZ);

  /* 7. Android policy routing - uplink is auto-detected, or pinned via
   * --upstream. */
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
    ds_dhcp_server_start(cfg, dhcp_iface, offer_ip, inet_addr(DS_NAT_GW_IP),
                         nat6 ? &ra_prefix : NULL);

    /* Store the container IP string (plain dotted-decimal) for port-forward
     * cleanup later.  static_nat_ip is already in that exact format. */
    safe_strncpy(cfg->nat_container_ip, cfg->static_nat_ip,
                 sizeof(cfg->nat_container_ip));

    /* --port DNAT/FORWARD rules were installed with the rest of the netfilter
     * set above, from the same snapshot the monitor replays. */
  }

  return 0;
}

/* Gateway segment lock
 *
 * One advisory file lock per delegated LAN segment (keyed on the bridge name).
 * Serialises (re)creation of the single shared gateway-side veth + bridge
 * across the independent monitor processes that touch a segment - concurrent
 * client starts, and the gateway re-wiring its clients on (re)boot - so they
 * cannot race each other into EEXIST half-states. */

static int gateway_segment_lock(const char *bridge) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/gw_%s.lock", get_net_dir(), bridge);
  int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0) {
    ds_warn("[NET] Gateway: could not open segment lock %s: %s", path,
            strerror(errno));
    return -1;
  }
  if (flock(fd, LOCK_EX) < 0) {
    ds_warn("[NET] Gateway: flock failed on %s: %s", path, strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

static void gateway_segment_unlock(int fd) {
  if (fd < 0)
    return;
  flock(fd, LOCK_UN);
  close(fd);
}

/* Gateway liveness */

/* Resolve the gateway container's init pid, or 0 if it is not running. */
static pid_t gateway_pid_of(const char *name) {
  struct ds_config c;
  memset(&c, 0, sizeof(c));
  c.net_ready_pipe[0] = c.net_ready_pipe[1] = -1;
  c.net_done_pipe[0] = c.net_done_pipe[1] = -1;
  safe_strncpy(c.container_name, name, sizeof(c.container_name));
  (void)ds_config_load_by_name(name, &c);
  pid_t p = 0;
  if (!is_container_running(&c, &p))
    p = 0;
  ds_config_free(&c);
  return p > 0 ? p : 0;
}

/* gateway_ensure_lan_uplink_locked
 *
 * Ensure the shared half of a delegated LAN segment is wired into the running
 * gateway container.  The caller holds the segment lock and has already
 * confirmed the gateway is up, passing its netns pid:
 *   - create/reuse the IP-less, policy-neutral bridge
 *   - ensure the gateway-side veth (ds-g<hash>) exists with its peer living in
 *     the gateway netns as gw_if (e.g. eth1)
 *
 * Idempotent: a repeat call is a cheap reattach when the cable is genuinely
 * healthy (its peer lives inside the CURRENT gateway netns).  When the cable is
 * absent - or present but stale (peer stranded in a zombie netns that outlived
 * the previous gateway) - we plug a fresh one into the gateway's (possibly
 * just-rebooted) netns.  This is what heals clients after a gateway restart,
 * with no client restart.
 *
 * Returns 0 when the gateway-side cable is up, -1 on a netlink failure. */

static int gateway_ensure_lan_uplink_locked(struct ds_config *cfg,
                                            pid_t gw_pid) {
  if (!cfg || !cfg->gateway_container[0] || gw_pid <= 0)
    return -1;

  const char *gw_if = gateway_lan_ifname(cfg);
  if (strlen(gw_if) >= IFNAMSIZ || !gateway_ifname_component_ok(gw_if)) {
    ds_warn("[NET] Gateway: invalid gateway interface name '%s'", gw_if);
    return -1;
  }

  char bridge[IFNAMSIZ], gw_host[IFNAMSIZ], gw_peer[IFNAMSIZ];
  gateway_bridge_name(cfg, bridge, sizeof(bridge));
  gateway_veth_names(cfg, gw_host, sizeof(gw_host), gw_peer, sizeof(gw_peer));

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[NET] Gateway: failed to open RTNETLINK socket");
    return -1;
  }

  /* Bridge first, unconditionally: an IP-less, policy-neutral switch.  Disable
   * bridge netfilter calls so OpenWrt's own firewalling stays the only policy
   * authority on the delegated network. */
  if (!ds_nl_link_exists(ctx, bridge)) {
    ds_log("[NET] Gateway: creating delegated LAN bridge %s", bridge);
    int br = ds_nl_create_bridge(ctx, bridge);
    if (br < 0) {
      /* Bail before any veth/netns work.  This should be unreachable - the
       * startup capability probe (enforce_nat_safety) refuses gateway mode on
       * a kernel without CONFIG_BRIDGE - but recognise EOPNOTSUPP explicitly
       * so we never cascade into further unsupported operations. */
      if (br == -EOPNOTSUPP)
        ds_warn("[NET] Gateway: kernel lacks CONFIG_BRIDGE - cannot wire "
                "delegated LAN (gateway mode requires bridge support)");
      else
        ds_warn("[NET] Gateway: failed to create bridge %s", bridge);
      ds_nl_close(ctx);
      return -1;
    }
  }
  /* IP-less has to hold for IPv6 too. Android leaves accept_ra at 2, so the
   * host would otherwise take an address and a default route from the
   * gateway's own router advertisements and sit on the LAN it only switches. */
  char v6_off[128];
  snprintf(v6_off, sizeof(v6_off), "/proc/sys/net/ipv6/conf/%s/disable_ipv6",
           bridge);
  write_file(v6_off, "1");
  if (ds_nl_link_up(ctx, bridge) < 0)
    ds_warn("[NET] Gateway: failed to bring up bridge %s", bridge);
  write_file("/proc/sys/net/bridge/bridge-nf-call-iptables", "0");
  write_file("/proc/sys/net/bridge/bridge-nf-call-ip6tables", "0");

  char gw_netns[PATH_MAX];
  snprintf(gw_netns, sizeof(gw_netns), "/proc/%d/ns/net", (int)gw_pid);

  /* Host-side cable present.  This does NOT prove the peer is inside the
   * CURRENT gateway netns: if a process kept the previous gateway's netns alive
   * past `stop` (e.g. tailscaled), the veth pair survived and its peer is
   * stranded there - so a stale ds-g<hash> can outlive the gateway it was built
   * for. Verify gw_if actually exists inside this gateway before trusting the
   * cable.
   *   - peer live in this netns → idempotent no-op: re-assert master + up.
   *   - peer absent           → stale cable: delete it (which also reaps the
   *                             stranded peer, veth pairs die together) and
   * fall through to build a fresh one into this netns. */
  if (ds_nl_link_exists(ctx, gw_host)) {
    if (netns_has_link(gw_netns, gw_if)) {
      if (ds_nl_set_master(ctx, gw_host, bridge) < 0)
        ds_warn("[NET] Gateway: failed to reattach %s to %s", gw_host, bridge);
      ds_nl_link_up(ctx, gw_host);
      ds_nl_close(ctx);
      return 0;
    }
    ds_warn("[NET] Gateway: stale cable %s (peer not in gateway netns) - "
            "rebuilding",
            gw_host);
    ds_nl_del_link(ctx, gw_host);
  }

  ds_log("[NET] Gateway: creating gateway veth %s <-> %s", gw_host, gw_peer);
  if (ds_nl_create_veth(ctx, gw_host, gw_peer) < 0) {
    ds_warn("[NET] Gateway: failed to create gateway veth pair");
    ds_nl_close(ctx);
    return -1;
  }

  /* Pin a stable, segment-derived MAC on the gateway-facing peer (becomes gw_if
   * inside the gateway) so netifd sees one persistent device across every
   * re-plug.  Set while down, before the move. */
  {
    char key[384];
    uint8_t mac[6];
    gateway_hash_key(cfg, key, sizeof(key));
    ds_derive_mac(key, "ds-gwmac:", mac);
    if (ds_nl_set_mac(ctx, gw_peer, mac) < 0)
      ds_warn("[NET] Gateway: failed to pin MAC on %s", gw_peer);
  }

  ds_net_disable_tx_checksum(gw_host);
  if (ds_nl_set_master(ctx, gw_host, bridge) < 0)
    ds_warn("[NET] Gateway: failed to attach %s to %s", gw_host, bridge);
  if (ds_nl_link_up(ctx, gw_host) < 0)
    ds_warn("[NET] Gateway: failed to bring up %s", gw_host);

  int gw_netns_fd = open_container_netns(gw_netns);
  if (gw_netns_fd < 0) {
    ds_warn("[NET] Gateway: failed to open %s: %s", gw_netns, strerror(errno));
    ds_nl_del_link(ctx, gw_host); /* drop the half-built pair */
    ds_nl_close(ctx);
    return -1;
  }

  /* Atomic move+rename: the peer appears inside the gateway already named
   * gw_if, so there is no transient raw-name device for netifd to race against
   * ("device initialization failed").  Fall back to a plain move on failure. */
  if (ds_nl_move_to_netns_named(ctx, gw_peer, gw_netns_fd, gw_if) != 0) {
    ds_warn("[NET] Gateway: atomic move+rename of %s failed - falling back",
            gw_peer);
    if (ds_nl_move_to_netns(ctx, gw_peer, gw_netns_fd) < 0) {
      ds_warn("[NET] Gateway: fallback move of %s into gateway netns failed",
              gw_peer);
      close(gw_netns_fd);
      ds_nl_close(ctx);
      return -1;
    }
  }
  close(gw_netns_fd);
  ds_nl_close(ctx);

  /* Bring it up inside the gateway under its final name (the atomic path
   * already renamed it; the fallback path renames here). */
  if (ds_netns_rename_up(gw_netns, gw_peer, gw_if) < 0)
    ds_warn("[NET] Gateway: moved %s but could not bring it up as %s", gw_peer,
            gw_if);

  ds_log("[NET] Gateway: LAN uplink ready on %s -> %s (%s)", bridge,
         cfg->gateway_container, gw_if);
  return 0;
}

/* gateway_wire_client
 *
 * Fully wire ONE gateway-mode client into its delegated LAN, entirely from the
 * host side.  Because the host owns every step - including bringing eth0 up
 * *inside* the client netns - this works identically whether the client is
 * just starting (its netns fresh, child blocked on the handshake) or already
 * running (the gateway came up later and is wiring it). The gateway-mode child
 * only brings up lo; it never touches eth0.
 *
 * Under the segment lock: ensure bridge + gateway uplink, then, unless the
 * client's cable is already in place, create the app veth with its peer born
 * inside the client netns as eth0 with the client's stable MAC, attach the
 * host end to the bridge and bring both ends up.  Returns 0 on success.
 * Caller passes the client's init pid and the confirmed-running gateway's
 * init pid. */

static int gateway_wire_client(struct ds_config *cfg, pid_t client_pid,
                               pid_t gateway_pid) {
  if (!cfg || client_pid <= 0 || gateway_pid <= 0)
    return -1;

  char bridge[IFNAMSIZ], app_host[IFNAMSIZ];
  gateway_bridge_name(cfg, bridge, sizeof(bridge));
  veth_host_name(cfg, client_pid, app_host, sizeof(app_host));

  /* One lock spans the shared uplink AND this client's app-veth attach, so a
   * concurrent client's cleanup cannot reap the bridge between them. */
  int lock = gateway_segment_lock(bridge);
  int ret = -1;

  if (gateway_ensure_lan_uplink_locked(cfg, gateway_pid) < 0) {
    ds_warn("[NET] Gateway: uplink for %s not ready - cannot wire '%s'", bridge,
            cfg->container_name);
    goto out;
  }

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[NET] Gateway: failed to open RTNETLINK socket");
    goto out;
  }

  char netns[PATH_MAX];
  snprintf(netns, sizeof(netns), "/proc/%d/ns/net", (int)client_pid);

  /* A client's cable runs to the host bridge, not through the gateway, so a
   * gateway restart leaves it intact and only the uplink above needs
   * rebuilding. Replacing a working cable takes eth0 away from under the
   * client's DHCP client: it loses its address, and on kernels before 5.x the
   * new link shows up under its raw name and is renamed to eth0 afterwards,
   * which older systemd-networkd (Ubuntu 22.04) never picks up again. So only
   * a cable that is really broken is rebuilt. */
  if (ds_nl_link_exists(ctx, app_host) && netns_has_link(netns, "eth0")) {
    /* Cheap and idempotent, in case the bridge itself was recreated */
    if (ds_nl_set_master(ctx, app_host, bridge) < 0)
      ds_warn("[NET] Gateway: failed to attach %s to %s", app_host, bridge);
    ds_nl_link_up(ctx, app_host);
    ds_nl_close(ctx);
    ret = 0;
    ds_log("Gateway: '%s' is still wired to %s, left as is",
           cfg->container_name, bridge);
    goto out;
  }

  ds_nl_del_link(ctx, app_host); /* drop any stale half from a prior wiring */

  int netns_fd = open_container_netns(netns);
  if (netns_fd < 0) {
    ds_warn("[NET] Gateway: failed to open client netns %s: %s", netns,
            strerror(errno));
    ds_nl_close(ctx);
    goto out;
  }

  /* The peer is created inside the client, as eth0 from its first moment, so
   * a client that is already running never sees a half-named link. Its MAC is
   * derived from the container name, so the gateway's DHCP leases see one
   * host across reboots. */
  uint8_t mac[6];
  ds_derive_mac(cfg->container_name, "ds-mac:", mac);
  ds_log("[NET] Gateway: creating app veth %s <-> eth0 in '%s'", app_host,
         cfg->container_name);
  int created = ds_nl_create_veth_in(ctx, app_host, "eth0", netns_fd, mac);
  close(netns_fd);
  if (created < 0) {
    ds_warn("[NET] Gateway: failed to create app veth pair");
    ds_nl_close(ctx);
    goto out;
  }

  ds_net_disable_tx_checksum(app_host);
  if (ds_nl_set_master(ctx, app_host, bridge) < 0)
    ds_warn("[NET] Gateway: failed to attach %s to %s", app_host, bridge);
  if (ds_nl_link_up(ctx, app_host) < 0)
    ds_warn("[NET] Gateway: failed to bring up %s", app_host);
  ds_nl_close(ctx);

  if (ds_netns_rename_up(netns, "eth0", "eth0") < 0)
    ds_warn("[NET] Gateway: wired '%s' but could not bring up its eth0",
            cfg->container_name);

  ret = 0;
  ds_log("Gateway: wiring complete for '%s': %s -> %s", cfg->container_name,
         bridge, cfg->gateway_container);

out:
  gateway_segment_unlock(lock);
  return ret;
}

/* setup_gateway_veth_side
 *
 * Called at client start.  OpenWrt gateway mode: Droidspaces owns only the L2
 * plumbing (bridge + veths), never NAT/DHCP/DNS/firewall - the gateway
 * container runs that policy.
 *
 * If the gateway is already running we wire this client now.  If it is NOT
 * running we skip ALL networking and return: the gateway wires every running
 * client itself when it (re)boots (ds_net_rewire_gateway_clients).  Deferring
 * instead of half-wiring also closes a race - a client started before its
 * gateway must not bake in gateway/LAN settings the user may still edit before
 * the gateway comes up. */

int setup_gateway_veth_side(struct ds_config *cfg, pid_t child_pid) {
  if (!cfg || !cfg->gateway_container[0]) {
    ds_warn("[NET] Gateway: no gateway container configured");
    return -1;
  }

  if (strcmp(cfg->gateway_container, cfg->container_name) == 0) {
    ds_warn("[NET] Gateway: refusing to attach container to itself");
    return -1;
  }

  pid_t gw_pid = gateway_pid_of(cfg->gateway_container);
  if (gw_pid <= 0) {
    ds_warn("Gateway: '%s' not running - deferring all networking for "
            "'%s'; the gateway will wire it when it starts",
            cfg->gateway_container, cfg->container_name);
    return 0;
  }

  ds_log("[NET] Gateway: wiring '%s' to gateway '%s'", cfg->container_name,
         cfg->gateway_container);
  return gateway_wire_client(cfg, child_pid, gw_pid);
}

/* ds_net_gateway_reconcile
 *
 * Called from a gateway client's own monitor until it returns 1, meaning the
 * client's cable is in place.
 *
 * A client that starts before its gateway is left for the gateway's boot scan
 * below, and a client that starts after it wires itself. Both sides decide by
 * looking for the other's pidfile, which only appears once that container's
 * init has exec'd. Two containers booting at the same moment therefore each
 * see the other as "not running", and neither ever wires the client. So the
 * client keeps looking until it is connected. */
int ds_net_gateway_reconcile(struct ds_config *cfg, pid_t client_pid) {
  if (!cfg || client_pid <= 0)
    return 0;

  char app_host[IFNAMSIZ];
  veth_host_name(cfg, client_pid, app_host, sizeof(app_host));
  ds_nl_ctx_t *ctx = ds_nl_open();
  int wired = ctx && ds_nl_link_exists(ctx, app_host);
  if (ctx)
    ds_nl_close(ctx);
  if (wired)
    return 1;

  pid_t gw_pid = gateway_pid_of(cfg->gateway_container);
  return gw_pid > 0 && gateway_wire_client(cfg, client_pid, gw_pid) == 0;
}

/* ds_net_rewire_gateway_clients
 *
 * Gateway self-heal, driven by the gateway itself.  On every boot the gateway
 * container's monitor calls this: it scans the running containers for the ones
 * that delegate to this gateway and (re)wires each into the gateway's current
 * netns via gateway_wire_client.  That one function also idempotently ensures
 * the shared LAN uplink, so a single pass covers the gateway-side cable and
 * every client's app veth.
 *
 * This restores clients after the gateway (re)boots - its old netns died and
 * took the LAN cable with it - and wires clients that were started while the
 * gateway was down.  One actor, no client restart. */
void ds_net_rewire_gateway_clients(const char *gateway_name,
                                   pid_t gateway_pid) {
  if (!gateway_name || !gateway_name[0] || gateway_pid <= 0)
    return;

  char containers_dir[PATH_MAX];
  snprintf(containers_dir, sizeof(containers_dir), "%s/Containers",
           get_workspace_dir());
  DIR *d = opendir(containers_dir);
  if (!d)
    return;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    struct ds_config c = {0};
    if (ds_config_load_by_name(ent->d_name, &c) != 0)
      continue;

    pid_t p = 0;
    if (c.net_mode == DS_NET_GATEWAY && c.gateway_container[0] &&
        strcmp(c.gateway_container, gateway_name) == 0 &&
        is_container_running(&c, &p) && p > 0)
      gateway_wire_client(&c, p, gateway_pid);

    ds_config_free(&c);
  }
  closedir(d);
}

/* ds_net_gateway_teardown
 *
 * Called when a container that ACTS AS A GATEWAY stops.  The gateway-side veth
 * ds-g<hash> lives in the host netns; its peer is the gateway's eth1.  When the
 * gateway stops the kernel does NOT auto-reap ds-g: the host-side veth itself
 * pins its now-process-less peer netns (a veth end holds a reference to its
 * peer's namespace), and that netns can only be freed by a cleanup_net that
 * cannot run while ds-g pins it - a self-sustaining orphan.  So we delete ds-g
 * explicitly, exactly as NAT mode deletes ds-v<pid>.
 *
 * We do not track our own segments (clients choose --gateway-net), so scan the
 * client configs that delegate to us, derive each segment's bridge + gateway
 * veth, delete the veth (reaping the peer and freeing the netns), and reap the
 * bridge once no client veths remain on it.  Per-segment work runs under the
 * same advisory lock client setup/cleanup use, and bridges are de-duplicated
 * since many clients can share one segment.  A no-op for a container that is
 * nobody's gateway. */
void ds_net_gateway_teardown(const char *gateway_name) {
  if (!gateway_name || !gateway_name[0])
    return;

  char containers_dir[PATH_MAX];
  snprintf(containers_dir, sizeof(containers_dir), "%s/Containers",
           get_workspace_dir());
  DIR *d = opendir(containers_dir);
  if (!d)
    return;

  /* De-dupe segments: many clients can share one --gateway-net (one bridge). */
  char seen[32][IFNAMSIZ];
  int seen_count = 0;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    struct ds_config c = {0};
    if (ds_config_load_by_name(ent->d_name, &c) != 0)
      continue;

    if (!(c.net_mode == DS_NET_GATEWAY && c.gateway_container[0] &&
          strcmp(c.gateway_container, gateway_name) == 0)) {
      ds_config_free(&c);
      continue;
    }

    char bridge[IFNAMSIZ], gw_host[IFNAMSIZ], gw_peer[IFNAMSIZ];
    gateway_bridge_name(&c, bridge, sizeof(bridge));
    gateway_veth_names(&c, gw_host, sizeof(gw_host), gw_peer, sizeof(gw_peer));

    int dup = 0;
    for (int i = 0; i < seen_count; i++)
      if (strcmp(seen[i], bridge) == 0) {
        dup = 1;
        break;
      }
    if (dup) {
      ds_config_free(&c);
      continue;
    }
    if (seen_count < (int)(sizeof(seen) / sizeof(seen[0])))
      safe_strncpy(seen[seen_count++], bridge, IFNAMSIZ);

    ds_nl_ctx_t *ctx = ds_nl_open();
    if (!ctx) {
      ds_config_free(&c);
      continue;
    }

    /* Same lock client setup/cleanup take, so we cannot race a concurrent
     * client start/wire or the gateway's own rewire on the segment. */
    int lock = gateway_segment_lock(bridge);

    ds_nl_del_link(ctx, gw_host);
    ds_log("[NET] Gateway teardown: removed gateway veth %s (segment %s)",
           gw_host, bridge);

    int clients = ds_nl_count_bridge_members_with_prefix(
        ctx, bridge, app_veth_host_prefix(&c));
    if (clients > 0) {
      ds_log("[NET] Gateway teardown: %d client(s) still on %s - keeping "
             "bridge",
             clients, bridge);
    } else {
      ds_nl_del_link(ctx, bridge);
      ds_log("[NET] Gateway teardown: reaped idle delegated LAN bridge %s",
             bridge);
    }

    gateway_segment_unlock(lock);
    ds_nl_close(ctx);
    ds_config_free(&c);
  }
  closedir(d);
}

/* setup_veth_child_side_named
 *
 * Called from internal_boot() INSIDE the container's new network namespace. */

int setup_veth_child_side_named(struct ds_config *cfg, const char *peer_name,
                                const char *ip_str) {
  (void)ip_str; /* IP is now assigned by the container's own DHCP client */

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ds_warn("[DEBUG] Child: Failed to open netlink socket");
    return -1;
  }

  /* Gateway mode: the host monitor owns the entire app-veth wiring - it renames
   * the peer to eth0, pins the MAC, and brings it up *inside* this netns - so
   * the child only needs loopback.  This lets the gateway wire us identically
   * whether we are starting now or already running when it (re)boots, and means
   * a client started before its gateway simply has no eth0 until the gateway
   * comes up and wires it. */
  if (cfg && cfg->net_mode == DS_NET_GATEWAY) {
    ds_nl_link_up(ctx, "lo");
    ds_nl_close(ctx);
    ds_log("[NET] Child: gateway mode - lo up; gateway container owns "
           "eth0/DHCP/routing");
    return 0;
  }

  /* NAT mode: the monitor moved the veth peer into this netns under its raw
   * name; we rename it to eth0, pin the MAC, and bring it up. */
  ds_log("[DEBUG] Child: configuring container veth (peer %s -> eth0, local "
         "PID %d)",
         peer_name ? peer_name : "(null)", (int)getpid());

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
    ds_derive_mac(cfg->container_name, "ds-mac:", mac);
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
  ds_log("[NET] Child: eth0 UP - awaiting DHCP lease from monitor");
  return 0;
}

/* Compatibility wrapper */

/* /etc/resolv.conf wiring (inside container, after pivot_root)
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
 * resolver and left a dangling symlink in gateway mode. */
static void setup_resolv_conf(struct ds_config *cfg) {
  const char *target;

  /* Gateway mode with no explicit --dns: DNS belongs to the gateway (OpenWrt
   * dnsmasq), advertised in the DHCP lease.  Droidspaces must NOT write a
   * static resolv.conf or it would bypass the gateway's DNS filtering/caching.
   */
  if (cfg->net_mode == DS_NET_GATEWAY && !cfg->dns_servers[0]) {
    if (is_systemd_rootfs("/")) {
      /* systemd-resolved consumes the lease and publishes the real resolver. */
      target = "/run/systemd/resolve/resolv.conf";
    } else {
      /* Non-systemd: leave /etc/resolv.conf to the container's own DHCP client,
       * which writes the gateway-supplied nameserver from the lease.  Writing a
       * hardcoded 1.1.1.1/8.8.8.8 here would silently defeat the gateway's DNS
       * (adblock, split-horizon, etc.).  Pass --dns to override. */
      ds_log("[NET] Gateway: leaving /etc/resolv.conf to the container's DHCP "
             "client (gateway owns DNS)");
      return;
    }
  } else {
    mkdir("/run/droidspaces", 0755);
    write_file(DS_RESOLV_CONF, cfg->dns_server_content);
    target = DS_RESOLV_CONF;
  }

  unlink("/etc/resolv.conf");
  if (symlink(target, "/etc/resolv.conf") < 0)
    ds_warn("Failed to link /etc/resolv.conf -> %s: %s", target,
            strerror(errno));
}

/* Rootfs-side networking setup (inside container, after pivot_root) */

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

  /* IPv6 is on in every mode unless explicitly disabled. NAT gets it from
   * our Router Advertisements, gateway mode from whatever OpenWrt hands out. */
  int ipv6_enabled = !cfg->disable_ipv6;
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
  } else if (cfg->net_mode == DS_NET_NAT) {
    /* Docker and friends turn on forwarding inside the container, and the
     * kernel then ignores our RAs unless accept_ra is 2. Without this the
     * IPv6 default route quietly expires half an hour after dockerd starts. */
    write_file("/proc/sys/net/ipv6/conf/default/accept_ra", "2");
    write_file("/proc/sys/net/ipv6/conf/eth0/accept_ra", "2");
  }

  /* 5. unprivileged ICMP sockets: new network namespaces reset
   * ping_group_range to "1 0". Allow all GIDs so ping works without
   * CAP_NET_RAW. */
  write_file("/proc/sys/net/ipv4/ping_group_range", "0 2147483647");

  return 0;
}

/* Uplink Route Monitor
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
 * re-asserts ip_forward, which Android periodically resets. */

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

static void log_uplink_change(const char *ifname, int table, const char *method,
                              int family) {
  if (strcmp(g_last_uplink_iface, ifname) != 0) {
    ds_log("[NET] Active uplink: %s (table %d) [%s%s]", ifname, table, method,
           family == AF_INET6 ? ", IPv6 only" : "");
    safe_strncpy(g_last_uplink_iface, ifname, sizeof(g_last_uplink_iface));
  }
  g_uplink_fail_warned = 0; /* reset so next failure logs once */
}

/* Single "no uplink" outcome for both modes: warn once (until the next success)
 * and clear the last-seen cache.  This is the one place the condition is
 * reported - callers must not log it again. */
static void log_no_uplink(void) {
  if (!g_uplink_fail_warned) {
    if (g_upstream_count > 0)
      ds_warn("[NET] Uplink: no pinned --upstream interface is up - container "
              "has no WAN; the route monitor will "
              "wire it when one appears");
    else
      ds_warn(
          "[NET] Uplink: no active internet interface found - container has "
          "no WAN; the route monitor will wire it when one appears");
    g_uplink_fail_warned = 1;
  }
  g_last_uplink_iface[0] = '\0';
}

/* Tier 3 (last resort): scan all interfaces against the built-in uplink
 * whitelist in priority order; return the first that is RUNNING and has
 * an IPv4 route in some table. */
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
      if (ds_nl_get_iface_table(ctx, AF_INET, all_ifaces[j], &tbl) != 0)
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

/* Manual override: resolve the user-pinned --upstream list in priority order.
 * The first entry that is RUNNING and has a route of `family` wins.  Literal
 * entries are checked directly; wildcard entries (containing * or ?) are
 * matched with fnmatch() against the live interface list (handles dynamic names
 * like rmnet_dataX or v4-rmnet_dataX whose number changes across reconnects).
 * No exclude heuristics apply here - the user picked the interface
 * deliberately. Returns 0 + fills iface/table, or -ENOENT when none are
 * currently available. */
static int resolve_pinned_uplink(ds_nl_ctx_t *ctx, int family, char *iface_out,
                                 int *table_out) {
  char all_ifaces[64][IFNAMSIZ];
  int all_count = -1; /* enumerated lazily, only if a wildcard needs it */

  for (int i = 0; i < g_upstream_count; i++) {
    const char *pat = g_upstream_ifaces[i];
    int is_wild = (strchr(pat, '*') != NULL || strchr(pat, '?') != NULL);

    if (!is_wild) {
      int tbl = 0;
      if (iface_is_running(pat) &&
          ds_nl_get_iface_table(ctx, family, pat, &tbl) == 0) {
        if (iface_out)
          safe_strncpy(iface_out, pat, IFNAMSIZ);
        if (table_out)
          *table_out = tbl;
        return 0;
      }
      continue;
    }

    if (all_count < 0)
      all_count = ds_nl_list_ifaces(ctx, all_ifaces, 64);
    for (int j = 0; j < all_count; j++) {
      if (fnmatch(pat, all_ifaces[j], 0) != 0)
        continue;
      int tbl = 0;
      if (!iface_is_running(all_ifaces[j]) ||
          ds_nl_get_iface_table(ctx, family, all_ifaces[j], &tbl) != 0)
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

/* One pass over the detection tiers for a single address family. Fills
 * name (IFNAMSIZ) and *tbl and returns how the uplink was found, or NULL.
 *
 * If --upstream is set this is a pure manual override: resolve ONLY from that
 * list (resolve_pinned_uplink) and disable all auto-detection - the WAN never
 * hops to whatever netd marks active.  Otherwise it is fully automatic, three
 * tiers, first hit wins:
 *
 *   1. Android netd default-network FIB rule (fwmark 0x0/0xffff iif lo).
 *      The kernel's ground truth - swapped atomically on every handoff,
 *      and never points at IMS/MMS-only interfaces.  Also the only
 *      interface Qualcomm IPA / MTK CCCI has hardware reply sessions
 *      for, which NAT'd forwarded traffic requires.
 *   2. The main routing table's default route - the standard Linux
 *      case (what LXC/Docker implicitly rely on), and ROMs/chroots that
 *      populate the main table.
 *   3. Built-in whitelist scan of known uplink interface families, IPv4 only.
 */
static const char *probe_uplink(ds_nl_ctx_t *ctx, int family, char *name,
                                int *tbl) {
  /* Manual override: pinned --upstream list only, no fallback to auto-detect.
   */
  if (g_upstream_count > 0)
    return resolve_pinned_uplink(ctx, family, name, tbl) == 0 ? "pinned" : NULL;

  /* Tier 1: netd default-network rule (Android only - the rule simply
   * does not exist elsewhere, but skip the dump cost off-Android). */
  if (is_android() && ds_nl_get_android_default(ctx, family, name, tbl) == 0 &&
      uplink_candidate_ok(name))
    return "netd rule";

  /* Tier 2: main-table default route (standard Linux semantics). */
  name[0] = '\0';
  *tbl = RT_TABLE_MAIN;
  if (ds_nl_get_table_default_oif(ctx, family, RT_TABLE_MAIN, name) == 0 &&
      uplink_candidate_ok(name))
    return "main table";

  /* Tier 3: built-in whitelist scan. IPv4 only, because a name match is a
   * guess and the IMS bearer fits it: an rmnetX that is always up, IPv6, with
   * its own default route. Whenever no real default network existed, the
   * container's traffic went out through the carrier's voice network. */
  name[0] = '\0';
  *tbl = 0;
  if (family == AF_INET && scan_uplink_whitelist(ctx, name, tbl) == 0)
    return "whitelist scan";

  return NULL;
}

/* Find the interface/table currently providing internet access.
 *
 * IPv4 goes through every tier first, so dual-stack and CLAT networks resolve
 * exactly as they always did. Only when nothing carries IPv4 is an IPv6-only
 * interface accepted: a carrier without NAT64 gives Android no v4-* device and
 * no IPv4 route in any table, yet the container still reaches the internet
 * through NAT66. Trying IPv6 per tier instead would let tier 1 return rmnetX
 * on a CLAT network before tier 3 finds v4-rmnetX, and cost it its IPv4.
 *
 * Returns 0 and fills iface_out (IFNAMSIZ) / table_out on success. table_out
 * is the IPv4 table, 0 for an IPv6-only uplink: sync_uplink6() works out the
 * IPv6 table from the interface either way. */
static int find_active_uplink(ds_nl_ctx_t *ctx, char *iface_out,
                              int *table_out) {
  char name[IFNAMSIZ] = {0};
  int tbl = 0, family = AF_INET;

  const char *method = probe_uplink(ctx, family, name, &tbl);
  if (!method && g_host_nat6) {
    family = AF_INET6;
    method = probe_uplink(ctx, family, name, &tbl);
  }
  if (!method) {
    log_no_uplink();
    return -ENOENT;
  }

  log_uplink_change(name, tbl, method, family);
  if (iface_out)
    safe_strncpy(iface_out, name, IFNAMSIZ);
  if (table_out)
    *table_out = (family == AF_INET) ? tbl : 0;
  return 0;
}

/* Re-probe which uplink is active and update the ip rule if needed. */
static void do_uplink_reprobe(void) {
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return;

  /* Reconcile our host rules before anything else.
   *
   * Android's netd rebuilds its entire netfilter and policy-routing state when
   * it restarts, and a system_server crash ("soft reboot") is enough to
   * trigger that.  It flushes the iptables built-in chains and the FIB rule
   * table wholesale, taking our rules with them, which silently strands every
   * running container: the policy rules are gone so nothing consults the main
   * table for 172.28.0.0/16 (the host cannot reach the container at all), and
   * MASQUERADE / FORWARD ACCEPT are gone so the container cannot reach the
   * WAN.  The container keeps running and the host's own connectivity comes
   * back, which is why this looks like a Droidspaces failure.
   *
   * Nothing notifies us, so reconcile instead of assuming the one-time setup
   * survived.  This runs even with no uplink at all, since host <-> container
   * traffic over the bridge needs the policy rules regardless of WAN state. */
  install_policy_rules(ctx);

  /* The netfilter set is gated behind a fork-free probe of every rule in it:
   * the port-forward helpers shell out to the iptables binary, so they must
   * not run every cycle.  Only 0 means
   * "something is definitely missing"; -1 is "tables unreadable", where a
   * blind reinstall through those binary fallbacks would stack duplicates. */
  const char *iface = host_filter_iface();
  if (iface[0] &&
      (ds_ipt_host_rules_present(AF_INET, iface, DS_DEFAULT_SUBNET,
                                 g_host_port_forward_count > 0) == 0 ||
       (g_host_nat6 &&
        ds_ipt_host_rules_present(AF_INET6, iface, DS_NAT6_SUBNET, 0) == 0))) {
    ds_warn("[NET] Route monitor: host netfilter rules are missing "
            "(netd restart or firewall app?) - reinstalling");
    install_netfilter_rules();
  }

  char new_iface[IFNAMSIZ] = {0};
  int new_table = 0;

  if (find_active_uplink(ctx, new_iface, &new_table) != 0) {
    /* No uplink available. */
    if (g_upstream_count > 0) {
      sync_uplink6(ctx, NULL);
      /* Pinned mode: the forced interface is gone.  Tear the FROM rule down so
       * container traffic is NOT silently re-routed onto a table the kernel may
       * recycle for a different network (no hopping / no leak).  When the
       * pinned interface returns, a later reprobe reinstalls the rule. */
      pthread_mutex_lock(&g_gw_mutex);
      int old_table = g_current_gw_table;
      pthread_mutex_unlock(&g_gw_mutex);
      if (old_table > 0) {
        uint32_t subnet_be, mask_be;
        parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
        (void)mask_be;
        ds_nl_del_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, old_table,
                        DS_RULE_PRIO_FROM_SUBNET);
        pthread_mutex_lock(&g_gw_mutex);
        g_current_gw_table = 0;
        pthread_mutex_unlock(&g_gw_mutex);
        ds_log("[NET] Route monitor: pinned uplink gone - removed FROM rule "
               "(container WAN is down until it returns)");
      }
    }
    /* Auto mode leaves the current rule in place (avoid flapping on
     * transients); pinned mode has already torn it down above. */
    ds_nl_close(ctx);
    return;
  }

  sync_uplink6(ctx, new_iface);
  sync_resolv_order(new_table == 0);

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

  /* An IPv6-only uplink has no IPv4 table to point at. With the rule gone the
   * host answers the container's IPv4 with "network unreachable" at once, so
   * clients fall back to IPv6 instead of waiting on a dead route. */
  if (new_table == 0) {
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = 0;
    pthread_mutex_unlock(&g_gw_mutex);
    ds_log("[NET] Route monitor: uplink has no IPv4 - removed rule from %s",
           DS_DEFAULT_SUBNET);
  } else if (ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, new_table,
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

  int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
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
    /* Enforce IPv4 forwarding in real-time. If ip_forward ever flips to 0
     * the NAT'd container loses all WAN traffic, so re-assert it on every
     * cycle regardless of platform - Android's netd is the usual culprit,
     * but a desktop firewall/sysctl reload or another tool can clear it too.
     * The kernel does not broadcast POLLERR/inotify events for /proc/sys/
     * memory variables, so we must poll; reading a 1-byte procfs flag takes
     * < 1 microsecond, costing 0% CPU. */
    if (g_current_gw_table > 0) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv4/ip_forward", val, sizeof(val)) > 0 &&
          val[0] == '0') {
        ds_log("[NET] Route monitor: ip_forward was disabled - re-enabling...");
        write_file("/proc/sys/net/ipv4/ip_forward", "1\n");
      }
    }

    if (g_host_nat6) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv6/conf/all/forwarding", val,
                    sizeof(val)) > 0 &&
          val[0] == '0')
        write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1\n");
    }

    /* Same treatment for route_localnet - required for localhost port-forward
     * DNAT (127.0.0.1:HOST -> container). Android/netd resets this the same
     * way it resets ip_forward. */
    if (g_local_forward_active) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv4/conf/all/route_localnet", val,
                    sizeof(val)) > 0 &&
          val[0] == '0') {
        ds_log("[NET] Route monitor: route_localnet was disabled - "
               "re-enabling...");
        write_file("/proc/sys/net/ipv4/conf/all/route_localnet", "1\n");
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
      ds_warn("[NET] Route monitor: poll failed (%s) - stopping",
              strerror(errno));
      break;
    }

    if (pr == 0) {
      do_uplink_reprobe();
      continue;
    }

    ssize_t len = recv(sock, buf, sizeof(buf), 0);
    if (len < 0) {
      if (g_stop_monitor)
        break;
      if (errno == EINTR || errno == EAGAIN)
        continue;
      if (errno == ENOBUFS) {
        /* The netlink multicast receive buffer overflowed during an event
         * burst (e.g. a container reboot tearing down and recreating veths).
         * The kernel dropped notifications but the socket is still valid:
         * resync by reprobing the uplink and keep monitoring, rather than
         * treating a recoverable condition as fatal and killing the thread. */
        do_uplink_reprobe();
        continue;
      }
      ds_warn("[NET] Route monitor: recv failed (%s) - stopping",
              strerror(errno));
      break;
    }
    if (len == 0) {
      /* Only our own shutdown() during stop yields a 0-length read; keep
       * monitoring otherwise instead of exiting. */
      if (g_stop_monitor)
        break;
      continue;
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
            /* RTA_OK bounds only the declared length; require a full 4-byte
             * payload before dereferencing (defensive; kernel-sourced). */
            if (rta->rta_type == RTA_OIF &&
                RTA_PAYLOAD(rta) >= (int)sizeof(int))
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
  /* Mark the loop inactive so a later ds_net_start_route_monitor() (e.g. the
   * next reboot cycle) spawns a fresh thread instead of no-op'ing on a stale
   * flag.  needs_join stays set: this thread has exited but is still joinable,
   * and start/stop will reap it. */
  g_route_monitor_started = 0;
  pthread_mutex_unlock(&g_gw_mutex);

  ds_log("[NET] Uplink route monitor stopped");
  return NULL;
}

void ds_net_stop_route_monitor(void) {
  pthread_mutex_lock(&g_gw_mutex);
  /* Gate the join on needs_join, not started: a thread that self-exited on a
   * fatal error has started==0 but is still joinable and must be reaped. */
  int needs_join = g_route_monitor_needs_join;
  pthread_t tid = g_route_monitor_tid;
  g_stop_monitor = 1;
  if (g_route_monitor_sock >= 0)
    shutdown(g_route_monitor_sock, SHUT_RDWR);
  g_route_monitor_needs_join = 0;
  pthread_mutex_unlock(&g_gw_mutex);

  /* Join so the monitor is fully stopped before cleanup removes the shared
   * MASQUERADE / FIB policy rules - otherwise an in-flight do_uplink_reprobe()
   * could re-add a rule we just deleted, or rewrite ip_forward after teardown.
   */
  if (needs_join) {
    pthread_join(tid, NULL);
    pthread_mutex_lock(&g_gw_mutex);
    g_route_monitor_started = 0;
    pthread_mutex_unlock(&g_gw_mutex);
  }
}

void ds_net_mark_local_forward_active(void) { g_local_forward_active = 1; }

void ds_net_start_route_monitor(void) {
  if (!is_android())
    return;

  pthread_mutex_lock(&g_gw_mutex);
  /* Idempotent: exactly one monitor thread per process.  setup_veth_host_side
   * calls this on every boot cycle (including reboots); the monitor tracks the
   * host uplink, which is independent of the container PID, so a single live
   * instance rightly persists across the container's reboots.
   */
  if (g_route_monitor_started) {
    pthread_mutex_unlock(&g_gw_mutex);
    return;
  }
  /* Not running.  A previous thread may have self-exited on a fatal error
   * without being joined (needs_join==1, started==0); reap it before spawning
   * a replacement so its resources are freed and g_route_monitor_tid is not
   * overwritten while still joinable.  Join outside the mutex (the pattern stop
   * uses) - the exiting thread takes g_gw_mutex on its way out. */
  int reap = g_route_monitor_needs_join;
  pthread_t old_tid = g_route_monitor_tid;
  g_route_monitor_needs_join = 0;
  pthread_mutex_unlock(&g_gw_mutex);

  if (reap)
    pthread_join(old_tid, NULL);

  pthread_mutex_lock(&g_gw_mutex);
  g_stop_monitor = 0;
  if (ds_thread_create(&g_route_monitor_tid, route_monitor_loop, NULL) != 0) {
    ds_warn("[NET] Failed to start route monitor thread: %s", strerror(errno));
  } else {
    g_route_monitor_started = 1;
    g_route_monitor_needs_join = 1;
  }
  pthread_mutex_unlock(&g_gw_mutex);
}

/* Network cleanup (called on container stop) */

void ds_net_cleanup(struct ds_config *cfg, pid_t container_pid) {
  /* If this container is a gateway for others, explicitly tear down the
   * gateway-side veth(s) it serves: the kernel will not auto-reap them (the
   * host-side veth pins its orphan peer netns).  No-op when nobody delegates
   * to us, so it is safe to run for every stopping container. */
  ds_net_gateway_teardown(cfg->container_name);

  if (cfg->net_mode == DS_NET_GATEWAY) {
    ds_nl_ctx_t *ctx = ds_nl_open();
    if (!ctx)
      return;

    char veth_host[IFNAMSIZ] = {0};
    pid_t effective_pid =
        container_pid > 0 ? container_pid : cfg->container_pid;
    if (effective_pid > 0) {
      veth_host_name(cfg, effective_pid, veth_host, sizeof(veth_host));

      char bridge[IFNAMSIZ];
      gateway_bridge_name(cfg, bridge, sizeof(bridge));

      /* Hold the segment lock across our veth removal + the client count +
       * reap, so a concurrent client's setup (which holds the same lock while
       * attaching its veth) cannot slip in between our count and our reap. */
      int lock = gateway_segment_lock(bridge);

      ds_nl_del_link(ctx, veth_host);
      ds_log("[NET] Gateway cleanup: removed %s", veth_host);

      int clients = ds_nl_count_bridge_members_with_prefix(
          ctx, bridge, app_veth_host_prefix(cfg));
      if (clients > 0) {
        ds_log("[NET] Gateway cleanup: %d client(s) still on %s - keeping "
               "delegated LAN",
               clients, bridge);
      } else if (gateway_pid_of(cfg->gateway_container) > 0) {
        /* No clients left, but the gateway is still up.  Do NOT reap the
         * bridge: deleting it flaps the gateway's live LAN iface (eth1)
         * carrier, and netifd occasionally fails to re-init it ("device
         * initialization failed").  The IP-less, policy-free bridge is harmless
         * idle and the next client reuses it; the gateway veth stays put. */
        ds_log("[NET] Gateway cleanup: no clients on %s but gateway '%s' is up "
               "- keeping bridge to avoid flapping its LAN iface",
               bridge, cfg->gateway_container);
      } else {
        /* No clients and the gateway is gone.  The gateway's own stop already
         * ran ds_net_gateway_teardown(), which explicitly deleted the gateway
         * veth (the kernel does not auto-reap it), so the bridge is now idle
         * and safe to reap. */
        ds_nl_del_link(ctx, bridge);
        ds_log("[NET] Gateway cleanup: reaped idle delegated LAN bridge %s",
               bridge);
      }

      gateway_segment_unlock(lock);
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

  /* Bridge or bridgeless is a property of the kernel, and ds-br0 is never
   * deleted once it exists, so ask the kernel instead of trusting a flag that
   * only start and restart ever probe for. A plain stop never set it, and
   * left the per-veth rules of a bridgeless container behind. */
  int bridgeless = !ds_nl_link_exists(ctx, DS_NAT_BRIDGE);

  /* 1. Delete host-side veth - peer in dead netns is already gone */
  char veth_host[IFNAMSIZ] = {0};
  pid_t effective_pid = container_pid > 0 ? container_pid : cfg->container_pid;
  if (effective_pid <= 0) {
    ds_warn("[NET] cleanup: cannot derive veth name - no valid PID");
    /* still proceed with iptables cleanup */
  } else {
    veth_host_name(cfg, effective_pid, veth_host, sizeof(veth_host));
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
    if (bridgeless && veth_host[0] != '\0') {
      ds_ipt_remove_iface_rules(AF_INET, veth_host);
      ds_ipt_remove_iface_rules(AF_INET6, veth_host);
    }
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

    struct in6_addr net6;
    inet_pton(AF_INET6, DS_NAT6_PREFIX, &net6);
    ds_nl_rule6(ctx, 0, &net6, DS_NAT6_PREFIX_LEN, 0, 0,
                DS_RULE_PRIO_TO_SUBNET);
    ds_nl_rule6(ctx, 0, &net6, DS_NAT6_PREFIX_LEN, 1, 0,
                DS_RULE_PRIO_FROM_SUBNET);
  }

  ds_nl_close(ctx);

  /* 3. Remove iptables rules. IPv6 unconditionally: the stop may run in a
   * process that never set g_host_nat6, and removing rules that are not there
   * is a no-op. */
  if (bridgeless && veth_host[0] != '\0') {
    ds_ipt_remove_iface_rules(AF_INET, veth_host);
    ds_ipt_remove_iface_rules(AF_INET6, veth_host);
  }
  ds_ipt_remove_portforwards(cfg);
  ds_ipt_remove_ds_rules(AF_INET);
  ds_ipt_remove_ds_rules(AF_INET6);
}
