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

/* Static NAT IP: validation, collision check, and resolution */

int ds_net_validate_static_ip(const char *ip_str, char *errbuf,
                              size_t errsize) {
  if (!ip_str || !ip_str[0]) {
    snprintf(errbuf, errsize, "empty IP string");
    return -1;
  }

  /* Reject CIDR notation - we store plain dotted-decimal */
  if (strchr(ip_str, '/')) {
    snprintf(errbuf, errsize,
             "pass plain IP without prefix length "
             "(e.g. 172.28.5.10, not 172.28.5.10/16)");
    return -1;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, ip_str, &addr) != 1) {
    snprintf(errbuf, errsize, "not a valid IPv4 address");
    return -1;
  }

  int o1, o2, o3, o4;
  if (sscanf(ip_str, "%d.%d.%d.%d", &o1, &o2, &o3, &o4) != 4) {
    snprintf(errbuf, errsize, "malformed IPv4 address");
    return -1;
  }

  /* Must be inside 172.28.0.0/16 */
  if (o1 != 172 || o2 != 28) {
    snprintf(errbuf, errsize,
             "must be inside the NAT subnet " DS_DEFAULT_SUBNET " (got %s)",
             ip_str);
    return -1;
  }

  /* Octet3: reserve row 0 entirely for gateway/infrastructure (172.28.0.x) */
  if (o3 < 1 || o3 > 254) {
    snprintf(errbuf, errsize,
             "third octet %d out of range - must be 1-254 "
             "(172.28.0.x is reserved for the gateway)",
             o3);
    return -1;
  }

  /* Octet4: exclude network address (0) and broadcast (255) */
  if (o4 < 1 || o4 > 254) {
    snprintf(errbuf, errsize, "fourth octet %d out of range - must be 1-254",
             o4);
    return -1;
  }

  return 0;
}

int ds_net_check_ip_collision(const char *ip_str, const char *exclude_name) {
  char containers_dir[PATH_MAX];
  snprintf(containers_dir, sizeof(containers_dir), "%s/Containers",
           get_workspace_dir());

  /* The directory entries are sanitized names (written by
   * ds_config_save_by_name via sanitize_container_name). Sanitize exclude_name
   * the same way so the self-skip comparison is always apples-to-apples. */
  char safe_exclude[256] = {0};
  if (exclude_name && exclude_name[0])
    sanitize_container_name(exclude_name, safe_exclude, sizeof(safe_exclude));

  DIR *d = opendir(containers_dir);
  if (!d)
    return 0; /* Can't scan → assume unique */

  struct dirent *ent;
  int collision = 0;

  while ((ent = readdir(d)) != NULL && !collision) {
    if (ent->d_name[0] == '.')
      continue;
    /* Skip the container being configured so it can keep its own IP on restart
     */
    if (safe_exclude[0] && strcmp(ent->d_name, safe_exclude) == 0)
      continue;

    /* config_path must hold: containers_dir (PATH_MAX-1) + '/' +
     * ent->d_name (NAME_MAX = 255) + "/container.config" (18) + NUL.
     * A plain PATH_MAX buffer overflows that worst case - use an explicit
     * worst-case size so -Werror=format-truncation is satisfied. */
    char config_path[PATH_MAX + NAME_MAX + 32];
    snprintf(config_path, sizeof(config_path), "%s/%s/container.config",
             containers_dir, ent->d_name);

    struct ds_config other;
    ds_config_init(&other);

    /* By path: this runs inside the auto-assign loop, and a by-name load
     * costs a /proc scan per directory. */
    if (ds_config_peek_path(config_path, &other) == 0) {
      if (other.static_nat_ip[0] && strcmp(other.static_nat_ip, ip_str) == 0)
        collision = 1;
      ds_config_free(&other);
    }
  }

  closedir(d);
  return collision;
}

/* Internal: derive a unique IP via djb2(container_name), walking forward on
 * collision.  Deterministic on first boot → same row every time the same
 * container name is used, spreading containers across the /16. */
static void ds_net_auto_assign_ip(struct ds_config *cfg) {
  uint32_t hash = ds_net_hash_string(cfg->container_name);

  int o3 = (int)((hash >> 8) % 254) + 1; /* 1-254 */
  int o4 = (int)(hash % 254) + 1;        /* 1-254 */

  /* Walk up to a full /16 to find an unoccupied slot */
  for (int attempts = 0; attempts < 254 * 254; attempts++) {
    char candidate[32];
    snprintf(candidate, sizeof(candidate), "172.28.%d.%d", o3, o4);

    if (ds_net_check_ip_collision(candidate, cfg->container_name) == 0) {
      safe_strncpy(cfg->static_nat_ip, candidate, sizeof(cfg->static_nat_ip));
      return;
    }

    if (++o4 > 254) {
      o4 = 1;
      if (++o3 > 254)
        o3 = 1;
    }
  }

  /* Subnet is completely exhausted - reuse hash address as last resort */
  char fallback[32];
  snprintf(fallback, sizeof(fallback), "172.28.%d.%d", o3, o4);
  safe_strncpy(cfg->static_nat_ip, fallback, sizeof(cfg->static_nat_ip));
  ds_warn("[NET] NAT subnet appears fully allocated - reusing %s as fallback",
          cfg->static_nat_ip);
}

void ds_net_resolve_static_ip(struct ds_config *cfg) {
  char errbuf[256];

  if (cfg->static_nat_ip[0]) {
    /* IP already set - from --nat-ip flag or loaded from a previous boot.
     * Re-validate in case the config was hand-edited since last boot. */
    if (ds_net_validate_static_ip(cfg->static_nat_ip, errbuf, sizeof(errbuf)) !=
        0) {
      ds_warn("[NET] static_nat_ip '%s' failed validation: %s "
              "- auto-assigning a new IP",
              cfg->static_nat_ip, errbuf);
      cfg->static_nat_ip[0] = '\0';

    } else if (ds_net_check_ip_collision(cfg->static_nat_ip,
                                         cfg->container_name)) {
      ds_warn("[NET] static_nat_ip '%s' is already assigned to another "
              "container - auto-assigning a new IP",
              cfg->static_nat_ip);
      cfg->static_nat_ip[0] = '\0';
    }
  }

  if (!cfg->static_nat_ip[0])
    ds_net_auto_assign_ip(cfg);

  ds_log("[NET] Container '%s' → static NAT IP: %s (persisted to config)",
         cfg->container_name, cfg->static_nat_ip);
}
