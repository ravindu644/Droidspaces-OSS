/*
 * Direct-L2 host access for ipvlan/macvlan containers.
 *
 * PTP gives every container a private veth /30 and routes the host/container
 * primary IPv4 addresses over it.  SHIM creates one shared ipvlan/macvlan
 * child per parent+kind, borrows the parent's IPv4 as /32, and installs a host
 * route for every live container address.
 */
#include "droidspace.h"
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/file.h>

#define DS_HA_PTP_POOL "169.254.240.0"
#define DS_HA_PTP_POOL_PREFIX 20
#define DS_HA_PTP_LINK_PREFIX 30
#define DS_HA_RULE_PRIORITY 6080
#define DS_HA_GUEST_IF "dshost0"

struct ha_state {
  char mode[8];
  char shim[IFNAMSIZ];
  char parent[IFNAMSIZ];
  char guest_ip[INET_ADDRSTRLEN];
  char host_ip[INET_ADDRSTRLEN];
  pid_t pid;
};

static uint32_t ha_hash(const char *s) {
  uint32_t h = 5381;
  if (!s)
    return h;
  while (*s)
    h = ((h << 5) + h) ^ (unsigned char)*s++;
  return h;
}

static const char *ha_key(const struct ds_config *cfg) {
  return cfg->uuid[0] ? cfg->uuid : cfg->container_name;
}

static void ha_ptp_names(const struct ds_config *cfg, char host[IFNAMSIZ],
                         char peer[IFNAMSIZ]) {
  uint32_t h = ha_hash(ha_key(cfg));
  snprintf(host, IFNAMSIZ, "ds-pt%08x", h);
  snprintf(peer, IFNAMSIZ, "ds-pg%08x", h);
}

static void ha_shim_name(const struct ds_config *cfg, char shim[IFNAMSIZ]) {
  char key[64];
  snprintf(key, sizeof(key), "%s:%s",
           cfg->net_mode == DS_NET_IPVLAN ? "ipvlan" : "macvlan",
           cfg->net_parent);
  snprintf(shim, IFNAMSIZ, "ds-sh%08x", ha_hash(key));
}

static void ha_state_path(const struct ds_config *cfg, char *path, size_t size) {
  char safe[256];
  sanitize_container_name(ha_key(cfg), safe, sizeof(safe));
  snprintf(path, size, "%.3800s/ha_%.200s.state", get_net_dir(), safe);
}

static void ha_stop_path(const struct ds_config *cfg, char *path, size_t size) {
  char safe[256];
  sanitize_container_name(ha_key(cfg), safe, sizeof(safe));
  snprintf(path, size, "%.3800s/ha_%.200s.stop", get_net_dir(), safe);
}

static int ha_is_stopping(const struct ds_config *cfg) {
  char path[PATH_MAX];
  ha_stop_path(cfg, path, sizeof(path));
  return access(path, F_OK) == 0;
}

static int ha_lock(const char *key) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%.3800s/ha_%.32s.lock", get_net_dir(), key);
  int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0)
    return -1;
  if (flock(fd, LOCK_EX) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void ha_unlock(int fd) {
  if (fd >= 0) {
    (void)flock(fd, LOCK_UN);
    close(fd);
  }
}

static void ha_write_state(const struct ds_config *cfg,
                           const struct ha_state *state) {
  char path[PATH_MAX];
  char content[512];
  ha_state_path(cfg, path, sizeof(path));
  snprintf(content, sizeof(content),
           "mode=%s\npid=%d\nshim=%s\nparent=%s\nguest_ip=%s\nhost_ip=%s\n",
           state->mode, (int)state->pid, state->shim, state->parent,
           state->guest_ip, state->host_ip);
  if (write_file_atomic(path, content) < 0)
    ds_warn("[NET] Host access: failed to save runtime state: %s",
            strerror(errno));
}

static int ha_read_state_path(const char *path, struct ha_state *state) {
  FILE *f = fopen(path, "re");
  if (!f)
    return -1;
  memset(state, 0, sizeof(*state));
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    char *nl = strpbrk(line, "\r\n");
    if (nl)
      *nl = '\0';
    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq++ = '\0';
    if (strcmp(line, "mode") == 0)
      safe_strncpy(state->mode, eq, sizeof(state->mode));
    else if (strcmp(line, "pid") == 0)
      state->pid = (pid_t)strtol(eq, NULL, 10);
    else if (strcmp(line, "shim") == 0)
      safe_strncpy(state->shim, eq, sizeof(state->shim));
    else if (strcmp(line, "parent") == 0)
      safe_strncpy(state->parent, eq, sizeof(state->parent));
    else if (strcmp(line, "guest_ip") == 0)
      safe_strncpy(state->guest_ip, eq, sizeof(state->guest_ip));
    else if (strcmp(line, "host_ip") == 0)
      safe_strncpy(state->host_ip, eq, sizeof(state->host_ip));
  }
  fclose(f);
  return 0;
}

