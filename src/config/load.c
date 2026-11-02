/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

#include "config/internal.h"

/* Forward declarations */
static void add_unknown_line(struct ds_config *cfg, const char *line);
/* ds_net_validate_static_ip is defined in network.c - declared in droidspace.h
 */
#include <libgen.h>

/* Helpers */

/* Strict boolean parser: accepts 0/1, true/false, yes/no, on/off */
static int parse_bool(const char *val) {
  if (!val)
    return 0;

  if (strcasecmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0)
    return 1;

  if (strcasecmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "no") == 0 || strcasecmp(val, "off") == 0)
    return 0;

  return 0;
}

/* Safe positive integer parser: uses strtoll with full error checking.
 * Returns -1 on any error (overflow, empty, non-numeric, negative). */
static long long parse_ll_positive(const char *val) {
  if (!val || !*val)
    return -1;
  char *end;
  errno = 0;
  long long v = strtoll(val, &end, 10);
  if (errno || end == val || *end != '\0' || v <= 0)
    return -1;
  return v;
}

static void parse_bind_mounts(const char *value, struct ds_config *cfg) {
  char copy[4096], err[256];
  safe_strncpy(copy, value, sizeof(copy));
  char *saveptr;
  for (char *tok = strtok_r(copy, ",", &saveptr); tok;
       tok = strtok_r(NULL, ",", &saveptr))
    if (ds_parse_bind_spec(tok, cfg, err, sizeof(err)) < 0)
      ds_warn("config: %s - skipping", err);
}

