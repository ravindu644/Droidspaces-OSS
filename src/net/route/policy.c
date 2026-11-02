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

/* Uplink routing globals - shared by android routing setup and monitor */

int g_current_gw_table = 0;
static int g_current_gw6_table = 0; /* same, for the uplink's IPv6 routes */
pthread_mutex_t g_gw_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Android-specific policy routing
 *
 * Detects the active uplink (the routing table netd designates as the
 * default internet network), then injects low-priority ip rules to direct
 * container traffic through that table.  Fully automatic - the route
 * monitor keeps the rule in sync across wifi <-> mobile-data handoffs. */

/* Install (or reinstall) the container policy rules.  Single source of truth
 * for the FIB rule set: container start calls it once, and the route monitor
 * calls it on every cycle, because Android's netd flushes the whole rule table
 * when it restarts and strands every running container (see the monitor's
 * reconciliation notes).  ds_nl_add_rule4() maps EEXIST to success, so
 * re-running this is a silent no-op once the rules are in place - which is why
 * it stays quiet and lets the caller report failures.
 *
 * DS_RULE_PRIO_TO_SUBNET (6090): inbound traffic to our subnet always resolves
 * via the main table.  Installed even with no uplink - without it the host
 * cannot reach the container at all.
 *
 * DS_RULE_PRIO_TETHER (6095): replies from our subnet to hotspot/USB-tether
 * clients must consult netd's local_network table (which holds every
 * downstream interface's connected route and no default route) before the
 * uplink table grabs them.  Also uplink-independent, so tether clients can
 * reach forwarded ports even with no WAN.
 *
 * DS_RULE_PRIO_FROM_SUBNET (6100): traffic from our subnet → the uplink's
 * internet table, so it only goes in once an uplink has been selected.
 *
 * All three sit above Android's VPN rule range (10000–22000), so they are
 * checked FIRST and container traffic is never hijacked by a VPN catch-all,
 * and above the OEM reserved low-priority rules (typically < 1000).
 *
 * Returns 0 when every applicable rule is in place, -1 if any add failed. */
static int install_policy_rules(ds_nl_ctx_t *ctx) {
  uint32_t subnet_be, mask_be;
  parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
  (void)mask_be;

  int ret = 0;

  if (ds_nl_add_rule4(ctx, 0, 0, subnet_be, DS_NAT_PREFIX, RT_TABLE_MAIN,
                      DS_RULE_PRIO_TO_SUBNET) < 0)
    ret = -1;

  if (ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0,
                      DS_ANDROID_TABLE_LOCAL_NETWORK, DS_RULE_PRIO_TETHER) < 0)
    ret = -1;

  pthread_mutex_lock(&g_gw_mutex);
  int gw_table = g_current_gw_table;
  pthread_mutex_unlock(&g_gw_mutex);

  if (gw_table > 0 && ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0,
                                      gw_table, DS_RULE_PRIO_FROM_SUBNET) < 0)
    ret = -1;

  /* The same TO and FROM pair for the NAT66 prefix. No tether rule: hotspot
   * clients reach forwarded ports over IPv4, and --port is IPv4 only. */
  if (g_host_nat6) {
    struct in6_addr net6;
    inet_pton(AF_INET6, DS_NAT6_PREFIX, &net6);

    pthread_mutex_lock(&g_gw_mutex);
    int gw6_table = g_current_gw6_table;
    pthread_mutex_unlock(&g_gw_mutex);

    if (ds_nl_rule6(ctx, 1, &net6, DS_NAT6_PREFIX_LEN, 0, RT_TABLE_MAIN,
                    DS_RULE_PRIO_TO_SUBNET) < 0)
      ret = -1;
    if (gw6_table > 0 && ds_nl_rule6(ctx, 1, &net6, DS_NAT6_PREFIX_LEN, 1,
                                     gw6_table, DS_RULE_PRIO_FROM_SUBNET) < 0)
      ret = -1;
  }

  return ret;
}