static int ha_read_state(const struct ds_config *cfg, struct ha_state *state) {
  char path[PATH_MAX];
  ha_state_path(cfg, path, sizeof(path));
  return ha_read_state_path(path, state);
}

static int ha_state_equal(const struct ha_state *a,
                          const struct ha_state *b) {
  return strcmp(a->mode, b->mode) == 0 && strcmp(a->shim, b->shim) == 0 &&
         strcmp(a->parent, b->parent) == 0 &&
         strcmp(a->guest_ip, b->guest_ip) == 0 &&
         strcmp(a->host_ip, b->host_ip) == 0 && a->pid == b->pid;
}

static int ha_write_disable_ipv6(const char *ifname) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/disable_ipv6",
           ifname);
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return errno == ENOENT ? 0 : -errno;
  ssize_t n = write(fd, "1\n", 2);
  int saved = errno;
  close(fd);
  return n == 2 ? 0 : -saved;
}

static int ha_parse_ptp_cidr(const char *cidr, uint32_t *network_be) {
  if (!cidr || !cidr[0])
    return -EINVAL;
  char copy[32];
  safe_strncpy(copy, cidr, sizeof(copy));
  char *slash = strchr(copy, '/');
  if (!slash || strcmp(slash, "/30") != 0)
    return -EINVAL;
  *slash = '\0';
  struct in_addr addr;
  struct in_addr pool;
  if (inet_pton(AF_INET, copy, &addr) != 1 ||
      inet_pton(AF_INET, DS_HA_PTP_POOL, &pool) != 1)
    return -EINVAL;
  uint32_t host = ntohl(addr.s_addr);
  uint32_t pool_host = ntohl(pool.s_addr);
  uint32_t pool_mask = 0xffffffffu << (32 - DS_HA_PTP_POOL_PREFIX);
  if ((host & pool_mask) != (pool_host & pool_mask) || (host & 3u) != 0)
    return -EINVAL;
  if (network_be)
    *network_be = addr.s_addr;
  return 0;
}

static int ha_ptp_collision(const char *cidr, const char *exclude_name) {
  char dir_path[PATH_MAX];
  snprintf(dir_path, sizeof(dir_path), "%s/Containers", get_workspace_dir());
  DIR *dir = opendir(dir_path);
  if (!dir)
    return 0;
  char safe_exclude[256] = {0};
  if (exclude_name && exclude_name[0])
    sanitize_container_name(exclude_name, safe_exclude, sizeof(safe_exclude));
  int collision = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && !collision) {
    if (ent->d_name[0] == '.' ||
        (safe_exclude[0] && strcmp(ent->d_name, safe_exclude) == 0))
      continue;
    char config_path[PATH_MAX + NAME_MAX + 32];
    snprintf(config_path, sizeof(config_path), "%s/%s/container.config",
             dir_path, ent->d_name);
    struct ds_config other = {0};
    other.net_ready_pipe[0] = other.net_ready_pipe[1] = -1;
    other.net_done_pipe[0] = other.net_done_pipe[1] = -1;
    if (ds_config_load(config_path, &other) == 0) {
      if (other.host_access == DS_HOST_ACCESS_PTP &&
          strcmp(other.host_access_ptp_cidr, cidr) == 0)
        collision = 1;
      ds_config_free(&other);
    }
  }
  closedir(dir);
  return collision;
}

