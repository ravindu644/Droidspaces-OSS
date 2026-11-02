/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <arpa/inet.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <sys/socket.h>

#include "internal.h"

/* Kernel capability probe for NAT networking
 *
 * Tests whether the running kernel supports:
 *   1. Network namespaces    (CONFIG_NET_NS)
 *   2. Bridge devices        (CONFIG_BRIDGE)
 *   3. Veth pairs            (CONFIG_VETH)
 *
 * Does NOT test iptables nat - that has a separate binary fallback path.
 *
 * Returns 0 if all supported.
 * Returns -1 and writes a human-readable reason into reason[rsz]. */

int ds_nl_probe_nat_capability(char *reason, size_t rsz) {
  int ret;
  /* Step 1: CONFIG_NET_NS */
  if (!check_ns(CLONE_NEWNET, "net")) {
    snprintf(reason, rsz,
             "CONFIG_NET_NS not compiled in. "
             "Network namespaces are required for --net=nat. "
             "Rebuild your kernel with CONFIG_NET_NS=y.");
    return -1;
  }

  /* A full pass creates a bridge and a veth pair and deletes all three, and
   * every netdev unregister waits out RCU grace periods: 170 ms on a
   * mid-range phone, on every start. The kernel does not change between
   * starts, so a pass is remembered for the boot it was made in, by the
   * boot_id the kernel mints at boot. Only the full verdict is kept: a
   * degraded or failed one is asked again, so a module loaded later in the
   * same boot is picked up. One unloaded later in the boot is not: the real
   * bridge or veth creation in setup_veth_host_side() reports that instead,
   * with a warning where the probe would have failed or gone bridgeless. */
  char boot_id[64] = "", seen[64] = "", stamp[PATH_MAX];
  read_file("/proc/sys/kernel/random/boot_id", boot_id, sizeof(boot_id));
  snprintf(stamp, sizeof(stamp), "%s/nat_caps", get_net_dir());
  if (boot_id[0] && read_file(stamp, seen, sizeof(seen)) > 0 &&
      strcmp(seen, boot_id) == 0) {
    ds_log("[NET] Kernel capability probe passed: NET_NS + BRIDGE + VETH OK.");
    if (reason)
      snprintf(reason, rsz, "OK (Full NAT)");
    return 0;
  }

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    snprintf(reason, rsz, "Failed to open NETLINK_ROUTE socket: %s",
             strerror(errno));
    return -1;
  }

  /* The probe interfaces have fixed names, so two starts probing at the same
   * moment collide, and a crashed probe can leave one behind. Either way the
   * interface existing is the proof we were after. It is just not ours to
   * delete. */

  /* Step 2: CONFIG_BRIDGE */
  int has_bridge = 1;
  const char *probe_br = "ds-cap-br0";
  ret = ds_nl_create_bridge(ctx, probe_br);
  int own_bridge = (ret == 0);
  if (ret < 0 && ret != -EEXIST) {
    if (ret == -EOPNOTSUPP) {
      has_bridge = 0;
      ds_log("[NET] CONFIG_BRIDGE not supported - will fallback to bridgeless "
             "NAT");
    } else {
      snprintf(reason, rsz, "Bridge probe failed unexpectedly: %s",
               strerror(-ret));
      ds_nl_close(ctx);
      return -1;
    }
  }

  /* Step 3: CONFIG_VETH */
  ret = ds_nl_create_veth(ctx, "ds-cap-h0", "ds-cap-p0");
  int own_veth = (ret == 0);
  int has_veth = (ret == 0 || ret == -EEXIST);
  int veth_err = ret;

  /* Cleanup Probe Interfaces */
  if (own_bridge)
    ds_nl_del_link(ctx, probe_br);
  if (own_veth)
    ds_nl_del_link(ctx, "ds-cap-h0");

  ds_nl_close(ctx);

  if (!has_veth) {
    if (veth_err == -EOPNOTSUPP) {
      snprintf(reason, rsz,
               "CONFIG_VETH not enabled (kernel returned EOPNOTSUPP). "
               "Virtual Ethernet pairs are required for --net=nat. "
               "Rebuild your kernel with CONFIG_VETH=y.");
    } else {
      snprintf(reason, rsz, "Veth probe failed unexpectedly: %s",
               strerror(-veth_err));
    }
    return -1;
  }

  if (has_bridge) {
    ds_log("[NET] Kernel capability probe passed: NET_NS + BRIDGE + VETH OK.");
    if (reason)
      snprintf(reason, rsz, "OK (Full NAT)");
    if (boot_id[0])
      write_file(stamp, boot_id);
    return 0;
  } else {
    ds_log(
        "[NET] Kernel capability probe limited: NET_NS + VETH OK (No BRIDGE).");
    if (reason)
      snprintf(reason, rsz, "OK (Bridgeless NAT Fallback)");
    return 1;
  }
}

/* Can this kernel make a macvlan? It needs an Ethernet parent and the phone
 * may have none plugged in, so the probe brings its own: a veth, which the NAT
 * probe already requires. A veth left behind by a probe that crashed is still a
 * fine parent, and a macvlan left on it is proof enough. A pass is remembered
 * for the boot, like the NAT probe's, since every start asks. */
int ds_nl_probe_macvlan(void) {
  char boot_id[64] = "", seen[64] = "", stamp[PATH_MAX];
  read_file("/proc/sys/kernel/random/boot_id", boot_id, sizeof(boot_id));
  snprintf(stamp, sizeof(stamp), "%s/macvlan_caps", get_net_dir());
  if (boot_id[0] && read_file(stamp, seen, sizeof(seen)) > 0 &&
      strcmp(seen, boot_id) == 0)
    return 1;

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return 0;
  static const uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0x01};
  int own = ds_nl_create_veth(ctx, "ds-cap-mh0", "ds-cap-mp0") == 0;
  int ret = ds_nl_create_macvlan(ctx, "ds-cap-mv0",
                                 ds_nl_get_ifindex(ctx, "ds-cap-mh0"), -1,
                                 MACVLAN_MODE_BRIDGE, mac);
  if (ret == 0)
    ds_nl_del_link(ctx, "ds-cap-mv0");
  if (own)
    ds_nl_del_link(ctx, "ds-cap-mh0"); /* takes the peer with it */
  ds_nl_close(ctx);

  int ok = (ret == 0 || ret == -EEXIST);
  if (ok && boot_id[0])
    write_file(stamp, boot_id);
  return ok;
}
