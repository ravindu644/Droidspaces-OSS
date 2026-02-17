/*
 * Droidspaces v3 — Networking setup
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Host-side networking setup (before container boot)
 * ---------------------------------------------------------------------------*/

int ds_get_dns_servers(char *dns1, char *dns2, size_t size) {
  dns1[0] = dns2[0] = '\0';

  /* 1. Try Android properties if on Android */
  if (is_android()) {
    android_fill_dns_from_props(dns1, dns2, size);
  }

  /* 2. Global stable fallbacks (Preferred over host resolv.conf which might be
   * local loops) */
  if (!dns1[0])
    safe_strncpy(dns1, "1.1.1.1", size);
  if (!dns2[0])
    safe_strncpy(dns2, "8.8.8.8", size);

  return 0;
}

int fix_networking_host(struct ds_config *cfg) {
  ds_log("Configuring host-side networking for %s...", cfg->container_name);

  /* Enable IPv4 forwarding */
  write_file("/proc/sys/net/ipv4/ip_forward", "1");

  /* Enable IPv6 forwarding if requested */
  if (cfg->enable_ipv6) {
    write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1");
  }

  /* Get DNS (Android props -> Host /etc/resolv.conf -> Google/Cloudflare) */
  char dns1[64] = {0}, dns2[64] = {0};
  ds_get_dns_servers(dns1, dns2, sizeof(dns1));

  /* Save DNS to temp file in rootfs for use after pivot_root */
  char dns_path[PATH_MAX];
  snprintf(dns_path, sizeof(dns_path), "%s/.dns_servers", cfg->rootfs_path);
  FILE *dns_fp = fopen(dns_path, "w");
  if (dns_fp) {
    if (dns2[0])
      fprintf(dns_fp, "nameserver %s\nnameserver %s\n", dns1, dns2);
    else
      fprintf(dns_fp, "nameserver %s\n", dns1);
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
    char hn_buf[128];
    snprintf(hn_buf, sizeof(hn_buf), "%s\n", cfg->hostname);
    write_file("/etc/hostname", hn_buf);
  }

  /* 2. /etc/hosts */
  char hosts_content[1024];
  snprintf(hosts_content, sizeof(hosts_content),
           "127.0.0.1\tlocalhost\n"
           "::1\t\tlocalhost ip6-localhost ip6-loopback\n"
           "127.0.1.1\t%s\n",
           cfg->hostname);
  write_file("/etc/hosts", hosts_content);

  /* 3. resolv.conf (Android DNS from host via .dns_servers) */
  mkdir("/run/resolvconf", 0755);
  FILE *dns_fp = fopen("/.old_root/.dns_servers", "r");
  if (dns_fp) {
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf), dns_fp);
    if (n > 0) {
      write_file("/run/resolvconf/resolv.conf", buf);
    }
    fclose(dns_fp);
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