void ds_host_access_resolve_ptp(struct ds_config *cfg) {
  if (!cfg || cfg->host_access != DS_HOST_ACCESS_PTP)
    return;
  int lock = ha_lock("ptp-pool");
  if (ha_parse_ptp_cidr(cfg->host_access_ptp_cidr, NULL) == 0 &&
      !ha_ptp_collision(cfg->host_access_ptp_cidr, cfg->container_name)) {
    ha_unlock(lock);
    return;
  }

  struct in_addr pool;
  if (inet_pton(AF_INET, DS_HA_PTP_POOL, &pool) != 1) {
    ha_unlock(lock);
    return;
  }
  uint32_t base = ntohl(pool.s_addr);
  unsigned int slots = 1u << (DS_HA_PTP_LINK_PREFIX - DS_HA_PTP_POOL_PREFIX);
  unsigned int first = ha_hash(ha_key(cfg)) % slots;
  for (unsigned int n = 0; n < slots; n++) {
    struct in_addr candidate = {.s_addr = htonl(base + ((first + n) % slots) * 4u)};
    char ip[INET_ADDRSTRLEN];
    char cidr[32];
    if (!inet_ntop(AF_INET, &candidate, ip, sizeof(ip)))
      continue;
    snprintf(cidr, sizeof(cidr), "%s/30", ip);
    if (!ha_ptp_collision(cidr, cfg->container_name)) {
      safe_strncpy(cfg->host_access_ptp_cidr, cidr,
                   sizeof(cfg->host_access_ptp_cidr));
      ds_log("[NET] Host access PTP: reserved %s for %s", cidr,
             cfg->container_name);
      ha_unlock(lock);
      return;
    }
  }
  cfg->host_access_ptp_cidr[0] = '\0';
  ds_warn("[NET] Host access PTP: address pool is exhausted");
  ha_unlock(lock);
}

static int ha_configure_netns_ptp(pid_t pid, uint32_t guest_be,
                                  uint32_t host_be, uint32_t old_host_be,
                                  uint32_t gateway_be) {
  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return -errno;
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/ns/net", (int)pid);
  int target_fd = open(path, O_RDONLY | O_CLOEXEC);
  if (target_fd < 0) {
    int ret = -errno;
    close(self_fd);
    return ret;
  }
  int ret = 0;
  if (setns(target_fd, CLONE_NEWNET) < 0) {
    ret = -errno;
    goto restore;
  }
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ret = -errno;
    goto restore;
  }
  ret = ds_nl_add_addr4(ctx, DS_HA_GUEST_IF, guest_be,
                        DS_HA_PTP_LINK_PREFIX);
  if (ret == 0)
    ret = ds_nl_link_up(ctx, DS_HA_GUEST_IF);
  if (ret == 0)
    (void)ha_write_disable_ipv6(DS_HA_GUEST_IF);
  int guest_idx = ds_nl_get_ifindex(ctx, DS_HA_GUEST_IF);
  if (ret == 0 && guest_idx <= 0)
    ret = -ENODEV;
  if (ret == 0 && old_host_be && old_host_be != host_be)
    (void)ds_nl_del_route4(ctx, old_host_be, 32, gateway_be, guest_idx);
  if (ret == 0 && host_be)
    ret = ds_nl_add_route4(ctx, host_be, 32, gateway_be, guest_idx);
  ds_nl_close(ctx);
restore:
  if (setns(self_fd, CLONE_NEWNET) < 0 && ret == 0)
    ret = -errno;
  close(target_fd);
  close(self_fd);
  return ret;
}

static int ha_get_guest_ip(pid_t pid, uint32_t *ip_be) {
  int self_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
  if (self_fd < 0)
    return -errno;
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/ns/net", (int)pid);
  int target_fd = open(path, O_RDONLY | O_CLOEXEC);
  if (target_fd < 0) {
    int ret = -errno;
    close(self_fd);
    return ret;
  }
  int ret = 0;
  if (setns(target_fd, CLONE_NEWNET) < 0) {
    ret = -errno;
    goto restore;
  }
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx)
    ret = -errno;
  else {
    ret = ds_nl_get_addr4(ctx, "eth0", ip_be, NULL);
    ds_nl_close(ctx);
  }
