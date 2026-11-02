/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * The network layer's shared state and helpers, for net/, nat/, route/ and
 * gateway/.
 *
 * All link/addr/route management uses the pure-C RTNETLINK API
 * (net/nl/). iptables rules go through the raw socket API (net/ipt/),
 * which falls back to the iptables binary only where the kernel refuses it
 * and for port forwards.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_INTERNAL_H
#define DS_NET_INTERNAL_H

#define DS_RESOLV_CONF "/run/droidspaces/resolv.conf"

extern int g_current_gw_table;
extern pthread_mutex_t g_gw_mutex;
extern char g_upstream_ifaces[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];
extern int g_upstream_count;
extern int g_host_bridgeless;
extern char g_host_veth[IFNAMSIZ];
extern struct ds_port_forward g_host_port_forwards[DS_MAX_PORT_FORWARDS];
extern int g_host_port_forward_count;
extern char g_host_container_ip[INET_ADDRSTRLEN];
extern int g_host_nat6;
extern pid_t g_host_init_pid;
extern int g_host_dns_managed;

uint32_t ds_net_hash_string(const char *s);
int find_active_uplink(ds_nl_ctx_t *ctx, char *iface_out, int *table_out);
const char *app_veth_host_prefix(const struct ds_config *cfg);
void veth_host_name(const struct ds_config *cfg, pid_t pid, char *buf,
                    size_t sz);
void veth_peer_name(const struct ds_config *cfg, pid_t pid, char *buf,
                    size_t sz);
void ds_derive_mac(const char *key, const char *salt_prefix, uint8_t mac[6]);
void pin_bridge_mac(ds_nl_ctx_t *ctx, const char *bridge);
void gateway_hash_key(struct ds_config *cfg, char *buf, size_t sz);
void gateway_veth_host_name(struct ds_config *cfg, char *host, size_t hsz);
int gateway_ifname_component_ok(const char *s);
void gateway_bridge_name(struct ds_config *cfg, char *buf, size_t sz);
const char *gateway_lan_ifname(struct ds_config *cfg);
int open_container_netns(const char *netns_path);
int ds_netns_link_up(const char *netns_path, const char *from,
                     const char *ifname);
int netns_has_link(const char *netns_path, const char *ifname);
void sync_resolv_order(int v6_first);
const char *host_filter_iface(void);
void install_netfilter_rules(void);
int setup_nat6_host(ds_nl_ctx_t *ctx, struct ds_config *cfg,
                    struct in6_addr *prefix);
void ds_net_setup_android_routing(ds_nl_ctx_t *ctx);
void do_uplink_reprobe(void);
int gateway_segment_lock(const char *bridge);
void gateway_segment_unlock(int fd);
pid_t gateway_pid_of(const char *name);
int gateway_wire_client(struct ds_config *cfg, pid_t client_pid,
                        pid_t gateway_pid);

#endif /* DS_NET_INTERNAL_H */
