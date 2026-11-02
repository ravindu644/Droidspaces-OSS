/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
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

#include "net/internal.h"

/* Internal helpers */

/* Host-side veth prefix for an application container.  NAT containers use
 * "ds-v"; gateway clients use "ds-c" so the NAT-mode "last container" refcount
 * (which scans for "ds-v") never miscounts a gateway client as a live NAT
 * container and so keeps shared NAT iptables/route rules alive forever. */
const char *app_veth_host_prefix(const struct ds_config *cfg) {
  return (cfg && cfg->net_mode == DS_NET_GATEWAY) ? "ds-c" : "ds-v";
}

/* Derive the host-side veth name (mode-aware). A NAT veth is named after the
 * container's init PID. A gateway client's is named after the container: if
 * an instance dies without cleanup, its cable can outlive it on the bridge
 * with the same MAC as its successor's, and a name that does not change is
 * what lets the next start find that leftover and replace it. */
void veth_host_name(const struct ds_config *cfg, pid_t pid, char *buf,
                    size_t sz) {
  if (cfg && cfg->net_mode == DS_NET_GATEWAY)
    snprintf(buf, sz, "%s%08x", app_veth_host_prefix(cfg),
             ds_net_hash_string(cfg->container_name));
  else
    snprintf(buf, sz, "%s%d", app_veth_host_prefix(cfg), (int)pid);
}

/* Derive the peer (container-side) veth name from a container init PID.
 * Gateway clients use "ds-q" to match the distinct host-side prefix. */
void veth_peer_name(const struct ds_config *cfg, pid_t pid, char *buf,
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

uint32_t ds_net_hash_string(const char *s) {
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
void ds_derive_mac(const char *key, const char *salt_prefix, uint8_t mac[6]) {
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

/* A bridge with no address of its own borrows the lowest MAC among its ports,
 * so it changes whenever that port's container stops. Every other container on
 * the bridge then holds a stale ARP entry for the gateway and loses IPv4 until
 * the kernel re-probes, up to a minute later. A MAC set from userspace is
 * exempt from that election, so pin one derived from the bridge name. */
void pin_bridge_mac(ds_nl_ctx_t *ctx, const char *bridge) {
  uint8_t mac[6];
  ds_derive_mac(bridge, "ds-brmac:", mac);
  int err = ds_nl_set_mac(ctx, bridge, mac);
  if (err < 0)
    ds_warn("[NET] Failed to pin MAC on bridge %s: %d", bridge, err);
}

void gateway_hash_key(struct ds_config *cfg, char *buf, size_t sz) {
  const char *gw =
      (cfg && cfg->gateway_container[0]) ? cfg->gateway_container : "gateway";
  const char *net = (cfg && cfg->gateway_net[0]) ? cfg->gateway_net : "lan";
  snprintf(buf, sz, "%s:%s", gw, net);
}

/* The host end of the gateway's LAN cable. Its peer is born inside the
 * gateway under the LAN interface name, so it never needs a host-side name. */
void gateway_veth_host_name(struct ds_config *cfg, char *host, size_t hsz) {
  char key[384];
  gateway_hash_key(cfg, key, sizeof(key));
  snprintf(host, hsz, "ds-g%08x", ds_net_hash_string(key));
}

int gateway_ifname_component_ok(const char *s) {
  if (!s || !s[0])
    return 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (!(isalnum(*p) || *p == '_' || *p == '-'))
      return 0;
  }
  return 1;
}

void gateway_bridge_name(struct ds_config *cfg, char *buf, size_t sz) {
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

const char *gateway_lan_ifname(struct ds_config *cfg) {
  if (cfg && cfg->gateway_lan_ifname[0])
    return cfg->gateway_lan_ifname;
  return "eth1";
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