restore:
  if (setns(self_fd, CLONE_NEWNET) < 0 && ret == 0)
    ret = -errno;
  close(target_fd);
  close(self_fd);
  return ret;
}

static int ha_setup_ptp(struct ds_config *cfg, pid_t pid,
                        int discover_guest) {
  uint32_t network_be;
  if (ha_parse_ptp_cidr(cfg->host_access_ptp_cidr, &network_be) < 0)
    return -EINVAL;
  uint32_t network = ntohl(network_be);
  uint32_t host_be = htonl(network + 1u);
  uint32_t guest_be = htonl(network + 2u);
  char host[IFNAMSIZ], peer[IFNAMSIZ];
  ha_ptp_names(cfg, host, peer);
  int lock = ha_lock(host);
  if (ha_is_stopping(cfg)) {
    ha_unlock(lock);
    return -ECANCELED;
  }
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ha_unlock(lock);
    return -errno;
  }

  struct ha_state old = {0};
  int have_old = ha_read_state(cfg, &old) == 0;
  int created = 0;
  int ret = 0;
  if (!ds_nl_link_exists(ctx, host)) {
    ret = ds_nl_create_veth(ctx, host, peer);
    if (ret < 0 && ret != -EEXIST)
      goto out;
    created = 1;
    char ns_path[64];
    snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/net", (int)pid);
    int ns_fd = open(ns_path, O_RDONLY | O_CLOEXEC);
    if (ns_fd < 0) {
      ret = -errno;
      goto out;
    }
    ret = ds_nl_move_to_netns_named(ctx, peer, ns_fd, DS_HA_GUEST_IF);
    close(ns_fd);
    if (ret < 0)
      goto out;
  }
  ret = ds_nl_add_addr4(ctx, host, host_be, DS_HA_PTP_LINK_PREFIX);
  if (ret == 0)
    ret = ds_nl_link_up(ctx, host);
  if (ret == 0)
    (void)ha_write_disable_ipv6(host);
  if (ret == 0)
    ret = ds_nl_add_rule4(ctx, 0, 0, inet_addr(DS_HA_PTP_POOL),
                          DS_HA_PTP_POOL_PREFIX, RT_TABLE_MAIN,
                          DS_HA_RULE_PRIORITY);

  uint32_t host_main_be = 0;
  (void)ds_nl_get_addr4(ctx, cfg->net_parent, &host_main_be, NULL);
  uint32_t old_host_main_be = 0;
  if (have_old && old.host_ip[0])
    (void)inet_pton(AF_INET, old.host_ip, &old_host_main_be);
  if (ret == 0)
    ret = ha_configure_netns_ptp(pid, guest_be, host_main_be,
                                 old_host_main_be, host_be);

  uint32_t guest_main_be = 0;
  if (cfg->net_ipam == DS_NET_IPAM_STATIC && cfg->net_address[0]) {
    char cidr[sizeof(cfg->net_address)];
    safe_strncpy(cidr, cfg->net_address, sizeof(cidr));
    char *slash = strchr(cidr, '/');
    if (slash)
      *slash = '\0';
    (void)inet_pton(AF_INET, cidr, &guest_main_be);
  } else if (discover_guest) {
    (void)ha_get_guest_ip(pid, &guest_main_be);
  }

  int host_idx = ds_nl_get_ifindex(ctx, host);
  if (ret == 0 && host_idx <= 0)
    ret = -ENODEV;
  char host_ip[INET_ADDRSTRLEN] = {0};
  char guest_ip[INET_ADDRSTRLEN] = {0};
  if (host_main_be) {
    struct in_addr addr = {.s_addr = host_main_be};
    (void)inet_ntop(AF_INET, &addr, host_ip, sizeof(host_ip));
  }
  if (guest_main_be) {
    struct in_addr addr = {.s_addr = guest_main_be};
    (void)inet_ntop(AF_INET, &addr, guest_ip, sizeof(guest_ip));
  }
  if (ret == 0 && have_old && old.guest_ip[0] &&
      strcmp(old.guest_ip, guest_ip) != 0) {
    struct in_addr old_guest;
    if (inet_pton(AF_INET, old.guest_ip, &old_guest) == 1) {
      (void)ds_nl_del_route4(ctx, old_guest.s_addr, 32, guest_be, host_idx);
      (void)ds_nl_del_rule4(ctx, 0, 0, old_guest.s_addr, 32,
                            RT_TABLE_MAIN, DS_HA_RULE_PRIORITY);
    }
  }
  if (ret == 0 && guest_main_be) {
    ret = ds_nl_add_route4(ctx, guest_main_be, 32, guest_be, host_idx);
    if (ret == 0)
      ret = ds_nl_add_rule4(ctx, 0, 0, guest_main_be, 32, RT_TABLE_MAIN,
                            DS_HA_RULE_PRIORITY);
  }
  if (ret == 0) {
    struct ha_state state = {0};
    safe_strncpy(state.mode, "ptp", sizeof(state.mode));
    safe_strncpy(state.shim, host, sizeof(state.shim));
    safe_strncpy(state.parent, cfg->net_parent, sizeof(state.parent));
    safe_strncpy(state.host_ip, host_ip, sizeof(state.host_ip));
    safe_strncpy(state.guest_ip, guest_ip, sizeof(state.guest_ip));
    state.pid = pid;
    if (!have_old || !ha_state_equal(&old, &state))
      ha_write_state(cfg, &state);
    if (!have_old || created || strcmp(old.guest_ip, guest_ip) != 0)
      ds_log("[NET] Host access PTP ready: host=%s guest=%s (%s), "
             "primary=%s<->%s",
             host, DS_HA_GUEST_IF, cfg->host_access_ptp_cidr,
             host_ip[0] ? host_ip : "unavailable",
             guest_ip[0] ? guest_ip : "waiting-for-DHCP");
  }
