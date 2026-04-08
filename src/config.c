/*
 * Droidspaces v5 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Forward declarations */
static void add_unknown_line(struct ds_config *cfg, const char *line);
/* ds_net_validate_static_ip is defined in network.c - declared in droidspace.h
 */
#include <libgen.h>

/* ---------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------*/

static char *trim_whitespace(char *str) {
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;

  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;

  *(end + 1) = 0;
  return str;
}

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

static void parse_bind_mounts(const char *value, struct ds_config *cfg) {
  if (!value)
    return;

  char copy[4096];
  safe_strncpy(copy, value, sizeof(copy));

  char *saveptr;
  char *token = strtok_r(copy, ",", &saveptr);

  while (token) {
    char *sep = strchr(token, ':');
    if (sep) {
      *sep = '\0';
      const char *src = trim_whitespace(token);
      const char *dest = trim_whitespace(sep + 1);

      /* Use proper allocation function instead of direct array access */
      ds_config_add_bind(cfg, src, dest);
    }
    token = strtok_r(NULL, ",", &saveptr);
  }
}

int ds_config_add_bind(struct ds_config *cfg, const char *src,
                       const char *dest) {
  if (!src || !dest || src[0] == '\0' || dest[0] == '\0')
    return 0;

  /* Check for duplication */
  for (int i = 0; i < cfg->bind_count; i++) {
    if (strcmp(cfg->binds[i].src, src) == 0 &&
        strcmp(cfg->binds[i].dest, dest) == 0) {
      return 0; /* Already exists, skip */
    }
  }

  /* Grow the array if needed */
  if (cfg->bind_count >= cfg->bind_capacity) {
    int old_cap = cfg->bind_capacity;
    int new_cap;

    if (old_cap == 0) {
      new_cap = DS_BIND_INITIAL_CAP;
    } else {
      /* Check for integer overflow */
      if (old_cap > INT_MAX / 2)
        return -1;
      new_cap = old_cap * 2;
    }

    /* Check allocation size won't overflow */
    size_t alloc_size = (size_t)new_cap * sizeof(*cfg->binds);
    if (alloc_size / sizeof(*cfg->binds) != (size_t)new_cap)
      return -1;

    struct ds_bind_mount *new_binds = realloc(cfg->binds, alloc_size);
    if (!new_binds)
      return -1;

    /* Zero the newly allocated portion */
    memset(new_binds + old_cap, 0,
           (size_t)(new_cap - old_cap) * sizeof(*new_binds));

    cfg->binds = new_binds;
    cfg->bind_capacity = new_cap;
  }

  safe_strncpy(cfg->binds[cfg->bind_count].src, src,
               sizeof(cfg->binds[cfg->bind_count].src));
  safe_strncpy(cfg->binds[cfg->bind_count].dest, dest,
               sizeof(cfg->binds[cfg->bind_count].dest));
  cfg->bind_count++;
  return 1;
}

/*
 * IMPORTANT: free_config_binds must NOT free unknown lines.
 * The --reset path in main.c saves unknown_head/tail pointers, calls this
 * function, then memset's the struct, then restores the saved pointers.
 * If we free unknown lines here, the restored pointers dangle → SIGSEGV.
 *
 * Unknown lines are freed separately via free_config_unknown_lines().
 */

void free_config_binds(struct ds_config *cfg) {

  if (!cfg->binds)
    return;
  free(cfg->binds);
  cfg->binds = NULL;
  cfg->bind_count = 0;
  cfg->bind_capacity = 0;
}

