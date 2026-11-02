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

/* Macvlan mode
 *
 * The container's eth0 is a macvlan on a host NIC: its own MAC on that NIC's
 * LAN, its address from the LAN's DHCP server, no NAT, and nothing on the host
 * to install or keep in sync. It is born inside the container as ds-m<pid> and
 * renamed to eth0 there (see ds_nl_create_macvlan for why it cannot be born
 * as eth0). At start that happens before the guest's init runs, so the guest
 * only ever sees eth0.
 *
 * Unplugging the parent makes the kernel delete every macvlan on it, and the
 * container cannot bring its eth0 back by itself. So the monitor calls this on
 * every heartbeat as well as at start, and a replugged NIC gets its eth0 back
 * within a couple of seconds. eth0 already being there is the common case and
 * the cheap one. */

/* Create eth0 in the container once. 1 when it is in place, 0 while the
 * parent is absent, or a negative errno. */
static int macvlan_create(struct ds_config *cfg, const char *netns,
                          pid_t init_pid) {
  if (netns_has_link(netns, "eth0"))
    return 1;
  int parent = (int)if_nametoindex(cfg->macvlan_parent);
  if (parent <= 0)
    return 0;

  uint8_t mac[6];
  char tmp[IFNAMSIZ];
  ds_derive_mac(cfg->container_name, "ds-mac:", mac);
  snprintf(tmp, sizeof(tmp), "ds-m%d", (int)init_pid);

  int fd = open_container_netns(netns);
  ds_nl_ctx_t *ctx = fd >= 0 ? ds_nl_open() : NULL;
  int ret = ctx ? ds_nl_create_macvlan(ctx, tmp, parent, fd,
                                       cfg->macvlan_mode ? cfg->macvlan_mode
                                                         : MACVLAN_MODE_BRIDGE,
                                       mac)
                : -errno;
  if (ctx)
    ds_nl_close(ctx);
  if (fd >= 0)
    close(fd);
  if (ret == 0)
    ret = ds_netns_link_up(netns, tmp, "eth0");
  return ret == 0 ? 1 : ret;
}

/* The running container, other than cfg's own, that holds cfg's parent in a
 * way the kernel will not share: passthru takes the whole NIC, so it cannot
 * sit next to any other macvlan. Fills `who` and returns 1, or returns 0. */
static int macvlan_parent_taken(const struct ds_config *cfg, char *who,
                                size_t sz) {
  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s/Containers", get_workspace_dir());
  DIR *d = opendir(dir);
  if (!d)
    return 0;

  int taken = 0;
  struct dirent *ent;
  while (!taken && (ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;
    struct ds_config c = {0};
    if (ds_config_peek(ent->d_name, &c) != 0)
      continue;
    pid_t p = 0;
    if (c.net_mode == DS_NET_MACVLAN &&
        strcmp(c.container_name, cfg->container_name) != 0 &&
        strcmp(c.macvlan_parent, cfg->macvlan_parent) == 0 &&
        (c.macvlan_mode == MACVLAN_MODE_PASSTHRU ||
         cfg->macvlan_mode == MACVLAN_MODE_PASSTHRU) &&
        is_container_running(&c, &p) && p > 0) {
      safe_strncpy(who, c.container_name, sz);
      taken = 1;
    }
    ds_config_free(&c);
  }
  closedir(d);
  return taken;
}

/* Everything that would leave a macvlan container without a network for good,
 * checked before it starts so the user gets a reason instead of a container
 * with no eth0. Start and restart both come through here, after a restart has
 * reloaded the config it will actually boot with. A parent that is merely
 * unplugged is fine: eth0 follows when it is plugged in. Returns 0 or -1. */
int ds_net_macvlan_check(const struct ds_config *cfg) {
  if (!cfg->macvlan_parent[0]) {
    ds_error("--net=macvlan requires --macvlan-parent=<interface>.");
    return -1;
  }
  if (!ds_nl_probe_macvlan()) {
    ds_error("--net=macvlan is not supported by this kernel (CONFIG_MACVLAN).");
    return -1;
  }

  char path[PATH_MAX], type[8] = "";
  snprintf(path, sizeof(path), "/sys/class/net/%s/type", cfg->macvlan_parent);
  if (read_file(path, type, sizeof(type)) > 0 &&
      (atoi(type) != 1 || strcmp(cfg->macvlan_parent, "lo") == 0)) {
    ds_error("%s is not an Ethernet interface, so it cannot carry a macvlan.",
             cfg->macvlan_parent);
    return -1;
  }

  char who[256];
  if (macvlan_parent_taken(cfg, who, sizeof(who))) {
    ds_error("%s is in use by the running container '%s': in passthru mode a "
             "network interface takes one container only.",
             cfg->macvlan_parent, who);
    return -1;
  }
  return 0;
}

/* at_start: the guest's init is still waiting for us. A restart can get here
 * while the previous instance's namespace is still being torn down, which the
 * kernel does in the background, and its eth0 still holds the parent: passthru
 * allows one macvlan per NIC (EINVAL), and the stable MAC can only be on it
 * once (EADDRINUSE). Waiting it out here, rather than leaving it to the
 * heartbeat, keeps eth0 from turning up under a guest that is already running.
 * The other ways a macvlan cannot be made were refused before the start, see
 * ds_net_macvlan_check().
 *
 * Logs only when the result changes, since it is called every 2 s. The memory
 * starts over with each boot, so a guest reboot logs its eth0 again. */
int ds_net_macvlan_wire(struct ds_config *cfg, pid_t init_pid, int at_start) {
  static int last = 2; /* none of the results below */
  char netns[PATH_MAX];

  if (at_start)
    last = 2;
  snprintf(netns, sizeof(netns), "/proc/%d/ns/net", (int)init_pid);
  /* A heartbeat between init exiting and the monitor noticing finds no
   * namespace. That is a shutdown, not a failure worth a warning. */
  if (init_pid <= 0 || access(netns, F_OK) != 0)
    return -ESRCH;

  int ret = macvlan_create(cfg, netns, init_pid);
  for (int i = 0; at_start && (ret == -EINVAL || ret == -EADDRINUSE) && i < 50;
       i++) {
    usleep(100000);
    ret = macvlan_create(cfg, netns, init_pid);
  }

  if (ret != last) {
    if (ret == 1)
      ds_log("[NET] Macvlan: eth0 on %s (%s mode)", cfg->macvlan_parent,
             ds_macvlan_mode_name(cfg->macvlan_mode));
    else if (ret == 0)
      ds_warn("[NET] Macvlan: %s is not present - eth0 comes up once it is",
              cfg->macvlan_parent);
    else
      ds_warn("[NET] Macvlan: could not create eth0 on %s: %s%s",
              cfg->macvlan_parent, strerror(-ret),
              ret == -EINVAL ? " (another container may hold it in passthru "
                               "mode)"
                             : "");
  }
  return last = ret;
}
