/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * ds_dhcp.c - Embedded single-lease DHCP server for NAT containers.
 *
 * Runs as a joinable thread inside the monitor process. Bound exclusively to
 * the container's veth_host interface via SO_BINDTODEVICE so it never
 * interferes with any DHCP server already running on the host.
 *
 * Serves a single static lease (the deterministic IP from veth_peer_ip()) in
 * response to DHCPDISCOVER and DHCPREQUEST. Handles lease renewals for the
 * full container lifetime.
 *
 * This replaces static RTNETLINK IP assignment on the child side, making IP
 * configuration distro-agnostic: every init system (systemd-networkd, OpenRC
 * + udhcpc, dhcpcd, dhclient) speaks DHCP and will configure eth0 correctly
 * without any rootfs modifications.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

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

/* ---------------------------------------------------------------------------
 * DHCP wire protocol constants  (RFC 2131 / RFC 2132)
 * ---------------------------------------------------------------------------*/

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define BOOTP_REQUEST 1
#define BOOTP_REPLY 2
#define HTYPE_ETHERNET 1
#define DHCP_MAGIC 0x63825363u

/* Option 53 message types */
#define DHCPDISCOVER 1
#define DHCPOFFER 2
#define DHCPREQUEST 3
#define DHCPACK 5

/* Option codes */
#define OPT_SUBNET_MASK 1
#define OPT_ROUTER 3
#define OPT_DNS 6
#define OPT_LEASE_TIME 51
#define OPT_MSG_TYPE 53
#define OPT_SERVER_ID 54
#define OPT_RENEWAL_T1 58
#define OPT_REBIND_T2 59
#define OPT_END 255
#define OPT_PAD 0

/* Lease timings (seconds) */
#define DHCP_LEASE_SEC 86400u /* 24 h */
#define DHCP_T1_SEC 43200u    /* 12 h */
#define DHCP_T2_SEC 75600u    /* 21 h */

/* ---------------------------------------------------------------------------
 * DHCP packet layout  (RFC 2131 §2 fixed-format fields)
 *
 * Total: 44 + 16 + 64 + 128 + 4 + 308 = 564 bytes ≤ 576 minimum MTU.
 * The options field is large enough for all options we ever send.
 * ---------------------------------------------------------------------------*/

struct dhcp_pkt {
  uint8_t op;    /* 1=REQUEST, 2=REPLY               */
  uint8_t htype; /* 1=Ethernet                        */
  uint8_t hlen;  /* 6                                 */
  uint8_t hops;  /* 0                                 */
  uint32_t xid;  /* transaction id (network order)    */
  uint16_t secs;
  uint16_t flags;     /* bit15=BROADCAST flag              */
  uint32_t ciaddr;    /* client IP (0 if unknown)          */
  uint32_t yiaddr;    /* "your" IP = offered address       */
  uint32_t siaddr;    /* server next-hop IP                */
  uint32_t giaddr;    /* relay agent IP                    */
  uint8_t chaddr[16]; /* client hardware address           */
  uint8_t sname[64];  /* server host name (unused)         */
  uint8_t file[128];  /* boot file (unused)                */
  uint32_t magic;     /* 0x63825363                        */
  uint8_t options[308];
} __attribute__((packed));

/* ---------------------------------------------------------------------------
 * Module-level state - one context per monitor process
 *
 * Droidspaces runs a separate monitor process per container, so a single
 * global context is sufficient and avoids any cross-container state sharing.
 * ---------------------------------------------------------------------------*/

typedef struct {
  int sock;
  int stop_efd;
  char iface[IFNAMSIZ];
  uint32_t offer_ip_be;  /* IP we are offering */
  uint32_t gw_ip_be;     /* Gateway IP (usually bridge IP) */
  uint32_t netmask_be;   /* 255.255.0.0 for /16               */
  uint32_t dns1_be;      /* DNS 1 */
  uint32_t dns2_be;      /* DNS 2 */
  uint8_t peer_mac[6];   /* Container's MAC */
  uint8_t server_mac[6]; /* Bridge's MAC */
  volatile sig_atomic_t stop;
  pthread_t tid; /* Server thread ID */
  /* Startup synchronization: ds_dhcp_server_start() blocks until the thread
   * has finished socket setup and logged "DHCP Server started", ensuring the
   * log line always appears before any subsequent caller logs (port forwards,
   * boot message, foreground prompt). */
  pthread_cond_t ready_cond;
  int ready; /* set to 1 by thread once past bind() */
} ds_dhcp_ctx_t;

