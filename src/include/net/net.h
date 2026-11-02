/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_NET_H
#define DS_NET_NET_H

/* Default DNS servers */
#define DS_DNS_DEFAULT_1 "1.1.1.1"
#define DS_DNS_DEFAULT_2 "8.8.8.8"
/* The only resolver a container can reach when the uplink has no IPv4. One
 * is all there is room for: glibc reads three nameserver lines. */
#define DS_DNS_DEFAULT_6 "2606:4700:4700::1111"

/* Handshake payload: Monitor → init via net_done_pipe */
struct ds_net_handshake {
  char peer_name[16]; /* e.g. "ds-p12345"        */
  char ip_str[32];    /* e.g. "172.28.4.47/16"  */
};

/* NAT networking constants */

#define DS_DEFAULT_SUBNET "172.28.0.0/16"
#define DS_NAT_BRIDGE "ds-br0"
#define DS_NAT_GW_IP "172.28.0.1"
#define DS_NAT_PREFIX 16

/* NAT66. One ULA /48 for everything we masquerade. RFC 4193 wants the global
 * ID random; ours is fixed (64:73:70 is "dsp") for the same reason the IPv4
 * side is a fixed 172.28.0.0/16: the monitor, the cleanup path and the docs
 * all need to name it without reading state.
 *   Bridge mode:  ds-br0 is fd64:7370::1 and containers share fd64:7370::/64.
 *   Bridgeless:   each container gets fd64:7370:0:XXYY::/64, where XX.YY are
 *                 the last two octets of its 172.28.XX.YY address.
 * Containers always route through fe80::1, which sits on the bridge or on the
 * bridgeless host veth. */
#define DS_NAT6_SUBNET "fd64:7370::/48"
#define DS_NAT6_PREFIX "fd64:7370::"
#define DS_NAT6_PREFIX_LEN 48
#define DS_NAT6_GW_LL "fe80::1"

/* Router Advertisement frame: ethernet + IPv6 + RA with a prefix option. The
 * router lifetime is 30 minutes and the DHCP thread re-announces every 5. */
#define DS_RA_FRAME_LEN 102
#define DS_RA_ROUTER_LIFETIME 1800
#define DS_RA_INTERVAL_SEC 300

/* Android ip rule priorities for DS subnet routing.
 *
 * Must be < 10000 so they are evaluated BEFORE Android's VPN rule range
 * (RULE_PRIORITY_VPN_OVERRIDE_SYSTEM = 10000) - this ensures container
 * traffic bypasses any active VPN and that return traffic for the container
 * subnet always resolves via the main table even when a VPN is up.
 *
 * Must be > 1000 to avoid colliding with OEM/carrier reserved rules that
 * some vendors install below 1000.  The previous values (90, 100) were
 * dangerously close to 0 and triggered route re-evaluation races on
 * certain OEM kernels.
 *
 * DS_RULE_PRIO_TO_SUBNET  - "to 172.28.0.0/16 lookup main"
 *   Guarantees reply traffic to the container always resolves correctly.
 *
 * DS_RULE_PRIO_TETHER - "from 172.28.0.0/16 lookup local_network(97)"
 *   Returns container replies to hotspot/USB-tether clients out the tether
 *   interface.  Reply packets of DNAT'd port-forward connections still carry
 *   the container source address at routing time (conntrack un-DNATs in
 *   POSTROUTING, after the route lookup), so without this rule they would
 *   match DS_RULE_PRIO_FROM_SUBNET and leak out the WAN uplink instead of
 *   reaching the tethered client.  netd keeps the connected route of every
 *   downstream interface (swlan*, ap*, rndis*, ncm*, bt-pan) in the
 *   local_network table, which has no default route - so this rule only
 *   matches tether-bound traffic and falls through to FROM_SUBNET otherwise.
 *   Must sit between TO_SUBNET and FROM_SUBNET.
 *
 * DS_RULE_PRIO_FROM_SUBNET - "from 172.28.0.0/16 lookup <gw_table>"
 *   Routes container-originated traffic through the active uplink table.
 */
#define DS_RULE_PRIO_TO_SUBNET 6090
#define DS_RULE_PRIO_TETHER 6095
#define DS_RULE_PRIO_FROM_SUBNET 6100

/* netd's fixed local_network routing table
 * (RouteController::ROUTE_TABLE_LOCAL_NETWORK, constant since Android 5.x). */
#define DS_ANDROID_TABLE_LOCAL_NETWORK 97

#endif /* DS_NET_NET_H */
