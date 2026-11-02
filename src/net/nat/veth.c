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

/* Shape of the host-side rules this monitor installed, captured in
 * setup_veth_host_side() before the monitor thread starts and only rewritten
 * from that same path on a container reboot.  The monitor replays exactly
 * these when Android's netd flushes the tables out from under a running
 * container - see install_netfilter_rules(). */
int g_host_bridgeless = 0;
char g_host_veth[IFNAMSIZ];
struct ds_port_forward g_host_port_forwards[DS_MAX_PORT_FORWARDS];
int g_host_port_forward_count = 0;
char g_host_container_ip[INET_ADDRSTRLEN];
int g_host_nat6 = 0; /* NAT66 is set up, replay its rules too */
pid_t g_host_init_pid = 0;
int g_host_dns_managed = 0; /* resolv.conf holds our default list */

/* setup_veth_host_side
 *
 * Called from the Monitor process AFTER receiving the "ready" signal from the
 * container init (via net_ready_pipe).
 *
 * Steps:
 *   1. Create or reuse bridge ds-br0 with IP 172.28.0.1/16
 *   2. iptables: MASQUERADE + FORWARD ACCEPT + INPUT ACCEPT + MSS clamp
 *   3. Create veth pair (ds-vXXXXX / ds-pXXXXX)
 *   4. Prepare the host veth: TX checksum off, RPS on
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

  /* A leftover with our name only exists if the kernel recycled the PID of
   * an instance whose netns is still draining. */
  if (ds_nl_del_link(ctx, veth_host) > 0)
    ds_log("[NET] Removed stale %s", veth_host);

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

    /* Always assert bridge MAC/IP/UP/Hardening even if it already exists.
     * This ensures everything is correct after host-side networking changes or
     * crashes. */
    pin_bridge_mac(ctx, DS_NAT_BRIDGE);
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

  /* 4. Prepare the host veth: TX checksum off, RPS on */
  ds_net_prepare_host_veth(veth_host);

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

    /* The guest holds a /16 and ARPs for its siblings as if they were on
     * its link. Nothing shares that link, so without proxy ARP the host
     * never answers and containers cannot reach each other. */
    char proxy_arp[128];
    snprintf(proxy_arp, sizeof(proxy_arp),
             "/proc/sys/net/ipv4/conf/%s/proxy_arp", veth_host);
    write_file(proxy_arp, "1");

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
   * The server binds to veth_host in both topologies. A packet socket on a
   * bridge port still sees the container's frames, because packet taps run
   * before the bridge takes the frame, and only this container's port is
   * this container's. */
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

    /* In bridge mode the kernel also floods sibling containers' broadcasts
     * out of this port. The DHCP loop drops those by ingress ifindex and
     * packet type, see net/dhcp/server.c. */
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