static ds_dhcp_ctx_t g_dhcp;
static pthread_mutex_t g_dhcp_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------------------------------------------------------------------------
 * Option helpers
 * ---------------------------------------------------------------------------*/

/* Append a single DHCP option into buf[*pos].  Returns 0 on success. */
static int opt_put(uint8_t *buf, int *pos, int buflen, uint8_t code,
                   uint8_t len, const void *data) {
  if (*pos + 2 + (int)len > buflen)
    return -1;
  buf[(*pos)++] = code;
  buf[(*pos)++] = len;
  memcpy(buf + *pos, data, len);
  *pos += (int)len;
  return 0;
}

static int opt_put_u8(uint8_t *buf, int *pos, int buflen, uint8_t code,
                      uint8_t v) {
  return opt_put(buf, pos, buflen, code, 1, &v);
}

static int opt_put_u32(uint8_t *buf, int *pos, int buflen, uint8_t code,
                       uint32_t v_be) {
  return opt_put(buf, pos, buflen, code, 4, &v_be);
}

/* Find option `code` in the options blob.  Returns length found, or -1. */
static int opt_get(const uint8_t *opts, int opts_len, uint8_t code,
                   uint8_t *out, int max_len) {
  int i = 0;
  while (i < opts_len) {
    uint8_t c = opts[i++];
    if (c == OPT_END)
      break;
    if (c == OPT_PAD)
      continue;
    if (i >= opts_len)
      break;
    uint8_t l = opts[i++];
    if (i + (int)l > opts_len)
      break;
    if (c == code) {
      int copy = ((int)l < max_len) ? (int)l : max_len;
      memcpy(out, opts + i, (size_t)copy);
      return (int)l;
    }
    i += (int)l;
  }
  return -1;
}

/* ---------------------------------------------------------------------------
 * Checksum helpers
 * ---------------------------------------------------------------------------*/
static uint16_t checksum(const void *data, size_t len) {
  uint32_t sum = 0;
  const uint16_t *buf = (const uint16_t *)data;
  while (len > 1) {
    sum += *buf++;
    len -= 2;
  }
  if (len == 1)
    sum += *(const uint8_t *)buf;
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return (uint16_t)(~sum);
}

/* ---------------------------------------------------------------------------
 * Reply builder
 *
 * Constructs a DHCPOFFER or DHCPACK into *reply.
 * Returns the total packet length to send.
 * ---------------------------------------------------------------------------*/