int ds_config_load(const char *config_path, struct ds_config *cfg) {
  FILE *f = fopen(config_path, "re");
  if (!f) {
    if (errno == ENOENT) {
      cfg->config_file_existed = 0;
      return 0; /* Optional config */
    }
    return -1;
  }

  /* Clear existing unknown lines to avoid duplication on re-load */
  free_config_unknown_lines(cfg);

  cfg->config_file_existed = 1;

  char line[2048];
  int line_num = 0;

  while (fgets(line, sizeof(line), f)) {
    line_num++;
    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));
    char *trimmed = trim_whitespace(line_copy);

    if (trimmed[0] == '#' || trimmed[0] == '\0')
      continue;

    char *equals = strchr(trimmed, '=');
    if (!equals) {
      continue;
    }

    *equals = '\0';
    char *key = trim_whitespace(trimmed);
    char *val = trim_whitespace(equals + 1);

    if (strcmp(key, "name") == 0) {
      if (validate_container_name(val))
        safe_strncpy(cfg->container_name, val, sizeof(cfg->container_name));
      else
        ds_warn("config: ignoring invalid container name '%s'", val);
    } else if (strcmp(key, "hostname") == 0) {
      safe_strncpy(cfg->hostname, val, sizeof(cfg->hostname));
    } else if (strcmp(key, "rootfs_path") == 0) {
      struct stat st;
      if (stat(val, &st) == 0 && (S_ISREG(st.st_mode) || S_ISBLK(st.st_mode))) {
        safe_strncpy(cfg->rootfs_img_path, val, sizeof(cfg->rootfs_img_path));
        if (!cfg->is_img_mount)
          cfg->rootfs_path[0] = '\0';
        cfg->is_img_mount = 1;
      } else {
        safe_strncpy(cfg->rootfs_path, val, sizeof(cfg->rootfs_path));
        if (cfg->is_img_mount)
          cfg->rootfs_img_path[0] = '\0';
        cfg->is_img_mount = 0;
      }
    } else if (strcmp(key, "disable_ipv6") == 0) {
      cfg->disable_ipv6 = parse_bool(val);
    } else if (strcmp(key, "enable_android_storage") == 0) {
      cfg->android_storage = parse_bool(val);
    } else if (strcmp(key, "enable_hw_access") == 0) {
      cfg->hw_access = parse_bool(val);
    } else if (strcmp(key, "enable_gpu_mode") == 0) {
      cfg->gpu_mode = parse_bool(val);
    } else if (strcmp(key, "enable_x11") == 0 ||
               strcmp(key, "enable_termux_x11") == 0) { /* old key */
      cfg->x11 = parse_bool(val);
    } else if (strcmp(key, "tx11_extra_flags") == 0) {
      free(cfg->tx11_extra_flags);
      cfg->tx11_extra_flags = val[0] ? strdup(val) : NULL;
    } else if (strcmp(key, "enable_virgl") == 0) {
      cfg->virgl = parse_bool(val);
    } else if (strcmp(key, "virgl_extra_flags") == 0) {
      free(cfg->virgl_extra_flags);
      cfg->virgl_extra_flags = val[0] ? strdup(val) : NULL;
    } else if (strcmp(key, "enable_pulseaudio") == 0) {
      cfg->pulseaudio = parse_bool(val);
    } else if (strcmp(key, "enable_anland") == 0) {
      cfg->anland = parse_bool(val);
    } else if (strcmp(key, "selinux_permissive") == 0) {
      cfg->selinux_permissive = parse_bool(val);
    } else if (strcmp(key, "allow_sandboxing") == 0 ||
               strcmp(key, "allow_userns") == 0) { /* old key */
      cfg->sandboxing_allowed = parse_bool(val);
    } else if (strcmp(key, "allow_vts") == 0) {
      cfg->allow_vts = parse_bool(val);
    } else if (strcmp(key, "allow_swap") == 0) {
      cfg->allow_swap = parse_bool(val);
    } else if (strcmp(key, "volatile_mode") == 0) {
      cfg->volatile_mode = parse_bool(val);
    } else if (strcmp(key, "force_cgroupv1") == 0) {
      cfg->force_cgroupv1 = parse_bool(val);
    } else if (strcmp(key, "block_nested_ns") == 0) {
      /* Deadlock Shield was removed; swallow the key so old files shed it on
       * the next save instead of carrying it forever as an unknown line. */
    } else if (strcmp(key, "memory_limit") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->memory_limit = v;
      else
        ds_warn("config: ignoring invalid memory_limit '%s'", val);
    } else if (strcmp(key, "cpu_quota") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->cpu_quota = v;
      else
        ds_warn("config: ignoring invalid cpu_quota '%s'", val);
    } else if (strcmp(key, "cpu_period") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->cpu_period = v;
      else
        ds_warn("config: ignoring invalid cpu_period '%s'", val);
    } else if (strcmp(key, "pids_limit") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->pids_limit = v;
      else
        ds_warn("config: ignoring invalid pids_limit '%s'", val);
    } else if (strcmp(key, "privileged") == 0) {
      if (parse_privileged(val, cfg) < 0)
        ds_warn("config: privileged mode off until the tag is fixed");
    } else if (strcmp(key, "custom_init") == 0) {
      if (val[0] != '/')
        ds_warn("config: ignoring non-absolute custom_init path '%s'", val);
      else if (strchr(val, ' '))
        ds_warn("config: ignoring custom_init path with spaces '%s'", val);
      else
        safe_strncpy(cfg->custom_init, val, sizeof(cfg->custom_init));
    } else if (strcmp(key, "bind_mounts") == 0) {
      parse_bind_mounts(val, cfg);
    } else if (strcmp(key, "dns_servers") == 0) {
      safe_strncpy(cfg->dns_servers, val, sizeof(cfg->dns_servers));
    } else if (strcmp(key, "foreground") == 0) {
      cfg->foreground = parse_bool(val);
    } else if (strcmp(key, "pidfile") == 0) {
    } else if (strcmp(key, "env_file") == 0) {
      if (strstr(val, "..") ||
          (val[0] == '/' && !is_subpath(get_workspace_dir(), val))) {
        continue;
      }
      safe_strncpy(cfg->env_file, val, sizeof(cfg->env_file));
    } else if (strcmp(key, "uuid") == 0) {
      safe_strncpy(cfg->uuid, val, sizeof(cfg->uuid));
    } else if (strcmp(key, "static_nat_ip") == 0) {
      /* Validate on load - reject obviously malformed values stored by older
       * builds or hand-edited configs so we never boot with a garbage IP. */
      char _errbuf[128];
      if (val[0] &&
          ds_net_validate_static_ip(val, _errbuf, sizeof(_errbuf)) == 0)
        safe_strncpy(cfg->static_nat_ip, val, sizeof(cfg->static_nat_ip));
      else if (val[0])
        ds_warn("config: ignoring invalid static_nat_ip '%s': %s", val,
                _errbuf);
    } else if (strcmp(key, "net_mode") == 0) {
      int mode = ds_parse_net_mode(val);
      if (mode < 0) {
        ds_warn(
            "Unknown network mode '%s' in config file. Defaulting to 'nat'.",
            val);
        mode = DS_NET_NAT;
      }
      cfg->net_mode = (enum ds_net_mode)mode;
    } else if (strcmp(key, "gateway_container") == 0) {
      if (validate_container_name(val))
        safe_strncpy(cfg->gateway_container, val,
                     sizeof(cfg->gateway_container));
      else
        ds_warn("config: ignoring invalid gateway_container '%s'", val);
    } else if (strcmp(key, "gateway_net") == 0) {
      safe_strncpy(cfg->gateway_net, val, sizeof(cfg->gateway_net));
    } else if (strcmp(key, "gateway_bridge") == 0) {
      if (strlen(val) < IFNAMSIZ)
        safe_strncpy(cfg->gateway_bridge, val, sizeof(cfg->gateway_bridge));
      else
        ds_warn("config: ignoring too-long gateway_bridge '%s'", val);
    } else if (strcmp(key, "gateway_lan_ifname") == 0) {
      if (strlen(val) < IFNAMSIZ)
        safe_strncpy(cfg->gateway_lan_ifname, val,
                     sizeof(cfg->gateway_lan_ifname));
      else
        ds_warn("config: ignoring too-long gateway_lan_ifname '%s'", val);
    } else if (strcmp(key, "macvlan_parent") == 0) {
      if (strlen(val) < IFNAMSIZ)
        safe_strncpy(cfg->macvlan_parent, val, sizeof(cfg->macvlan_parent));
      else
        ds_warn("config: ignoring too-long macvlan_parent '%s'", val);
    } else if (strcmp(key, "macvlan_mode") == 0) {
      cfg->macvlan_mode = ds_parse_macvlan_mode(val);
      if (!cfg->macvlan_mode)
        ds_warn("config: unknown macvlan_mode '%s' - using bridge", val);
    } else if (strcmp(key, "upstream_interfaces") == 0) {
      /* Comma-separated interface names/wildcards, e.g. "wlan0,rmnet*".  When
       * present this pins NAT WAN to these interfaces in priority order and
       * disables automatic uplink detection. */
      if (ds_parse_iface_csv(val, cfg->upstream_ifaces,
                             &cfg->upstream_iface_count,
                             DS_MAX_UPSTREAM_IFACES) > 0)
        ds_warn("config: too many upstream_interfaces (max %d) - extra entries "
                "ignored",
                DS_MAX_UPSTREAM_IFACES);
    } else if (strcmp(key, "port_forwards") == 0) {
      /* HOST[:CONTAINER][/proto] list, each side PORT or START-END */
      char copy[1024], err[128];
      safe_strncpy(copy, val, sizeof(copy));
      char *pf_saveptr;
      for (char *tok = strtok_r(copy, ",", &pf_saveptr); tok;
           tok = strtok_r(NULL, ",", &pf_saveptr))
        if (ds_parse_port_forward(tok, cfg, err, sizeof(err)) < 0)
          ds_warn("config: %s in port_forwards - skipping", err);
    } else {
      /* Unknown key - preserve verbatim so Android App metadata
       * (run_at_boot, use_sparse_image, sparse_image_size_gb, etc.)
       * survives ds_config_save() unchanged. */
      add_unknown_line(cfg, line);
    }
  }

  fclose(f);
  return 0;
}