/* ---------------------------------------------------------------------------
 * Core Implementation
 * ---------------------------------------------------------------------------*/

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
      safe_strncpy(cfg->container_name, val, sizeof(cfg->container_name));
    } else if (strcmp(key, "hostname") == 0) {
      safe_strncpy(cfg->hostname, val, sizeof(cfg->hostname));
    } else if (strcmp(key, "rootfs_path") == 0) {
      if (strstr(val, ".img")) {
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
    } else if (strcmp(key, "enable_termux_x11") == 0) {
      cfg->termux_x11 = parse_bool(val);
    } else if (strcmp(key, "selinux_permissive") == 0) {
      cfg->selinux_permissive = parse_bool(val);
    } else if (strcmp(key, "volatile_mode") == 0) {
      cfg->volatile_mode = parse_bool(val);
    } else if (strcmp(key, "force_cgroupv1") == 0) {
      cfg->force_cgroupv1 = parse_bool(val);
    } else if (strcmp(key, "block_nested_ns") == 0) {
      cfg->block_nested_ns = parse_bool(val);
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
      if (strcmp(val, "nat") == 0) {
        cfg->net_mode = DS_NET_NAT;
      } else if (strcmp(val, "none") == 0) {
        cfg->net_mode = DS_NET_NONE;
      } else if (strcmp(val, "host") == 0) {
        cfg->net_mode = DS_NET_HOST;
      } else {
        ds_warn(
            "Unknown network mode '%s' in config file. Defaulting to 'host'.",
            val);
        cfg->net_mode = DS_NET_HOST;
      }
    } else if (strcmp(key, "upstream_interfaces") == 0) {
      /* Comma-separated interface names, e.g. "wlan0,rmnet0,ccmni1" */
      char copy[1024];
      safe_strncpy(copy, val, sizeof(copy));
      char *up_saveptr;
      char *up_tok = strtok_r(copy, ",", &up_saveptr);
      while (up_tok && cfg->upstream_iface_count < DS_MAX_UPSTREAM_IFACES) {
        while (*up_tok == ' ' || *up_tok == '\t')
          up_tok++;
        char *up_end = up_tok + strlen(up_tok) - 1;
        while (up_end > up_tok && (*up_end == ' ' || *up_end == '\t'))
          *up_end-- = '\0';
        if (up_tok[0] && strlen(up_tok) < IFNAMSIZ) {
          int dup = 0;
          for (int i = 0; i < cfg->upstream_iface_count; i++) {
            if (strcmp(cfg->upstream_ifaces[i], up_tok) == 0) {
              dup = 1;
              break;
            }
          }
          if (!dup) {
            safe_strncpy(cfg->upstream_ifaces[cfg->upstream_iface_count++],
                         up_tok, IFNAMSIZ);
          }
        }
        up_tok = strtok_r(NULL, ",", &up_saveptr);
      }
      if (up_tok)
        ds_warn("config: too many upstream_interfaces (max %d) - extra entries "
                "ignored",
                DS_MAX_UPSTREAM_IFACES);
    } else if (strcmp(key, "port_forwards") == 0) {
      /* Comma-separated HOST:CONTAINER[/proto], supporting both single ports
       * and ranges.  Accepted formats:
       *   22:22/tcp          single port, explicit proto
       *   8096:8096          single port, default tcp
       *   1-500:1-500/tcp    range, both sides must have equal width
       *   1-500              symmetric range shorthand (host == container)
       */
      char copy[1024];
      safe_strncpy(copy, val, sizeof(copy));
      char *pf_saveptr;
      char *pf_tok = strtok_r(copy, ",", &pf_saveptr);
      while (pf_tok && cfg->port_forward_count < DS_MAX_PORT_FORWARDS) {
        while (*pf_tok == ' ' || *pf_tok == '\t')
          pf_tok++;

        struct ds_port_forward *pf =
            &cfg->port_forwards[cfg->port_forward_count];
        memset(pf, 0, sizeof(*pf));
        strncpy(pf->proto, "tcp", sizeof(pf->proto));

        /* Strip optional /proto suffix */
        char *slash = strchr(pf_tok, '/');
        if (slash) {
          *slash = '\0';
          strncpy(pf->proto, slash + 1, sizeof(pf->proto) - 1);
          pf->proto[sizeof(pf->proto) - 1] = '\0';
        }

        /* Split HOST_SIDE:CONTAINER_SIDE.
         * No colon → symmetric: both sides are the same spec. */
        char *host_side = pf_tok;
        char *cont_side = pf_tok; /* symmetric default */
        char *colon = strchr(pf_tok, ':');
        if (colon) {
          *colon = '\0';
          cont_side = colon + 1;
        }

        /* Parse a single "PORT" or "START-END" spec into (port, port_end).
         * Returns 1 on success, 0 on parse/range error. */
        int valid = 1;

        /* Host side */
        {
          char *dash = strchr(host_side, '-');
          if (dash) {
            int a = atoi(host_side), b = atoi(dash + 1);
            if (a <= 0 || a > 65535 || b < a || b > 65535) {
              ds_warn("config: invalid host port range '%s' - skipping",
                      host_side);
              valid = 0;
            } else {
              pf->host_port = (uint16_t)a;
              pf->host_port_end = (uint16_t)b;
            }
          } else {
            int p = atoi(host_side);
            if (p <= 0 || p > 65535) {
              ds_warn("config: invalid host port '%s' - skipping", host_side);
              valid = 0;
            } else {
              pf->host_port = (uint16_t)p;
              pf->host_port_end = 0;
            }
          }
        }

        /* Container side */
        if (valid) {
          char *dash = strchr(cont_side, '-');
          if (dash) {
            int a = atoi(cont_side), b = atoi(dash + 1);
            if (a <= 0 || a > 65535 || b < a || b > 65535) {
              ds_warn("config: invalid container port range '%s' - skipping",
                      cont_side);
              valid = 0;
            } else {
              pf->container_port = (uint16_t)a;
              pf->container_port_end = (uint16_t)b;
            }
          } else {
            int p = atoi(cont_side);
            if (p <= 0 || p > 65535) {
              ds_warn("config: invalid container port '%s' - skipping",
                      cont_side);
              valid = 0;
            } else {
              pf->container_port = (uint16_t)p;
              pf->container_port_end = 0;
            }
          }
        }

        /* Both sides must span the same number of ports */
        if (valid) {
          int hw = pf->host_port_end ? (pf->host_port_end - pf->host_port) : 0;
          int cw = pf->container_port_end
                       ? (pf->container_port_end - pf->container_port)
                       : 0;
          if (hw != cw) {
            ds_warn("config: port_forwards range width mismatch "
                    "(host %d ports vs container %d ports) - skipping",
                    hw + 1, cw + 1);
            valid = 0;
          }
        }

        /* Overlap check - reject if host OR container ranges intersect
         * with any existing rule of the same protocol. */
        if (valid) {
          int skip = 0;
          for (int i = 0; i < cfg->port_forward_count; i++) {
            struct ds_port_forward *ex = &cfg->port_forwards[i];
            if (strcmp(ex->proto, pf->proto) != 0)
              continue;

            /* Exact duplicate - silently skip */
            if (pf->host_port == ex->host_port &&
                pf->host_port_end == ex->host_port_end &&
                pf->container_port == ex->container_port &&
                pf->container_port_end == ex->container_port_end) {
              skip = 1;
              break;
            }

            /* Host-side overlap */
            uint16_t hs1 = pf->host_port, he1 = pf->host_port_end
                                                    ? pf->host_port_end
                                                    : pf->host_port;
            uint16_t hs2 = ex->host_port, he2 = ex->host_port_end
                                                    ? ex->host_port_end
                                                    : ex->host_port;
            int host_overlap = (hs1 <= he2 && hs2 <= he1);

            /* Container-side overlap */
            uint16_t cs1 = pf->container_port,
                     ce1 = pf->container_port_end ? pf->container_port_end
                                                  : pf->container_port;
            uint16_t cs2 = ex->container_port,
                     ce2 = ex->container_port_end ? ex->container_port_end
                                                  : ex->container_port;
            int cont_overlap = (cs1 <= ce2 && cs2 <= ce1);

            if (host_overlap || cont_overlap) {
              ds_warn("config: port_forwards overlap detected (%s side) "
                      "- skipping",
                      host_overlap ? "host" : "container");
              skip = 1;
              break;
            }
          }
          if (!skip)
            cfg->port_forward_count++;
        }

        pf_tok = strtok_r(NULL, ",", &pf_saveptr);
      }
      if (pf_tok)
        ds_warn(
            "config: too many port_forwards (max %d) - extra entries ignored",
            DS_MAX_PORT_FORWARDS);
    } else if (strcmp(key, "allow_user_ns") == 0) {
      cfg->allow_user_ns = parse_bool(val);
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

int ds_config_save(const char *config_path, struct ds_config *cfg) {
  /* Sort bind mounts before saving so they are persisted in a sane order. */
  sort_bind_mounts(cfg);

  char temp_path[PATH_MAX];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", config_path);

  /* Step 1: Skip Step 1 - we now use the in-memory preservation from
   * ds_config_load. This ensures mirroring and internal backups preserve all
   * metadata. */

  /* Step 2: Write all configurations to temporary file */
  FILE *f_out = fopen(temp_path, "we");
  if (!f_out)
    return -1;

  fprintf(f_out, "# Droidspaces Container Configuration\n");
  fprintf(f_out, "# Generated automatically - Changes may be overwritten\n\n");

  /* Write managed keys */
  if (cfg->container_name[0])
    fprintf(f_out, "name=%s\n", cfg->container_name);
  if (cfg->hostname[0])
    fprintf(f_out, "hostname=%s\n", cfg->hostname);

  if (cfg->is_img_mount && cfg->rootfs_img_path[0]) {
    char *abs_path = ds_resolve_path_arg(cfg->rootfs_img_path);
    fprintf(f_out, "rootfs_path=%s\n",
            abs_path ? abs_path : cfg->rootfs_img_path);
    free(abs_path);
  } else if (cfg->rootfs_path[0]) {
    char *abs_path = ds_resolve_path_arg(cfg->rootfs_path);
    fprintf(f_out, "rootfs_path=%s\n", abs_path ? abs_path : cfg->rootfs_path);
    free(abs_path);
  }

  fprintf(f_out, "disable_ipv6=%d\n", cfg->disable_ipv6);
  if (is_android()) {
    fprintf(f_out, "enable_android_storage=%d\n", cfg->android_storage);
    fprintf(f_out, "enable_termux_x11=%d\n", cfg->termux_x11);
  }
  fprintf(f_out, "enable_hw_access=%d\n", cfg->hw_access);
  fprintf(f_out, "selinux_permissive=%d\n", cfg->selinux_permissive);
  fprintf(f_out, "volatile_mode=%d\n", cfg->volatile_mode);
  fprintf(f_out, "force_cgroupv1=%d\n", cfg->force_cgroupv1);
  fprintf(f_out, "block_nested_ns=%d\n", cfg->block_nested_ns);
  fprintf(f_out, "allow_user_ns=%d\n", cfg->allow_user_ns);
  fprintf(f_out, "foreground=%d\n", cfg->foreground);

  if (cfg->net_mode == DS_NET_NAT) {
    fprintf(f_out, "net_mode=nat\n");
  } else if (cfg->net_mode == DS_NET_NONE) {
    fprintf(f_out, "net_mode=none\n");
  } else {
    fprintf(f_out, "net_mode=host\n");
  }

  /* NAT-mode extras: upstream interfaces and port forwards */
  if (cfg->net_mode == DS_NET_NAT && cfg->upstream_iface_count > 0) {
    fprintf(f_out, "upstream_interfaces=");
    for (int i = 0; i < cfg->upstream_iface_count; i++) {
      fprintf(f_out, "%s%s", cfg->upstream_ifaces[i],
              (i < cfg->upstream_iface_count - 1) ? "," : "");
    }
    fprintf(f_out, "\n");
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->port_forward_count > 0) {
    fprintf(f_out, "port_forwards=");
    for (int i = 0; i < cfg->port_forward_count; i++) {
      const struct ds_port_forward *pf = &cfg->port_forwards[i];
      if (pf->host_port_end) {
        /* Range mapping: START-END:START-END/proto */
        fprintf(f_out, "%u-%u:%u-%u/%s", pf->host_port, pf->host_port_end,
                pf->container_port, pf->container_port_end, pf->proto);
      } else {
        /* Single port: HOST:CONTAINER/proto */
        fprintf(f_out, "%u:%u/%s", pf->host_port, pf->container_port,
                pf->proto);
      }
      if (i < cfg->port_forward_count - 1)
        fprintf(f_out, ",");
    }
    fprintf(f_out, "\n");
  }

  /* Persist the resolved static NAT IP so every subsequent boot reuses
   * the same address.  Written for all NAT containers (user-supplied or
   * auto-assigned); skipped for host/none modes where it's irrelevant. */
  if (cfg->net_mode == DS_NET_NAT && cfg->static_nat_ip[0])
    fprintf(f_out, "static_nat_ip=%s\n", cfg->static_nat_ip);

  if (cfg->env_file[0])
    fprintf(f_out, "env_file=%s\n", cfg->env_file);
  if (cfg->uuid[0])
    fprintf(f_out, "uuid=%s\n", cfg->uuid);

  if (cfg->dns_servers[0])
    fprintf(f_out, "dns_servers=%s\n", cfg->dns_servers);

  if (cfg->bind_count > 0) {
    fprintf(f_out, "bind_mounts=");
    for (int i = 0; i < cfg->bind_count; i++) {
      fprintf(f_out, "%s:%s%s", cfg->binds[i].src, cfg->binds[i].dest,
              (i < cfg->bind_count - 1) ? "," : "");
    }
    fprintf(f_out, "\n");
  }

  /* Step 3: Append preserved keys (Android App Config) from memory */
  if (cfg->unknown_head) {
    fprintf(f_out, "\n# Android App Configuration\n");
    struct ds_config_line *node = cfg->unknown_head;
    while (node) {
      fprintf(f_out, "%s", node->line);
      node = node->next;
    }
  }

  fclose(f_out);

  /* Step 4: Atomic rename commit */
  if (rename(temp_path, config_path) < 0) {
    unlink(temp_path);
    return -1;
  }

  if (!cfg->config_file_existed) {
    cfg->config_file_existed = 1;
  }
  return 0;
}

int ds_config_validate(struct ds_config *cfg) {
  int errors = 0;

  if (cfg->rootfs_path[0] && cfg->rootfs_img_path[0])
    errors++;
  if (!cfg->container_name[0])
    errors++;
  if (!cfg->rootfs_path[0] && !cfg->rootfs_img_path[0])
    errors++;

  /* Existence checks */
  if (cfg->rootfs_path[0] && access(cfg->rootfs_path, F_OK) != 0)
    errors++;
  if (cfg->rootfs_img_path[0] && access(cfg->rootfs_img_path, F_OK) != 0)
    errors++;

  /* Image mode requires a name for the mount point */
  if (cfg->rootfs_img_path[0] && !cfg->container_name[0])
    errors++;

  return (errors > 0) ? -1 : 0;
}

char *ds_config_auto_path(const char *rootfs_path) {
  if (!rootfs_path || rootfs_path[0] == '\0')
    return NULL;

  char temp[PATH_MAX];
  safe_strncpy(temp, rootfs_path, sizeof(temp));

  char *dir = dirname(temp);
  char *final_path = malloc(PATH_MAX);
  if (final_path) {
    snprintf(final_path, PATH_MAX, "%s/container.config", dir);
  }

  return final_path;
}

int ds_config_load_by_name(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path),
           "%s/Containers/%s/container.config", get_workspace_dir(), safe_name);

  return ds_config_load(config_path, cfg);
}

