/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * IPv6 Router Advertisements for NAT containers (RFC 4861).
 *
 * This is the IPv6 half of what the embedded DHCP server does for IPv4: the
 * container configures itself, so every init system works without touching
 * the rootfs. LXC gets the same result from dnsmasq's ra-only mode; Android
 * has no dnsmasq, and one fixed 102-byte frame does not need one.
 *
 * Only the frame is built here. The DHCP thread already owns a packet socket
 * on the container's veth, so it does the listening and the sending.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

#define ETH_LEN 14
#define IP6_HLEN 40
#define RA_LEN 48 /* 16 header + 32 prefix option */

static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

/* A Router Solicitation is what a container sends when eth0 comes up. No
 * extension headers are allowed in front of it, so fixed offsets are enough. */
int ds_ra_is_solicit(const uint8_t *frame, size_t len) {
  return len >= ETH_LEN + IP6_HLEN + 8 && frame[12] == 0x86 &&
         frame[13] == 0xdd && frame[ETH_LEN + 6] == IPPROTO_ICMPV6 &&
         frame[ETH_LEN + IP6_HLEN] == 133;
}

/* Build an all-nodes Router Advertisement from fe80::1 announcing `prefix`/64
 * for SLAAC. `out` must hold DS_RA_FRAME_LEN bytes.
 *
 * `mac` is only the ethernet source. The RA deliberately carries no source
 * link-layer option: udev re-assigns a fresh veth's MAC shortly after it is
 * created, so any MAC we captured may already be stale, and the option would
 * plant it in the container's neighbour cache as the gateway. Left out, the
 * container resolves fe80::1 with a normal neighbour solicitation and gets the
 * real answer from the kernel. */
void ds_ra_build(uint8_t *out, const uint8_t mac[6],
                 const struct in6_addr *prefix) {
  static const uint8_t all_nodes_mac[6] = {0x33, 0x33, 0, 0, 0, 1};
  uint8_t *ip = out + ETH_LEN, *ra = ip + IP6_HLEN;

  memset(out, 0, DS_RA_FRAME_LEN);
  memcpy(out, all_nodes_mac, 6);
  memcpy(out + 6, mac, 6);
  out[12] = 0x86;
  out[13] = 0xdd;

  ip[0] = 0x60;
  ip[5] = RA_LEN;
  ip[6] = IPPROTO_ICMPV6;
  ip[7] = 255; /* receivers drop an RA whose hop limit is not 255 */
  ip[8] = 0xfe;
  ip[9] = 0x80;
  ip[23] = 1; /* source fe80::1 */
  ip[24] = 0xff;
  ip[25] = 0x02;
  ip[39] = 1; /* destination ff02::1 */

  ra[0] = 134;
  ra[4] = 64; /* hop limit the container should use */
  ra[6] = DS_RA_ROUTER_LIFETIME >> 8;
  ra[7] = DS_RA_ROUTER_LIFETIME & 0xff;

  uint8_t *opt = ra + 16; /* prefix information */
  opt[0] = 3;
  opt[1] = 4;
  opt[2] = 64;
  opt[3] = 0xc0;            /* on-link + autonomous (SLAAC) */
  put_be32(opt + 4, 86400); /* valid lifetime */
  put_be32(opt + 8, 14400); /* preferred lifetime */
  memcpy(opt + 16, prefix, 8);

  /* ICMPv6 checksum covers a pseudo-header: both addresses, the payload
   * length and the next-header value. */
  uint32_t sum = RA_LEN + IPPROTO_ICMPV6;
  for (int i = 8; i < IP6_HLEN + RA_LEN; i += 2)
    sum += (uint32_t)(ip[i] << 8 | ip[i + 1]);
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  ra[2] = (uint8_t)(~sum >> 8);
  ra[3] = (uint8_t)~sum;
}