/* Point the IPv6 FROM rule at the table of the uplink IPv4 just settled on.
 * On an IPv6-only carrier the IPv4 uplink is the CLAT device v4-rmnetX, whose
 * IPv6 routes live in the table of the real rmnetX underneath, so the prefix
 * is stripped. An uplink with no IPv6 route (or iface == NULL) clears the
 * rule, and the container simply keeps using IPv4. */
static void sync_uplink6(ds_nl_ctx_t *ctx, const char *iface) {
  if (!g_host_nat6)
    return;

  int table = 0;
  if (iface) {
    if (strncmp(iface, "v4-", 3) == 0)
      iface += 3;
    ds_nl_get_iface_table(ctx, AF_INET6, iface, &table);
  }

  pthread_mutex_lock(&g_gw_mutex);
  int old = g_current_gw6_table;
  g_current_gw6_table = table;
  pthread_mutex_unlock(&g_gw_mutex);
  if (old == table)
    return;

  struct in6_addr net6;
  inet_pton(AF_INET6, DS_NAT6_PREFIX, &net6);
  if (old > 0)
    ds_nl_rule6(ctx, 0, &net6, DS_NAT6_PREFIX_LEN, 1, old,
                DS_RULE_PRIO_FROM_SUBNET);
  if (table > 0)
    ds_nl_rule6(ctx, 1, &net6, DS_NAT6_PREFIX_LEN, 1, table,
                DS_RULE_PRIO_FROM_SUBNET);
  ds_log("[NET] IPv6 uplink table %d → %d (from %s, prio %d)", old, table,
         DS_NAT6_SUBNET, DS_RULE_PRIO_FROM_SUBNET);
}

void ds_net_setup_android_routing(ds_nl_ctx_t *ctx) {
  char active_iface[IFNAMSIZ] = {0};
  int gw_table = 0;

  /* No uplink yet: find_active_uplink() already logged the single "no WAN"
   * line.  The uplink-independent rules still go in below; the route monitor
   * adds the FROM rule once an interface appears. */
  if (find_active_uplink(ctx, active_iface, &gw_table) == 0) {
    /* Table 0 is an IPv6-only uplink: no IPv4 rule, sync_uplink6() logs. */
    if (gw_table > 0)
      ds_log("[NET] Android routing: active uplink %s → table %d, rule from %s "
             "lookup table %d (prio %d)",
             active_iface, gw_table, DS_DEFAULT_SUBNET, gw_table,
             DS_RULE_PRIO_FROM_SUBNET);
    /* Seed the monitor's current table so it knows the baseline */
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = gw_table;
    pthread_mutex_unlock(&g_gw_mutex);
    sync_uplink6(ctx, active_iface);
  }

  if (install_policy_rules(ctx) < 0)
    ds_warn("[NET] Android routing: one or more policy rules failed to install "
            "- container reachability may be degraded");
}