static int build_reply(struct dhcp_pkt *reply, const struct dhcp_pkt *req,
                       uint8_t msg_type, const ds_dhcp_ctx_t *ctx) {
  memset(reply, 0, sizeof(*reply));

  reply->op = BOOTP_REPLY;
  reply->htype = HTYPE_ETHERNET;
  reply->hlen = req->hlen;
  reply->hops = 0;
  reply->xid = req->xid;     /* echo client's transaction id */
  reply->flags = req->flags; /* preserve BROADCAST flag      */
  reply->ciaddr = 0;
  reply->yiaddr = ctx->offer_ip_be;
  reply->siaddr = ctx->gw_ip_be;
  reply->giaddr = 0;
  memcpy(reply->chaddr, req->chaddr, sizeof(reply->chaddr));
  reply->magic = htonl(DHCP_MAGIC);

  int pos = 0;
  int blen = (int)sizeof(reply->options);

  opt_put_u8(reply->options, &pos, blen, OPT_MSG_TYPE, msg_type);
  opt_put_u32(reply->options, &pos, blen, OPT_SERVER_ID, ctx->gw_ip_be);
  opt_put_u32(reply->options, &pos, blen, OPT_LEASE_TIME,
              htonl(DHCP_LEASE_SEC));
  opt_put_u32(reply->options, &pos, blen, OPT_RENEWAL_T1, htonl(DHCP_T1_SEC));
  opt_put_u32(reply->options, &pos, blen, OPT_REBIND_T2, htonl(DHCP_T2_SEC));
  opt_put_u32(reply->options, &pos, blen, OPT_SUBNET_MASK, ctx->netmask_be);
  opt_put_u32(reply->options, &pos, blen, OPT_ROUTER, ctx->gw_ip_be);

  /* DNS: up to two servers packed as a single option */
  if (ctx->dns1_be) {
    uint8_t dns_buf[8];
    int dns_len = 0;
    memcpy(dns_buf, &ctx->dns1_be, 4);
    dns_len += 4;
    if (ctx->dns2_be) {
      memcpy(dns_buf + 4, &ctx->dns2_be, 4);
      dns_len += 4;
    }
    opt_put(reply->options, &pos, blen, OPT_DNS, (uint8_t)dns_len, dns_buf);
  }

  reply->options[pos++] = OPT_END;

  return (int)offsetof(struct dhcp_pkt, options) + pos;
}

/* ---------------------------------------------------------------------------
 * Reply transmitter
 *
 * Always broadcasts to 255.255.255.255:68.  The veth pair is a private
 * point-to-point link so broadcast reaches only the container - no ARP
 * dependency during initial address acquisition.
 * ---------------------------------------------------------------------------*/

