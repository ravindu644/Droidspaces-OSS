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

  /* The host end alone proves nothing: it is named after the container, so a
   * cable left over from an earlier boot whose namespace something kept alive
   * looks the same. Only an eth0 in our current namespace means wired. */
  char app_host[IFNAMSIZ], netns[PATH_MAX];
  veth_host_name(cfg, client_pid, app_host, sizeof(app_host));
  snprintf(netns, sizeof(netns), "/proc/%d/ns/net", (int)client_pid);
  ds_nl_ctx_t *ctx = ds_nl_open();
  int wired =
      ctx && ds_nl_link_exists(ctx, app_host) && netns_has_link(netns, "eth0");
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
    if (ds_config_peek(ent->d_name, &c) != 0)
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

  /* De-dupe segments: many clients share one. The key is the cable and the
   * bridge together, because --gateway-bridge can pull them apart: two
   * segments forced onto one bridge still have a cable each, and one segment
   * whose clients name different bridges still has every bridge to reap. */
  char seen[32][2][IFNAMSIZ];
  int seen_count = 0;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    struct ds_config c = {0};
    if (ds_config_peek(ent->d_name, &c) != 0)
      continue;

    if (!(c.net_mode == DS_NET_GATEWAY && c.gateway_container[0] &&
          strcmp(c.gateway_container, gateway_name) == 0)) {
      ds_config_free(&c);
      continue;
    }

    char bridge[IFNAMSIZ], gw_host[IFNAMSIZ];
    gateway_bridge_name(&c, bridge, sizeof(bridge));
    gateway_veth_host_name(&c, gw_host, sizeof(gw_host));

    int dup = 0;
    for (int i = 0; i < seen_count; i++)
      if (strcmp(seen[i][0], gw_host) == 0 && strcmp(seen[i][1], bridge) == 0) {
        dup = 1;
        break;
      }
    if (dup) {
      ds_config_free(&c);
      continue;
    }
    if (seen_count < (int)(sizeof(seen) / sizeof(seen[0]))) {
      safe_strncpy(seen[seen_count][0], gw_host, IFNAMSIZ);
      safe_strncpy(seen[seen_count++][1], bridge, IFNAMSIZ);
    }

    ds_nl_ctx_t *ctx = ds_nl_open();
    if (!ctx) {
      ds_config_free(&c);
      continue;
    }

    /* Same lock client setup/cleanup take, so we cannot race a concurrent
     * client start/wire or the gateway's own rewire on the segment. */
    int lock = gateway_segment_lock(bridge);

    if (ds_nl_del_link(ctx, gw_host) > 0)
      ds_log("[NET] Gateway teardown: removed gateway veth %s (segment %s)",
             gw_host, bridge);

    int clients = ds_nl_count_bridge_members_with_prefix(
        ctx, bridge, app_veth_host_prefix(&c));
    if (clients > 0) {
      ds_log("[NET] Gateway teardown: %d client(s) still on %s - keeping "
             "bridge",
             clients, bridge);
    } else {
      if (ds_nl_del_link(ctx, bridge) > 0)
        ds_log("[NET] Gateway teardown: reaped idle delegated LAN bridge %s",
               bridge);
    }

    gateway_segment_unlock(lock);
    ds_nl_close(ctx);
    ds_config_free(&c);
  }
  closedir(d);
}
