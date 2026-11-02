/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <arpa/inet.h>
#include <fnmatch.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include "net/internal.h"

static int g_route_monitor_sock = -1;
static volatile sig_atomic_t g_stop_monitor = 0;
/* Helpers lock, held for exactly as long as the route monitor thread lives so
 * a stop command can wait for it. See ds_container_lock_helpers(). */
static int g_helpers_fd = -1;
static pthread_t g_route_monitor_tid;
static int g_route_monitor_started = 0; /* loop active; guarded by g_gw_mutex */
static int g_route_monitor_needs_join = 0; /* thread created, not yet joined;
                                              guarded by g_gw_mutex */
/* Set once any container installs localhost port-forward DNAT rules.
 * Sticky - never cleared, since route_localnet=1 is harmless to leave on
 * and other containers may still depend on it. */
static volatile sig_atomic_t g_local_forward_active = 0;

/* Uplink Route Monitor
 *
 * Watches FIB rule, route, link, and IPv4 address changes on the host.
 * When a relevant change is detected it re-probes which uplink is currently
 * active and atomically updates the container policy rule.  Fully automatic
 * - no user-declared interface list.
 *
 * Event triggers:
 *   RTM_NEWRULE / RTM_DELRULE   - netd swaps the default-network rule
 *                                 (this IS the wifi <-> mobile handoff)
 *   RTM_NEWROUTE / RTM_DELROUTE - default route moved between tables
 *   RTM_NEWLINK / RTM_DELLINK   - interface state change (UP/RUNNING/DOWN)
 *   RTM_NEWADDR / RTM_DELADDR   - IPv4 address assigned or removed
 *
 * With NAT66 on, IPv6 rule and route changes are watched too: on a carrier
 * with no IPv4 the uplink comes and goes with an IPv6 default route.
 *
 * A 1.5s heartbeat covers devices with broken netlink notifications and
 * re-asserts ip_forward, which Android periodically resets. On Linux the
 * monitor runs too, for the forwarding and the netfilter reconcile only. */

