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

/* Enumerate all network interfaces via RTM_GETLINK dump.
 *
 * Fills `names` with up to `max` interface name strings.
 * Returns the number of interfaces found.
 * Used by the uplink whitelist scan in net/route/uplink.c for pattern matching
 * against interface families like "rmnet*" or "*ccmni*". */
int ds_nl_list_ifaces(ds_nl_ctx_t *ctx, char names[][IFNAMSIZ], int max) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_GETLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.i.ifi_family = AF_UNSPEC;
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;

  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return 0;

  int count = 0;
  uint8_t buf[NL_BUFSIZE];

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;
    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto list_ifaces_done;
      if (h->nlmsg_type != RTM_NEWLINK)
        continue;
      struct ifinfomsg *ifi = NLMSG_DATA(h);
      struct rtattr *rta = IFLA_RTA(ifi);
      int rlen = (int)IFLA_PAYLOAD(h);
      char ifname[IFNAMSIZ] = {0};
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == IFLA_IFNAME) {
          safe_strncpy(ifname, RTA_DATA(rta), IFNAMSIZ);
          break;
        }
      }
      if (ifname[0] && count < max)
        safe_strncpy(names[count++], ifname, IFNAMSIZ);
    }
  }

list_ifaces_done:
  return count;
}

/* Count how many interfaces with a given prefix currently exist.
 * Used by ds_net_cleanup() to decide whether to remove shared rules:
 * shared rules (MASQUERADE, FORWARD, Android policy) must only be removed
 * when the LAST container stops, not when one of many stops. */
int ds_nl_count_ifaces_with_prefix(ds_nl_ctx_t *ctx, const char *prefix) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_GETLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.i.ifi_family = AF_UNSPEC;
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;

  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return 0;

  int count = 0;
  size_t prefix_len = strlen(prefix);
  uint8_t buf[NL_BUFSIZE];

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;
    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto count_done;
      if (h->nlmsg_type != RTM_NEWLINK)
        continue;
      struct ifinfomsg *ifi = NLMSG_DATA(h);
      struct rtattr *rta = IFLA_RTA(ifi);
      int rlen = (int)IFLA_PAYLOAD(h);
      char ifname[IFNAMSIZ] = {0};
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == IFLA_IFNAME) {
          safe_strncpy(ifname, RTA_DATA(rta), IFNAMSIZ);
          break;
        }
      }
      if (ifname[0] && strncmp(ifname, prefix, prefix_len) == 0)
        count++;
    }
  }

count_done:
  return count;
}

/* Count links enslaved to `bridge` whose name starts with `prefix`.
 *
 * Used by gateway-mode cleanup to refcount clients on a specific delegated
 * bridge (the global ds-v* count cannot tell ds-br0 from ds-lan clients).
 * Returns 0 if the bridge does not exist. */
int ds_nl_count_bridge_members_with_prefix(ds_nl_ctx_t *ctx, const char *bridge,
                                           const char *prefix) {
  unsigned int br_idx = if_nametoindex(bridge);
  if (br_idx == 0)
    return 0;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_GETLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.i.ifi_family = AF_UNSPEC;
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;

  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return 0;

  int count = 0;
  size_t prefix_len = strlen(prefix);
  uint8_t buf[NL_BUFSIZE];

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;
    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto member_done;
      if (h->nlmsg_type != RTM_NEWLINK)
        continue;
      struct ifinfomsg *ifi = NLMSG_DATA(h);
      struct rtattr *rta = IFLA_RTA(ifi);
      int rlen = (int)IFLA_PAYLOAD(h);
      char ifname[IFNAMSIZ] = {0};
      int master = 0;
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == IFLA_IFNAME)
          safe_strncpy(ifname, RTA_DATA(rta), IFNAMSIZ);
        else if (rta->rta_type == IFLA_MASTER)
          master = (int)nl_rta_u32(rta);
      }
      if (master == (int)br_idx && ifname[0] &&
          strncmp(ifname, prefix, prefix_len) == 0)
        count++;
    }
  }

member_done:
  return count;
}
