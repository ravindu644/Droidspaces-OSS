/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Host-side networking setup (before container boot)
 * ---------------------------------------------------------------------------*/

int ds_get_dns_servers(const char *custom_dns, char *out, size_t size) {
  out[0] = '\0';
  int count = 0;

  /* 0. Try custom DNS if provided */
  if (custom_dns && custom_dns[0]) {
    char buf[256];
    safe_strncpy(buf, custom_dns, sizeof(buf));
    char *saveptr;
    char *token = strtok_r(buf, ", ", &saveptr);
    while (token && (size_t)strlen(out) < size - 64) {
      char line[128];
      snprintf(line, sizeof(line), "nameserver %s\n", token);
      strcat(out, line);
      count++;
      token = strtok_r(NULL, ", ", &saveptr);
    }
  }

  /* 1. Try Android properties if on Android (if still empty) */
  if (count == 0 && is_android()) {
    char dns1[64] = {0}, dns2[64] = {0};
    android_fill_dns_from_props(dns1, dns2, sizeof(dns1));
    if (dns1[0]) {
      strcat(out, "nameserver ");
      strcat(out, dns1);
      strcat(out, "\n");
      count++;
    }
    if (dns2[0]) {
      strcat(out, "nameserver ");
      strcat(out, dns2);
      strcat(out, "\n");
      count++;
    }
  }

  /* 2. Global stable fallbacks */
  if (count == 0) {
    strcat(out, "nameserver 1.1.1.1\nnameserver 8.8.8.8\n");
    count = 2;
  }

  return count;
}

int fix_networking_host(struct ds_config *cfg) {
  ds_log("Configuring host-side networking for %s...", cfg->container_name);

  /* Enable IPv4 forwarding */
  write_file("/proc/sys/net/ipv4/ip_forward", "1");

  /* IPv6: default disabled unless explicitly enabled via --enable-ipv6 */
  if (cfg->enable_ipv6) {
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "0");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "0");
    write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1");
  } else {
    /* If IPv6 is not available, these writes might fail, which is fine */
    write_file("/proc/sys/net/ipv6/conf/all/disable_ipv6", "1");
    write_file("/proc/sys/net/ipv6/conf/default/disable_ipv6", "1");
  }

  /* Get DNS (Custom -> Android props -> Google/Cloudflare) */
  char dns_buf[1024] = {0};
  int count = ds_get_dns_servers(cfg->dns_servers, dns_buf, sizeof(dns_buf));

  if (cfg->dns_servers[0])
    ds_log("Setting up %d custom DNS servers...", count);
  else
    ds_log("Setting up %d default DNS servers...", count);

  /* Save DNS to temp file in rootfs for use after pivot_root */
  char dns_path[PATH_MAX];
  snprintf(dns_path, sizeof(dns_path), "%.4080s/.dns_servers",
           cfg->rootfs_path);
  FILE *dns_fp = fopen(dns_path, "w");
  if (dns_fp) {
    fputs(dns_buf, dns_fp);
    fclose(dns_fp);
  }

  if (is_android()) {
    /* Android specific NAT and firewall */
    android_configure_iptables();
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Rootfs-side networking setup (inside container, after pivot_root)
 * ---------------------------------------------------------------------------*/

int fix_networking_rootfs(struct ds_config *cfg) {
  /* 1. Hostname */
  if (cfg->hostname[0]) {
    if (sethostname(cfg->hostname, strlen(cfg->hostname)) < 0) {
      ds_warn("Failed to set hostname to %s: %s", cfg->hostname,
              strerror(errno));
    }
    /* Persist to /etc/hostname */
    char hn_buf[256 + 2];
    snprintf(hn_buf, sizeof(hn_buf), "%.256s\n", cfg->hostname);
    write_file("/etc/hostname", hn_buf);
  }

  /* 2. /etc/hosts */
  char hosts_content[1024];
  const char *hostname = (cfg->hostname[0]) ? cfg->hostname : "localhost";
  snprintf(hosts_content, sizeof(hosts_content),
           "127.0.0.1\tlocalhost\n"
           "127.0.1.1\t%s\n"
           "::1\t\tlocalhost ip6-localhost ip6-loopback\n"
           "ff02::1\t\tip6-allnodes\n"
           "ff02::2\t\tip6-allrouters\n",
           hostname);
  write_file("/etc/hosts", hosts_content);

  /* 3. resolv.conf (Android DNS from host via .dns_servers) */
  mkdir("/run/resolvconf", 0755);
  FILE *dns_fp = fopen("/.dns_servers", "r");
  if (dns_fp) {
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, dns_fp);
    fclose(dns_fp);
    unlink("/.dns_servers"); /* Cleanup the temporary marker */
    if (n > 0) {
      buf[n] = '\0'; /* Ensure null-termination */
      write_file("/run/resolvconf/resolv.conf", buf);
    }
  } else {
    /* Fallback/Linux default */
    write_file("/run/resolvconf/resolv.conf",
               "nameserver 1.1.1.1\nnameserver 8.8.8.8\n");
  }

  /* Link /etc/resolv.conf */
  unlink("/etc/resolv.conf");
  symlink("/run/resolvconf/resolv.conf", "/etc/resolv.conf");

  /* 4. Android Network Groups */
  if (is_android()) {
    /* If /etc/group exists, ensure aid_inet and other groups are present
     * so the user can actually use the network. */
    const char *etc_group = "/etc/group";
    if (access(etc_group, F_OK) == 0) {
      if (!grep_file(etc_group, "aid_inet")) {
        FILE *fg = fopen(etc_group, "a");
        if (fg) {
          fprintf(
              fg,
              "aid_inet:x:3003:\naid_net_raw:x:3004:\naid_net_admin:x:3005:\n");
          fclose(fg);
        }
      }
    }

    /* Add root to groups if usermod exists */
    if (access("/usr/sbin/usermod", X_OK) == 0 ||
        access("/sbin/usermod", X_OK) == 0) {
      /* Performance skip: check if root is already in aid_inet */
      if (!grep_file("/etc/group", "aid_inet:x:3003:root") &&
          !grep_file("/etc/group", "aid_inet:*:3003:root")) {
        char *args[] = {"usermod", "-a", "-G", "aid_inet,aid_net_raw",
                        "root",    NULL};
        run_command_quiet(args);
      }
    }
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Runtime introspection
 * ---------------------------------------------------------------------------*/

int detect_ipv6_in_container(pid_t pid) {
  char path[PATH_MAX];
  build_proc_root_path(pid, "/proc/sys/net/ipv6/conf/all/disable_ipv6", path,
                       sizeof(path));

  char buf[16];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;

  /* 0 means enabled, 1 means disabled */
  return (buf[0] == '0') ? 1 : 0;
}
