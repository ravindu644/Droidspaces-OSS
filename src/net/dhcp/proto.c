/*
 * Droidspaces v7 - High-performance Container Runtime
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

#include "internal.h"

/* Option helpers */

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
int opt_get(const uint8_t *opts, int opts_len, uint8_t code, uint8_t *out,
            int max_len) {
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

/* Checksum helpers */
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

/* Reply builder
 *
 * Constructs a DHCPOFFER or DHCPACK into *reply.
 * Returns the total packet length to send. */

int build_reply(struct dhcp_pkt *reply, const struct dhcp_pkt *req,
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

/* Reply transmitter
 *
 * Always broadcasts to 255.255.255.255:68.  The veth pair is a private
 * point-to-point link so broadcast reaches only the container - no ARP
 * dependency during initial address acquisition. */

int send_reply(int sock, int ifindex, const struct dhcp_pkt *pkt, int pkt_len,
               const ds_dhcp_ctx_t *ctx) {
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