out:
  if (ret < 0)
    ds_nl_del_link(ctx, host);
  ds_nl_close(ctx);
  ha_unlock(lock);
  return ret;
}

static int ha_setup_shim(struct ds_config *cfg, pid_t pid, int discover_guest) {
  char shim[IFNAMSIZ];
  ha_shim_name(cfg, shim);
  int lock = ha_lock(shim);
  if (ha_is_stopping(cfg)) {
    ha_unlock(lock);
    return -ECANCELED;
  }
  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    ha_unlock(lock);
    return -errno;
  }

  uint32_t host_be = 0;
  int ret = ds_nl_get_addr4(ctx, cfg->net_parent, &host_be, NULL);
  if (ret < 0) {
    ds_nl_close(ctx);
    ha_unlock(lock);
    return ret;
  }

  struct ha_state old = {0};
  int have_old = ha_read_state(cfg, &old) == 0;
  if (!ds_nl_link_exists(ctx, shim)) {
    const char *kind =
        cfg->net_mode == DS_NET_IPVLAN ? "ipvlan" : "macvlan";
    ret = ds_nl_create_parent_link(ctx, cfg->net_parent, shim, kind);
    if (ret < 0 && ret != -EEXIST)
      goto out;
  }
  int shim_idx = ds_nl_get_ifindex(ctx, shim);
  if (shim_idx <= 0) {
    ret = -ENODEV;
    goto out;
  }
  ret = ds_nl_link_up(ctx, shim);
  if (ret < 0)
    goto out;
  (void)ha_write_disable_ipv6(shim);

  char host_ip[INET_ADDRSTRLEN] = {0};
  struct in_addr host_addr = {.s_addr = host_be};
  if (!inet_ntop(AF_INET, &host_addr, host_ip, sizeof(host_ip))) {
    ret = -errno;
    goto out;
  }
  if (have_old && old.host_ip[0] && strcmp(old.host_ip, host_ip) != 0) {
    struct in_addr old_host;
    if (inet_pton(AF_INET, old.host_ip, &old_host) == 1)
      (void)ds_nl_del_addr4(ctx, shim, old_host.s_addr, 32);
  }
  ret = ds_nl_add_addr4(ctx, shim, host_be, 32);
  if (ret < 0)
    goto out;

  uint32_t guest_be = 0;
  if (cfg->net_ipam == DS_NET_IPAM_STATIC && cfg->net_address[0]) {
    char cidr[sizeof(cfg->net_address)];
    safe_strncpy(cidr, cfg->net_address, sizeof(cidr));
    char *slash = strchr(cidr, '/');
    if (slash)
      *slash = '\0';
    (void)inet_pton(AF_INET, cidr, &guest_be);
  } else if (discover_guest) {
    (void)ha_get_guest_ip(pid, &guest_be);
  }

  char guest_ip[INET_ADDRSTRLEN] = {0};
  if (guest_be) {
    struct in_addr guest_addr = {.s_addr = guest_be};
    (void)inet_ntop(AF_INET, &guest_addr, guest_ip, sizeof(guest_ip));
  }
  if (have_old && old.guest_ip[0] && strcmp(old.guest_ip, guest_ip) != 0) {
    struct in_addr old_guest;
    if (inet_pton(AF_INET, old.guest_ip, &old_guest) == 1) {
      (void)ds_nl_del_route4(ctx, old_guest.s_addr, 32, 0, shim_idx);
      (void)ds_nl_del_rule4(ctx, 0, 0, old_guest.s_addr, 32, RT_TABLE_MAIN,
                            DS_HA_RULE_PRIORITY);
    }
  }
  if (guest_be) {
    ret = ds_nl_add_route4(ctx, guest_be, 32, 0, shim_idx);
    if (ret == 0)
      ret = ds_nl_add_rule4(ctx, 0, 0, guest_be, 32, RT_TABLE_MAIN,
                            DS_HA_RULE_PRIORITY);
    if (ret < 0)
      goto out;
  }

  struct ha_state state = {0};
  safe_strncpy(state.mode, "shim", sizeof(state.mode));
  safe_strncpy(state.shim, shim, sizeof(state.shim));
  safe_strncpy(state.parent, cfg->net_parent, sizeof(state.parent));
  safe_strncpy(state.host_ip, host_ip, sizeof(state.host_ip));
  safe_strncpy(state.guest_ip, guest_ip, sizeof(state.guest_ip));
  state.pid = pid;
  if (!have_old || !ha_state_equal(&old, &state))
    ha_write_state(cfg, &state);
  if (!have_old || strcmp(old.guest_ip, guest_ip) != 0)
    ds_log("[NET] Host access shim ready: %s host=%s/32 guest=%s",
           shim, host_ip, guest_ip[0] ? guest_ip : "waiting-for-DHCP");
