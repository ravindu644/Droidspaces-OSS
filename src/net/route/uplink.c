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

/* User-pinned upstream interfaces (--upstream).  When non-empty, the uplink is
 * resolved ONLY from this list (priority order, literals + wildcards) and all
 * automatic detection is disabled - an explicit manual override.  Copied from
 * cfg in setup_veth_host_side(), before routing setup and the monitor read it.
 */
char g_upstream_ifaces[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];
int g_upstream_count = 0;

/* Returns 1 if ifname exists and is both UP and RUNNING.
 * On Android, the active data interface has IFF_RUNNING set; an interface
 * that is physically present but not carrying data loses IFF_RUNNING. */
static int iface_is_running(const char *ifname) {
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return 0;
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  safe_strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
  int ret = 0;
  if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
    ret = (ifr.ifr_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING);
  close(fd);
  return ret;
}

/* Built-in uplink classification (no user configuration)
 *
 * k_uplink_patterns: the only interface families that terminate internet
 * access on Android, highest precedence first - Wi-Fi STA, ethernet
 * adapters, CLAT (464xlat IPv4-over-IPv6), Qualcomm mobile data, MTK
 * mobile data.
 *
 * k_uplink_excludes: interfaces that must NEVER be picked as an uplink.
 * swlan, ap, usb, rndis and ncm devices are created by Android to SHARE
 * its connectivity downstream (hotspot / USB tethering - Android acts as
 * the router, these face clients, not the internet).  tun/ppp are VPN
 * tunnels which container traffic intentionally bypasses (see the
 * DS_RULE_PRIO_* rationale in droidspaces.h).  The rest are loopback,
 * placeholders, and our own bridge/veth devices. */

static const char *const k_uplink_patterns[] = {
    "wlan*", "eth*", "v4-*", "rmnet*", "*ccmni*",
};

static const char *const k_uplink_excludes[] = {
    "ds-*",   "lo",   "dummy*", "swlan*", "ap*",  "usb*",
    "rndis*", "ncm*", "p2p*",   "tun*",   "ppp*", "bt-pan",
};

static int uplink_name_excluded(const char *ifname) {
  for (size_t i = 0;
       i < sizeof(k_uplink_excludes) / sizeof(k_uplink_excludes[0]); i++) {
    if (fnmatch(k_uplink_excludes[i], ifname, 0) == 0)
      return 1;
  }
  return 0;
}

/* Last detected uplink interface.  Suppresses the "[NET] active uplink:"
 * log line on every heartbeat - only log when the result changes. */
static char g_last_uplink_iface[IFNAMSIZ];

/* Set to 1 when we've already warned that automatic detection found no
 * uplink.  Cleared when a probe succeeds again, so the next failure after
 * a working period logs once more. */
static int g_uplink_fail_warned;

/* Basic sanity for a probe result: a real, carrier-up interface that is
 * not loopback, not ours, and not a downstream/tether/VPN device. */
static int uplink_candidate_ok(const char *ifname) {
  if (!ifname[0])
    return 0;
  if (uplink_name_excluded(ifname))
    return 0;
  return iface_is_running(ifname);
}

static void log_uplink_change(const char *ifname, int table, const char *method,
                              int family) {
  if (strcmp(g_last_uplink_iface, ifname) != 0) {
    ds_log("[NET] Active uplink: %s (table %d) [%s%s]", ifname, table, method,
           family == AF_INET6 ? ", IPv6 only" : "");
    safe_strncpy(g_last_uplink_iface, ifname, sizeof(g_last_uplink_iface));
  }
  g_uplink_fail_warned = 0; /* reset so next failure logs once */
}

/* Single "no uplink" outcome for both modes: warn once (until the next success)
 * and clear the last-seen cache.  This is the one place the condition is
 * reported - callers must not log it again. */
static void log_no_uplink(void) {
  if (!g_uplink_fail_warned) {
    if (g_upstream_count > 0)
      ds_warn("[NET] Uplink: no pinned --upstream interface is up - container "
              "has no WAN; the route monitor will "
              "wire it when one appears");
    else
      ds_warn(
          "[NET] Uplink: no active internet interface found - container has "
          "no WAN; the route monitor will wire it when one appears");
    g_uplink_fail_warned = 1;
  }
  g_last_uplink_iface[0] = '\0';
}

/* Tier 3 (last resort): scan all interfaces against the built-in uplink
 * whitelist in priority order; return the first that is RUNNING and has
 * an IPv4 route in some table. */
static int scan_uplink_whitelist(ds_nl_ctx_t *ctx, char *iface_out,
                                 int *table_out) {
  char all_ifaces[64][IFNAMSIZ];
  int all_count = ds_nl_list_ifaces(ctx, all_ifaces, 64);

  for (size_t p = 0;
       p < sizeof(k_uplink_patterns) / sizeof(k_uplink_patterns[0]); p++) {
    for (int j = 0; j < all_count; j++) {
      if (fnmatch(k_uplink_patterns[p], all_ifaces[j], 0) != 0)
        continue;
      if (!uplink_candidate_ok(all_ifaces[j]))
        continue;
      int tbl = 0;
      if (ds_nl_get_iface_table(ctx, AF_INET, all_ifaces[j], &tbl) != 0)
        continue;
      if (iface_out)
        safe_strncpy(iface_out, all_ifaces[j], IFNAMSIZ);
      if (table_out)
        *table_out = tbl;
      return 0;
    }
  }
  return -ENOENT;
}

