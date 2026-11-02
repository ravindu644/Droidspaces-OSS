/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * ds_dhcp.c - Embedded single-lease DHCP server for NAT containers.
 *
 * Runs as a joinable thread inside the monitor process. Bound exclusively to
 * the container's veth_host interface via SO_BINDTODEVICE + sll_ifindex so it
 * never interferes with sibling containers sharing the same bridge.
 *
 * Serves a single static lease (the container's persisted static_nat_ip) in
 * response to DHCPDISCOVER and DHCPREQUEST. Handles lease renewals for the
 * full container lifetime.
 *
 * This replaces static RTNETLINK IP assignment on the child side, making IP
 * configuration distro-agnostic: every init system (systemd-networkd, OpenRC
 * + udhcpc, dhcpcd, dhclient) speaks DHCP and will configure eth0 correctly
 * without any rootfs modifications.
 *
 * The same thread does the IPv6 half when NAT66 is available: it answers
 * Router Solicitations and re-announces on a timer (see ra.c), since it
 * already owns the only packet socket on this veth.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>

#include "net/dhcp/internal.h"

/* Module-level state - one context per monitor process
 *
 * Droidspaces runs a separate monitor process per container, so a single
 * global context is sufficient and avoids any cross-container state sharing. */

static ds_dhcp_ctx_t g_dhcp;
static pthread_mutex_t g_dhcp_lock = PTHREAD_MUTEX_INITIALIZER;

/* The IPv6 counterpart of an OFFER: one Router Advertisement out of our veth.
 * It goes to the all-nodes group, but the frame leaves through this
 * container's own bridge port, so no sibling ever sees it. */
static void send_ra(int sock, int ifindex, const ds_dhcp_ctx_t *ctx) {
  uint8_t frame[DS_RA_FRAME_LEN];
  ds_ra_build(frame, ctx->server_mac, &ctx->ra_prefix);

  struct sockaddr_ll dst;
  memset(&dst, 0, sizeof(dst));
  dst.sll_family = AF_PACKET;
  dst.sll_ifindex = ifindex;
  dst.sll_halen = 6;
  memcpy(dst.sll_addr, frame, 6);
  if (sendto(sock, frame, sizeof(frame), 0, (struct sockaddr *)&dst,
             sizeof(dst)) < 0)
    ds_warn("[DHCP] RA sendto: %s", strerror(errno));
}

/* Core server loop (runs as joinable thread) */

static void *dhcp_server_loop(void *arg) {
  ds_dhcp_ctx_t *ctx = (ds_dhcp_ctx_t *)arg;

  char offer_str[INET_ADDRSTRLEN];
  struct in_addr tmp_addr;
  tmp_addr.s_addr = ctx->offer_ip_be;
  if (!inet_ntop(AF_INET, &tmp_addr, offer_str, sizeof(offer_str)))
    offer_str[0] = '\0';

  int packet_sock = -1;
  struct dhcp_pkt reply;
  uint8_t rx_buf[2048];

  /* 0. Create the AF_PACKET socket. */
  packet_sock = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
  if (packet_sock < 0) {
    ds_warn("[DHCP] packet socket: %s", strerror(errno));
    pthread_mutex_lock(&g_dhcp_lock);
    ctx->ready = 1;
    pthread_cond_signal(&ctx->ready_cond);
    pthread_mutex_unlock(&g_dhcp_lock);
    goto out;
  }

  /* In bridge mode, the kernel floods L2 broadcasts to all slave ports.
   * SO_BINDTODEVICE is best-effort: some Android kernels ignore it for
   * AF_PACKET. The real guard is recvfrom() + sll_ifindex check below. */
  if (setsockopt(packet_sock, SOL_SOCKET, SO_BINDTODEVICE, ctx->iface,
                 (socklen_t)(strlen(ctx->iface) + 1)) < 0) {
    ds_warn("[DHCP] SO_BINDTODEVICE(%s): %s - cross-container DHCP leakage "
            "possible",
            ctx->iface, strerror(errno));
    /* non-fatal: fall through, sll_ifindex still provides partial isolation */
  }

  struct sockaddr_ll sll;
  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_ifindex = (int)if_nametoindex(ctx->iface);
  sll.sll_protocol = htons(ETH_P_ALL);

  if (bind(packet_sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    ds_warn("[DHCP] packet bind(%s): %s", ctx->iface, strerror(errno));
    pthread_mutex_lock(&g_dhcp_lock);
    ctx->ready = 1;
    pthread_cond_signal(&ctx->ready_cond);
    pthread_mutex_unlock(&g_dhcp_lock);
    goto out;
  }

  ds_log("DHCP Server started on %s  offer=%s", ctx->iface, offer_str);

  /* Signal the launcher that we are fully up and the log line has been
   * emitted.  From this point the caller's subsequent logs (port forwards,
   * boot message, foreground prompt) will always appear after us. */
  pthread_mutex_lock(&g_dhcp_lock);
  ctx->ready = 1;
  pthread_cond_signal(&ctx->ready_cond);
  pthread_mutex_unlock(&g_dhcp_lock);

  struct pollfd fds[2];
  fds[0].fd = packet_sock;
  fds[0].events = POLLIN;
  fds[1].fd = ctx->stop_efd;
  fds[1].events = POLLIN;

  /* Unsolicited RAs keep the container's default route from expiring. The
   * deadline is checked after every wakeup, not only on a poll timeout,
   * because steady traffic on the veth would otherwise starve the timeout. */
  struct timespec now;
  time_t next_ra = 0;

  while (!ctx->stop) {
    /* Multiplex */
    int poll_ret = poll(fds, 2, ctx->ra_on ? DS_RA_INTERVAL_SEC * 1000 : -1);
    if (poll_ret < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      ds_warn("[DHCP] poll: %s", strerror(errno));
      break;
    }

    if (fds[1].revents & POLLIN) {
      /* Stop signaled via eventfd */
      break;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (ctx->ra_on && now.tv_sec >= next_ra) {
      send_ra(packet_sock, sll.sll_ifindex, ctx);
      next_ra = now.tv_sec + DS_RA_INTERVAL_SEC;
    }

    if (!(fds[0].revents & POLLIN))
      continue;

    /* Receive - use recvfrom to get the ingress ifindex.
     * SO_BINDTODEVICE is unreliable on Android kernels for AF_PACKET; the
     * bridge can still deliver sibling-veth frames to our socket.
     * sll_ifindex in the returned sockaddr_ll is the ground truth: it reflects
     * which interface the frame physically arrived on, so we drop anything
     * that didn't come from our own veth. */
    struct sockaddr_ll rx_sll;
    socklen_t rx_sll_len = sizeof(rx_sll);
    ssize_t len = recvfrom(packet_sock, rx_buf, sizeof(rx_buf), 0,
                           (struct sockaddr *)&rx_sll, &rx_sll_len);
    if (len < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      if (errno == ENETDOWN || errno == ESHUTDOWN || errno == EBADF)
        break;
      ds_warn("[DHCP] packet recv: %s", strerror(errno));
      break;
    }

    /* Drop frames from sibling bridge ports */
    if (rx_sll.sll_ifindex != sll.sll_ifindex)
      continue;

    /* Ignore packets sent by the host (e.g., bridge floods from sibling
     * containers). The bridge forwards sibling broadcasts *out* of our veth
     * interface. AF_PACKET intercepts these transmit events and flags them as
     * PACKET_OUTGOING. We only want incoming frames transmitted BY the
     * container. */
    if (rx_sll.sll_pkttype == PACKET_OUTGOING)
      continue;

    if (ctx->ra_on && ds_ra_is_solicit(rx_buf, (size_t)len)) {
      send_ra(packet_sock, sll.sll_ifindex, ctx);
      continue;
    }

    /* 1. Ethernet Header (14 bytes) */
    if (len < (ssize_t)(sizeof(struct ethhdr) + sizeof(struct iphdr) +
                        sizeof(struct udphdr)))
      continue;

    struct ethhdr eth;
    memcpy(&eth, rx_buf, sizeof(eth));
    if (ntohs(eth.h_proto) != ETH_P_IP)
      continue;

    /* 2. IP Header */
    struct iphdr ip;
    memcpy(&ip, rx_buf + sizeof(eth), sizeof(ip));
    if (ip.protocol != IPPROTO_UDP || ip.ihl < 5)
      continue;

    /* 3. UDP Header */
    int ip_hdr_len = ip.ihl * 4;
    if (len < (ssize_t)(sizeof(eth) + ip_hdr_len + sizeof(struct udphdr)))
      continue;

    struct udphdr udp;
    memcpy(&udp, rx_buf + sizeof(eth) + ip_hdr_len, sizeof(udp));

    if (ntohs(udp.dest) != DHCP_SERVER_PORT)
      continue;

    /* 4. DHCP Payload */
    int payload_off = (int)sizeof(eth) + ip_hdr_len + (int)sizeof(udp);
    int req_len = (int)len - payload_off;
    if (req_len < (int)offsetof(struct dhcp_pkt, options))
      continue;

    /* FIND-04: Fix unaligned pointer cast by copying to stack */
    struct dhcp_pkt req;
    if (req_len > (int)sizeof(req))
      req_len = (int)sizeof(req);
    memcpy(&req, rx_buf + payload_off, (size_t)req_len);

    if (ntohl(req.magic) != DHCP_MAGIC)
      continue;

    if (req.op != BOOTP_REQUEST)
      continue;

    int opts_len = (int)(req_len - (int)offsetof(struct dhcp_pkt, options));

    /* chaddr is NOT used for filtering (Ubuntu 24.04+ / systemd-networkd
     * sets it to all-zeros in DUID mode). We filter on eth->h_source instead,
     * which is the kernel-set L2 MAC and always correct. */
    uint8_t type_byte = 0;
    if (opt_get(req.options, opts_len, OPT_MSG_TYPE, &type_byte, 1) < 0)
      continue;

    /* Dispatch */
    switch (type_byte) {

    case DHCPDISCOVER:
      ds_log("[DHCP] DISCOVER  xid=%08x  chaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             ntohl(req.xid), req.chaddr[0], req.chaddr[1], req.chaddr[2],
             req.chaddr[3], req.chaddr[4], req.chaddr[5]);
      {
        int plen = build_reply(&reply, &req, DHCPOFFER, ctx);
        if (send_reply(packet_sock, sll.sll_ifindex, &reply, plen, ctx) == 0)
          ds_log("[DHCP] OFFER    → %s  xid=%08x", offer_str, ntohl(req.xid));
      }
      break;

    case DHCPREQUEST: {
      /* Skip SERVER_ID check for INIT-REBOOT (broadcast requests)
       * Some clients (like Void's dhclient) might be rebinding/rebooting. */
      uint8_t sid[4];
      if (opt_get(req.options, opts_len, OPT_SERVER_ID, sid, 4) == 4) {
        uint32_t sid_be;
        memcpy(&sid_be, sid, 4);
        if (sid_be != ctx->gw_ip_be)
          break;
      }

      ds_log("[DHCP] REQUEST   xid=%08x  chaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             ntohl(req.xid), req.chaddr[0], req.chaddr[1], req.chaddr[2],
             req.chaddr[3], req.chaddr[4], req.chaddr[5]);

      int plen = build_reply(&reply, &req, DHCPACK, ctx);
      if (send_reply(packet_sock, sll.sll_ifindex, &reply, plen, ctx) == 0)
        ds_log("[DHCP] ACK      → %s  xid=%08x", offer_str, ntohl(req.xid));
      break;
    }
    }
  }

out:
  if (packet_sock >= 0)
    close(packet_sock);
  ctx->sock = -1;
  ds_log("[DHCP] Server stopped on %s", ctx->iface);
  return NULL;
}

/* Public API */

void ds_dhcp_server_start(struct ds_config *cfg, const char *veth_host,
                          uint32_t offer_ip_be, uint32_t gw_ip_be,
                          const struct in6_addr *ra_prefix) {
  pthread_mutex_lock(&g_dhcp_lock);

  memset(&g_dhcp, 0, sizeof(g_dhcp));
  g_dhcp.sock = -1;
  g_dhcp.stop_efd = eventfd(0, EFD_CLOEXEC);
  if (g_dhcp.stop_efd < 0) {
    ds_warn("[DHCP] eventfd failed: %s - aborting", strerror(errno));
    pthread_mutex_unlock(&g_dhcp_lock);
    return;
  }
  g_dhcp.netmask_be = htonl(0xFFFF0000u); /* /16 */
  g_dhcp.stop = 0;
  safe_strncpy(g_dhcp.iface, veth_host, sizeof(g_dhcp.iface));
  g_dhcp.offer_ip_be = offer_ip_be;
  g_dhcp.gw_ip_be = gw_ip_be;
  if (ra_prefix) {
    g_dhcp.ra_on = 1;
    g_dhcp.ra_prefix = *ra_prefix;
  }

  /* Source MAC for DHCP replies = the MAC of the interface we transmit on
   * (veth_host, the bind interface).  That device egresses the reply frame,
   * and in bridgeless mode it also owns the gateway IP, so the container's ARP
   * for the gateway resolves to a real MAC.  Looking up the bind interface
   * works in both bridged and bridgeless modes - no hardcoded bridge
   * dependency.  Falling back to all-zeros would break the container's ARP. */
  int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (s >= 0) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    safe_strncpy(ifr.ifr_name, veth_host, IFNAMSIZ);
    if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
      memcpy(g_dhcp.server_mac, ifr.ifr_hwaddr.sa_data, 6);
    } else {
      /* Fallback to all-zeros if SIOCGIFHWADDR fails (unlikely) */
      memset(g_dhcp.server_mac, 0, 6);
      ds_warn("[DHCP] Failed to get MAC for %s: %s. Using all-zeros.",
              veth_host, strerror(errno));
    }
    close(s);
  } else {
    memset(g_dhcp.server_mac, 0, 6);
    ds_warn(
        "[DHCP] Failed to create socket for MAC lookup: %s. Using all-zeros.",
        strerror(errno));
  }

  /* DNS to advertise in the lease: the first two IPv4 entries of --dns, or
   * the compiled-in defaults. Option 6 can only carry IPv4, so an IPv6 entry
   * is skipped rather than allowed to cost the user one of their servers. */
  g_dhcp.dns1_be = inet_addr(DS_DNS_DEFAULT_1);
  g_dhcp.dns2_be = inet_addr(DS_DNS_DEFAULT_2);
  if (cfg && cfg->dns_servers[0]) {
    char tmp[sizeof(cfg->dns_servers)];
    safe_strncpy(tmp, cfg->dns_servers, sizeof(tmp));
    uint32_t found[2];
    int n = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(tmp, ", ", &saveptr); tok && n < 2;
         tok = strtok_r(NULL, ", ", &saveptr)) {
      struct in_addr a;
      if (inet_pton(AF_INET, tok, &a) == 1)
        found[n++] = a.s_addr;
    }
    if (n > 0) {
      g_dhcp.dns1_be = found[0];
      g_dhcp.dns2_be = n > 1 ? found[1] : 0;
    }
  }

  /* No sockets created here */
  /* We build and bind all sockets inside dhcp_server_loop. This is cleaner
   * and avoids having two threads access ctx->sock during teardown/start. */
  g_dhcp.sock = -1;

  /* Init startup synchronization */
  g_dhcp.ready = 0;
  pthread_cond_init(&g_dhcp.ready_cond, NULL);

  /* Spawn joinable thread */
  /* Joinable (default) so ds_dhcp_server_stop() can pthread_join() and
   * guarantee the thread has fully exited before the next start() call
   * calls memset(&g_dhcp, 0).  A detached thread could still be running
   * when memset fires, corrupting its own context mid-loop. */
  if (ds_thread_create(&g_dhcp.tid, dhcp_server_loop, &g_dhcp) != 0) {
    ds_warn("[DHCP] pthread_create: %s", strerror(errno));
    g_dhcp.sock = -1;
    pthread_mutex_unlock(&g_dhcp_lock);
    return;
  }

  /* Block until the thread has bound its socket and emitted the
   * "DHCP Server started" log line (or failed).  This guarantees that
   * all subsequent caller logs - port forwards, boot message, foreground
   * prompt - are always sequenced strictly after the DHCP line.
   *
   * pthread_cond_wait atomically releases g_dhcp_lock while sleeping,
   * so the DHCP thread can acquire it to set ready=1 and signal us. */
  while (!g_dhcp.ready)
    pthread_cond_wait(&g_dhcp.ready_cond, &g_dhcp_lock);

  pthread_mutex_unlock(&g_dhcp_lock);
}

void ds_dhcp_server_stop(void) {
  pthread_mutex_lock(&g_dhcp_lock);

  if (g_dhcp.tid != 0) {
    g_dhcp.stop = 1;
    if (g_dhcp.stop_efd >= 0) {
      uint64_t val = 1;
      if (write(g_dhcp.stop_efd, &val, sizeof(val)) < 0) {
        /* If write fails, we still proceed to join; poll would eventually
         * timeout or error */
      }
    }
  }

  pthread_t tid = g_dhcp.tid;
  pthread_mutex_unlock(&g_dhcp_lock);

  if (tid != 0) {
    pthread_join(tid, NULL);
    pthread_mutex_lock(&g_dhcp_lock);
    g_dhcp.tid = 0;
    if (g_dhcp.stop_efd >= 0) {
      close(g_dhcp.stop_efd);
      g_dhcp.stop_efd = -1;
    }
    pthread_cond_destroy(&g_dhcp.ready_cond);
    pthread_mutex_unlock(&g_dhcp_lock);
  }
}
