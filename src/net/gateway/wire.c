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

#include "../internal.h"

/* Gateway segment lock
 *
 * One advisory file lock per delegated LAN segment (keyed on the bridge name).
 * Serialises (re)creation of the single shared gateway-side veth + bridge
 * across the independent monitor processes that touch a segment - concurrent
 * client starts, and the gateway re-wiring its clients on (re)boot - so they
 * cannot race each other into EEXIST half-states. */

int gateway_segment_lock(const char *bridge) {
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

void gateway_segment_unlock(int fd) {
  if (fd < 0)
    return;
  flock(fd, LOCK_UN);
  close(fd);
}

/* Gateway liveness */

/* Resolve the gateway container's init pid, or 0 if it is not running. */
pid_t gateway_pid_of(const char *name) {
  struct ds_config c;
  ds_config_init(&c);
  safe_strncpy(c.container_name, name, sizeof(c.container_name));
  (void)ds_config_peek(name, &c);
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

  char bridge[IFNAMSIZ], gw_host[IFNAMSIZ];
  gateway_bridge_name(cfg, bridge, sizeof(bridge));
  gateway_veth_host_name(cfg, gw_host, sizeof(gw_host));

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
  pin_bridge_mac(ctx, bridge);
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

  /* The peer is born inside the gateway under its final name, in the one
   * RTM_NEWLINK that creates the pair. Moving a host-side peer in and renaming
   * it there is a race netifd loses: the kernel announces the new name before
   * the device can be looked up by it (a grace period sits in between), and
   * netifd's claim of the hot-plugged eth1 fails for good on a kernel with
   * slow RCU. With a stable, segment-derived MAC pinned at creation, netifd
   * sees one persistent device across every re-plug. */
  if (netns_has_link(gw_netns, gw_if)) {
    ds_warn("[NET] Gateway: '%s' already has an interface named %s that is "
            "not this segment's cable - cannot wire %s",
            cfg->gateway_container, gw_if, bridge);
    ds_nl_close(ctx);
    return -1;
  }
  int gw_netns_fd = open_container_netns(gw_netns);
  if (gw_netns_fd < 0) {
    ds_warn("[NET] Gateway: failed to open %s: %s", gw_netns, strerror(errno));
    ds_nl_close(ctx);
    return -1;
  }
  char key[384];
  uint8_t mac[6];
  gateway_hash_key(cfg, key, sizeof(key));
  ds_derive_mac(key, "ds-gwmac:", mac);

  ds_log("[NET] Gateway: creating gateway veth %s -> %s inside the gateway",
         gw_host, gw_if);
  int created = ds_nl_create_veth_in(ctx, gw_host, gw_if, gw_netns_fd, mac);
  close(gw_netns_fd);
  if (created < 0) {
    ds_warn("[NET] Gateway: failed to create gateway veth pair (%s -> %s): %s",
            gw_host, gw_if, strerror(-created));
    ds_nl_close(ctx);
    return -1;
  }

  ds_net_prepare_host_veth(gw_host);
  if (ds_nl_set_master(ctx, gw_host, bridge) < 0)
    ds_warn("[NET] Gateway: failed to attach %s to %s", gw_host, bridge);
  if (ds_nl_link_up(ctx, gw_host) < 0)
    ds_warn("[NET] Gateway: failed to bring up %s", gw_host);
  ds_nl_close(ctx);

  if (ds_netns_link_up(gw_netns, NULL, gw_if) < 0)
    ds_warn("[NET] Gateway: created %s but could not bring it up", gw_if);

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

int gateway_wire_client(struct ds_config *cfg, pid_t client_pid,
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

  ds_net_prepare_host_veth(app_host);
  if (ds_nl_set_master(ctx, app_host, bridge) < 0)
    ds_warn("[NET] Gateway: failed to attach %s to %s", app_host, bridge);
  if (ds_nl_link_up(ctx, app_host) < 0)
    ds_warn("[NET] Gateway: failed to bring up %s", app_host);
  ds_nl_close(ctx);

  if (ds_netns_link_up(netns, NULL, "eth0") < 0)
    ds_warn("[NET] Gateway: wired '%s' but could not bring up its eth0",
            cfg->container_name);

  ret = 0;
  ds_log("Gateway: wiring complete for '%s': %s -> %s", cfg->container_name,
         bridge, cfg->gateway_container);

out:
  gateway_segment_unlock(lock);
  return ret;
}
