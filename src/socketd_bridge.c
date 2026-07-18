/*
 * Droidspaces v6 - private backend bridge for droidspaces-socketd
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "socketd_bridge.h"
#include "droidspace.h"
#include "socketd_protocol.h"

#include <pwd.h>
#include <sys/time.h>

/*
 * The public Docker/Podman-compatible socket belongs to the external C++
 * droidspaces-socketd daemon.  This bridge is deliberately narrower: it is a
 * local, private, privileged control path that will eventually expose the
 * Droidspaces-native operations needed by that compatibility daemon.
 */

#define DS_SOCKETD_GROUP "droidspaces"
#define DS_SOCKETD_CONN_TIMEOUT_SEC 10

static void socketd_set_conn_timeouts(int fd) {
  struct timeval tv;
  memset(&tv, 0, sizeof(tv));
  tv.tv_sec = DS_SOCKETD_CONN_TIMEOUT_SEC;

  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    ds_warn("socketd backend: failed to set receive timeout: %s",
            strerror(errno));
  }

  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
    ds_warn("socketd backend: failed to set send timeout: %s", strerror(errno));
  }
}

static int socketd_read_exact(int fd, void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  while (len > 0) {
    ssize_t r = read(fd, p, len);
    if (r == 0)
      return -1;
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    p += (size_t)r;
    len -= (size_t)r;
  }
  return 0;
}

static socklen_t socketd_backend_addr(struct sockaddr_un *addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;

  size_t name_len = strlen(DS_SOCKETD_BACKEND_SOCK_NAME);
  if (name_len >= sizeof(addr->sun_path))
    name_len = sizeof(addr->sun_path) - 1;

  memcpy(addr->sun_path + 1, DS_SOCKETD_BACKEND_SOCK_NAME, name_len);
  return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);
}

static int socketd_send_response(int fd, enum ds_socketd_status status,
                                 const void *payload, uint32_t payload_len) {
  struct ds_socketd_response_header hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic_be = htonl(DS_SOCKETD_PROTO_MAGIC);
  hdr.version_be = htons(DS_SOCKETD_PROTO_VERSION);
  hdr.status_be = htons((uint16_t)status);
  hdr.payload_len_be = htonl(payload_len);

  if (write_all(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
    return -1;

  if (payload_len > 0 && payload != NULL) {
    if (write_all(fd, payload, payload_len) != (ssize_t)payload_len)
      return -1;
  }
  return 0;
}

static int socketd_peer_authorized(int fd) {
#ifdef SO_PEERCRED
  struct ucred cred;
  socklen_t cred_len = sizeof(cred);
  memset(&cred, 0, sizeof(cred));

  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0)
    return 0;
  if (cred_len != sizeof(cred))
    return 0;

  /*
   * Abstract sockets are scoped to the network namespace, not the filesystem,
   * so a HOST-net container (sharing our netns but running in its own PID
   * namespace, presenting uid 0 without a user namespace) could otherwise
   * connect.  Reject peers outside our PID namespace before any uid/group
   * check; legitimate host clients are always inside it.
   */
  if (!ds_peer_in_pidns(cred.pid))
    return 0;

  /*
   * The abstract backend socket has no filesystem permissions, so enforce the
   * same local authorization model used by the main Droidspaces daemon: root or
   * members of the dedicated 'droidspaces' group may connect.
   */
  if (cred.uid == 0)
    return 1;

  struct group *gr = getgrnam(DS_SOCKETD_GROUP);
  struct passwd *pw = getpwuid(cred.uid);
  if (!gr || !pw)
    return 0;

  int ngroups = 64;
  gid_t groups[64];
  if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0)
    return 0;

  for (int i = 0; i < ngroups; i++) {
    if (groups[i] == gr->gr_gid)
      return 1;
  }

  return 0;
#else
  (void)fd;
  return 0;
#endif
}

static int socketd_discard_payload(int fd, uint32_t len) {
  char buf[4096];
  uint32_t remaining = len;
  while (remaining > 0) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    if (socketd_read_exact(fd, buf, chunk) < 0)
      return -1;
    remaining -= (uint32_t)chunk;
  }
  return 0;
}

static int socketd_read_payload(int fd, void *buf, uint32_t expected_len,
                                uint32_t actual_len) {
  if (actual_len != expected_len)
    return -1;
  return socketd_read_exact(fd, buf, expected_len);
}

/*
 * Wire-format 64-bit host -> network conversion.
 *
 * CONCERN(socketd-wire):
 * The private protocol now carries several int64_t timestamp fields. Keep
 * conversion explicit here rather than relying on a non-standard htonll().
 */
static uint64_t socketd_hton64(uint64_t value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return value;
#else
  return ((uint64_t)htonl((uint32_t)(value & 0xffffffffULL)) << 32) |
         (uint64_t)htonl((uint32_t)(value >> 32));
#endif
}
static uint64_t socketd_ntoh64(uint64_t value) {
  /* Network<->host 64-bit swap is symmetric, so this just forwards. */
  return socketd_hton64(value);
}

static int socketd_read_proc_start_ticks(pid_t pid,
                                         unsigned long long *ticks_out) {
  if (pid <= 0 || ticks_out == NULL)
    return -1;

  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/stat", pid);

  FILE *f = fopen(path, "re");
  if (!f)
    return -1;

  char stat_buf[4096];
  size_t n = fread(stat_buf, 1, sizeof(stat_buf) - 1, f);
  int read_failed = ferror(f);
  fclose(f);

  if (read_failed || n == 0)
    return -1;

  stat_buf[n] = '\0';

  char *rparen = strrchr(stat_buf, ')');
  if (!rparen || rparen[1] != ' ')
    return -1;

  /*
   * /proc/<pid>/stat field 2 is the parenthesized command name and may
   * contain spaces. Start tokenizing only after the final ')' so that field
   * numbering remains aligned with procfs; starttime is field 22.
   */
  char *cursor = rparen + 2;
  char *saveptr = NULL;
  int field_no = 3;

  for (char *tok = strtok_r(cursor, " \t\r\n", &saveptr); tok != NULL;
       tok = strtok_r(NULL, " \t\r\n", &saveptr), field_no++) {
    if (field_no == 22) {
      char *end = NULL;
      errno = 0;
      unsigned long long ticks = strtoull(tok, &end, 10);
      if (errno != 0 || end == tok || *end != '\0')
        return -1;

      *ticks_out = ticks;
      return 0;
    }
  }

  return -1;
}

