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

#include "net/nl/internal.h"

/* Find the default-route table used for internet connectivity
 *
 * On Android, the internet default route is in a policy table with id > 100
 * (named "wlan0", "rmnet1", etc.) rather than in the main table (254).
 * On desktop Linux, the default route is in RT_TABLE_MAIN (254).
 *
 * We dump all routes and return the first default route in a table > 100
 * that is not "dummy0" (Android placeholder interface).
 * Falls back to RT_TABLE_MAIN if no policy table is found.
 *
 * ifname_out and table_out may be NULL.
 * Returns 0 on success, -ENOENT if no default route found. */

/* Per-interface route table lookup
 *
 * Finds the routing table that carries a specific named interface's egress
 * routes for `family` (AF_INET or AF_INET6; the /8 cut-off below holds for
 * both, since a connected IPv6 subnet is a /64 and the internet-bearing
 * routes are ::/0 or 2000::/3).
 * This is the core primitive used by the uplink monitor: rather than guessing
 * the active internet table from all routes (which is ambiguous on Android
 * where multiple interfaces can have simultaneous default routes in separate
 * per-interface tables), we ask directly: "what table does wlan0 / rmnet0 /
 * ccmni1 use?"
 *
 * The route with the shortest prefix wins. A real default route (/0) always
 * does, but split-tunnel VPNs on Android (NordVPN, Tailscale, WireGuard with
 * the LAN excluded) never install one: their netd table holds 0.0.0.0/5,
 * 8.0.0.0/7 and so on instead. Insisting on a /0 left --upstream=tun0 with no
 * table at all (issue #310). Connected subnets are never wider than /8, so
 * anything wider than that is treated as internet-bearing.
 *
 * Returns 0 and fills *table_out on success.
 * Returns -ENODEV if the interface doesn't exist.
 * Returns -ENOENT if no unicast route is found for that interface. */
int ds_nl_get_iface_table(ds_nl_ctx_t *ctx, int family, const char *ifname,
                          int *table_out) {
  unsigned int target_idx = if_nametoindex(ifname);
  if (target_idx == 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct rtmsg r;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = RTM_GETROUTE;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.r.rtm_family = (unsigned char)family;
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;

  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return -errno;

  uint8_t buf[NL_BUFSIZE];
  int found_table = 0;
  /* Only prefixes wider than any connected subnet count. Carriers hand out a
   * /8, Wi-Fi a /24, and neither reaches the internet on its own, so an
   * interface with nothing wider stays "no uplink" and a pinned list falls
   * through to its next entry exactly as it did before split-tunnel support. */
  int best_len = 8;

  /* Read the whole dump: returning early would leave unread messages on the
   * socket for the next request to trip over. */
  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto iface_table_done;
      if (h->nlmsg_type != RTM_NEWROUTE)
        continue;

      struct rtmsg *r = NLMSG_DATA(h);
      if (r->rtm_family != family || r->rtm_type != RTN_UNICAST ||
          r->rtm_dst_len >= best_len)
        continue;

      int r_table = r->rtm_table;
      int r_oif = 0;

      struct rtattr *rta = RTM_RTA(r);
      int rlen = (int)RTM_PAYLOAD(h);
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == RTA_TABLE)
          r_table = (int)nl_rta_u32(rta);
        if (rta->rta_type == RTA_OIF)
          r_oif = (int)nl_rta_u32(rta);
      }

      if ((unsigned int)r_oif == target_idx) {
        found_table = r_table;
        best_len = r->rtm_dst_len;
      }
    }
  }

iface_table_done:
  if (!found_table)
    return -ENOENT;
  if (table_out)
    *table_out = found_table;
  return 0;
}

/* Default-route OIF lookup for a specific routing table
 *
 * Dumps the routes of `family` and returns the interface owning the default
 * route in `table`.  When several default routes coexist in the table
 * (multi-homed hosts), the lowest metric (RTA_PRIORITY) wins - the same
 * tie-break the kernel itself applies.
 *
 * On Android this transparently handles 464xlat: on IPv6-only mobile
 * networks the IPv4 default route inside the default network's table points
 * at the CLAT interface (v4-rmnet_dataX), which is exactly the interface
 * IPv4 forwarding needs.
 *
 * Returns 0 and fills ifname_out (IFNAMSIZ) on success.
 * Returns -ENOENT if the table has no default route for that family. */
int ds_nl_get_table_default_oif(ds_nl_ctx_t *ctx, int family, int table,
                                char *ifname_out) {
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = RTM_GETROUTE;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.r.rtm_family = (unsigned char)family;
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;

  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return -errno;

  uint8_t buf[NL_BUFSIZE];
  int best_oif = 0;
  uint32_t best_metric = 0;
  int found = 0;

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto table_oif_done;
      if (h->nlmsg_type != RTM_NEWROUTE)
        continue;

      struct rtmsg *r = NLMSG_DATA(h);
      /* Only unicast default routes of the family asked for */
      if (r->rtm_family != family || r->rtm_dst_len != 0)
        continue;
      if (r->rtm_type != RTN_UNICAST)
        continue;

      int r_table = r->rtm_table;
      int r_oif = 0;
      uint32_t r_metric = 0;

      struct rtattr *rta = RTM_RTA(r);
      int rlen = (int)RTM_PAYLOAD(h);
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == RTA_TABLE)
          r_table = (int)nl_rta_u32(rta);
        if (rta->rta_type == RTA_OIF)
          r_oif = (int)nl_rta_u32(rta);
        if (rta->rta_type == RTA_PRIORITY)
          r_metric = nl_rta_u32(rta);
      }

      if (r_table != table || r_oif <= 0)
        continue;
      if (!found || r_metric < best_metric) {
        best_oif = r_oif;
        best_metric = r_metric;
        found = 1;
      }
    }
  }