static void *route_monitor_loop(void *arg) {
  (void)arg;

  ds_log("[NET] Uplink route monitor started (automatic detection)");

  int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (sock < 0) {
    ds_warn("[NET] Route monitor: failed to open netlink socket: %s",
            strerror(errno));
    return NULL;
  }

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  /* RTMGRP_LINK        - interface state changes (IFF_RUNNING, up/down)
   * RTMGRP_IPV4_IFADDR - IPv4 address add/remove
   * RTMGRP_IPV4_ROUTE  - default route moved between tables
   * RTNLGRP_IPV4_RULE  - netd swapping the default-network FIB rule:
   *                      this IS the wifi <-> mobile handoff signal.
   *                      No legacy RTMGRP_ bitmask macro exists for rule
   *                      groups; the bit is 1 << (RTNLGRP_IPV4_RULE - 1). */
  sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
                 (1u << (RTNLGRP_IPV4_RULE - 1));
  if (g_host_nat6)
    sa.nl_groups |= RTMGRP_IPV6_ROUTE | (1u << (RTNLGRP_IPV6_RULE - 1));

  if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    ds_warn("[NET] Route monitor: failed to bind netlink socket: %s",
            strerror(errno));
    close(sock);
    return NULL;
  }

  pthread_mutex_lock(&g_gw_mutex);
  g_route_monitor_sock = sock;
  pthread_mutex_unlock(&g_gw_mutex);

  uint8_t buf[8192];
  struct pollfd pfd = {.fd = sock, .events = POLLIN};

  while (!g_stop_monitor) {
    /* Enforce IPv4 forwarding in real-time. If ip_forward ever flips to 0
     * the NAT'd container loses all WAN traffic, so re-assert it on every
     * cycle. Android's netd resets it, and on Linux any other tool that
     * restores its own saved value on exit (a USB tether, a VPN) does the
     * same. Android only needs it while an IPv4 uplink is wired; a Linux host
     * has no per-network table to wait for. The kernel does not broadcast
     * POLLERR/inotify events for /proc/sys/ memory variables, so we must
     * poll; reading a 1-byte procfs flag takes < 1 microsecond, costing 0%
     * CPU. */
    if (g_current_gw_table > 0 || !is_android()) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv4/ip_forward", val, sizeof(val)) > 0 &&
          val[0] == '0') {
        ds_log("[NET] Route monitor: ip_forward was disabled - re-enabling...");
        write_file("/proc/sys/net/ipv4/ip_forward", "1\n");
      }
    }

    if (g_host_nat6) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv6/conf/all/forwarding", val,
                    sizeof(val)) > 0 &&
          val[0] == '0')
        write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1\n");
    }

    /* Same treatment for route_localnet - required for localhost port-forward
     * DNAT (127.0.0.1:HOST -> container). Android/netd resets this the same
     * way it resets ip_forward. */
    if (g_local_forward_active) {
      char val[4] = {0};
      if (read_file("/proc/sys/net/ipv4/conf/all/route_localnet", val,
                    sizeof(val)) > 0 &&
          val[0] == '0') {
        ds_log("[NET] Route monitor: route_localnet was disabled - "
               "re-enabling...");
        write_file("/proc/sys/net/ipv4/conf/all/route_localnet", "1\n");
      }
    }

    /* 1.5-second heartbeat: aggressively re-asserts ip_forward and covers
     * devices with broken netlink notifications. */
    int pr = poll(&pfd, 1, 1500);
    if (pr < 0) {
      if (g_stop_monitor)
        break;
      if (errno == EINTR)
        continue;
      ds_warn("[NET] Route monitor: poll failed (%s) - stopping",
              strerror(errno));
      break;
    }

    if (pr == 0) {
      /* A stop that landed during the sleep must not get one more reprobe:
       * it would put back rules a stop command is about to remove. */
      if (g_stop_monitor)
        break;
      do_uplink_reprobe();
      continue;
    }

    ssize_t len = recv(sock, buf, sizeof(buf), 0);
    if (len < 0) {
      if (g_stop_monitor)
        break;
      if (errno == EINTR || errno == EAGAIN)
        continue;
      if (errno == ENOBUFS) {
        /* The netlink multicast receive buffer overflowed during an event
         * burst (e.g. a container reboot tearing down and recreating veths).
         * The kernel dropped notifications but the socket is still valid:
         * resync by reprobing the uplink and keep monitoring, rather than
         * treating a recoverable condition as fatal and killing the thread. */
        do_uplink_reprobe();
        continue;
      }
      ds_warn("[NET] Route monitor: recv failed (%s) - stopping",
              strerror(errno));
      break;
    }
    if (len == 0) {
      /* Only our own shutdown() during stop yields a 0-length read; keep
       * monitoring otherwise instead of exiting. */
      if (g_stop_monitor)
        break;
      continue;
    }

    int should_reprobe = 0;
    struct nlmsghdr *h = (struct nlmsghdr *)buf;

    for (; NLMSG_OK(h, (uint32_t)len); h = NLMSG_NEXT(h, len)) {
      if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
        break;

      if (h->nlmsg_type == RTM_NEWRULE || h->nlmsg_type == RTM_DELRULE) {
        /* FIB rule change - on Android this is netd switching the default
         * network.  Always reprobe; the reprobe is idempotent so the echo
         * of our own rule updates settles in one extra no-op pass. */
        should_reprobe = 1;
      } else if (h->nlmsg_type == RTM_NEWLINK || h->nlmsg_type == RTM_DELLINK) {
        /* Any real interface changing state may affect the uplink - e.g.
         * a new rmnet_dataX popping up mid-session.  Only our own veths
         * and loopback are noise. */
        struct ifinfomsg *ifi = NLMSG_DATA(h);
        char evname[IFNAMSIZ] = {0};
        if_indextoname((unsigned int)ifi->ifi_index, evname);
        if (evname[0] && strncmp(evname, "ds-", 3) != 0 &&
            strcmp(evname, "lo") != 0)
          should_reprobe = 1;
      } else if (h->nlmsg_type == RTM_NEWADDR || h->nlmsg_type == RTM_DELADDR) {
        struct ifaddrmsg *ifa = NLMSG_DATA(h);
        if (ifa->ifa_family == AF_INET) {
          char evname[IFNAMSIZ] = {0};
          if_indextoname((unsigned int)ifa->ifa_index, evname);
          if (evname[0] && strncmp(evname, "ds-", 3) != 0 &&
              strcmp(evname, "lo") != 0)
            should_reprobe = 1;
        }
      } else if (h->nlmsg_type == RTM_NEWROUTE ||
                 h->nlmsg_type == RTM_DELROUTE) {
        /* Default route added/removed in some table.  Filter our own
         * subnet/bridge routes by output interface.  IPv6 also announces
         * every /64, neighbour and cached clone; only a route as short as
         * the ones uplink detection accepts can change the uplink. */
        struct rtmsg *rtm = NLMSG_DATA(h);
        if (rtm->rtm_family == AF_INET ||
            (rtm->rtm_family == AF_INET6 && rtm->rtm_dst_len < 8)) {
          int oif = 0;
          struct rtattr *rta = RTM_RTA(rtm);
          int rlen = (int)RTM_PAYLOAD(h);
          for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
            /* RTA_OK bounds only the declared length; require a full 4-byte
             * payload before dereferencing (defensive; kernel-sourced). */
            if (rta->rta_type == RTA_OIF &&
                RTA_PAYLOAD(rta) >= (int)sizeof(int))
              oif = *(int *)RTA_DATA(rta);
          }
          char evname[IFNAMSIZ] = {0};
          if (oif > 0)
            if_indextoname((unsigned int)oif, evname);
          if (!evname[0] ||
              (strncmp(evname, "ds-", 3) != 0 && strcmp(evname, "lo") != 0))
            should_reprobe = 1;
        }
      }

      if (should_reprobe)
        break;
    }

    if (should_reprobe) {
      /* Handoffs emit a burst of rule/route/addr events back-to-back.
       * Drain the socket for a short window so the burst collapses into
       * a single reprobe instead of one per event. */
      struct pollfd dp = {.fd = sock, .events = POLLIN};
      while (!g_stop_monitor && poll(&dp, 1, 200) > 0) {
        if (recv(sock, buf, sizeof(buf), 0) <= 0)
          break;
      }
      do_uplink_reprobe();
    }
  }

  pthread_mutex_lock(&g_gw_mutex);
  close(sock);
  g_route_monitor_sock = -1;
  /* Mark the loop inactive so a later ds_net_start_route_monitor() (e.g. the
   * next reboot cycle) spawns a fresh thread instead of no-op'ing on a stale
   * flag.  needs_join stays set: this thread has exited but is still joinable,
   * and start/stop will reap it. */
  g_route_monitor_started = 0;
  pthread_mutex_unlock(&g_gw_mutex);

  ds_log("[NET] Uplink route monitor stopped");
  return NULL;
}