/* Internal helper to add a raw line to the unknown list */
static void add_unknown_line(struct ds_config *cfg, const char *line) {
  struct ds_config_line *node = malloc(sizeof(*node));
  if (!node)
    return;
  safe_strncpy(node->line, line, sizeof(node->line));
  node->next = NULL;
  if (!cfg->unknown_head) {
    cfg->unknown_head = cfg->unknown_tail = node;
  } else {
    cfg->unknown_tail->next = node;
    cfg->unknown_tail = node;
  }
}

void free_config_unknown_lines(struct ds_config *cfg) {
  struct ds_config_line *curr = cfg->unknown_head;
  while (curr) {
    struct ds_config_line *next = curr->next;
    free(curr);
    curr = next;
  }
  cfg->unknown_head = cfg->unknown_tail = NULL;
}

char *ds_config_auto_path(const char *rootfs_path) {
  if (!rootfs_path || rootfs_path[0] == '\0')
    return NULL;

  char temp[PATH_MAX];
  safe_strncpy(temp, rootfs_path, sizeof(temp));

  char *dir = dirname(temp);
  char *final_path = malloc(PATH_MAX);
  if (final_path) {
    if (strcmp(dir, "/") == 0)
      snprintf(final_path, PATH_MAX, "/container.config");
    else
      snprintf(final_path, PATH_MAX, "%s/container.config", dir);
  }

  return final_path;
}

int ds_config_load_by_name(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;
  if (!validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path),
           "%s/Containers/%s/container.config", get_workspace_dir(), safe_name);

  /* Single source of truth: if the container is running, load the immutable
   * snapshot it actually booted with (/run/droidspaces/container.config inside
   * its mount ns) so host-side edits to container.config while it runs never
   * desync stop/cleanup/info. Keep config_file pointing at the workspace copy
   * so later saves still target the host file. Fall back to the workspace copy
   * when not running or the snapshot is unreadable. */
  pid_t pid = find_container_by_name(name);
  if (pid > 0) {
    char run_path[PATH_MAX];
    if (build_proc_root_path(pid, "/run/droidspaces/container.config", run_path,
                             sizeof(run_path)) == 0 &&
        access(run_path, F_OK) == 0 && ds_config_load(run_path, cfg) == 0) {
      safe_strncpy(cfg->config_file, config_path, sizeof(cfg->config_file));
      return 0;
    }
  }

  return ds_config_load(config_path, cfg);
}

/* See the peek level in ds_log_internal. */
static int peek(int (*load)(const char *, struct ds_config *), const char *arg,
                struct ds_config *cfg) {
  int was = ds_log_silent;
  ds_log_silent = DS_LOG_PEEK;
  int ret = load(arg, cfg);
  ds_log_silent = was;
  return ret;
}

int ds_config_peek(const char *name, struct ds_config *cfg) {
  return peek(ds_config_load_by_name, name, cfg);
}

int ds_config_peek_path(const char *config_path, struct ds_config *cfg) {
  return peek(ds_config_load, config_path, cfg);
}