out:
  ds_nl_close(ctx);
  ha_unlock(lock);
  return ret;
}

int ds_host_access_setup(struct ds_config *cfg, pid_t child_pid) {
  if (!cfg || child_pid <= 0 || cfg->host_access == DS_HOST_ACCESS_NONE)
    return 0;
  char stop_path[PATH_MAX];
  ha_stop_path(cfg, stop_path, sizeof(stop_path));
  unlink(stop_path);
  if (cfg->host_access == DS_HOST_ACCESS_PTP)
    return ha_setup_ptp(cfg, child_pid, 0);
  if (cfg->host_access == DS_HOST_ACCESS_SHIM)
    return ha_setup_shim(cfg, child_pid, 0);
  return -EINVAL;
}

void ds_host_access_refresh(struct ds_config *cfg, pid_t child_pid) {
  if (!cfg || child_pid <= 0)
    return;
  if (ha_is_stopping(cfg))
    return;
  if (cfg->host_access == DS_HOST_ACCESS_PTP)
    (void)ha_setup_ptp(cfg, child_pid, 1);
  else if (cfg->host_access == DS_HOST_ACCESS_SHIM)
    (void)ha_setup_shim(cfg, child_pid, 1);
}

static int ha_other_live_shim_users(const char *shim,
                                    const char *skip_path) {
  DIR *dir = opendir(get_net_dir());
  if (!dir)
    return 0;
  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strncmp(ent->d_name, "ha_", 3) != 0 ||
        !strstr(ent->d_name, ".state"))
      continue;
    char path[PATH_MAX + NAME_MAX + 2];
    snprintf(path, sizeof(path), "%s/%s", get_net_dir(), ent->d_name);
    if (skip_path && strcmp(path, skip_path) == 0)
      continue;
    struct ha_state state;
    if (ha_read_state_path(path, &state) == 0 &&
        strcmp(state.mode, "shim") == 0 && strcmp(state.shim, shim) == 0 &&
        state.pid > 0 && (kill(state.pid, 0) == 0 || errno == EPERM)) {
      count++;
      break;
    }
  }
  closedir(dir);
  return count;
}

