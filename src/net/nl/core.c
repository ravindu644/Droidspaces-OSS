/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Pure-C RTNETLINK client: link, address, route, and policy-rule management.
 * Replaces all `ip link/addr/route/rule` shell invocations.
 *
 * Kernel compatibility: 3.10+ (Android & Linux)
 * No external dependencies beyond musl/glibc.
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

/* Netlink message helpers */

/* Pointer to byte just past the last written byte in a netlink message */
#define NLMSG_TAIL(n)                                                          \
  ((struct rtattr *)(((uint8_t *)(n)) + NLMSG_ALIGN((n)->nlmsg_len)))

/* Append an rtattr with optional payload to a netlink message.
 *
 * Kept out of line: once GCC 14's LTO inlines this into a builder whose
 * attribute sizes fold to constants, it takes `n` for the 16-byte header
 * member rather than the request it heads and reports every memcpy as an
 * overflow. The maxlen check is the real bound, and here it stays opaque. */
__attribute__((noinline)) struct rtattr *nl_addattr(struct nlmsghdr *n,
                                                    int maxlen, int type,
                                                    const void *data,
                                                    int dlen) {
  int len = RTA_LENGTH(dlen);
  if ((int)(NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len)) > maxlen)
    return NULL;
  struct rtattr *rta = NLMSG_TAIL(n);
  rta->rta_type = (unsigned short)type;
  rta->rta_len = (unsigned short)len;
  if (dlen && data)
    memcpy(RTA_DATA(rta), data, (size_t)dlen);
  n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
  return rta;
}

/* Begin a nested rtattr (container with length fixed up by nl_nest_end) */
struct rtattr *nl_nest_begin(struct nlmsghdr *n, int maxlen, int type) {
  return nl_addattr(n, maxlen, type, NULL, 0);
}

/* Fix up the length of a nested rtattr opened by nl_nest_begin */
void nl_nest_end(struct nlmsghdr *n, struct rtattr *nest) {
  /* nl_nest_begin() returns NULL if the buffer was exhausted; guard here so
   * every caller is covered without a NULL write.  The message is then built
   * without the nest and the kernel rejects it (EINVAL) rather than crashing.
   */
  if (!nest)
    return;
  nest->rta_len = (unsigned short)((uint8_t *)NLMSG_TAIL(n) - (uint8_t *)nest);
}

/* Read a 4-byte netlink attribute payload safely.  RTA_OK() bounds only the
 * declared attribute length, not that the payload is a full 4 bytes, so a
 * truncated/malformed attribute would otherwise be read out of bounds.
 * Returns 0 for a short attribute.  (Defensive: the source is the host kernel,
 * which always emits full-width attributes.)  memcpy also avoids any unaligned
 * access. */
uint32_t nl_rta_u32(struct rtattr *rta) {
  uint32_t v = 0;
  if (RTA_PAYLOAD(rta) >= (int)sizeof(v))
    memcpy(&v, RTA_DATA(rta), sizeof(v));
  return v;
}

/* Context lifecycle */

ds_nl_ctx_t *ds_nl_open(void) {
  ds_nl_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return NULL;

  ctx->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (ctx->fd < 0) {
    free(ctx);
    return NULL;
  }

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  if (bind(ctx->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(ctx->fd);
    free(ctx);
    return NULL;
  }

  ctx->pid = getpid();
  ctx->seq = 1;
  return ctx;
}

void ds_nl_close(ds_nl_ctx_t *ctx) {
  if (ctx) {
    close(ctx->fd);
    free(ctx);
  }
}

/* Send + blocking receive with full multi-part / ACK loop
 *
 * Returns 0 on success, negative errno on error.
 * NLMSG_ERROR with error==0 is an explicit ACK (success). */

int ds_nl_talk(ds_nl_ctx_t *ctx, struct nlmsghdr *req) {
  req->nlmsg_seq = ++ctx->seq;
  req->nlmsg_pid = (uint32_t)ctx->pid;

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;

  struct iovec iov = {req, req->nlmsg_len};
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = &sa;
  msg.msg_namelen = sizeof(sa);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  if (sendmsg(ctx->fd, &msg, 0) < 0)
    return -errno;

  uint8_t buf[NL_BUFSIZE];
  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -errno;
    }

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      /* Ignore responses for other in-flight requests */
      if (h->nlmsg_seq != req->nlmsg_seq)
        continue;

      if (h->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = NLMSG_DATA(h);
        return err->error; /* 0 = ACK/success, negative = error */
      }
      if (h->nlmsg_type == NLMSG_DONE)
        return 0;
      if (h->nlmsg_flags & NLM_F_MULTI)
        continue; /* more fragments coming */
      return 0;
    }
    /* No message matching our sequence was in this datagram.  This happens when
     * a prior request left a trailing ACK in the socket buffer, or for a
     * multipart reply that spans datagrams.  recv() again for our actual reply
     * rather than (wrongly) reporting success - the bug that made
     * ds_nl_link_exists() return false positives. */
  }
  return 0;
}