int ds_config_save_by_name(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  char container_dir[PATH_MAX];
  snprintf(container_dir, sizeof(container_dir), "%s/Containers/%s",
           get_workspace_dir(), safe_name);
  mkdir_p(container_dir, 0755);

  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%.3800s/container.config",
           container_dir);

  return ds_config_save(config_path, cfg);
}

void apply_reset_config(struct ds_config *cfg, int cli_net_mode_set,
                        enum ds_net_mode cli_net_mode) {
  char save_name[256], save_rootfs[PATH_MAX], save_img[PATH_MAX];
  char save_config[PATH_MAX], save_prog[64], save_uuid[64];
  int save_is_img = cfg->is_img_mount;
  int save_specified = cfg->config_file_specified;
  int save_existed = cfg->config_file_existed;
  struct ds_config_line *save_head = cfg->unknown_head;
  struct ds_config_line *save_tail = cfg->unknown_tail;
  int save_block_nested_ns = cfg->block_nested_ns;
  int save_allow_user_ns = cfg->allow_user_ns;

  safe_strncpy(save_name, cfg->container_name, sizeof(save_name));
  safe_strncpy(save_rootfs, cfg->rootfs_path, sizeof(save_rootfs));
  safe_strncpy(save_img, cfg->rootfs_img_path, sizeof(save_img));
  safe_strncpy(save_config, cfg->config_file, sizeof(save_config));
  safe_strncpy(save_prog, cfg->prog_name, sizeof(save_prog));
  safe_strncpy(save_uuid, cfg->uuid, sizeof(save_uuid));

  free_config_env_vars(cfg);
  free_config_binds(cfg);
  memset(cfg, 0, sizeof(*cfg));

  cfg->net_ready_pipe[0] = cfg->net_ready_pipe[1] = -1;
  cfg->net_done_pipe[0] = cfg->net_done_pipe[1] = -1;

  safe_strncpy(cfg->container_name, save_name, sizeof(cfg->container_name));
  safe_strncpy(cfg->rootfs_path, save_rootfs, sizeof(cfg->rootfs_path));
  safe_strncpy(cfg->rootfs_img_path, save_img, sizeof(cfg->rootfs_img_path));
  safe_strncpy(cfg->config_file, save_config, sizeof(cfg->config_file));
  safe_strncpy(cfg->prog_name, save_prog, sizeof(cfg->prog_name));
  safe_strncpy(cfg->uuid, save_uuid, sizeof(cfg->uuid));
  cfg->is_img_mount = save_is_img;
  cfg->config_file_specified = save_specified;
  cfg->config_file_existed = save_existed;
  cfg->unknown_head = save_head;
  cfg->unknown_tail = save_tail;
  cfg->block_nested_ns = save_block_nested_ns;
  cfg->allow_user_ns = save_allow_user_ns;

  if (cli_net_mode_set)
    cfg->net_mode = cli_net_mode;

  ds_config_save_by_name(cfg->container_name, cfg);
}