static int socketd_read_uptime_seconds(double *uptime_out) {
  if (!uptime_out)
    return -1;

  FILE *f = fopen("/proc/uptime", "re");
  if (!f)
    return -1;

  double uptime = 0.0;
  int scanned = fscanf(f, "%lf", &uptime);
  fclose(f);

  if (scanned != 1 || uptime < 0.0)
    return -1;

  *uptime_out = uptime;
  return 0;
}

static int64_t socketd_container_started_at_epoch(pid_t pid) {
  unsigned long long start_ticks = 0;
  double uptime_seconds = 0.0;

  if (socketd_read_proc_start_ticks(pid, &start_ticks) < 0)
    return 0;

  if (socketd_read_uptime_seconds(&uptime_seconds) < 0)
    return 0;

  long ticks_per_second = sysconf(_SC_CLK_TCK);
  if (ticks_per_second <= 0)
    return 0;

  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) < 0)
    return 0;

  double started_since_boot = (double)start_ticks / (double)ticks_per_second;
  double boot_epoch = (double)now.tv_sec - uptime_seconds;
  double started_epoch = boot_epoch + started_since_boot;

  if (started_epoch <= 0.0 || started_epoch > (double)INT64_MAX)
    return 0;

  return (int64_t)started_epoch;
}

static int socketd_pidfile_to_container_name(const char *filename,
                                             char *name_out, size_t name_size) {
  if (!filename || !name_out || name_size == 0)
    return -1;

  size_t filename_len = strlen(filename);
  size_t suffix_len = strlen(DS_EXT_PID);

  if (filename_len <= suffix_len)
    return -1;

  if (strcmp(filename + filename_len - suffix_len, DS_EXT_PID) != 0)
    return -1;

  size_t name_len = filename_len - suffix_len;
  if (name_len >= name_size)
    return -1;

  memcpy(name_out, filename, name_len);
  name_out[name_len] = '\0';

  return validate_container_name(name_out) ? 0 : -1;
}

static void socketd_free_loaded_config(struct ds_config *cfg) {
  ds_config_free(cfg);
}

static int socketd_is_uuid_prefix_ref(const char *ref) {
  if (!ref || ref[0] == '\0')
    return 0;

  size_t len = strlen(ref);
  if (len > DS_UUID_LEN)
    return 0;

  for (size_t i = 0; i < len; i++) {
    if (!isxdigit((unsigned char)ref[i]))
      return 0;
  }

  return 1;
}

