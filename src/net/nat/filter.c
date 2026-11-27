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
#include <linux/if_addr.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include "../internal.h"

/* install_netfilter_rules
 *
 * Install (or reinstall) the host-side netfilter rules for this container's
 * topology.  Counterpart to install_policy_rules(): the single source of truth
 * for the iptables side, called once at container start and again by the route
 * monitor after netd flushes the tables.  Every helper is idempotent.
 *
 * Reads the g_host_* snapshot rather than cfg, so the monitor thread can call
 * it without reaching into a struct the reboot path rewrites in place. */

/* Interface the filter rules match on: bridge mode filters on the bridge,
 * bridgeless PTP filters on the veth itself.  Shared by the installer and the
 * monitor's presence probe so the two can never disagree about what to look
 * for.  Empty until setup_veth_host_side() has taken its snapshot. */
const char *host_filter_iface(void) {
  return g_host_bridgeless ? g_host_veth : DS_NAT_BRIDGE;
}

/* One family's rule set. Returns the MASQUERADE result, the one rule without
 * which that family has no connectivity at all. */
static int install_family_rules(int family, const char *subnet) {
  const char *iface = host_filter_iface();
  const char *label = (family == AF_INET6) ? "IPv6" : "IPv4";

  if (iface[0]) {
    ds_ipt_ensure_input_accept(family, iface);
    if (ds_ipt_ensure_forward_accept(family, iface) < 0)
      ds_warn("[NET] %s FORWARD ACCEPT failed", label);
  }

  int masq = ds_ipt_ensure_masquerade(family, subnet);
  if (masq < 0)
    ds_warn("[NET] %s MASQUERADE rule failed", label);

  ds_ipt_ensure_mss_clamp(family);
  return masq;
}

void install_netfilter_rules(void) {
  install_family_rules(AF_INET, DS_DEFAULT_SUBNET);
  if (g_host_nat6)
    install_family_rules(AF_INET6, DS_NAT6_SUBNET);

  if (g_host_port_forward_count > g_host_port_forward6_count &&
      g_host_container_ip[0])
    ds_ipt_add_portforwards(g_host_port_forwards, g_host_port_forward_count,
                            g_host_container_ip);
}

int g_host_port_forward6_active;

/* SLAAC can finish after init starts, and some guests use stable privacy
 * addresses. Read the address the kernel assigned instead of guessing it
 * from the veth MAC. Only consider our advertised prefix on eth0. */
void sync_ipv6_portforwards(int force) {
  static char current[INET6_ADDRSTRLEN];
  char next[INET6_ADDRSTRLEN] = "";
  if (g_host_nat6 && g_host_init_pid > 0) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/net/if_inet6", g_host_init_pid);
    FILE *f = fopen(path, "re");
    if (!f && !force)
      return;
    if (f) {
      char line[128], hex[33], iface[IFNAMSIZ];
      while (fgets(line, sizeof(line), f)) {
        unsigned int scope, flags;
        if (sscanf(line, "%32[0-9a-f] %*x %*x %x %x %15s", hex, &scope, &flags,
                   iface) != 4 ||
            strlen(hex) != 32 || scope || strcmp(iface, "eth0") != 0 ||
            (flags & (IFA_F_TEMPORARY | IFA_F_TENTATIVE | IFA_F_DADFAILED |
                      IFA_F_DEPRECATED)))
          continue;
        char expanded[40];
        snprintf(expanded, sizeof(expanded),
                 "%.4s:%.4s:%.4s:%.4s:%.4s:%.4s:%.4s:%.4s", hex, hex + 4,
                 hex + 8, hex + 12, hex + 16, hex + 20, hex + 24, hex + 28);
        struct in6_addr addr;
        if (inet_pton(AF_INET6, expanded, &addr) == 1 &&
            memcmp(&addr, &g_host_nat6_prefix, 8) == 0) {
          inet_ntop(AF_INET6, &addr, next, sizeof(next));
          if (strcmp(next, current) == 0)
            break;
        }
      }
      int failed = ferror(f);
      fclose(f);
      if (failed)
        return;
    }
  }

  if (force || strcmp(current, next) != 0) {
    g_host_port_forward6_active =
        ds_ipt_sync_portforwards6(g_host_port_forwards,
                                  g_host_port_forward_count,
                                  g_host_container_ip, next) > 0;
    /* Record the address even on failure so a kernel without ip6tables DNAT or
     * addrtype does not re-attempt and spam warnings on every 1.5s tick. */
    safe_strncpy(current, next, sizeof(current));
  }
}

/* Turning on IPv6 forwarding makes the kernel ignore Router Advertisements on
 * every interface left at accept_ra=1. A host that got its own address by
 * SLAAC would then lose its default route when the current one expires. 2
 * means "accept even while forwarding". Android already runs its uplinks at 2,
 * and if forwarding is already on, whoever enabled it has dealt with this. */
