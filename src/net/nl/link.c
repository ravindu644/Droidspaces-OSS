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

/* Link existence check */

int ds_nl_link_exists(ds_nl_ctx_t *ctx, const char *ifname) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[512];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_GETLINK;
  /* No NLM_F_ACK: a successful GETLINK already replies with RTM_NEWLINK; adding
   * ACK makes the kernel send a *second* (NLMSG_ERROR err=0) message, whose
   * leftover in the socket buffer used to desync the next request's reply.
   * A missing link still returns NLMSG_ERROR(-ENODEV) regardless of ACK. */
  req.n.nlmsg_flags = NLM_F_REQUEST;
  req.i.ifi_family = AF_UNSPEC;
  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, ifname,
             (int)strlen(ifname) + 1);
  return (ds_nl_talk(ctx, &req.n) == 0) ? 1 : 0;
}

/* Get interface index by name
 * (uses if_nametoindex - one ioctl, no netlink round-trip needed) */

int ds_nl_get_ifindex(ds_nl_ctx_t *ctx, const char *ifname) {
  (void)ctx;
  unsigned int idx = if_nametoindex(ifname);
  return (idx > 0) ? (int)idx : -ENODEV;
}

/* Create bridge */

int ds_nl_create_bridge(ds_nl_ctx_t *ctx, const char *name) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[1024];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;

  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, name,
             (int)strlen(name) + 1);

  struct rtattr *linfo = nl_nest_begin(&req.n, (int)sizeof(req), IFLA_LINKINFO);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_INFO_KIND, "bridge", 7);
  nl_nest_end(&req.n, linfo);

  int ret = ds_nl_talk(ctx, &req.n);
  /* -EEXIST means bridge already present - idempotent */
  return (ret == 0 || ret == -EEXIST) ? 0 : ret;
}

/* Create veth pair
 *
 * The VETH_INFO_PEER attribute wraps a full struct ifinfomsg header followed
 * by IFLA_* sub-attributes - exactly as iproute2/ip/link_veth.c does it.
 * We write the ifinfomsg directly at NLMSG_TAIL (it is NOT an rtattr payload)
 * then append IFLA_IFNAME as a normal sub-rtattr. */

/* Create a veth pair. With peer_netns_fd >= 0 the peer is born inside that
 * network namespace, already under its final name and with peer_mac if given.
 * That matters when the namespace belongs to a running container: a link
 * created on the host and moved in afterwards shows up there under its
 * temporary name first on kernels before 5.x, even when the move carries the
 * new name, and older systemd-networkd does not look at it again after the
 * rename. Peer placement at creation works on every kernel we support. */
int ds_nl_create_veth_in(ds_nl_ctx_t *ctx, const char *host, const char *peer,
                         int peer_netns_fd, const uint8_t *peer_mac) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[2048];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;

  /* Host-side name */
  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, host,
             (int)strlen(host) + 1);

  /* LINKINFO → INFO_KIND="veth" → INFO_DATA → VETH_INFO_PEER */
  struct rtattr *linfo = nl_nest_begin(&req.n, (int)sizeof(req), IFLA_LINKINFO);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_INFO_KIND, "veth", 5);

  struct rtattr *ldata =
      nl_nest_begin(&req.n, (int)sizeof(req), IFLA_INFO_DATA);
  struct rtattr *peer_rta =
      nl_nest_begin(&req.n, (int)sizeof(req), VETH_INFO_PEER);

  /* Embed peer ifinfomsg header directly (not as rtattr payload) */
  {
    struct ifinfomsg peer_ifi;
    memset(&peer_ifi, 0, sizeof(peer_ifi));
    peer_ifi.ifi_family = AF_UNSPEC;

    uint8_t *base = (uint8_t *)&req.n;
    size_t off = NLMSG_ALIGN(req.n.nlmsg_len);
    if (off + sizeof(peer_ifi) > sizeof(req))
      return -ENOSPC;
    memcpy(base + off, &peer_ifi, sizeof(peer_ifi));
    req.n.nlmsg_len = (uint32_t)(off + sizeof(peer_ifi));

    /* Peer-side IFLA_IFNAME */
    nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, peer,
               (int)strlen(peer) + 1);
    if (peer_netns_fd >= 0)
      nl_addattr(&req.n, (int)sizeof(req), IFLA_NET_NS_FD, &peer_netns_fd,
                 (int)sizeof(int));
    if (peer_mac)
      nl_addattr(&req.n, (int)sizeof(req), IFLA_ADDRESS, peer_mac, 6);
  }

  nl_nest_end(&req.n, peer_rta);
  nl_nest_end(&req.n, ldata);
  nl_nest_end(&req.n, linfo);

  return ds_nl_talk(ctx, &req.n);
}

