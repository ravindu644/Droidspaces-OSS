/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * ds_dns_proxy.c - Transparent DNS proxy for NAT containers.
 *
 * Binds a UDP socket to 172.28.0.1:53 (the bridge gateway IP that is always
 * reachable from inside the container's isolated netns).  Forwards every DNS
 * query to the real upstream DNS discovered dynamically from the host, then
 * returns the reply to the container.
 *
 * Why this exists ?
 * Hard-coding 1.1.1.1 / 8.8.8.8 in the container's resolv.conf breaks on
 * devices where those IPs are routed through a different kernel path than
 * container traffic:
 *
 *   • MTK / Qualcomm 464XLAT (CLAT) - mobile data uses v4-rmnet_data* with
 *     its own per-interface routing table.  The container's MASQUERADE traffic
 *     reaches the internet fine, but DNS UDP to 1.1.1.1 hits a dead wlan0
 *     policy rule from a previous wifi session and is silently dropped.
 *
 *   • ISP-restricted DNS - some ISPs block 1.1.1.1 and 8.8.8.8, but their own
 *     DNS (e.g. 202.69.205.1) works because it's in the interface's routing
 *     table already.
 *
 * Solution:
 * The container always queries 172.28.0.1:53.  This proxy forwards to the real
 * upstream DNS that the HOST is currently using, discovered via:
 *   1. Android: "dumpsys connectivity" - per-interface DnsAddresses from netd
 *   2. Linux:   /run/systemd/resolve/resolv.conf (real upstream, not stub)
 *   3. Fallback: /etc/resolv.conf (loopback stubs are skipped)
 *   4. Last resort: DS_DNS_DEFAULT_1 / DS_DNS_DEFAULT_2
 *
 * When the route monitor switches upstream interfaces (wifi ↔ mobile data),
 * ds_dns_proxy_update_upstream() is called with the new interface name and
 * re-probes DNS in-process - no container restart required.
 *
 * Custom DNS (--dns):
 * If the user specified --dns, the proxy is never started.  The container's
 * resolv.conf is written with those servers directly and DHCP also offers them.
 * This is the explicit escape hatch - the proxy only activates when Droidspaces
 * is responsible for figuring out the right DNS.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>

/* DNS wire protocol - max UDP payload per RFC 1035 §2.3.4 (512)
 * but EDNS0 (RFC 6891) allows up to 4096. */
#define DNS_UDP_MAX 4096
/* Max buffer for upstream reply */
#define DNS_REPLY_MAX 4096
/* Upstream reply timeout */
#define DNS_TIMEOUT_SEC 3
/* Poll interval for stop-flag check (ms) */
#define DNS_POLL_MS 200
/* Buffer for one NetworkAgentInfo line from dumpsys (can be very long) */
#define DUMPSYS_LINE_MAX 131072

/* ---------------------------------------------------------------------------
 * Module state - one context per monitor process
 * ---------------------------------------------------------------------------*/

typedef struct {
  int sock;
  in_addr_t dns1; /* current primary upstream, host order */
  in_addr_t dns2; /* current secondary upstream           */
  pthread_mutex_t dns_mutex;
  volatile sig_atomic_t stop;
  pthread_t tid;
  pid_t container_pid;
  in_addr_t last_warn_v4; /* rate-limit per IP */
  time_t last_warn_time;  /* rate-limit threshold */
} ds_dns_proxy_ctx_t;

static ds_dns_proxy_ctx_t g_proxy = {.sock = -1};
static pthread_mutex_t g_proxy_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------------------------------------------------------------------------
 * Upstream DNS discovery
 * ---------------------------------------------------------------------------*/

/* 1 if ip string looks like a loopback / link-local stub */
static int is_stub_ip(const char *ip) {
  /* Skip 127.x.x.x (loopback) and 0.0.0.0 */
  return strncmp(ip, "127.", 4) == 0 || strcmp(ip, "0.0.0.0") == 0;
}

/* Parse `nameserver X.X.X.X` lines from a resolv.conf-style file.
 * Skips loopback stubs.  Returns count of servers found (fills dns1/dns2). */
static int parse_resolv_file(const char *path, char *dns1, char *dns2) {
  FILE *f = fopen(path, "re");
  if (!f)
    return 0;

  int count = 0;
  char line[256];
  while (fgets(line, sizeof(line), f) && count < 2) {
    if (strncmp(line, "nameserver ", 11) != 0)
      continue;
    char *ip = line + 11;
    /* trim trailing whitespace */
    size_t len = strlen(ip);
    while (len > 0 && (unsigned char)ip[len - 1] <= ' ')
      ip[--len] = '\0';
    if (len == 0)
      continue;

    struct in_addr addr;
    if (inet_pton(AF_INET, ip, &addr) != 1)
      continue; /* skip IPv6 */
    if (is_stub_ip(ip))
      continue;

    if (count == 0)
      safe_strncpy(dns1, ip, INET_ADDRSTRLEN);
    else
      safe_strncpy(dns2, ip, INET_ADDRSTRLEN);
    count++;
  }
  fclose(f);
  return count;
}

/* Android: extract IPv4 DNS for `iface` from `dumpsys connectivity`.
 *
 * The output format (Android 5+) has one NetworkAgentInfo block per line.
 * Each block contains:
 *   lp{{InterfaceName: wlan0 ... DnsAddresses: [ /192.168.8.1 ] ...}}
 *
 * We split on "}}" to isolate blocks, then check for our interface name
 * and parse the /x.x.x.x IPs from the DnsAddresses field.
 *
 * The leading '/' is Android's InetAddress.toString() format.
 *
 * Returns count of IPv4 DNS addresses found. */
static int parse_dumpsys_dns(const char *iface, char *dns1, char *dns2) {
  if (!iface || !iface[0])
    return 0;

  FILE *fp = popen("dumpsys connectivity 2>/dev/null", "r");
  if (!fp)
    return 0;

  /* Build the exact pattern we search for so "wlan0" doesn't match "wlan01" */
  char iface_pat[IFNAMSIZ + 20];
  snprintf(iface_pat, sizeof(iface_pat), "InterfaceName: %s ", iface);

  char *line = malloc(DUMPSYS_LINE_MAX);
  if (!line) {
    pclose(fp);
    return 0;
  }

  int count = 0;

  while (fgets(line, DUMPSYS_LINE_MAX, fp) && count == 0) {
    /* Does this line contain our interface? */
    if (!strstr(line, iface_pat))
      continue;

    /* Find DnsAddresses: [ ... ] on the same line */
    char *da = strstr(line, "DnsAddresses: [");
    if (!da)
      continue;
    char *bracket_end = strchr(da, ']');
    if (!bracket_end)
      continue;

    /* Walk the block extracting /x.x.x.x patterns */
    char *p = da;
    while (p < bracket_end && count < 2) {
      char *slash = memchr(p, '/', (size_t)(bracket_end - p));
      if (!slash)
        break;

      /* Read dotted-decimal digits after the slash */
      char ip_buf[INET_ADDRSTRLEN];
      size_t i = 0;
      char *s = slash + 1;
      while (
          i < sizeof(ip_buf) - 1 &&
          (*s == '.' || ((unsigned char)*s >= '0' && (unsigned char)*s <= '9')))
        ip_buf[i++] = *s++;
      ip_buf[i] = '\0';

      struct in_addr addr;
      if (i > 0 && inet_pton(AF_INET, ip_buf, &addr) == 1 &&
          !is_stub_ip(ip_buf)) {
        if (count == 0)
          safe_strncpy(dns1, ip_buf, INET_ADDRSTRLEN);
        else
          safe_strncpy(dns2, ip_buf, INET_ADDRSTRLEN);
        count++;
      }
      p = slash + 1;
    }
  }

  free(line);
  pclose(fp);
  return count;
}

/* Full probe chain.  iface_hint is the currently active upstream interface
 * name (e.g. "v4-rmnet_data2") - used only on Android. Pass NULL to skip
 * the dumpsys step and go straight to resolv.conf. */
static int probe_upstream_dns(const char *iface_hint, char *dns1, char *dns2) {
  dns1[0] = dns2[0] = '\0';
  int count = 0;

  /* 1. Android - dumpsys connectivity (most accurate, per-interface) */
  if (is_android() && iface_hint && iface_hint[0] && !strchr(iface_hint, '*') &&
      !strchr(iface_hint, '?')) {
    count = parse_dumpsys_dns(iface_hint, dns1, dns2);
    if (count > 0) {
      ds_log("[DNS] Upstream DNS from dumpsys (%s): %s%s%s", iface_hint, dns1,
             dns2[0] ? " / " : "", dns2[0] ? dns2 : "");
      goto fill_missing;
    }
    ds_warn("[DNS] dumpsys found no DNS for '%s' - trying resolv.conf",
            iface_hint);
  }

  /* 2. systemd-resolved - real upstream (not the 127.0.0.53 stub) */
  count = parse_resolv_file("/run/systemd/resolve/resolv.conf", dns1, dns2);
  if (count > 0) {
    ds_log("[DNS] Upstream DNS from systemd-resolved: %s%s%s", dns1,
           dns2[0] ? " / " : "", dns2[0] ? dns2 : "");
    goto fill_missing;
  }

  /* 3. Classic resolv.conf (skip loopback stubs) */
  count = parse_resolv_file("/etc/resolv.conf", dns1, dns2);
  if (count > 0) {
    ds_log("[DNS] Upstream DNS from /etc/resolv.conf: %s%s%s", dns1,
           dns2[0] ? " / " : "", dns2[0] ? dns2 : "");
    goto fill_missing;
  }

fill_missing:
  /* Always guarantee at least one usable server */
  if (!dns1[0]) {
    safe_strncpy(dns1, DS_DNS_DEFAULT_1, INET_ADDRSTRLEN);
    ds_warn("[DNS] No upstream DNS found - falling back to %s", dns1);
  }
  if (!dns2[0])
    safe_strncpy(dns2, DS_DNS_DEFAULT_2, INET_ADDRSTRLEN);

  return count;
}

/* ---------------------------------------------------------------------------
 * Forward one DNS query to upstream and return the reply.
 * Returns reply length on success, -1 on timeout or error.
 * ---------------------------------------------------------------------------*/
static ssize_t forward_dns_query(in_addr_t upstream, const uint8_t *query,
                                 size_t qlen, uint8_t *reply_buf,
                                 size_t reply_sz) {
  int fwd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fwd < 0)
    return -1;

  struct timeval tv = {.tv_sec = DNS_TIMEOUT_SEC, .tv_usec = 0};
  setsockopt(fwd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fwd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(53);
  dst.sin_addr.s_addr = upstream;

  ssize_t ret = -1;
  if (sendto(fwd, query, qlen, 0, (struct sockaddr *)&dst, sizeof(dst)) ==
      (ssize_t)qlen)
    ret = recv(fwd, reply_buf, reply_sz, 0);

  close(fwd);
  return (ret > 0) ? ret : -1;
}

/* ---------------------------------------------------------------------------
 * Proxy thread loop
 * ---------------------------------------------------------------------------*/
static void *dns_proxy_loop(void *arg) {
  ds_dns_proxy_ctx_t *ctx = (ds_dns_proxy_ctx_t *)arg;
  ds_log("[DNS] Proxy started on " DS_NAT_GW_IP ":53");

  uint8_t query[DNS_UDP_MAX];
  uint8_t reply[DNS_REPLY_MAX];
  struct pollfd pfd = {.fd = ctx->sock, .events = POLLIN};

  while (!ctx->stop) {
    int pr = poll(&pfd, 1, DNS_POLL_MS);
    if (pr < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (pr == 0)
      continue; /* timeout - recheck stop flag */

    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    ssize_t qlen = recvfrom(ctx->sock, query, sizeof(query), 0,
                            (struct sockaddr *)&client, &clen);
    if (qlen < 0) {
      if (ctx->stop)
        break;
      if (errno == EINTR || errno == EAGAIN)
        continue;
      ds_warn("[DNS] recvfrom: %s", strerror(errno));
      break;
    }
    if (qlen < 12)
      continue; /* too short to be a valid DNS header */

    /* Snapshot current upstream servers under mutex */
    pthread_mutex_lock(&ctx->dns_mutex);
    in_addr_t d1 = ctx->dns1;
    in_addr_t d2 = ctx->dns2;
    pthread_mutex_unlock(&ctx->dns_mutex);

    /* Try primary, then secondary on failure */
    ssize_t rlen =
        forward_dns_query(d1, query, (size_t)qlen, reply, sizeof(reply));
    if (rlen < 0 && d2 != 0 && d2 != d1)
      rlen = forward_dns_query(d2, query, (size_t)qlen, reply, sizeof(reply));

    if (rlen < 0) {
      char s[INET_ADDRSTRLEN];
      struct in_addr ia;
      ia.s_addr = d1;
      inet_ntop(AF_INET, &ia, s, sizeof(s));

      time_t now = time(NULL);
      if (d1 != ctx->last_warn_v4 || (now - ctx->last_warn_time) > 30) {
        ds_warn("[DNS] Upstream %s timed out - dropping query", s);
        ctx->last_warn_v4 = d1;
        ctx->last_warn_time = now;
      }
      continue;
    }

    sendto(ctx->sock, reply, (size_t)rlen, 0, (struct sockaddr *)&client, clen);
  }

  close(ctx->sock);
  ctx->sock = -1;
  ds_log("[DNS] Proxy stopped");
  return NULL;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

void ds_dns_proxy_start(struct ds_config *cfg, pid_t container_pid) {
  /* Only for NAT mode without a --dns override */
  if (!cfg || cfg->net_mode != DS_NET_NAT || cfg->dns_servers[0])
    return;

  pthread_mutex_lock(&g_proxy_lock);

  memset(&g_proxy, 0, sizeof(g_proxy));
  g_proxy.sock = -1;
  g_proxy.stop = 0;
  g_proxy.container_pid = container_pid;
  pthread_mutex_init(&g_proxy.dns_mutex, NULL);

  /* Probe initial upstream DNS.
   *
   * On Android we first ask the kernel which interface is the active
   * default internet network (via "ip rule show" - the same method the
   * route monitor uses).  This resolves wildcard patterns correctly:
   * a user who declares "rmnet*" would previously get no DNS on startup
   * because all wildcard entries were skipped, falling through to the
   * resolv.conf fallback which returns 1.1.1.1 instead of the real ISP
   * DNS.  Using the ip rule result directly gives us the exact interface
   * name regardless of what pattern the user wrote in the config.
   *
   * Probe order:
   *   1. Android: ip rule → resolved iface name → dumpsys DNS
   *   2. Android: literal entries in upstream_ifaces list (no wildcards)
   *   3. systemd-resolved / /etc/resolv.conf fallback
   *   4. compiled-in defaults (1.1.1.1 / 8.8.8.8) */
  char dns1[INET_ADDRSTRLEN] = {0}, dns2[INET_ADDRSTRLEN] = {0};
  int found = 0;

  if (is_android()) {
    /* Step 1: use ip rule to find the actual default network interface.
     * This works even when the user configured only wildcard patterns. */
    char default_iface[IFNAMSIZ] = {0};
    FILE *fp = popen("ip rule show 2>/dev/null", "r");
    if (fp) {
      char line[512];
      while (fgets(line, sizeof(line), fp) && !found) {
        if (!strstr(line, "fwmark 0x0/0xffff"))
          continue;
        if (!strstr(line, "iif lo"))
          continue;
        char *lookup = strstr(line, "lookup ");
        if (!lookup)
          continue;
        lookup += 7;
        size_t li = 0;
        while (lookup[li] && lookup[li] != ' ' && lookup[li] != '\n' &&
               lookup[li] != '\r' && li < (size_t)(IFNAMSIZ - 1))
          li++;
        if (li == 0)
          continue;
        memcpy(default_iface, lookup, li);
        default_iface[li] = '\0';
        if (strcmp(default_iface, "dummy0") == 0 ||
            strcmp(default_iface, "lo") == 0 ||
            strncmp(default_iface, "ds-", 3) == 0) {
          default_iface[0] = '\0';
          continue;
        }
        found = parse_dumpsys_dns(default_iface, dns1, dns2);
        if (found)
          ds_log("[DNS] Initial upstream DNS via ip rule iface '%s': "
                 "%s / %s",
                 default_iface, dns1, dns2[0] ? dns2 : "(none)");
      }
      pclose(fp);
    }

    /* Step 2: try literal entries from the upstream_ifaces config list */
    for (int i = 0; i < cfg->upstream_iface_count && !found; i++) {
      const char *iface = cfg->upstream_ifaces[i];
      if (strchr(iface, '*') || strchr(iface, '?'))
        continue; /* skip wildcards - already handled above */
      found = parse_dumpsys_dns(iface, dns1, dns2);
    }
  }

  /* Step 3 & 4: resolv.conf / compiled-in defaults */
  if (!found)
    probe_upstream_dns(NULL, dns1, dns2);

  g_proxy.dns1 = inet_addr(dns1);
  g_proxy.dns2 = inet_addr(dns2);
  ds_log("[DNS] Proxy initial upstream: %s / %s", dns1,
         dns2[0] ? dns2 : "(none)");

  /* Create UDP socket bound to 172.28.0.1:53.
   * We bind to the specific gateway IP so the socket only receives queries
   * from the container - no accidental interception of host DNS traffic. */
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ds_warn("[DNS] socket: %s - proxy disabled", strerror(errno));
    goto unlock;
  }

  int one = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = inet_addr(DS_NAT_GW_IP);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ds_warn("[DNS] bind(" DS_NAT_GW_IP ":53): %s - proxy disabled",
            strerror(errno));
    close(sock);
    goto unlock;
  }

  g_proxy.sock = sock;

  /* Joinable - ds_dns_proxy_stop() does pthread_join() to guarantee
   * the thread has fully exited before the next start() call. */
  if (pthread_create(&g_proxy.tid, NULL, dns_proxy_loop, &g_proxy) != 0) {
    ds_warn("[DNS] pthread_create: %s - proxy disabled", strerror(errno));
    close(sock);
    g_proxy.sock = -1;
  }

unlock:
  pthread_mutex_unlock(&g_proxy_lock);
}

void ds_dns_proxy_stop(void) {
  pthread_mutex_lock(&g_proxy_lock);
  g_proxy.stop = 1;
  pthread_t tid = g_proxy.tid;
  if (g_proxy.sock >= 0)
    shutdown(g_proxy.sock, SHUT_RDWR); /* unblocks poll/recvfrom */
  pthread_mutex_unlock(&g_proxy_lock);

  if (tid != 0)
    pthread_join(tid, NULL); /* wait fully before memset in next start() */
}

void ds_dns_proxy_update_upstream(const char *new_iface) {
  /* Called from do_upstream_reprobe() in network.c whenever the route
   * monitor switches to a new upstream interface.  Re-probes the correct
   * ISP DNS for that interface and updates the proxy's in-memory servers.
   * The container's resolv.conf still points to 172.28.0.1 - no restart
   * or resolv.conf rewrite needed from the container's perspective. */
  pthread_mutex_lock(&g_proxy_lock);
  int running = (g_proxy.sock >= 0 && g_proxy.tid != 0 && !g_proxy.stop);
  pthread_mutex_unlock(&g_proxy_lock);

  if (!running)
    return;

  char dns1[INET_ADDRSTRLEN], dns2[INET_ADDRSTRLEN];
  probe_upstream_dns(new_iface, dns1, dns2);

  pthread_mutex_lock(&g_proxy.dns_mutex);
  g_proxy.dns1 = inet_addr(dns1);
  g_proxy.dns2 = inet_addr(dns2);
  pthread_mutex_unlock(&g_proxy.dns_mutex);

  ds_log("[DNS] Upstream updated (iface=%s): %s / %s",
         new_iface ? new_iface : "?", dns1, dns2[0] ? dns2 : "(none)");
}