void ds_net_stop_route_monitor(void) {
  pthread_mutex_lock(&g_gw_mutex);
  /* Gate the join on needs_join, not started: a thread that self-exited on a
   * fatal error has started==0 but is still joinable and must be reaped. */
  int needs_join = g_route_monitor_needs_join;
  pthread_t tid = g_route_monitor_tid;
  g_stop_monitor = 1;
  if (g_route_monitor_sock >= 0)
    shutdown(g_route_monitor_sock, SHUT_RDWR);
  g_route_monitor_needs_join = 0;
  pthread_mutex_unlock(&g_gw_mutex);

  /* Join so the monitor is fully stopped before cleanup removes the shared
   * MASQUERADE / FIB policy rules - otherwise an in-flight do_uplink_reprobe()
   * could re-add a rule we just deleted, or rewrite ip_forward after teardown.
   */
  if (needs_join) {
    pthread_join(tid, NULL);
    pthread_mutex_lock(&g_gw_mutex);
    g_route_monitor_started = 0;
    pthread_mutex_unlock(&g_gw_mutex);
  }
  /* Joined, so nothing in this process can re-add host rules now. */
  ds_container_unlock(g_helpers_fd);
  g_helpers_fd = -1;
}

void ds_net_mark_local_forward_active(void) { g_local_forward_active = 1; }

void ds_net_start_route_monitor(const char *container_name) {
  pthread_mutex_lock(&g_gw_mutex);
  /* Idempotent: exactly one monitor thread per process.  setup_veth_host_side
   * calls this on every boot cycle (including reboots); the monitor tracks the
   * host uplink, which is independent of the container PID, so a single live
   * instance rightly persists across the container's reboots.
   */
  if (g_route_monitor_started) {
    pthread_mutex_unlock(&g_gw_mutex);
    return;
  }
  /* Not running.  A previous thread may have self-exited on a fatal error
   * without being joined (needs_join==1, started==0); reap it before spawning
   * a replacement so its resources are freed and g_route_monitor_tid is not
   * overwritten while still joinable.  Join outside the mutex (the pattern stop
   * uses) - the exiting thread takes g_gw_mutex on its way out. */
  int reap = g_route_monitor_needs_join;
  pthread_t old_tid = g_route_monitor_tid;
  g_route_monitor_needs_join = 0;
  pthread_mutex_unlock(&g_gw_mutex);

  if (reap)
    pthread_join(old_tid, NULL);

  pthread_mutex_lock(&g_gw_mutex);
  g_stop_monitor = 0;
  if (g_helpers_fd < 0)
    g_helpers_fd = ds_container_lock_helpers(container_name);
  if (ds_thread_create(&g_route_monitor_tid, route_monitor_loop, NULL) != 0) {
    ds_warn("[NET] Failed to start route monitor thread: %s", strerror(errno));
    ds_container_unlock(g_helpers_fd);
    g_helpers_fd = -1;
  } else {
    g_route_monitor_started = 1;
    g_route_monitor_needs_join = 1;
  }
  pthread_mutex_unlock(&g_gw_mutex);
}