int ds_nl_create_veth(ds_nl_ctx_t *ctx, const char *host, const char *peer) {
  return ds_nl_create_veth_in(ctx, host, peer, -1, NULL);
}

/* Create a macvlan on the NIC parent_index of the namespace ctx lives in.
 * With netns_fd >= 0 it is born inside that namespace, under `name` and with
 * `mac`, in one request, exactly like the far end of a veth.
 *
 * It cannot be born as eth0 when its parent is called eth0, whichever
 * namespace asks: the kernel checks the name, and with IFLA_LINK_NETNSID even
 * registers the device, in the parent's namespace before moving it. So the
 * caller creates it under a name the host does not have and renames it. */
int ds_nl_create_macvlan(ds_nl_ctx_t *ctx, const char *name, int parent_index,
                         int netns_fd, uint32_t mode, const uint8_t mac[6]) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[512];
  } req;
  uint32_t link = (uint32_t)parent_index;

  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;

  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, name,
             (int)strlen(name) + 1);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_LINK, &link, sizeof(link));
  if (netns_fd >= 0)
    nl_addattr(&req.n, (int)sizeof(req), IFLA_NET_NS_FD, &netns_fd,
               (int)sizeof(int));
  nl_addattr(&req.n, (int)sizeof(req), IFLA_ADDRESS, mac, 6);

  struct rtattr *linfo = nl_nest_begin(&req.n, (int)sizeof(req), IFLA_LINKINFO);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_INFO_KIND, "macvlan", 8);
  struct rtattr *ldata =
      nl_nest_begin(&req.n, (int)sizeof(req), IFLA_INFO_DATA);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_MACVLAN_MODE, &mode, sizeof(mode));
  nl_nest_end(&req.n, ldata);
  nl_nest_end(&req.n, linfo);

  return ds_nl_talk(ctx, &req.n);
}

/* Attach an interface to a bridge (IFLA_MASTER) */

int ds_nl_set_master(ds_nl_ctx_t *ctx, const char *ifname, const char *master) {
  int master_idx = ds_nl_get_ifindex(ctx, master);
  if (master_idx <= 0)
    return -ENODEV;

  int if_idx = ds_nl_get_ifindex(ctx, ifname);
  if (if_idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = if_idx;

  nl_addattr(&req.n, (int)sizeof(req), IFLA_MASTER, &master_idx,
             (int)sizeof(int));
  return ds_nl_talk(ctx, &req.n);
}

/* Bring link UP / DOWN */

/* Set or clear IFF_UP on an interface (shared by link_up / link_down). */
static int ds_nl_link_set_up(ds_nl_ctx_t *ctx, const char *ifname, int up) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;
  req.i.ifi_flags = up ? IFF_UP : 0;
  req.i.ifi_change = IFF_UP;
  return ds_nl_talk(ctx, &req.n);
}

int ds_nl_link_up(ds_nl_ctx_t *ctx, const char *ifname) {
  return ds_nl_link_set_up(ctx, ifname, 1);
}

static int ds_nl_link_down(ds_nl_ctx_t *ctx, const char *ifname) {
  return ds_nl_link_set_up(ctx, ifname, 0);
}

/* Delete a link by name. Returns 1 when this call removed it, 0 when it was
 * already gone (so callers can log only what they actually did), negative on
 * any other error. */

int ds_nl_del_link(ds_nl_ctx_t *ctx, const char *ifname) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return 0;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_DELLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;

  int ret = ds_nl_talk(ctx, &req.n);
  if (ret == 0)
    return 1;
  return (ret == -ENODEV || ret == -ENOENT) ? 0 : ret;
}

/* Rename an interface
 * Note: interface must be DOWN before rename; we bring it down first. */

int ds_nl_rename(ds_nl_ctx_t *ctx, const char *ifname, const char *newname) {
  ds_nl_link_down(ctx, ifname); /* must be down or EBUSY */

  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;
  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, newname,
             (int)strlen(newname) + 1);
  return ds_nl_talk(ctx, &req.n);
}

/* Set an interface's L2 hardware address (IFLA_ADDRESS).
 * mac points to 6 bytes.  The link should be DOWN to avoid EBUSY on some
 * drivers; veth tolerates it either way, but callers set it before bringing
 * the interface up. */

int ds_nl_set_mac(ds_nl_ctx_t *ctx, const char *ifname, const uint8_t mac[6]) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;
  nl_addattr(&req.n, (int)sizeof(req), IFLA_ADDRESS, mac, 6);
  return ds_nl_talk(ctx, &req.n);
}