void ds_host_access_cleanup(struct ds_config *cfg, pid_t child_pid) {
  (void)child_pid;
  if (!cfg || cfg->host_access == DS_HOST_ACCESS_NONE)
    return;
  /* The CLI can begin cleanup while the monitor is completing its final
   * heartbeat.  Leave a tombstone before taking any segment lock, and check it
   * again inside setup after acquiring that lock.  The next genuine start
   * removes the marker in ds_host_access_setup(). */
  char stop_path[PATH_MAX];
  ha_stop_path(cfg, stop_path, sizeof(stop_path));
  if (write_file_atomic(stop_path, "1\n") < 0)
    ds_warn("[NET] Host access: failed to write stop marker: %s",
            strerror(errno));
  char path[PATH_MAX];
  ha_state_path(cfg, path, sizeof(path));
  struct ha_state state = {0};
  (void)ha_read_state_path(path, &state);

  if (cfg->host_access == DS_HOST_ACCESS_PTP) {
    char host[IFNAMSIZ], peer[IFNAMSIZ];
    ha_ptp_names(cfg, host, peer);
    int lock = ha_lock(host);
    ds_nl_ctx_t *ctx = ds_nl_open();
    if (ctx) {
      if (state.guest_ip[0]) {
        struct in_addr guest;
        int idx = ds_nl_get_ifindex(ctx, host);
        if (inet_pton(AF_INET, state.guest_ip, &guest) == 1) {
          if (idx > 0)
            (void)ds_nl_del_route4(ctx, guest.s_addr, 32, 0, idx);
          (void)ds_nl_del_rule4(ctx, 0, 0, guest.s_addr, 32,
                                RT_TABLE_MAIN, DS_HA_RULE_PRIORITY);
        }
      }
      ds_nl_del_link(ctx, host);
      if (ds_nl_count_ifaces_with_prefix(ctx, "ds-pt") == 0)
        (void)ds_nl_del_rule4(ctx, 0, 0, inet_addr(DS_HA_PTP_POOL),
                              DS_HA_PTP_POOL_PREFIX, RT_TABLE_MAIN,
                              DS_HA_RULE_PRIORITY);
      ds_nl_close(ctx);
    }
    unlink(path);
    ha_unlock(lock);
    return;
  }

  if (cfg->host_access == DS_HOST_ACCESS_SHIM) {
    char shim[IFNAMSIZ];
    if (state.shim[0])
      safe_strncpy(shim, state.shim, sizeof(shim));
    else
      ha_shim_name(cfg, shim);
    int lock = ha_lock(shim);
    ds_nl_ctx_t *ctx = ds_nl_open();
    if (ctx && state.guest_ip[0]) {
      struct in_addr guest;
      int idx = ds_nl_get_ifindex(ctx, shim);
      if (inet_pton(AF_INET, state.guest_ip, &guest) == 1) {
        if (idx > 0)
          (void)ds_nl_del_route4(ctx, guest.s_addr, 32, 0, idx);
        (void)ds_nl_del_rule4(ctx, 0, 0, guest.s_addr, 32, RT_TABLE_MAIN,
                              DS_HA_RULE_PRIORITY);
      }
    }
    unlink(path);
    if (ctx && !ha_other_live_shim_users(shim, path)) {
      ds_nl_del_link(ctx, shim);
      ds_log("[NET] Host access shim cleanup: removed idle %s", shim);
    }
    if (ctx)
      ds_nl_close(ctx);
    ha_unlock(lock);
  }
}
