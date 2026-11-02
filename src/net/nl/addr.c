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

/* Add an IPv4 address to an interface
 * ip_be and bcast_be are in network byte order. */

int ds_nl_add_addr4(ds_nl_ctx_t *ctx, const char *ifname, uint32_t ip_be,
                    uint8_t prefix) {
  if (prefix > 32)
    return -EINVAL;

  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifaddrmsg ifa;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
  req.n.nlmsg_type = RTM_NEWADDR;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
  req.ifa.ifa_family = AF_INET;
  req.ifa.ifa_prefixlen = prefix;
  req.ifa.ifa_index = (unsigned int)idx;
  req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;

  nl_addattr(&req.n, (int)sizeof(req), IFA_LOCAL, &ip_be, 4);
  nl_addattr(&req.n, (int)sizeof(req), IFA_ADDRESS, &ip_be, 4);

  if (prefix < 32) {
    uint32_t bcast = ip_be | htonl(0xffffffffu >> prefix);
    nl_addattr(&req.n, (int)sizeof(req), IFA_BROADCAST, &bcast, 4);
  }

  return ds_nl_talk(ctx, &req.n);
}

/* Add an IPv4 route
 * dst_be=0 + dst_len=0 → default route.
 * gw_be=0               → connected/link-scope route. */

int ds_nl_add_route4(ds_nl_ctx_t *ctx, uint32_t dst_be, uint8_t dst_len,
                     uint32_t gw_be, int oif_idx) {
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = RTM_NEWROUTE;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
  req.r.rtm_family = AF_INET;
  req.r.rtm_dst_len = dst_len;
  req.r.rtm_table = RT_TABLE_MAIN;
  req.r.rtm_protocol = RTPROT_BOOT;
  req.r.rtm_scope = (gw_be == 0) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
  req.r.rtm_type = RTN_UNICAST;

  if (dst_len > 0)
    nl_addattr(&req.n, (int)sizeof(req), RTA_DST, &dst_be, 4);
  if (gw_be)
    nl_addattr(&req.n, (int)sizeof(req), RTA_GATEWAY, &gw_be, 4);
  nl_addattr(&req.n, (int)sizeof(req), RTA_OIF, &oif_idx, (int)sizeof(int));

  return ds_nl_talk(ctx, &req.n);
}

/* IPv6 siblings of the two helpers above, for NAT66.
 *
 * IFA_F_NODAD: these are our own router addresses on a link we created, so
 * there is nobody to collide with, and waiting out duplicate address detection
 * would leave the address unusable for the first second of every boot. It is
 * the old 8-bit flag, so it works on the 3.10 floor.
 *
 * The route is always link scope (no gateway): the only caller points a
 * container's /64 at its point-to-point veth. */

int ds_nl_add_addr6(ds_nl_ctx_t *ctx, const char *ifname,
                    const struct in6_addr *ip, uint8_t prefix) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifaddrmsg ifa;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
  req.n.nlmsg_type = RTM_NEWADDR;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
  req.ifa.ifa_family = AF_INET6;
  req.ifa.ifa_prefixlen = prefix;
  req.ifa.ifa_index = (unsigned int)idx;
  req.ifa.ifa_flags = IFA_F_NODAD;

  nl_addattr(&req.n, (int)sizeof(req), IFA_LOCAL, ip, sizeof(*ip));
  nl_addattr(&req.n, (int)sizeof(req), IFA_ADDRESS, ip, sizeof(*ip));
  return ds_nl_talk(ctx, &req.n);
}

int ds_nl_add_route6(ds_nl_ctx_t *ctx, const struct in6_addr *dst,
                     uint8_t dst_len, int oif_idx) {
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = RTM_NEWROUTE;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
  req.r.rtm_family = AF_INET6;
  req.r.rtm_dst_len = dst_len;
  req.r.rtm_table = RT_TABLE_MAIN;
  req.r.rtm_protocol = RTPROT_BOOT;
  req.r.rtm_scope = RT_SCOPE_LINK;
  req.r.rtm_type = RTN_UNICAST;

  nl_addattr(&req.n, (int)sizeof(req), RTA_DST, dst, sizeof(*dst));
  nl_addattr(&req.n, (int)sizeof(req), RTA_OIF, &oif_idx, (int)sizeof(int));
  return ds_nl_talk(ctx, &req.n);
}
