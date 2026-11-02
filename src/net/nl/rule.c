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

/* Policy rule management (RTM_NEWRULE / RTM_DELRULE), either family.
 * src and dst point at an in_addr or in6_addr matching `family`. */

/* Is this exact rule already installed? Kernels before 4.12 ignore NLM_F_EXCL
 * on RTM_NEWRULE and happily add a second copy, so the EEXIST that makes
 * re-installing a rule a no-op never comes. The monitor re-installs on every
 * rule change, its own included, and on those kernels that fed itself: about
 * fifteen duplicates a second until reboot. The dump is always read to the
 * end, a half-read one would be mistaken for the next request's reply. */
static int ds_nl_rule_exists(ds_nl_ctx_t *ctx, int family, const void *src,
                             uint8_t src_len, const void *dst, uint8_t dst_len,
                             int table, int priority) {
  size_t alen = (family == AF_INET6) ? 16 : 4;
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
    return 0;

  uint8_t buf[NL_BUFSIZE];
  int found = 0;
  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      return found;

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
        return found;
      struct rtmsg *r = NLMSG_DATA(h);
      if (h->nlmsg_type != RTM_NEWRULE || r->rtm_family != family ||
          r->rtm_src_len != src_len || r->rtm_dst_len != dst_len)
        continue;

      int r_table = r->rtm_table, r_prio = 0, addr_ok = 1;
      struct rtattr *rta = RTM_RTA(r);
      int rlen = (int)RTM_PAYLOAD(h);
      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == FRA_TABLE)
          r_table = (int)nl_rta_u32(rta);
        else if (rta->rta_type == FRA_PRIORITY)
          r_prio = (int)nl_rta_u32(rta);
        else if (rta->rta_type == FRA_SRC && src_len &&
                 (RTA_PAYLOAD(rta) != alen || memcmp(RTA_DATA(rta), src, alen)))
          addr_ok = 0;
        else if (rta->rta_type == FRA_DST && dst_len &&
                 (RTA_PAYLOAD(rta) != alen || memcmp(RTA_DATA(rta), dst, alen)))
          addr_ok = 0;
      }
      if (addr_ok && r_table == table && r_prio == priority)
        found = 1;
    }
  }
}

static int ds_nl_rule_op(ds_nl_ctx_t *ctx, int cmd, int family, const void *src,
                         uint8_t src_len, const void *dst, uint8_t dst_len,
                         int table, int priority) {
  int alen = (family == AF_INET6) ? 16 : 4;
  if (cmd == RTM_NEWRULE && ds_nl_rule_exists(ctx, family, src, src_len, dst,
                                              dst_len, table, priority))
    return 0;
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = (uint16_t)cmd;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  if (cmd == RTM_NEWRULE)
    req.n.nlmsg_flags |= NLM_F_CREATE | NLM_F_EXCL; /* EXCL: reject duplicates
                                                     * so EEXIST is returned
                                                     * and treated as success
                                                     * by the idempotency
                                                     * handler below */

  req.r.rtm_family = (unsigned char)family;
  req.r.rtm_protocol = 0; /* res1 in fib_rule_hdr */
  req.r.rtm_scope = 0;    /* res2 in fib_rule_hdr */
  req.r.rtm_type = 1;     /* FR_ACT_TO_TBL (1) == RTN_UNICAST */
  req.r.rtm_src_len = src_len;
  req.r.rtm_dst_len = dst_len;
  req.r.rtm_table =
      (table > 0 && table < 256) ? (uint8_t)table : 0; /* RT_TABLE_UNSPEC */

  if (src_len > 0)
    nl_addattr(&req.n, (int)sizeof(req), FRA_SRC, src, alen);
  if (dst_len > 0)
    nl_addattr(&req.n, (int)sizeof(req), FRA_DST, dst, alen);
  if (table > 0) {
    uint32_t t = (uint32_t)table;
    nl_addattr(&req.n, (int)sizeof(req), FRA_TABLE, &t, sizeof(uint32_t));
  }
  if (priority >= 0) {
    uint32_t p = (uint32_t)priority;
    nl_addattr(&req.n, (int)sizeof(req), FRA_PRIORITY, &p, sizeof(uint32_t));
  }

  int ret = ds_nl_talk(ctx, &req.n);
  /* Idempotent: EEXIST for NEW and ENOENT for DEL are both success */
  return (ret == -EEXIST || ret == -ENOENT) ? 0 : ret;
}

int ds_nl_add_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority) {
  return ds_nl_rule_op(ctx, RTM_NEWRULE, AF_INET, &src_be, src_len, &dst_be,
                       dst_len, table, priority);
}

int ds_nl_del_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority) {
  return ds_nl_rule_op(ctx, RTM_DELRULE, AF_INET, &src_be, src_len, &dst_be,
                       dst_len, table, priority);
}

/* IPv6 rules only ever match on one side, so `net` is the source when
 * from_net is set and the destination otherwise. */
int ds_nl_rule6(ds_nl_ctx_t *ctx, int add, const struct in6_addr *net,
                uint8_t len, int from_net, int table, int priority) {
  return ds_nl_rule_op(ctx, add ? RTM_NEWRULE : RTM_DELRULE, AF_INET6,
                       from_net ? net : NULL, from_net ? len : 0,
                       from_net ? NULL : net, from_net ? 0 : len, table,
                       priority);
}