static int send_reply(int sock, int ifindex, const struct dhcp_pkt *pkt,
                      int pkt_len, const ds_dhcp_ctx_t *ctx) {
  uint8_t buffer[2048 + 2];
  uint8_t *frame = buffer + 2;
  struct ethhdr *eth = (struct ethhdr *)frame;
  struct iphdr *ip = (struct iphdr *)(frame + sizeof(struct ethhdr));
  struct udphdr *udp =
      (struct udphdr *)(frame + sizeof(struct ethhdr) + sizeof(struct iphdr));
  uint8_t *payload = frame + sizeof(struct ethhdr) + sizeof(struct iphdr) +
                     sizeof(struct udphdr);

  int total_len = (int)(sizeof(struct ethhdr) + sizeof(struct iphdr) +
                        sizeof(struct udphdr) + pkt_len);
  if ((size_t)total_len > sizeof(buffer) - 2)
    return -1;

  /* 1. Ethernet Header */
  /* Target the client's MAC from the request */
  memcpy(eth->h_dest, pkt->chaddr, 6);
  memcpy(eth->h_source, ctx->server_mac, 6); /* Use bridge MAC */
  eth->h_proto = htons(ETH_P_IP);

  /* 2. IP Header */
  memset(ip, 0, sizeof(*ip));
  ip->ihl = 5;
  ip->version = 4;
  ip->tos = 0x10; /* Low delay */
  ip->tot_len =
      htons((uint16_t)(sizeof(struct iphdr) + sizeof(struct udphdr) + pkt_len));
  ip->id = 0;
  ip->frag_off = 0;
  ip->ttl = 64;
  ip->protocol = IPPROTO_UDP;
  ip->saddr = ctx->gw_ip_be; /* Spoof 172.28.0.1 */
  ip->daddr = htonl(INADDR_BROADCAST);
  ip->check = checksum(ip, sizeof(*ip));

  /* 3. UDP Header */
  udp->source = htons(DHCP_SERVER_PORT);
  udp->dest = htons(DHCP_CLIENT_PORT);
  udp->len = htons((uint16_t)(sizeof(struct udphdr) + pkt_len));
  udp->check = 0; /* UDP checksum is optional for IPv4, set to 0 */

  /* 4. DHCP Payload */
  memcpy(payload, pkt, (size_t)pkt_len);

  struct sockaddr_ll dst;
  memset(&dst, 0, sizeof(dst));
  dst.sll_family = AF_PACKET;
  dst.sll_ifindex = ifindex;
  dst.sll_halen = 6;
  memset(dst.sll_addr, 0xff, 6);

  ssize_t sent = sendto(sock, frame, (size_t)total_len, 0,
                        (struct sockaddr *)&dst, sizeof(dst));
  if (sent < 0) {
    ds_warn("[DHCP] sendto: %s", strerror(errno));
    return -1;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Core server loop (runs as joinable thread)
 * ---------------------------------------------------------------------------*/

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

  /* 0. Create the AF_PACKET socket for SNIFFING EVERYTHING.
   * We use SOCK_RAW + ETH_P_ALL to ensure NO kernel filtering hidden from us.
   */
  packet_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (packet_sock < 0) {
    ds_warn("[DHCP] packet socket: %s", strerror(errno));
    pthread_mutex_lock(&g_dhcp_lock);
    ctx->ready = 1;
    pthread_cond_signal(&ctx->ready_cond);
    pthread_mutex_unlock(&g_dhcp_lock);
    goto out;
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

  while (!ctx->stop) {
    /* Multiplex */
    int poll_ret = poll(fds, 2, -1);
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

    if (!(fds[0].revents & POLLIN))
      continue;

    /* Receive */
    ssize_t len = recv(packet_sock, rx_buf, sizeof(rx_buf), 0);
    if (len < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      if (errno == ENETDOWN || errno == ESHUTDOWN || errno == EBADF)
        break;
      ds_warn("[DHCP] packet recv: %s", strerror(errno));
      break;
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

    /* MAC filter */
    if (memcmp(req.chaddr, ctx->peer_mac, 6) != 0)
      continue;

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

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

void ds_dhcp_server_start(struct ds_config *cfg, const char *veth_host,
                          uint32_t offer_ip_be, uint32_t gw_ip_be,
                          const uint8_t peer_mac[6]) {
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
  memcpy(g_dhcp.peer_mac, peer_mac, 6);

  /* Fetch Bridge MAC */
  /* We need the bridge MAC to spoof the source in DHCP replies.
   * If we use 00:00:00... or a random MAC, the container's ARP will break. */
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s >= 0) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    safe_strncpy(ifr.ifr_name, DS_NAT_BRIDGE, IFNAMSIZ);
    if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
      memcpy(g_dhcp.server_mac, ifr.ifr_hwaddr.sa_data, 6);
    } else {
      /* Fallback to all-zeros if SIOCGIFHWADDR fails (unlikely) */
      memset(g_dhcp.server_mac, 0, 6);
      ds_warn("[DHCP] Failed to get MAC for %s: %s. Using all-zeros.",
              DS_NAT_BRIDGE, strerror(errno));
    }
    close(s);
  } else {
    memset(g_dhcp.server_mac, 0, 6);
    ds_warn(
        "[DHCP] Failed to create socket for MAC lookup: %s. Using all-zeros.",
        strerror(errno));
  }

  /* Resolve DNS to advertise in the DHCP lease.
   * Always use the explicit --dns servers if given, otherwise fall back to
   * the compiled-in defaults (1.1.1.1 / 8.8.8.8). */
  g_dhcp.dns1_be = inet_addr(DS_DNS_DEFAULT_1);
  g_dhcp.dns2_be = inet_addr(DS_DNS_DEFAULT_2);
  if (cfg && cfg->dns_servers[0]) {
    char tmp[256];
    strncpy(tmp, cfg->dns_servers, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *saveptr = NULL;
    char *tok = strtok_r(tmp, ", ", &saveptr);
    if (tok) {
      in_addr_t a = inet_addr(tok);
      if (a != (in_addr_t)(-1))
        g_dhcp.dns1_be = (uint32_t)a;
    }
    tok = strtok_r(NULL, ", ", &saveptr);
    if (tok) {
      in_addr_t a = inet_addr(tok);
      if (a != (in_addr_t)(-1))
        g_dhcp.dns2_be = (uint32_t)a;
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
  if (pthread_create(&g_dhcp.tid, NULL, dhcp_server_loop, &g_dhcp) != 0) {
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
