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

#include "internal.h"

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

  /* Gateway or macvlan mode with no explicit --dns: DNS belongs to whoever
   * serves the LAN (OpenWrt's dnsmasq, a home router), advertised in the DHCP
   * lease.  Droidspaces must NOT write a static resolv.conf or it would bypass
   * that DNS and its filtering/caching. */
  if ((cfg->net_mode == DS_NET_GATEWAY || cfg->net_mode == DS_NET_MACVLAN) &&
      !cfg->dns_servers[0]) {
    if (is_systemd_rootfs("/")) {
      /* systemd-resolved consumes the lease and publishes the real resolver. */
      target = "/run/systemd/resolve/resolv.conf";
    } else {
      /* Non-systemd: leave /etc/resolv.conf to the container's own DHCP client,
       * which writes the gateway-supplied nameserver from the lease.  Writing a
       * hardcoded 1.1.1.1/8.8.8.8 here would silently defeat the gateway's DNS
       * (adblock, split-horizon, etc.).  Pass --dns to override. */
      ds_log("[NET] Leaving /etc/resolv.conf to the container's DHCP client "
             "(the LAN owns DNS)");
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
  } else if (cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_MACVLAN) {
    /* Docker and friends turn on forwarding inside the container, and the
     * kernel then ignores RAs (ours, or the LAN router's) unless accept_ra is
     * 2. Without this the IPv6 default route quietly expires half an hour
     * after dockerd starts. */
    write_file("/proc/sys/net/ipv6/conf/default/accept_ra", "2");
    write_file("/proc/sys/net/ipv6/conf/eth0/accept_ra", "2");
  }

  /* 5. unprivileged ICMP sockets: new network namespaces reset
   * ping_group_range to "1 0". Allow all GIDs so ping works without
   * CAP_NET_RAW. */
  write_file("/proc/sys/net/ipv4/ping_group_range", "0 2147483647");

  return 0;
}
