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

/* Host-side networking setup (before container boot) */

/* The default resolvers in the two orders an uplink can call for. Same lines,
 * so same length: sync_resolv_order() swaps one for the other in place. */

#define DNS_LINE(ip) "nameserver " ip "\n"
static const char k_dns_v4_first[] = DNS_LINE(DS_DNS_DEFAULT_1)
    DNS_LINE(DS_DNS_DEFAULT_2) DNS_LINE(DS_DNS_DEFAULT_6);
static const char k_dns_v6_first[] = DNS_LINE(DS_DNS_DEFAULT_6)
    DNS_LINE(DS_DNS_DEFAULT_1) DNS_LINE(DS_DNS_DEFAULT_2);

int ds_get_dns_servers(const char *custom_dns, char *out, size_t size) {
  out[0] = '\0';
  int count = 0;

  /* 0. Try custom DNS if provided */
  if (custom_dns && custom_dns[0]) {
    char buf[1024];
    safe_strncpy(buf, custom_dns, sizeof(buf));
    char *saveptr;
    char *token = strtok_r(buf, ", ", &saveptr);
    while (token && (size_t)strlen(out) < size - 32) {
      char line[128];
      snprintf(line, sizeof(line), "nameserver %s\n", token);
      size_t current_len = strlen(out);
      snprintf(out + current_len, size - current_len, "%s", line);
      count++;
      token = strtok_r(NULL, ", ", &saveptr);
    }
  }

  /* 1. Global stable fallbacks (defined in net/net.h) */
  if (count == 0 && size >= sizeof(k_dns_v4_first)) {
    memcpy(out, k_dns_v4_first, sizeof(k_dns_v4_first));
    count = 3;
  }

  return count;
}

int fix_networking_host(struct ds_config *cfg) {
  ds_log("Configuring host-side networking for %s...", cfg->container_name);

  /* Enable IPv4 forwarding */
  write_file("/proc/sys/net/ipv4/ip_forward", "1");

  /* Re-enable IPv6 globally only in host mode if not disabled */
  if (cfg->net_mode == DS_NET_HOST && !cfg->disable_ipv6) {
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "0");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "0");
  }

  /* Get DNS and store it in the config struct to be used after pivot_root */
  cfg->dns_server_content[0] = '\0';
  int count = ds_get_dns_servers(cfg->dns_servers, cfg->dns_server_content,
                                 sizeof(cfg->dns_server_content));

  if (cfg->dns_servers[0])
    ds_log("Setting up %d custom DNS servers...", count);

  return 0;
}

/* glibc asks the nameservers in file order and waits out a timeout on every
 * one it cannot reach. On an IPv6-only uplink the two IPv4 defaults in front
 * cost each lookup several seconds, and the IPv6 one in front would do the
 * same on an IPv4-only network. So the default list follows the uplink.
 *
 * The file lives inside the container, whose root can put anything at that
 * path. We only ever swap one of our two lists for the other: a file holding
 * anything else, a hand edit included, is left alone, and so is whatever is
 * not a regular file on tmpfs. */
void sync_resolv_order(int v6_first) {
  if (!g_host_dns_managed)
    return;

  const char *want = v6_first ? k_dns_v6_first : k_dns_v4_first;
  const char *stale = v6_first ? k_dns_v4_first : k_dns_v6_first;
  size_t len = sizeof(k_dns_v4_first) - 1;

  int fd =
      ds_open_in_root(g_host_init_pid, DS_RESOLV_CONF, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    return;

  /* One byte more than a list, so a longer file can never compare equal. */
  char cur[sizeof(k_dns_v4_first) + 1] = {0};
  struct stat st;
  struct statfs sfs;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && fstatfs(fd, &sfs) == 0 &&
      sfs.f_type == TMPFS_MAGIC && read(fd, cur, sizeof(cur) - 1) >= 0 &&
      strcmp(cur, stale) == 0 && pwrite(fd, want, len, 0) == (ssize_t)len)
    ds_log("[NET] Resolver order: IPv%d first", v6_first ? 6 : 4);
  close(fd);
}
