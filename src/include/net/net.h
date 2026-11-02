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

/* Opaque RTNETLINK context - defined in net/nl/internal.h */
typedef struct ds_nl_ctx ds_nl_ctx_t;

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

/* cleanup.c */
void ds_net_cleanup(struct ds_config *cfg, pid_t container_pid);

/* dhcp/server.c */
/* Start a single-lease DHCP server on veth_host (detached monitor thread).
 * Offers offer_ip_be to any DHCP client that broadcasts on the interface.
 * gw_ip_be becomes the router/server-id option (typically DS_NAT_GW_IP).
 * Isolation is enforced by AF_PACKET bind to veth_host's ifindex; no MAC
 * filter is needed or used (see net/dhcp/server.c for rationale). */
void ds_dhcp_server_start(struct ds_config *cfg, const char *veth_host,
                          uint32_t offer_ip_be, uint32_t gw_ip_be,
                          const struct in6_addr *ra_prefix);

/* Stop the DHCP server and unblock its recv() loop. Call before veth teardown.
 */
void ds_dhcp_server_stop(void);

/* dns.c */
int fix_networking_host(struct ds_config *cfg);
int ds_get_dns_servers(const char *custom_dns, char *out, size_t size);

/* gateway/lifecycle.c */
/* Gateway LAN lifecycle: bridge-only veth plumbing, no NAT/DHCP/firewall. */
int setup_gateway_veth_side(struct ds_config *cfg, pid_t child_pid);

/* Gateway self-heal: when the gateway container (re)boots, re-wire its LAN
 * cable to every running client that delegates to it, with no client restart.
 * Called from the gateway container's monitor on each boot cycle. */
void ds_net_rewire_gateway_clients(const char *gateway_name, pid_t gateway_pid);
int ds_net_gateway_reconcile(struct ds_config *cfg, pid_t client_pid);

/* Gateway teardown: when a container that ACTS AS A GATEWAY stops, explicitly
 * delete the gateway-side veth(s) it serves and reap any now-idle delegated
 * bridge.  The kernel does not auto-reap these (the host-side veth pins its
 * orphan peer netns), so this prevents the leak.  No-op for a non-gateway. */
void ds_net_gateway_teardown(const char *gateway_name);

/* guest.c */
int fix_networking_rootfs(struct ds_config *cfg);
int setup_veth_child_side_named(struct ds_config *cfg, const char *peer_name,
                                const char *ip_str);

/* ipt/core.c */
void parse_cidr(const char *cidr, uint32_t *ip_out, uint32_t *mask_out);

/* ipt/portfwd.c */
int ds_ipt_add_portforwards(struct ds_port_forward *pfs, int count,
                            const char *container_ip);
int ds_ipt_remove_portforwards(struct ds_config *cfg);

/* ipt/rules.c */
/* family is AF_INET or AF_INET6. Each call tries the raw ip_tables /
 * ip6_tables socket first and falls back to the iptables / ip6tables binary
 * for whatever the kernel rejects. */
int ds_ipt_ensure_masquerade(int family, const char *src_cidr);
int ds_ipt_host_rules_present(int family, const char *iface,
                              const char *src_cidr, int expect_dnat);
int ds_ipt_ensure_forward_accept(int family, const char *iface);
int ds_ipt_ensure_input_accept(int family, const char *iface);
int ds_ipt_ensure_mss_clamp(int family);
int ds_ipt_remove_iface_rules(int family, const char *iface);
int ds_ipt_remove_ds_rules(int family);
int ds_ipt6_available(void);

/* macvlan.c */
/* Macvlan: create eth0 in the container on its parent NIC. Called at start
 * and then on every heartbeat, since unplugging the parent deletes it. */
int ds_net_macvlan_wire(struct ds_config *cfg, pid_t init_pid, int at_start);

/* Refuse a macvlan start that could never get a network. 0 or -1. */
int ds_net_macvlan_check(const struct ds_config *cfg);

/* naming.c */
/* Populate a ds_net_handshake from a container init PID + resolved config.
 * peer_name is derived from the PID (stable per-boot interface name).
 * ip_str    is taken from cfg->static_nat_ip (stable across boots). */
void ds_net_derive_handshake(pid_t init_pid, struct ds_config *cfg,
                             struct ds_net_handshake *hs);

/* nat/veth.c */
/* NAT veth/bridge lifecycle */
int setup_veth_host_side(struct ds_config *cfg, pid_t child_pid);

/* netns.c */
void ds_net_prepare_host_veth(const char *ifname);
void ds_net_steer_rx(const char *sys, const char *ifname);

/* nl/addr.c */
int ds_nl_add_addr4(ds_nl_ctx_t *ctx, const char *ifname, uint32_t ip_be,
                    uint8_t prefix);
