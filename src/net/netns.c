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

#include "internal.h"

/* Open a container's network namespace for the host side to plug cables
 * into or act in. Fails closed when the namespace turns out to be our own: a
 * pid that was recycled, or an init that died before its monitor noticed,
 * would otherwise have the host create an "eth0" in the host's own namespace,
 * or wire and query itself. */
int open_container_netns(const char *netns_path) {
  int fd = open(netns_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat target, self;
  if (fstat(fd, &target) == 0 && stat("/proc/self/ns/net", &self) == 0 &&
      target.st_ino == self.st_ino && target.st_dev == self.st_dev) {
    ds_warn("[NET] %s is the host's own network namespace - refusing to use it",
            netns_path);
    close(fd);
    errno = EPERM;
    return -1;
  }
  return fd;
}

/* Bring a link up inside a container's network namespace, from the host.
 * With `from` set, the link is first renamed from that name to ifname, and
 * deleted if the rename or the bring-up fails, so a half-made one is never
 * left behind. Returns 0 or a negative errno. */
int ds_netns_link_up(const char *netns_path, const char *from,
                     const char *ifname) {
  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return -errno;

  int target_fd = open_container_netns(netns_path);
  if (target_fd < 0) {
    int e = -errno;
    close(self_fd);
    return e;
  }

  int ret = 0;
  if (setns(target_fd, CLONE_NEWNET) < 0) {
    ret = -errno;
    goto out_restore;
  }

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ret = -errno;
    goto out_restore;
  }
  if (from && (ret = ds_nl_rename(ctx, from, ifname)) < 0)
    ds_nl_del_link(ctx, from);
  else if ((ret = ds_nl_link_up(ctx, ifname)) < 0 && from)
    ds_nl_del_link(ctx, ifname);
  ds_nl_close(ctx);

out_restore:
  if (setns(self_fd, CLONE_NEWNET) < 0 && ret == 0)
    ret = -errno;
  close(target_fd);
  close(self_fd);
  return ret;
}

/* Return 1 if interface `ifname` exists inside the netns at `netns_path`, else
 * 0.  Used to tell a healthy gateway cable (its peer really lives inside the
 * CURRENT gateway netns) apart from a stale host-side veth whose peer is
 * stranded in a zombie netns - one kept alive past container stop by a leftover
 * process (e.g. tailscaled).  On any error it returns 0, i.e. "not present", so
 * the caller rebuilds the cable: the safe default. */
int netns_has_link(const char *netns_path, const char *ifname) {
  if (!netns_path || !ifname || !ifname[0])
    return 0;

  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return 0;
  int target_fd = open_container_netns(netns_path);
  if (target_fd < 0) {
    close(self_fd);
    return 0;
  }

  int present = 0;
  if (setns(target_fd, CLONE_NEWNET) == 0) {
    ds_nl_ctx_t *ctx = ds_nl_open();
    if (ctx) {
      present = ds_nl_link_exists(ctx, ifname) ? 1 : 0;
      ds_nl_close(ctx);
    }
    if (setns(self_fd, CLONE_NEWNET) < 0)
      ds_warn("[NET] Gateway: failed to restore netns after liveness check: %s",
              strerror(errno));
  }

  close(target_fd);
  close(self_fd);
  return present;
}

/* A veth hands each packet to netif_rx() on the CPU that sent it, and TCP
 * sends from whichever CPU took the last ACK, so one flow lands on several
 * CPUs' backlogs and they drain out of order. The receiver reads that as loss:
 * a download to a Wi-Fi client ran at a third of the link with thousands of
 * retransmits and nothing dropped. RPS hashes each flow to one CPU instead.
 * The mask is every CPU but CPU0, which takes most interrupts, as Pixel does
 * for its modem. A kernel without CONFIG_RPS has no rps_cpus, so a failed
 * write is the probe. `sys` is where sysfs is mounted for the caller.
 * ponytail: the kernel keeps only the CPUs online at the moment of the write,
 * for the life of the device, so a phone that has hotplugged its big cores off
 * steers that container's flows to the little ones until it restarts, and with
 * only CPU0 online RPS stays off. Re-apply on CPU hotplug if that shows. */
void ds_net_steer_rx(const char *sys, const char *ifname) {
  char path[PATH_MAX], buf[64], mask[300];

  snprintf(path, sizeof(path), "%s/devices/system/cpu/present", sys);
  if (read_file(path, buf, sizeof(buf)) < 0)
    return;
  /* "0-7" or "0,2-5": the highest CPU id is the last number */
  char *p = buf + strlen(buf);
  while (p > buf && !isdigit((unsigned char)p[-1]))
    p--;
  while (p > buf && isdigit((unsigned char)p[-1]))
    p--;
  int max = atoi(p);
  if (max < 1)
    return; /* one CPU: nothing to steer to */
  if (max > 1023)
    max = 1023;

  /* The kernel parses the mask in 32-bit words, most significant first */
  size_t off = 0;
  for (int w = max / 32; w >= 0; w--) {
    uint32_t bits = 0;
    for (int b = 0; b < 32; b++) {
      int cpu = w * 32 + b;
      if (cpu >= 1 && cpu <= max)
        bits |= 1u << b;
    }
    off += (size_t)snprintf(mask + off, sizeof(mask) - off,
                            w == max / 32 ? "%x" : ",%08x", bits);
  }

  snprintf(path, sizeof(path), "%s/class/net/%s/queues/rx-0/rps_cpus", sys,
           ifname);
  write_file(path, mask);
}

/* Every host-side veth: TX checksum off (Samsung/MTK kernel workaround), and
 * RPS so a container's flows leave it in order. */
void ds_net_prepare_host_veth(const char *ifname) {
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd >= 0) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    safe_strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

    struct ethtool_value eval;
    eval.cmd = ETHTOOL_STXCSUM;
    eval.data = 0; /* Disable */
    ifr.ifr_data = (caddr_t)&eval;

    ioctl(fd, SIOCETHTOOL, &ifr);
    close(fd);
  }
  ds_net_steer_rx("/sys", ifname);
}