/* Manual override: resolve the user-pinned --upstream list in priority order.
 * The first entry that is RUNNING and has a route of `family` wins.  Literal
 * entries are checked directly; wildcard entries (containing * or ?) are
 * matched with fnmatch() against the live interface list (handles dynamic names
 * like rmnet_dataX or v4-rmnet_dataX whose number changes across reconnects).
 * No exclude heuristics apply here - the user picked the interface
 * deliberately. Returns 0 + fills iface/table, or -ENOENT when none are
 * currently available. */
static int resolve_pinned_uplink(ds_nl_ctx_t *ctx, int family, char *iface_out,
                                 int *table_out) {
  char all_ifaces[64][IFNAMSIZ];
  int all_count = -1; /* enumerated lazily, only if a wildcard needs it */

  for (int i = 0; i < g_upstream_count; i++) {
    const char *pat = g_upstream_ifaces[i];
    int is_wild = (strchr(pat, '*') != NULL || strchr(pat, '?') != NULL);

    if (!is_wild) {
      int tbl = 0;
      if (iface_is_running(pat) &&
          ds_nl_get_iface_table(ctx, family, pat, &tbl) == 0) {
        if (iface_out)
          safe_strncpy(iface_out, pat, IFNAMSIZ);
        if (table_out)
          *table_out = tbl;
        return 0;
      }
      continue;
    }

    if (all_count < 0)
      all_count = ds_nl_list_ifaces(ctx, all_ifaces, 64);
    for (int j = 0; j < all_count; j++) {
      if (fnmatch(pat, all_ifaces[j], 0) != 0)
        continue;
      int tbl = 0;
      if (!iface_is_running(all_ifaces[j]) ||
          ds_nl_get_iface_table(ctx, family, all_ifaces[j], &tbl) != 0)
        continue;
      if (iface_out)
        safe_strncpy(iface_out, all_ifaces[j], IFNAMSIZ);
      if (table_out)
        *table_out = tbl;
      return 0;
    }
  }
  return -ENOENT;
}

/* One pass over the detection tiers for a single address family. Fills
 * name (IFNAMSIZ) and *tbl and returns how the uplink was found, or NULL.
 *
 * If --upstream is set this is a pure manual override: resolve ONLY from that
 * list (resolve_pinned_uplink) and disable all auto-detection - the WAN never
 * hops to whatever netd marks active.  Otherwise it is fully automatic, three
 * tiers, first hit wins:
 *
 *   1. Android netd default-network FIB rule (fwmark 0x0/0xffff iif lo).
 *      The kernel's ground truth - swapped atomically on every handoff,
 *      and never points at IMS/MMS-only interfaces.  Also the only
 *      interface Qualcomm IPA / MTK CCCI has hardware reply sessions
 *      for, which NAT'd forwarded traffic requires.
 *   2. The main routing table's default route - the standard Linux
 *      case (what LXC/Docker implicitly rely on), and ROMs/chroots that
 *      populate the main table.
 *   3. Built-in whitelist scan of known uplink interface families, IPv4 only.
 */
static const char *probe_uplink(ds_nl_ctx_t *ctx, int family, char *name,
                                int *tbl) {
  /* Manual override: pinned --upstream list only, no fallback to auto-detect.
   */
  if (g_upstream_count > 0)
    return resolve_pinned_uplink(ctx, family, name, tbl) == 0 ? "pinned" : NULL;

  /* Tier 1: netd default-network rule (Android only - the rule simply
   * does not exist elsewhere, but skip the dump cost off-Android). */
  if (is_android() && ds_nl_get_android_default(ctx, family, name, tbl) == 0 &&
      uplink_candidate_ok(name))
    return "netd rule";

  /* Tier 2: main-table default route (standard Linux semantics). */
  name[0] = '\0';
  *tbl = RT_TABLE_MAIN;
  if (ds_nl_get_table_default_oif(ctx, family, RT_TABLE_MAIN, name) == 0 &&
      uplink_candidate_ok(name))
    return "main table";

  /* Tier 3: built-in whitelist scan. IPv4 only, because a name match is a
   * guess and the IMS bearer fits it: an rmnetX that is always up, IPv6, with
   * its own default route. Whenever no real default network existed, the
   * container's traffic went out through the carrier's voice network. */
  name[0] = '\0';
  *tbl = 0;
  if (family == AF_INET && scan_uplink_whitelist(ctx, name, tbl) == 0)
    return "whitelist scan";

  return NULL;
}

/* Find the interface/table currently providing internet access.
 *
 * IPv4 goes through every tier first, so dual-stack and CLAT networks resolve
 * exactly as they always did. Only when nothing carries IPv4 is an IPv6-only
 * interface accepted: a carrier without NAT64 gives Android no v4-* device and
 * no IPv4 route in any table, yet the container still reaches the internet
 * through NAT66. Trying IPv6 per tier instead would let tier 1 return rmnetX
 * on a CLAT network before tier 3 finds v4-rmnetX, and cost it its IPv4.
 *
 * Returns 0 and fills iface_out (IFNAMSIZ) / table_out on success. table_out
 * is the IPv4 table, 0 for an IPv6-only uplink: sync_uplink6() works out the
 * IPv6 table from the interface either way. */
int find_active_uplink(ds_nl_ctx_t *ctx, char *iface_out, int *table_out) {
  char name[IFNAMSIZ] = {0};
  int tbl = 0, family = AF_INET;

  const char *method = probe_uplink(ctx, family, name, &tbl);
  if (!method && g_host_nat6) {
    family = AF_INET6;
    method = probe_uplink(ctx, family, name, &tbl);
  }
  if (!method) {
    log_no_uplink();
    return -ENOENT;
  }

  log_uplink_change(name, tbl, method, family);
  if (iface_out)
    safe_strncpy(iface_out, name, IFNAMSIZ);
  if (table_out)
    *table_out = (family == AF_INET) ? tbl : 0;
  return 0;
}