int ds_nl_add_route4(ds_nl_ctx_t *ctx, uint32_t dst_be, uint8_t dst_len,
                     uint32_t gw_be, int oif_idx);
int ds_nl_add_addr6(ds_nl_ctx_t *ctx, const char *ifname,
                    const struct in6_addr *ip, uint8_t prefix);
int ds_nl_add_route6(ds_nl_ctx_t *ctx, const struct in6_addr *dst,
                     uint8_t dst_len, int oif_idx);

/* nl/core.c */
ds_nl_ctx_t *ds_nl_open(void);
void ds_nl_close(ds_nl_ctx_t *ctx);

/* nl/link.c */
int ds_nl_link_exists(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_get_ifindex(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_create_bridge(ds_nl_ctx_t *ctx, const char *name);
int ds_nl_create_veth(ds_nl_ctx_t *ctx, const char *host, const char *peer);
int ds_nl_create_macvlan(ds_nl_ctx_t *ctx, const char *name, int parent_index,
                         int netns_fd, uint32_t mode, const uint8_t mac[6]);
int ds_nl_create_veth_in(ds_nl_ctx_t *ctx, const char *host, const char *peer,
                         int peer_netns_fd, const uint8_t *peer_mac);
int ds_nl_set_master(ds_nl_ctx_t *ctx, const char *ifname, const char *master);
int ds_nl_link_up(ds_nl_ctx_t *ctx, const char *ifname);

/* 1 = removed by this call, 0 = was already gone, negative = error. */
int ds_nl_del_link(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_rename(ds_nl_ctx_t *ctx, const char *ifname, const char *newname);
int ds_nl_set_mac(ds_nl_ctx_t *ctx, const char *ifname, const uint8_t mac[6]);

/* nl/linkdump.c */
int ds_nl_count_ifaces_with_prefix(ds_nl_ctx_t *ctx, const char *prefix);

/* Count links enslaved to `bridge` whose name starts with `prefix`.
 * Returns 0 if the bridge does not exist. */
int ds_nl_count_bridge_members_with_prefix(ds_nl_ctx_t *ctx, const char *bridge,
                                           const char *prefix);
int ds_nl_list_ifaces(ds_nl_ctx_t *ctx, char names[][IFNAMSIZ], int max);

/* nl/probe.c */
/* 1 if the kernel can create a macvlan, cached per boot */
int ds_nl_probe_macvlan(void);

/* Kernel capability probe - call before any NAT setup */
int ds_nl_probe_nat_capability(char *reason, size_t rsz);

/* nl/route_query.c */
int ds_nl_get_iface_table(ds_nl_ctx_t *ctx, int family, const char *ifname,
                          int *table_out);
int ds_nl_get_table_default_oif(ds_nl_ctx_t *ctx, int family, int table,
                                char *ifname_out);
int ds_nl_get_android_default(ds_nl_ctx_t *ctx, int family, char *ifname_out,
                              int *table_out);

/* nl/rule.c */
int ds_nl_rule6(ds_nl_ctx_t *ctx, int add, const struct in6_addr *net,
                uint8_t len, int from_net, int table, int priority);
int ds_nl_add_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority);
int ds_nl_del_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority);

/* route/monitor.c */
void ds_net_start_route_monitor(const char *container_name);
void ds_net_stop_route_monitor(void);

/* Marks route_localnet as required; route monitor re-asserts it every cycle
 * (same pattern as ip_forward). Called once port-forward rules with localhost
 * DNAT are installed. Sticky for process lifetime - never cleared. */
void ds_net_mark_local_forward_active(void);

/* static_ip.c */
/* Validate a user-supplied static NAT IP string.
 * Must be a valid IPv4 address inside DS_DEFAULT_SUBNET (172.28.0.0/16),
 * with octet3 in 1-254 and octet4 in 1-254 (excludes gateway row .0.x).
 * Returns 0 on success, -1 on failure (reason written to errbuf). */
int ds_net_validate_static_ip(const char *ip_str, char *errbuf, size_t errsize);

/* Scan all container.config files in the workspace for a static_nat_ip
 * collision.  exclude_name (the current container) is skipped.
 * Returns 0 if ip_str is unique across all other configs, 1 if collision. */
int ds_net_check_ip_collision(const char *ip_str, const char *exclude_name);

/* Called from start_rootfs() before the first config save and before fork.
 * Resolves cfg->static_nat_ip:
 *   - If user provided --nat-ip: validate + uniqueness check; fall back to
 *     auto-assign on failure.
 *   - If not provided (or fell back): derive a name-based deterministic IP,
 *     then walk forward until a unique slot is found.
 * After this call cfg->static_nat_ip is always a valid, unique IP string.
 * Callers must save config after this returns to persist the result. */
void ds_net_resolve_static_ip(struct ds_config *cfg);

#endif /* DS_NET_NET_H */