static void keep_host_slaac(void) {
  char val[4] = {0};
  if (read_file("/proc/sys/net/ipv6/conf/all/forwarding", val, sizeof(val)) >
          0 &&
      val[0] == '1')
    return;

  DIR *d = opendir("/proc/sys/net/ipv6/conf");
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.' || !strcmp(e->d_name, "all") ||
        !strcmp(e->d_name, "lo") || !strncmp(e->d_name, "ds-", 3))
      continue;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/accept_ra",
             e->d_name);
    if (read_file(path, val, sizeof(val)) > 0 && val[0] == '1')
      write_file(path, "2");
  }
  closedir(d);
}

/* NAT66 host side: the IPv6 mirror of steps 1, 2 and 5 below, modelled on
 * LXC's lxc-net (ULA on the bridge, forwarding, one MASQUERADE rule, RAs).
 *
 * Runs once the bridge or the bridgeless veth is up. Fills *prefix with the
 * /64 this container should be advertised and returns 1, or returns 0 when
 * IPv6 stays off: the user disabled it, the kernel has no IPv6, or it has no
 * ip6tables nat table. In that last case the container keeps a link-local
 * address only and everything goes over IPv4 as before. */
int setup_nat6_host(ds_nl_ctx_t *ctx, struct ds_config *cfg,
                    struct in6_addr *prefix) {
  g_host_nat6 = 0;
  if (cfg->disable_ipv6 ||
      access("/proc/sys/net/ipv6/conf/all/forwarding", F_OK) != 0)
    return 0;

  /* Not a warning: stock GKI kernels have no IPv6 nat table, and telling
   * every one of those users on every start would be noise. `droidspaces
   * check` reports it. */
  if (!ds_ipt6_available()) {
    ds_log("[NET] IPv6 NAT unavailable (the kernel lacks CONFIG_IP6_NF_NAT or "
           "CONFIG_IP6_NF_TARGET_MASQUERADE) - container stays IPv4 only");
    return 0;
  }

  /* Our end is a router: it must have IPv6 on even if the host default is
   * off, and it must never autoconfigure itself from a container's RA.
   *
   * accept_ra_rt_table only exists on Android kernels, where its default of
   * -1000 makes the kernel file an interface's IPv6 connected routes in a
   * private table (1000 + ifindex) instead of main. 0 means main, which is
   * where our policy rule sends traffic for the container prefix. */
  const char *iface = host_filter_iface();
  static const char *const knobs[][2] = {{"disable_ipv6", "0"},
                                         {"accept_ra", "0"},
                                         {"autoconf", "0"},
                                         {"accept_ra_rt_table", "0"}};
  for (size_t i = 0; i < sizeof(knobs) / sizeof(knobs[0]); i++) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/%s", iface,
             knobs[i][0]);
    write_file(path, knobs[i][1]);
  }

  struct in6_addr gw_ll, net6;
  inet_pton(AF_INET6, DS_NAT6_GW_LL, &gw_ll);
  inet_pton(AF_INET6, DS_NAT6_PREFIX, &net6);
  *prefix = net6;

  /* Give every container its own /64 from the last two octets of static_nat_ip
   * (172.28.XX.YY, where XX is 1..254). The IPv4 allocator guarantees
   * uniqueness and reserves subnet 0000 for ds-br0's fd64:7370::1, so a guest
   * cannot steer IPv6 port forwarding at the host or a sibling container. */
  struct in_addr v4;
  if (inet_pton(AF_INET, cfg->static_nat_ip, &v4) != 1)
    return 0;
  memcpy(&prefix->s6_addr[6], (uint8_t *)&v4 + 2, 2);

  int err = ds_nl_add_addr6(ctx, iface, &gw_ll, 64);
  if (!cfg->net_bridgeless && err == 0) {
    struct in6_addr gw = net6;
    gw.s6_addr[15] = 1;
    err = ds_nl_add_addr6(ctx, iface, &gw, 64);
  }

  /* Route the container prefix in main in both modes: /64 per veth in
   * bridgeless mode, or the whole /48 on ds-br0 in bridge mode. On Android the
   * kernel may file connected routes in a private per-interface table instead
   * of main, which would drop replies as "network unreachable". */
  if (err == 0)
    err = ds_nl_add_route6(ctx, cfg->net_bridgeless ? prefix : &net6,
                           cfg->net_bridgeless ? 64 : DS_NAT6_PREFIX_LEN,
                           ds_nl_get_ifindex(ctx, iface));
  if (err < 0) {
    ds_warn("[NET] IPv6 setup on %s failed (%d) - container stays IPv4 only",
            iface, err);
    return 0;
  }

  /* Fail closed: without MASQUERADE the container would be handed an address
   * and a default route that lead nowhere, and every IPv6 connection would
   * hang until it timed out instead of falling back to IPv4 at once. A kernel
   * with the nat table but no CONFIG_IP6_NF_TARGET_MASQUERADE lands here. */
  if (install_family_rules(AF_INET6, DS_NAT6_SUBNET) < 0)
    return 0;

  keep_host_slaac();
  write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1");
  g_host_nat6 = 1;

  char pfx[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, prefix, pfx, sizeof(pfx));
  ds_log("[NET] IPv6 NAT enabled: advertising %s/64 via %s", pfx,
         DS_NAT6_GW_LL);
  return 1;
}
