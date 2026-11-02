/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_DHCP_INTERNAL_H
#define DS_NET_DHCP_INTERNAL_H

/* DHCP wire protocol constants  (RFC 2131 / RFC 2132) */

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

/* DHCP packet layout  (RFC 2131 §2 fixed-format fields)
 *
 * Total: 44 + 16 + 64 + 128 + 4 + 308 = 564 bytes ≤ 576 minimum MTU.
 * The options field is large enough for all options we ever send. */

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

typedef struct {
  int sock;
  int stop_efd;
  char iface[IFNAMSIZ];
  uint32_t offer_ip_be;  /* IP we are offering */
  uint32_t gw_ip_be;     /* Gateway IP (usually bridge IP) */
  uint32_t netmask_be;   /* 255.255.0.0 for /16               */
  uint32_t dns1_be;      /* DNS 1 */
  uint32_t dns2_be;      /* DNS 2 */
  uint8_t server_mac[6]; /* Bridge's MAC (source MAC for replies) */
  int ra_on;             /* also answer IPv6 Router Solicitations */
  struct in6_addr ra_prefix;
  volatile sig_atomic_t stop;
  pthread_t tid; /* Server thread ID */
  /* Startup synchronization: ds_dhcp_server_start() blocks until the thread
   * has finished socket setup and logged "DHCP Server started", ensuring the
   * log line always appears before any subsequent caller logs (port forwards,
   * boot message, foreground prompt). */
  pthread_cond_t ready_cond;
  int ready; /* set to 1 by thread once past bind() */
} ds_dhcp_ctx_t;

int opt_get(const uint8_t *opts, int opts_len, uint8_t code, uint8_t *out,
            int max_len);
int build_reply(struct dhcp_pkt *reply, const struct dhcp_pkt *req,
                uint8_t msg_type, const ds_dhcp_ctx_t *ctx);
int send_reply(int sock, int ifindex, const struct dhcp_pkt *pkt, int pkt_len,
               const ds_dhcp_ctx_t *ctx);

#endif /* DS_NET_DHCP_INTERNAL_H */