static int socketd_load_config_by_ref(const char *ref, struct ds_config *cfg) {
  if (!ref || !cfg)
    return DS_SOCKETD_STATUS_BAD_REQUEST;

  size_t ref_len = strlen(ref);
  if (ref_len == 0 || ref_len >= DS_SOCKETD_RECORD_NAME_MAX)
    return DS_SOCKETD_STATUS_BAD_REQUEST;

  /*
   * Docker-compatible callers may address containers by name or ID. Prefer an
   * exact container-name match, then fall back to matching the persisted
   * Droidspaces UUID by prefix.
   */
  if (validate_container_name(ref)) {
    if (ds_config_load_by_name(ref, cfg) < 0)
      return DS_SOCKETD_STATUS_INTERNAL_ERROR;

    if (cfg->config_file_existed)
      return DS_SOCKETD_STATUS_OK;

    socketd_free_loaded_config(cfg);
    memset(cfg, 0, sizeof(*cfg));
  }

  if (!socketd_is_uuid_prefix_ref(ref))
    return DS_SOCKETD_STATUS_NOT_FOUND;

  char containers_path[PATH_MAX];
  snprintf(containers_path, sizeof(containers_path), "%s/Containers",
           get_workspace_dir());

  DIR *containers_dir = opendir(containers_path);
  if (!containers_dir) {
    if (errno == ENOENT)
      return DS_SOCKETD_STATUS_NOT_FOUND;
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;
  }

  enum ds_socketd_status status = DS_SOCKETD_STATUS_NOT_FOUND;
  struct dirent *ent;

  while ((ent = readdir(containers_dir)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    if (!validate_container_name(ent->d_name))
      continue;

    struct ds_config candidate;
    memset(&candidate, 0, sizeof(candidate));

    if (ds_config_load_by_name(ent->d_name, &candidate) < 0) {
      socketd_free_loaded_config(&candidate);
      status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
      break;
    }

    if (!candidate.config_file_existed || candidate.uuid[0] == '\0' ||
        strncmp(candidate.uuid, ref, ref_len) != 0) {
      socketd_free_loaded_config(&candidate);
      continue;
    }

    if (status == DS_SOCKETD_STATUS_OK) {
      socketd_free_loaded_config(&candidate);
      socketd_free_loaded_config(cfg);
      memset(cfg, 0, sizeof(*cfg));
      status = DS_SOCKETD_STATUS_BAD_REQUEST;
      break;
    }

    *cfg = candidate;
    status = DS_SOCKETD_STATUS_OK;

    if (ref_len == DS_UUID_LEN)
      break;
  }

  closedir(containers_dir);
  return status;
}
static void socketd_pack_port_record(struct ds_socketd_port_record *dst,
                                     const struct ds_port_forward *src);

static void
socketd_pack_container_record(struct ds_socketd_container_record *record,
                              const struct ds_config *cfg, pid_t pid) {
  memset(record, 0, sizeof(*record));

  safe_strncpy(record->name, cfg->container_name, sizeof(record->name));

  /*
   * CONCERN(socketd-identity):
   * This private wire record follows the current plan and forwards the
   * Droidspaces config UUID verbatim. Any public Docker-compatible identity
   * policy remains the responsibility of the C++ JSON seam.
   */
  safe_strncpy(record->uuid, cfg->uuid, sizeof(record->uuid));

  const char *rootfs_ref =
      cfg->rootfs_img_path[0] ? cfg->rootfs_img_path : cfg->rootfs_path;

  safe_strncpy(record->rootfs_path, rootfs_ref, sizeof(record->rootfs_path));

  safe_strncpy(record->hostname, cfg->hostname, sizeof(record->hostname));

  if (cfg->net_mode == DS_NET_NAT) {
    const char *nat_ip =
        cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
    safe_strncpy(record->nat_ip, nat_ip, sizeof(record->nat_ip));
  }

  safe_strncpy(record->custom_init, cfg->custom_init,
               sizeof(record->custom_init));

  record->pid_be = (int32_t)htonl((uint32_t)(pid > 0 ? pid : 0));
  record->net_mode = (uint8_t)cfg->net_mode;

  int port_count = cfg->port_forward_count;
  if (port_count < 0)
    port_count = 0;
  if (port_count > DS_SOCKETD_RECORD_PORTS_MAX)
    port_count = DS_SOCKETD_RECORD_PORTS_MAX;

  record->port_count = (uint8_t)port_count;

  for (int i = 0; i < port_count; i++)
    socketd_pack_port_record(&record->ports[i], &cfg->port_forwards[i]);

  int64_t started_at = pid > 0 ? socketd_container_started_at_epoch(pid) : 0;
  record->started_at_be = (int64_t)socketd_hton64((uint64_t)started_at);
}

static int socketd_u16_count(int count, int max_count) {
  if (count < 0)
    return 0;
  if (count > max_count)
    return max_count;
  return count;
}

static void socketd_pack_port_record(struct ds_socketd_port_record *dst,
                                     const struct ds_port_forward *src) {
  memset(dst, 0, sizeof(*dst));
  dst->host_port_be = htons(src->host_port);
  dst->host_port_end_be = htons(src->host_port_end);
  dst->container_port_be = htons(src->container_port);
  dst->container_port_end_be = htons(src->container_port_end);
  dst->proto = (strcmp(src->proto, "udp") == 0) ? 1u : 0u;
}

static void socketd_pack_inspect_record(
    struct ds_socketd_inspect_container_record_v1 *record,
    const struct ds_config *cfg, pid_t pid) {
  memset(record, 0, sizeof(*record));

  record->record_version_be = htons(1u);
  record->record_size_be =
      htonl((uint32_t)sizeof(struct ds_socketd_inspect_container_record_v1));

  safe_strncpy(record->name, cfg->container_name, sizeof(record->name));
  safe_strncpy(record->uuid, cfg->uuid, sizeof(record->uuid));
  safe_strncpy(record->rootfs_path, cfg->rootfs_path,
               sizeof(record->rootfs_path));

  const char *image_ref =
      cfg->rootfs_img_path[0] ? cfg->rootfs_img_path : cfg->rootfs_path;
  safe_strncpy(record->image_ref, image_ref, sizeof(record->image_ref));
  safe_strncpy(record->hostname, cfg->hostname, sizeof(record->hostname));

  if (cfg->net_mode == DS_NET_NAT) {
    const char *nat_ip =
        cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
    safe_strncpy(record->nat_ip, nat_ip, sizeof(record->nat_ip));
  }

  safe_strncpy(record->custom_init, cfg->custom_init,
               sizeof(record->custom_init));
  safe_strncpy(record->dns_servers, cfg->dns_servers,
               sizeof(record->dns_servers));

  record->pid_be = (int32_t)htonl((uint32_t)(pid > 0 ? pid : 0));
  int64_t started_at = pid > 0 ? socketd_container_started_at_epoch(pid) : 0;
  record->started_at_be = (int64_t)socketd_hton64((uint64_t)started_at);

  record->memory_limit_be =
      (int64_t)socketd_hton64((uint64_t)cfg->memory_limit);
  record->cpu_quota_be = (int64_t)socketd_hton64((uint64_t)cfg->cpu_quota);
  record->cpu_period_be = (int64_t)socketd_hton64((uint64_t)cfg->cpu_period);
  record->pids_limit_be = (int64_t)socketd_hton64((uint64_t)cfg->pids_limit);
  record->privileged_mask_be = (int32_t)htonl((uint32_t)cfg->privileged_mask);

  record->net_mode = (uint8_t)cfg->net_mode;
  record->foreground = cfg->foreground ? 1u : 0u;
  record->volatile_mode = cfg->volatile_mode ? 1u : 0u;
  record->force_cgroupv1 = cfg->force_cgroupv1 ? 1u : 0u;
  record->disable_ipv6 = cfg->disable_ipv6 ? 1u : 0u;
  record->android_storage = cfg->android_storage ? 1u : 0u;
  record->selinux_permissive = cfg->selinux_permissive ? 1u : 0u;
  record->hw_access = cfg->hw_access ? 1u : 0u;
  record->gpu_mode = cfg->gpu_mode ? 1u : 0u;
  record->termux_x11 = cfg->termux_x11 ? 1u : 0u;
  record->block_nested_ns = cfg->block_nested_ns ? 1u : 0u;
  record->is_img_mount = cfg->is_img_mount ? 1u : 0u;

  int env_count =
      socketd_u16_count(cfg->env_var_count, DS_SOCKETD_INSPECT_ENV_MAX);
  record->env_count_be = htons((uint16_t)env_count);
  record->env_total_count_be =
      htons((uint16_t)socketd_u16_count(cfg->env_var_count, UINT16_MAX));
  for (int i = 0; i < env_count; i++) {
    if (cfg->env_vars[i].key)
      safe_strncpy(record->env[i].key, cfg->env_vars[i].key,
                   sizeof(record->env[i].key));
    if (cfg->env_vars[i].value)
      safe_strncpy(record->env[i].value, cfg->env_vars[i].value,
                   sizeof(record->env[i].value));
  }

  int bind_count =
      socketd_u16_count(cfg->bind_count, DS_SOCKETD_INSPECT_BINDS_MAX);
  record->bind_count_be = htons((uint16_t)bind_count);
  record->bind_total_count_be =
      htons((uint16_t)socketd_u16_count(cfg->bind_count, UINT16_MAX));
  for (int i = 0; i < bind_count; i++) {
    safe_strncpy(record->binds[i].source, cfg->binds[i].src,
                 sizeof(record->binds[i].source));
    safe_strncpy(record->binds[i].destination, cfg->binds[i].dest,
                 sizeof(record->binds[i].destination));
    record->binds[i].read_only = cfg->binds[i].ro ? 1u : 0u;
  }

  int port_count =
      socketd_u16_count(cfg->port_forward_count, DS_SOCKETD_RECORD_PORTS_MAX);
  record->port_count_be = htons((uint16_t)port_count);
  record->port_total_count_be =
      htons((uint16_t)socketd_u16_count(cfg->port_forward_count, UINT16_MAX));
  for (int i = 0; i < port_count; i++)
    socketd_pack_port_record(&record->ports[i], &cfg->port_forwards[i]);
}

static int socketd_append_container_record(
    struct ds_socketd_container_record **records_inout, size_t *count_inout,
    size_t *capacity_inout, const struct ds_socketd_container_record *record) {
  if (!records_inout || !count_inout || !capacity_inout || !record)
    return -1;

  if (*count_inout >= *capacity_inout) {
    size_t old_capacity = *capacity_inout;
    size_t new_capacity = old_capacity == 0 ? 16u : old_capacity * 2u;

    if (new_capacity < old_capacity)
      return -1;

    if (new_capacity >
        DS_SOCKETD_MAX_PAYLOAD / sizeof(struct ds_socketd_container_record)) {
      return -1;
    }

    struct ds_socketd_container_record *grown =
        realloc(*records_inout, new_capacity * sizeof(*grown));
    if (!grown)
      return -1;

    memset(grown + old_capacity, 0,
           (new_capacity - old_capacity) * sizeof(*grown));

    *records_inout = grown;
    *capacity_inout = new_capacity;
  }

  (*records_inout)[*count_inout] = *record;
  (*count_inout)++;
  return 0;
}

static void socketd_pack_image_record(struct ds_socketd_image_record *record,
                                      const struct ds_config *cfg,
                                      int is_running) {
  memset(record, 0, sizeof(*record));

  safe_strncpy(record->name, cfg->container_name, sizeof(record->name));

  /*
   * The plan defines pseudo-images as the rootfs object associated with each
   * installed container config. For image-backed containers, preserve the
   * underlying image-file path; otherwise expose the configured rootfs path.
   */
  if (cfg->rootfs_img_path[0]) {
    safe_strncpy(record->rootfs_path, cfg->rootfs_img_path,
                 sizeof(record->rootfs_path));
  } else {
    safe_strncpy(record->rootfs_path, cfg->rootfs_path,
                 sizeof(record->rootfs_path));
  }

  safe_strncpy(record->uuid, cfg->uuid, sizeof(record->uuid));

  record->is_running_be = (int32_t)htonl(is_running ? 1u : 0u);

  /*
   * Droidspaces container configs do not currently persist an explicit
   * creation timestamp. The wire contract reserves this field for later
   * enrichment; Phase 2.5 intentionally reports "unknown" as zero.
   */
  record->created_at_be = (int64_t)socketd_hton64(0);
}

static int
socketd_append_image_record(struct ds_socketd_image_record **records_inout,
                            size_t *count_inout, size_t *capacity_inout,
                            const struct ds_socketd_image_record *record) {
  if (!records_inout || !count_inout || !capacity_inout || !record)
    return -1;

  if (*count_inout >= *capacity_inout) {
    size_t old_capacity = *capacity_inout;
    size_t new_capacity = old_capacity == 0 ? 16u : old_capacity * 2u;

    if (new_capacity < old_capacity)
      return -1;

    if (new_capacity >
        DS_SOCKETD_MAX_PAYLOAD / sizeof(struct ds_socketd_image_record)) {
      return -1;
    }

    struct ds_socketd_image_record *grown =
        realloc(*records_inout, new_capacity * sizeof(*grown));
    if (!grown)
      return -1;

    memset(grown + old_capacity, 0,
           (new_capacity - old_capacity) * sizeof(*grown));

    *records_inout = grown;
    *capacity_inout = new_capacity;
  }

  (*records_inout)[*count_inout] = *record;
  (*count_inout)++;
  return 0;
}

static int socketd_append_core_event_record(
    struct ds_socketd_core_event_record **records_inout, size_t *count_inout,
    size_t *capacity_inout, const struct ds_socketd_core_event_record *record) {
  if (!records_inout || !count_inout || !capacity_inout || !record)
    return -1;

  if (*count_inout >= *capacity_inout) {
    size_t old_capacity = *capacity_inout;
    size_t new_capacity = old_capacity == 0 ? 16u : old_capacity * 2u;

    if (new_capacity < old_capacity)
      return -1;

    if (new_capacity >
        DS_SOCKETD_MAX_PAYLOAD / sizeof(struct ds_socketd_core_event_record)) {
      return -1;
    }

    struct ds_socketd_core_event_record *grown =
        realloc(*records_inout, new_capacity * sizeof(*grown));
    if (!grown)
      return -1;

    memset(grown + old_capacity, 0,
           (new_capacity - old_capacity) * sizeof(*grown));

    *records_inout = grown;
    *capacity_inout = new_capacity;
  }

  (*records_inout)[*count_inout] = *record;
  (*count_inout)++;
  return 0;
}

static int socketd_count_installed_containers(uint32_t *count_out) {
  if (!count_out)
    return -1;

  *count_out = 0;

  char containers_path[PATH_MAX];
  snprintf(containers_path, sizeof(containers_path), "%s/Containers",
           get_workspace_dir());

  DIR *containers_dir = opendir(containers_path);
  if (!containers_dir) {
    if (errno == ENOENT)
      return 0;
    return -1;
  }

  uint32_t count = 0;
  struct dirent *ent;

  /*
   * CONCERN(socketd-info):
   * Treat the workspace inventory as the source of "installed" containers:
   * only valid container-name entries with a loadable mirrored config count
   * here. This keeps INFO aligned with the same installed-container view that
   * the later LIST_IMAGES pass will expose.
   */
  while ((ent = readdir(containers_dir)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    if (!validate_container_name(ent->d_name))
      continue;

    struct ds_config cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (ds_config_load_by_name(ent->d_name, &cfg) == 0 &&
        cfg.config_file_existed) {
      if (count < UINT32_MAX)
        count++;
    }

    socketd_free_loaded_config(&cfg);
  }

  closedir(containers_dir);
  *count_out = count;
  return 0;
}

static int socketd_validate_kernel_version(void) {
  int major = 0;
  int minor = 0;

  if (get_kernel_version(&major, &minor) < 0)
    return -1;

  if (major < DS_MIN_KERNEL_MAJOR ||
      (major == DS_MIN_KERNEL_MAJOR && minor < DS_MIN_KERNEL_MINOR)) {
    ds_error("Droidspaces requires at least Linux %d.%d.0; detected %d.%d",
             DS_MIN_KERNEL_MAJOR, DS_MIN_KERNEL_MINOR, major, minor);
    return -1;
  }

  return 0;
}

static int socketd_validate_start_config(struct ds_config *cfg) {
  if (!cfg)
    return -1;

  if (!cfg->container_name[0] || reject_container_name(cfg->container_name) < 0)
    return -1;

  if (cfg->rootfs_path[0] && cfg->rootfs_img_path[0]) {
    ds_error("Container '%s' has both rootfs directory and image configured",
             cfg->container_name);
    return -1;
  }

  if (!cfg->rootfs_path[0] && !cfg->rootfs_img_path[0]) {
    ds_error("Container '%s' has no rootfs target configured",
             cfg->container_name);
    return -1;
  }

  if (cfg->rootfs_path[0] && access(cfg->rootfs_path, F_OK) != 0) {
    ds_error("Rootfs directory not found for '%s': %s", cfg->container_name,
             cfg->rootfs_path);
    return -1;
  }

  if (cfg->rootfs_img_path[0] && access(cfg->rootfs_img_path, F_OK) != 0) {
    ds_error("Rootfs image not found for '%s': %s", cfg->container_name,
             cfg->rootfs_img_path);
    return -1;
  }

  if (cfg->custom_init[0]) {
    if (cfg->custom_init[0] != '/' || strchr(cfg->custom_init, ' ')) {
      ds_error("Invalid custom init for '%s': %s", cfg->container_name,
               cfg->custom_init);
      return -1;
    }
  }

  if (socketd_validate_kernel_version() < 0)
    return -1;

  if (check_requirements_hw(cfg->hw_access) < 0)
    return -1;

  if ((cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_NONE) &&
      !check_ns(CLONE_NEWNET, "net")) {
    ds_error("Container '%s' requires network namespaces for its net mode",
             cfg->container_name);
    return -1;
  }

  return 0;
}

static enum ds_socketd_status socketd_read_lifecycle_request(
    int conn, uint32_t payload_len, struct ds_socketd_lifecycle_req *req_out,
    char *target_out, size_t target_size, int *timeout_seconds_out) {
  if (!req_out || !target_out || target_size == 0 || !timeout_seconds_out)
    return DS_SOCKETD_STATUS_BAD_REQUEST;

  memset(req_out, 0, sizeof(*req_out));
  if (socketd_read_payload(conn, req_out, (uint32_t)sizeof(*req_out),
                           payload_len) < 0) {
    return DS_SOCKETD_STATUS_BAD_REQUEST;
  }

  /* The wire format does not guarantee target[] is NUL-terminated; force it
   * before safe_strncpy() (which calls strlen) so a client sending 256
   * non-zero bytes cannot make strlen read past the fixed-size field. */
  req_out->target[sizeof(req_out->target) - 1] = '\0';
  safe_strncpy(target_out, req_out->target, target_size);
  if (!target_out[0])
    return DS_SOCKETD_STATUS_BAD_REQUEST;

  int32_t timeout = (int32_t)ntohl((uint32_t)req_out->timeout_seconds_be);
  if (timeout < -1)
    return DS_SOCKETD_STATUS_BAD_REQUEST;

  *timeout_seconds_out = (int)timeout;
  return DS_SOCKETD_STATUS_OK;
}

static void socketd_prepare_runtime_for_container(struct ds_config *cfg) {
  if (!cfg)
    return;

  safe_strncpy(ds_log_container_name, cfg->container_name,
               sizeof(ds_log_container_name));
  ds_cgroup_host_bootstrap(cfg->force_cgroupv1);
}

static enum ds_socketd_status socketd_lifecycle_start(struct ds_config *cfg) {
  pid_t pid = 0;
  if (is_container_running(cfg, &pid) && pid > 0)
    return DS_SOCKETD_STATUS_ALREADY_RUNNING;

  if (socketd_validate_start_config(cfg) < 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  socketd_prepare_runtime_for_container(cfg);

  if (start_rootfs(cfg) < 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  return DS_SOCKETD_STATUS_OK;
}

static enum ds_socketd_status socketd_lifecycle_stop(struct ds_config *cfg,
                                                     int timeout_seconds) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0)
    return DS_SOCKETD_STATUS_ALREADY_STOPPED;

  socketd_prepare_runtime_for_container(cfg);

  if (stop_rootfs_with_timeout(cfg, 0, timeout_seconds) < 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  pid = 0;
  if (is_container_running(cfg, &pid) && pid > 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  return DS_SOCKETD_STATUS_OK;
}

static enum ds_socketd_status socketd_lifecycle_restart(struct ds_config *cfg,
                                                        int timeout_seconds) {
  pid_t pid = 0;
  int was_running = is_container_running(cfg, &pid) && pid > 0;

  if (socketd_validate_start_config(cfg) < 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  socketd_prepare_runtime_for_container(cfg);

  if (was_running) {
    if (restart_rootfs_with_timeout(cfg, timeout_seconds) < 0)
      return DS_SOCKETD_STATUS_INTERNAL_ERROR;
  } else {
    if (start_rootfs(cfg) < 0)
      return DS_SOCKETD_STATUS_INTERNAL_ERROR;
  }

  pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0)
    return DS_SOCKETD_STATUS_INTERNAL_ERROR;

  if (was_running)
    ds_socketd_record_core_event("restart", cfg->container_name, cfg->uuid);

  return DS_SOCKETD_STATUS_OK;
}

static int socketd_handle_lifecycle_request(int conn, uint32_t payload_len,
                                            enum ds_socketd_opcode opcode) {
  struct ds_socketd_lifecycle_req req;
  char target[DS_SOCKETD_RECORD_NAME_MAX];
  int timeout_seconds = -1;

  enum ds_socketd_status status = socketd_read_lifecycle_request(
      conn, payload_len, &req, target, sizeof(target), &timeout_seconds);
  if (status != DS_SOCKETD_STATUS_OK) {
    socketd_send_response(conn, status, NULL, 0);
    return 0;
  }

  struct ds_config cfg;
  memset(&cfg, 0, sizeof(cfg));

  int load_status = socketd_load_config_by_ref(target, &cfg);
  if (load_status != DS_SOCKETD_STATUS_OK) {
    socketd_free_loaded_config(&cfg);
    socketd_send_response(conn, (enum ds_socketd_status)load_status, NULL, 0);
    return 0;
  }

  switch (opcode) {
  case DS_SOCKETD_OP_START_CONTAINER:
    status = socketd_lifecycle_start(&cfg);
    break;
  case DS_SOCKETD_OP_STOP_CONTAINER:
    status = socketd_lifecycle_stop(&cfg, timeout_seconds);
    break;
  case DS_SOCKETD_OP_RESTART_CONTAINER:
    status = socketd_lifecycle_restart(&cfg, timeout_seconds);
    break;
  default:
    status = DS_SOCKETD_STATUS_UNSUPPORTED;
    break;
  }

  socketd_free_loaded_config(&cfg);
  socketd_send_response(conn, status, NULL, 0);
  return 0;
}

static int socketd_build_core_event_path(char *path, size_t path_size) {
  if (!path || path_size == 0)
    return -1;

  int r =
      snprintf(path, path_size, "%.4076s/socketd-events.bin", get_logs_dir());
  return (r > 0 && (size_t)r < path_size) ? 0 : -1;
}

static void socketd_handle_conn(int conn) {
  struct ds_socketd_request_header req;
  memset(&req, 0, sizeof(req));

  if (!socketd_peer_authorized(conn)) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_FORBIDDEN, NULL, 0);
    return;
  }

  if (socketd_read_exact(conn, &req, sizeof(req)) < 0) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  uint32_t magic = ntohl(req.magic_be);
  uint16_t version = ntohs(req.version_be);
  uint16_t opcode = ntohs(req.opcode_be);
  uint32_t payload_len = ntohl(req.payload_len_be);

  if (magic != DS_SOCKETD_PROTO_MAGIC || version != DS_SOCKETD_PROTO_VERSION) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  if (payload_len > DS_SOCKETD_MAX_PAYLOAD) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  switch ((enum ds_socketd_opcode)opcode) {
  case DS_SOCKETD_OP_PING: {
    if (payload_len > 0 && socketd_discard_payload(conn, payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    static const char pong[] = "PONG";
    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, pong,
                          (uint32_t)(sizeof(pong) - 1));
    return;
  }

  case DS_SOCKETD_OP_CAPABILITIES: {
    if (payload_len > 0 && socketd_discard_payload(conn, payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    uint32_t caps_be =
        htonl(DS_SOCKETD_CAP_PROTOCOL_V1 | DS_SOCKETD_CAP_PING |
              DS_SOCKETD_CAP_CAPABILITIES | DS_SOCKETD_CAP_INFO |
              DS_SOCKETD_CAP_LIST_CONTAINERS |
              DS_SOCKETD_CAP_INSPECT_CONTAINER | DS_SOCKETD_CAP_LIFECYCLE |
              DS_SOCKETD_CAP_LIST_IMAGES | DS_SOCKETD_CAP_POLL_EVENTS);

    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, &caps_be,
                          (uint32_t)sizeof(caps_be));
    return;
  }

  case DS_SOCKETD_OP_INFO: {
    if (payload_len > 0 && socketd_discard_payload(conn, payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    uint32_t installed_count = 0;
    if (socketd_count_installed_containers(&installed_count) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    int running_i = count_running_containers(NULL, 0);
    uint32_t running_count = running_i > 0 ? (uint32_t)running_i : 0u;

    /*
     * CONCERN(socketd-info):
     * Running sidecar state and mirrored workspace-config state are maintained
     * through separate recovery paths. Keep subtraction saturating so a
     * transient metadata mismatch never underflows the wire-format stopped
     * counter.
     */
    uint32_t stopped_count =
        installed_count > running_count ? installed_count - running_count : 0u;

    struct ds_socketd_info_payload info;
    memset(&info, 0, sizeof(info));

    info.containers_total_be = htonl(installed_count);
    info.containers_running_be = htonl(running_count);
    info.containers_stopped_be = htonl(stopped_count);

    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, &info,
                          (uint32_t)sizeof(info));
    return;
  }

  case DS_SOCKETD_OP_LIST_CONTAINERS: {
    struct ds_socketd_list_containers_req list_req;
    memset(&list_req, 0, sizeof(list_req));

    /*
     * A zero-length payload is accepted as the historical default:
     * running containers only.
     */
    if (payload_len != 0 &&
        socketd_read_payload(conn, &list_req, (uint32_t)sizeof(list_req),
                             payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    int running_hint = count_running_containers(NULL, 0);

    size_t capacity = 16;
    if (running_hint > 16)
      capacity = (size_t)running_hint;

    if (capacity >
        DS_SOCKETD_MAX_PAYLOAD / sizeof(struct ds_socketd_container_record)) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    struct ds_socketd_container_record *records =
        calloc(capacity, sizeof(*records));
    if (!records) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    size_t record_count = 0;

    /*
     * Pass 1: running containers from the Pids/ directory.
     *
     * This mirrors the runtime's own status/reporting scans and keeps the
     * bridge anchored to the existing PID-sidecar authority.
     */
    DIR *pids_dir = opendir(get_pids_dir());
    if (pids_dir) {
      struct dirent *ent;

      while ((ent = readdir(pids_dir)) != NULL) {
        char name[256];
        if (socketd_pidfile_to_container_name(ent->d_name, name, sizeof(name)) <
            0) {
          continue;
        }

        struct ds_config cfg;
        memset(&cfg, 0, sizeof(cfg));

        if (ds_config_load_by_name(name, &cfg) < 0 ||
            !cfg.config_file_existed) {
          socketd_free_loaded_config(&cfg);
          continue;
        }

        pid_t pid = 0;
        if (!is_container_running(&cfg, &pid) || pid <= 0) {
          socketd_free_loaded_config(&cfg);
          continue;
        }

        struct ds_socketd_container_record record;
        socketd_pack_container_record(&record, &cfg, pid);

        if (socketd_append_container_record(&records, &record_count, &capacity,
                                            &record) < 0) {
          socketd_free_loaded_config(&cfg);
          closedir(pids_dir);
          free(records);
          socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL,
                                0);
          return;
        }

        socketd_free_loaded_config(&cfg);
      }

      closedir(pids_dir);
    } else if (errno != ENOENT) {
      free(records);
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    /*
     * Pass 2: when include_all is set, append stopped installed containers
     * from Containers/. Running ones are skipped because they were already
     * emitted by the PID-sidecar pass above.
     */
    if (list_req.include_all) {
      char containers_path[PATH_MAX];
      snprintf(containers_path, sizeof(containers_path), "%s/Containers",
               get_workspace_dir());

      DIR *containers_dir = opendir(containers_path);
      if (containers_dir) {
        struct dirent *ent;

        while ((ent = readdir(containers_dir)) != NULL) {
          if (ent->d_name[0] == '.')
            continue;

          if (!validate_container_name(ent->d_name))
            continue;

          struct ds_config cfg;
          memset(&cfg, 0, sizeof(cfg));

          if (ds_config_load_by_name(ent->d_name, &cfg) < 0 ||
              !cfg.config_file_existed) {
            socketd_free_loaded_config(&cfg);
            continue;
          }

          pid_t pid = 0;
          if (is_container_running(&cfg, &pid) && pid > 0) {
            socketd_free_loaded_config(&cfg);
            continue;
          }

          struct ds_socketd_container_record record;
          socketd_pack_container_record(&record, &cfg, 0);

          if (socketd_append_container_record(&records, &record_count,
                                              &capacity, &record) < 0) {
            socketd_free_loaded_config(&cfg);
            closedir(containers_dir);
            free(records);
            socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL,
                                  0);
            return;
          }

          socketd_free_loaded_config(&cfg);
        }

        closedir(containers_dir);
      } else if (errno != ENOENT) {
        free(records);
        socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
        return;
      }
    }

    uint32_t payload_bytes = (uint32_t)(record_count * sizeof(*records));

    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, records, payload_bytes);
    free(records);
    return;
  }

  case DS_SOCKETD_OP_LIST_IMAGES: {
    if (payload_len > 0 && socketd_discard_payload(conn, payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    size_t capacity = 16;
    struct ds_socketd_image_record *records =
        calloc(capacity, sizeof(*records));
    if (!records) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    size_t record_count = 0;

    char containers_path[PATH_MAX];
    snprintf(containers_path, sizeof(containers_path), "%s/Containers",
             get_workspace_dir());

    DIR *containers_dir = opendir(containers_path);
    if (containers_dir) {
      struct dirent *ent;

      /*
       * One pseudo-image record is emitted for each installed container config,
       * as defined by the implementation plan. The bridge does not attempt to
       * deduplicate configs that happen to reference the same rootfs object.
       */
      while ((ent = readdir(containers_dir)) != NULL) {
        if (ent->d_name[0] == '.')
          continue;

        if (!validate_container_name(ent->d_name))
          continue;

        struct ds_config cfg;
        memset(&cfg, 0, sizeof(cfg));

        if (ds_config_load_by_name(ent->d_name, &cfg) < 0 ||
            !cfg.config_file_existed) {
          socketd_free_loaded_config(&cfg);
          continue;
        }

        pid_t pid = 0;
        int is_running = is_container_running(&cfg, &pid) && pid > 0;

        struct ds_socketd_image_record record;
        socketd_pack_image_record(&record, &cfg, is_running);

        if (socketd_append_image_record(&records, &record_count, &capacity,
                                        &record) < 0) {
          socketd_free_loaded_config(&cfg);
          closedir(containers_dir);
          free(records);
          socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL,
                                0);
          return;
        }

        socketd_free_loaded_config(&cfg);
      }

      closedir(containers_dir);
    } else if (errno != ENOENT) {
      free(records);
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    uint32_t payload_bytes = (uint32_t)(record_count * sizeof(*records));

    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, records, payload_bytes);
    free(records);
    return;
  }

  case DS_SOCKETD_OP_INSPECT_CONTAINER: {
    struct ds_socketd_inspect_container_req inspect_req;
    memset(&inspect_req, 0, sizeof(inspect_req));

    if (socketd_read_payload(conn, &inspect_req, (uint32_t)sizeof(inspect_req),
                             payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    /* Force NUL-termination of the wire field before safe_strncpy()'s strlen
     * to avoid an out-of-bounds read past the fixed-size target[]. */
    inspect_req.target[sizeof(inspect_req.target) - 1] = '\0';
    char target[DS_SOCKETD_RECORD_NAME_MAX];
    safe_strncpy(target, inspect_req.target, sizeof(target));
    if (!target[0]) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    struct ds_config cfg;
    memset(&cfg, 0, sizeof(cfg));

    int status = socketd_load_config_by_ref(target, &cfg);
    if (status != DS_SOCKETD_STATUS_OK) {
      socketd_free_loaded_config(&cfg);
      socketd_send_response(conn, (enum ds_socketd_status)status, NULL, 0);
      return;
    }

    pid_t pid = 0;
    if (!is_container_running(&cfg, &pid) || pid <= 0)
      pid = 0;

    struct ds_socketd_inspect_container_record_v1 record;
    socketd_pack_inspect_record(&record, &cfg, pid);
    socketd_free_loaded_config(&cfg);

    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, &record,
                          (uint32_t)sizeof(record));
    return;
  }

  case DS_SOCKETD_OP_POLL_EVENTS: {
    struct ds_socketd_poll_events_req poll_req;
    memset(&poll_req, 0, sizeof(poll_req));

    /*
     * A zero-length request means "all events currently retained".
     */
    if (payload_len != 0 &&
        socketd_read_payload(conn, &poll_req, (uint32_t)sizeof(poll_req),
                             payload_len) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
      return;
    }

    int64_t since = (int64_t)socketd_ntoh64((uint64_t)poll_req.since_be);

    char event_path[PATH_MAX];
    if (socketd_build_core_event_path(event_path, sizeof(event_path)) < 0) {
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    FILE *f = fopen(event_path, "re");
    if (!f) {
      if (errno == ENOENT) {
        socketd_send_response(conn, DS_SOCKETD_STATUS_OK, NULL, 0);
        return;
      }

      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    size_t capacity = 16;
    struct ds_socketd_core_event_record *records =
        calloc(capacity, sizeof(*records));
    if (!records) {
      fclose(f);
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    size_t record_count = 0;
    struct ds_socketd_core_event_record record;

    while (fread(&record, sizeof(record), 1, f) == 1) {
      int64_t record_time = (int64_t)socketd_ntoh64((uint64_t)record.time_be);

      if (record_time < since)
        continue;

      if (socketd_append_core_event_record(&records, &record_count, &capacity,
                                           &record) < 0) {
        fclose(f);
        free(records);
        socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
        return;
      }
    }

    if (ferror(f)) {
      fclose(f);
      free(records);
      socketd_send_response(conn, DS_SOCKETD_STATUS_INTERNAL_ERROR, NULL, 0);
      return;
    }

    fclose(f);

    uint32_t payload_bytes = (uint32_t)(record_count * sizeof(*records));

    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, records, payload_bytes);
    free(records);
    return;
  }

  case DS_SOCKETD_OP_START_CONTAINER:
  case DS_SOCKETD_OP_STOP_CONTAINER:
  case DS_SOCKETD_OP_RESTART_CONTAINER:
    socketd_handle_lifecycle_request(conn, payload_len,
                                     (enum ds_socketd_opcode)opcode);
    return;

  default:
    socketd_send_response(conn, DS_SOCKETD_STATUS_UNSUPPORTED, NULL, 0);
    return;
  }
}

static int socketd_bridge_loop(void) {
  struct sockaddr_un addr;
  int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server < 0)
    return -1;

  socklen_t addr_len = socketd_backend_addr(&addr);
  if (bind(server, (struct sockaddr *)&addr, addr_len) < 0) {
    close(server);
    return -1;
  }

  if (listen(server, SOMAXCONN) < 0) {
    close(server);
    return -1;
  }

  ds_log("droidspaces-socketd backend bridge listening on @%s",
         DS_SOCKETD_BACKEND_SOCK_NAME);

  for (;;) {
    int conn = accept4(server, NULL, NULL, SOCK_CLOEXEC);
    if (conn < 0) {
      if (errno == EINTR)
        continue;
      ds_warn("socketd backend accept failed: %s", strerror(errno));
      continue;
    }

    socketd_set_conn_timeouts(conn);
    socketd_handle_conn(conn);
    close(conn);
  }

  return 0;
}

int ds_socketd_bridge_start(void) {
  pid_t child = fork();
  if (child < 0)
    return -1;

  if (child > 0) {
    ds_log("droidspaces-socketd backend bridge process started (PID %d)",
           child);
    return 0;
  }

  prctl(PR_SET_NAME, "[ds-socketd]", 0, 0, 0);
  signal(SIGPIPE, SIG_IGN);

#ifdef PR_SET_PDEATHSIG
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  if (getppid() == 1)
    _exit(0);
#endif

  int rc = socketd_bridge_loop();
  ds_error("droidspaces-socketd backend bridge exited: %s", strerror(errno));
  _exit(rc == 0 ? 0 : 1);
}