table_oif_done:
  if (!found)
    return -ENOENT;
  if (ifname_out) {
    ifname_out[0] = '\0';
    if (!if_indextoname((unsigned int)best_oif, ifname_out) || !ifname_out[0])
      return -ENODEV;
  }
  return 0;
}

/* Android default-network detection via the kernel FIB rule table
 *
 * Android's netd installs exactly one rule per family of the form:
 *   "<prio>: from all fwmark 0x0/0xffff iif lo lookup <table>"
 * for the active default internet network.  It is swapped atomically when
 * the default network changes (wifi <-> mobile data handoffs), making it
 * the kernel's ground truth.  IMS/MMS-only interfaces never appear here -
 * they use explicit fwmarks (0xd0064, 0xd0066 etc).
 *
 * This is the same source of truth AOSP's own tethering (RouteController)
 * consumes; we read it directly over RTM_GETRULE instead of shelling out
 * to `ip rule show`, so it works even where no `ip` binary exists.
 *
 * If several matching rules exist, the lowest FRA_PRIORITY (= highest
 * precedence) wins, matching kernel rule evaluation order.
 *
 * Returns 0 and fills ifname_out (IFNAMSIZ) / table_out on success.
 * Returns -ENOENT when no such rule exists (non-Android, airplane mode). */
int ds_nl_get_android_default(ds_nl_ctx_t *ctx, int family, char *ifname_out,
                              int *table_out) {
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = RTM_GETRULE;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.r.rtm_family = (unsigned char)family;
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;

  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return -errno;

  uint8_t buf[NL_BUFSIZE];
  int best_table = 0;
  uint32_t best_prio = 0;
  int found = 0;

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto rule_dump_done;
      if (h->nlmsg_type != RTM_NEWRULE)
        continue;

      struct rtmsg *r = NLMSG_DATA(h);
      if (r->rtm_family != family)
        continue;
      /* Only table-lookup actions - skips prohibit/unreachable variants */
      if (r->rtm_type != FR_ACT_TO_TBL)
        continue;
      /* "from all" only - src/dst selectors mean a different kind of rule */
      if (r->rtm_src_len != 0 || r->rtm_dst_len != 0)
        continue;

      int r_table = r->rtm_table;
      uint32_t r_prio = 0;
      uint32_t fwmark = 0, fwmask = 0;
      int have_mark = 0, have_mask = 0, have_uidrange = 0;
      char iifname[IFNAMSIZ] = {0};

      struct rtattr *rta = RTM_RTA(r);
      int rlen = (int)RTM_PAYLOAD(h);
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        switch (rta->rta_type) {
        case FRA_TABLE:
          r_table = (int)nl_rta_u32(rta);
          break;
        case FRA_PRIORITY:
          r_prio = nl_rta_u32(rta);
          break;
        case FRA_FWMARK:
          fwmark = nl_rta_u32(rta);
          have_mark = 1;
          break;
        case FRA_FWMASK:
          fwmask = nl_rta_u32(rta);
          have_mask = 1;
          break;
        case FRA_IIFNAME:
          safe_strncpy(iifname, RTA_DATA(rta), IFNAMSIZ);
          break;
        case FRA_UID_RANGE: {
          /* {start, end} as two u32. Android 15+ netd writes the default
           * network as a range over every uid, 0-2147483647, instead of a
           * bare rule; only a narrower range is a per-app preference. */
          const uint32_t *ur = RTA_DATA(rta);
          if (RTA_PAYLOAD(rta) < 2 * sizeof(*ur) || ur[0] != 0 ||
              ur[1] < 0x7fffffffu)
            have_uidrange = 1;
          break;
        }
        }
      }

      /* The netd default-network signature: fwmark 0x0/0xffff iif lo.
       *
       * Kernel quirk: fib_nl_fill_rule() only emits FRA_FWMARK when the
       * mark is non-zero, so for this rule (mark 0x0) the attribute is
       * absent from the dump and only FRA_FWMASK (0xffff) is present.
       * An absent FRA_FWMARK therefore means mark == 0 - exactly what
       * `ip rule show` assumes when it prints "fwmark 0x0/0xffff". */
      if (!have_mask || fwmask != 0xffff)
        continue;
      if (have_mark && fwmark != 0)
        continue;
      if (strcmp(iifname, "lo") != 0)
        continue;
      /* Skip per-app and work-profile rules (Android 12+ network
       * preference). They share the default-network signature at higher
       * precedence but only apply to some uids. A range over every uid is
       * the global default network itself, see above. */
      if (have_uidrange)
        continue;
      if (r_table <= 0)
        continue;

      if (!found || r_prio < best_prio) {
        best_table = r_table;
        best_prio = r_prio;
        found = 1;
      }
    }
  }

rule_dump_done:
  if (!found)
    return -ENOENT;
  if (table_out)
    *table_out = best_table;
  return ds_nl_get_table_default_oif(ctx, family, best_table, ifname_out);
}
