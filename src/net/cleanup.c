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

      if (ds_nl_del_link(ctx, veth_host) > 0)
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
        if (ds_nl_del_link(ctx, bridge) > 0)
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