/* Re-probe which uplink is active and update the ip rule if needed. */
void do_uplink_reprobe(void) {
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    return;

  /* Reconcile our host rules before anything else.
   *
   * Android's netd rebuilds its entire netfilter and policy-routing state when
   * it restarts, and a system_server crash ("soft reboot") is enough to
   * trigger that.  It flushes the iptables built-in chains and the FIB rule
   * table wholesale, taking our rules with them, which silently strands every
   * running container: the policy rules are gone so nothing consults the main
   * table for 172.28.0.0/16 (the host cannot reach the container at all), and
   * MASQUERADE / FORWARD ACCEPT are gone so the container cannot reach the
   * WAN.  The container keeps running and the host's own connectivity comes
   * back, which is why this looks like a Droidspaces failure.
   *
   * Nothing notifies us, so reconcile instead of assuming the one-time setup
   * survived.  This runs even with no uplink at all, since host <-> container
   * traffic over the bridge needs the policy rules regardless of WAN state.
   * The policy rules are Android's; a Linux host routes from main. */
  if (is_android())
    install_policy_rules(ctx);

  /* The netfilter set is gated behind a fork-free probe of every rule in it:
   * the port-forward helpers shell out to the iptables binary, so they must
   * not run every cycle.  Only 0 means
   * "something is definitely missing"; -1 is "tables unreadable", where a
   * blind reinstall through those binary fallbacks would stack duplicates. */
  const char *iface = host_filter_iface();
  if (iface[0] &&
      (ds_ipt_host_rules_present(AF_INET, iface, DS_DEFAULT_SUBNET,
                                 g_host_port_forward_count > 0) == 0 ||
       (g_host_nat6 &&
        ds_ipt_host_rules_present(AF_INET6, iface, DS_NAT6_SUBNET, 0) == 0))) {
    ds_warn("[NET] Route monitor: host netfilter rules are missing "
            "(netd restart or firewall app?) - reinstalling");
    install_netfilter_rules();
  }

  /* Everything below points Android's per-network tables at the uplink. A
   * Linux host has one main table, which already follows the default route. */
  if (!is_android()) {
    ds_nl_close(ctx);
    return;
  }

  char new_iface[IFNAMSIZ] = {0};
  int new_table = 0;

  if (find_active_uplink(ctx, new_iface, &new_table) != 0) {
    /* No uplink available. */
    if (g_upstream_count > 0) {
      sync_uplink6(ctx, NULL);
      /* Pinned mode: the forced interface is gone.  Tear the FROM rule down so
       * container traffic is NOT silently re-routed onto a table the kernel may
       * recycle for a different network (no hopping / no leak).  When the
       * pinned interface returns, a later reprobe reinstalls the rule. */
      pthread_mutex_lock(&g_gw_mutex);
      int old_table = g_current_gw_table;
      pthread_mutex_unlock(&g_gw_mutex);
      if (old_table > 0) {
        uint32_t subnet_be, mask_be;
        parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
        (void)mask_be;
        ds_nl_del_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, old_table,
                        DS_RULE_PRIO_FROM_SUBNET);
        pthread_mutex_lock(&g_gw_mutex);
        g_current_gw_table = 0;
        pthread_mutex_unlock(&g_gw_mutex);
        ds_log("[NET] Route monitor: pinned uplink gone - removed FROM rule "
               "(container WAN is down until it returns)");
      }
    }
    /* Auto mode leaves the current rule in place (avoid flapping on
     * transients); pinned mode has already torn it down above. */
    ds_nl_close(ctx);
    return;
  }

  sync_uplink6(ctx, new_iface);
  sync_resolv_order(new_table == 0);

  pthread_mutex_lock(&g_gw_mutex);
  int old_table = g_current_gw_table;
  pthread_mutex_unlock(&g_gw_mutex);

  if (new_table == old_table) {
    ds_nl_close(ctx);
    return;
  }

  ds_log("[NET] Route monitor: uplink switch table %d → %d (%s)", old_table,
         new_table, new_iface);

  uint32_t subnet_be, mask_be;
  parse_cidr(DS_DEFAULT_SUBNET, &subnet_be, &mask_be);
  (void)mask_be;

  if (old_table > 0)
    ds_nl_del_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, old_table,
                    DS_RULE_PRIO_FROM_SUBNET);

  /* An IPv6-only uplink has no IPv4 table to point at. With the rule gone the
   * host answers the container's IPv4 with "network unreachable" at once, so
   * clients fall back to IPv6 instead of waiting on a dead route. */
  if (new_table == 0) {
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = 0;
    pthread_mutex_unlock(&g_gw_mutex);
    ds_log("[NET] Route monitor: uplink has no IPv4 - removed rule from %s",
           DS_DEFAULT_SUBNET);
  } else if (ds_nl_add_rule4(ctx, subnet_be, DS_NAT_PREFIX, 0, 0, new_table,
                             DS_RULE_PRIO_FROM_SUBNET) == 0) {
    pthread_mutex_lock(&g_gw_mutex);
    g_current_gw_table = new_table;
    pthread_mutex_unlock(&g_gw_mutex);
    ds_log("[NET] Route monitor: rule updated → from %s lookup %d (prio %d)",
           DS_DEFAULT_SUBNET, new_table, DS_RULE_PRIO_FROM_SUBNET);

    if (is_android())
      write_file("/proc/sys/net/ipv4/ip_forward", "1");
  } else {
    ds_warn("[NET] Route monitor: failed to install new rule for table %d",
            new_table);
  }

  ds_nl_close(ctx);
}
