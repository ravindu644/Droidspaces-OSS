/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

int ds_parse_net_mode(const char *s) {
  if (strcmp(s, "nat") == 0)
    return DS_NET_NAT;
  if (strcmp(s, "none") == 0)
    return DS_NET_NONE;
  if (strcmp(s, "host") == 0)
    return DS_NET_HOST;
  if (strcmp(s, "gateway") == 0 || strcmp(s, "delegated-gateway") == 0)
    return DS_NET_GATEWAY;
  if (strcmp(s, "macvlan") == 0)
    return DS_NET_MACVLAN;
  return -1;
}

/* The config-file spelling of a mode, the inverse of ds_parse_net_mode. */
const char *ds_net_mode_name(enum ds_net_mode mode) {
  static const char *const names[] = {
      [DS_NET_NAT] = "nat",         [DS_NET_HOST] = "host",
      [DS_NET_NONE] = "none",       [DS_NET_GATEWAY] = "gateway",
      [DS_NET_MACVLAN] = "macvlan",
  };
  return mode >= DS_NET_NAT && mode <= DS_NET_MACVLAN ? names[mode] : "host";
}

static const struct {
  const char *name;
  uint32_t mode;
} k_macvlan_modes[] = {
    {"bridge", MACVLAN_MODE_BRIDGE},
    {"private", MACVLAN_MODE_PRIVATE},
    {"vepa", MACVLAN_MODE_VEPA},
    {"passthru", MACVLAN_MODE_PASSTHRU},
};

uint32_t ds_parse_macvlan_mode(const char *s) {
  for (size_t i = 0; i < sizeof(k_macvlan_modes) / sizeof(k_macvlan_modes[0]);
       i++)
    if (strcmp(s, k_macvlan_modes[i].name) == 0)
      return k_macvlan_modes[i].mode;
  return 0;
}

const char *ds_macvlan_mode_name(uint32_t mode) {
  for (size_t i = 0; i < sizeof(k_macvlan_modes) / sizeof(k_macvlan_modes[0]);
       i++)
    if (k_macvlan_modes[i].mode == mode)
      return k_macvlan_modes[i].name;
  return "bridge";
}

char *trim_whitespace(char *str) {
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

static const struct {
  int bit;
  const char *name;
} priv_bits[] = {
    {DS_PRIV_NOMASK, "nomask"},   {DS_PRIV_NOCAPS, "nocaps"},
    {DS_PRIV_NOSEC, "noseccomp"}, {DS_PRIV_SHARED, "shared"},
    {DS_PRIV_FULL, "full"},
};

/* 0 with the mask filled, or -1 with it empty when a tag is not one of ours:
 * a typo in the flag that removes security barriers must leave them all up.
 * The CLI refuses the start on that; a config file boots confined and keeps
 * the value it had, so the warning repeats until it is fixed. */
int parse_privileged(const char *value, struct ds_config *cfg) {
  const size_t n = sizeof(priv_bits) / sizeof(priv_bits[0]);
  cfg->privileged_mask = 0;
  cfg->privileged_bad[0] = '\0';
  if (!value)
    return 0;

  char copy[1024];
  safe_strncpy(copy, value, sizeof(copy));

  char *saveptr;
  for (char *tok = strtok_r(copy, ",", &saveptr); tok;
       tok = strtok_r(NULL, ",", &saveptr)) {
    const char *t = trim_whitespace(tok);
    size_t i = 0;
    while (i < n && strcasecmp(t, priv_bits[i].name) != 0)
      i++;
    if (i == n) {
      ds_warn("Unknown privileged tag '%s' (nomask, nocaps, noseccomp, shared, "
              "full)",
              t);
      cfg->privileged_mask = 0;
      safe_strncpy(cfg->privileged_bad, value, sizeof(cfg->privileged_bad));
      return -1;
    }
    cfg->privileged_mask |= priv_bits[i].bit;
  }
  return 0;
}

/* "full", or the set tags joined by sep, into buf. */
void ds_privileged_names(int mask, char *buf, size_t size, const char *sep) {
  size_t pos = 0;
  buf[0] = '\0';
  if (mask == DS_PRIV_FULL) {
    safe_strncpy(buf, "full", size);
    return;
  }
  for (size_t i = 0; i < sizeof(priv_bits) / sizeof(priv_bits[0]) && pos < size;
       i++)
    if (priv_bits[i].bit != DS_PRIV_FULL && (mask & priv_bits[i].bit))
      pos += (size_t)snprintf(buf + pos, size - pos, "%s%s", pos ? sep : "",
                              priv_bits[i].name);
}

int ds_parse_bind_spec(char *spec, struct ds_config *cfg, char *err,
                       size_t err_size) {
  char *dest = strchr(spec, ':');
  if (!dest) {
    snprintf(err, err_size,
             "Invalid bind mount format: %s (expected SRC:DEST[:ro])", spec);
    return -1;
  }
  *dest++ = '\0';
  char *flag = strchr(dest, ':');
  if (flag)
    *flag++ = '\0';
  int ro = flag && strcmp(trim_whitespace(flag), "ro") == 0;
  const char *src = trim_whitespace(spec);
  dest = trim_whitespace(dest);

  /* A source must already be absolute: the CLI's arrive that way from
   * ds_resolve_argv_paths, and a config file is read by a root daemon whose
   * home and cwd are not the user's, so nothing is resolved here. */
  if (src[0] != '/') {
    snprintf(err, err_size, "Bind source must be an absolute path: %s", src);
    return -1;
  }
  if (!validate_bind_destination(dest)) {
    snprintf(err, err_size,
             "Unsafe bind destination '%s': it must be an absolute path inside "
             "the container, not / itself, with no '.', '..' or control "
             "characters",
             dest);
    return -1;
  }
  if (ds_config_add_bind(cfg, src, dest, ro) < 0) {
    snprintf(err, err_size, "Cannot add bind mount %s:%s", src, dest);
    return -1;
  }
  return 0;
}

int ds_config_add_bind(struct ds_config *cfg, const char *src, const char *dest,
                       int ro) {
  if (!src || !dest || src[0] == '\0' || dest[0] == '\0')
    return 0;
  /* Defensive: callers must pre-validate; this is a last-resort assert */
  if (!validate_bind_destination(dest))
    return -1;

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
  cfg->binds[cfg->bind_count].ro = ro;
  cfg->bind_count++;
  return 1;
}

/*
 * IMPORTANT: free_config_binds must NOT free unknown lines.
 * ds_config_reset_defaults (config/config.c), used by --reset and by
 * restart's reload, saves the unknown_head/tail pointers, calls this
 * function, re-inits the struct (ds_config_init), then restores them.
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

/* parse_env_file_to_config() - load cfg->env_file into cfg->env_vars,
 * replacing what was there. Call while host paths are still reachable: before
 * pivot_root on boot, before setns on enter and run. Supports unlimited line
 * length and variable count via dynamic allocation. */
void parse_env_file_to_config(struct ds_config *cfg) {
  const char *path = cfg->env_file;
  if (path[0] == '\0')
    return;
  free_config_env_vars(cfg);

  FILE *f = fopen(path, "re");
  if (!f) {
    /* Non-fatal: env file missing is fine, container still boots */
    if (errno != ENOENT) {
      ds_warn("Failed to open env file: %s (%s)", path, strerror(errno));
    }
    return;
  }

  /* The progress lines go to the log file only; warnings about bad lines
   * still reach the terminal. */
  int prev_silent = ds_log_silent;
  ds_log_silent = 1;
  ds_log("Parsing environment file: %s", path);

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  int line_num = 0;
  int failed_count = 0;

  while ((linelen = getline(&line, &linecap, f)) > 0) {
    line_num++;

    /* Strip trailing newline and carriage return */
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      line[--linelen] = '\0';
    }

    /* Skip empty lines */
    if (linelen == 0)
      continue;

    /* Skip comment lines and leading whitespace */
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == '\0')
      continue;

    /* Strip optional 'export ' prefix (written by ds_env_save) */
    if (strncmp(p, "export ", 7) == 0)
      p += 7;

    /* Find the '=' separator */
    char *eq = strchr(p, '=');
    if (!eq) {
      ds_warn("Env file line %d: no '=' found, skipping", line_num);
      failed_count++;
      continue;
    }

    /* Extract key */
    size_t key_len = (size_t)(eq - p);
    if (key_len == 0) {
      ds_warn("Env file line %d: empty key, skipping", line_num);
      failed_count++;
      continue;
    }

    char *key = malloc(key_len + 1);
    if (!key)
      break;
    memcpy(key, p, key_len);
    key[key_len] = '\0';

    /* Validate key: [A-Za-z_][A-Za-z0-9_]* */
    if (!isalpha((unsigned char)key[0]) && key[0] != '_') {
      ds_warn("Env file line %d: invalid key '%s' "
              "(must start with letter or _), skipping",
              line_num, key);
      free(key);
      failed_count++;
      continue;
    }
    int key_valid = 1;
    for (size_t i = 1; i < key_len; i++) {
      if (!isalnum((unsigned char)key[i]) && key[i] != '_') {
        key_valid = 0;
        break;
      }
    }
    if (!key_valid) {
      ds_warn("Env file line %d: invalid key '%s' "
              "(only [A-Za-z0-9_] allowed), skipping",
              line_num, key);
      free(key);
      failed_count++;
      continue;
    }

    /* Extract value - everything after '=' */
    char *val = eq + 1;
    size_t val_len = strlen(val);
    char *value = NULL;

    /* Strip surrounding quotes (single or double) */
    if (val_len >= 2 && ((val[0] == '"' && val[val_len - 1] == '"') ||
                         (val[0] == '\'' && val[val_len - 1] == '\'')))
      value = strndup(val + 1, val_len - 2);
    else
      value = strdup(val);

    if (!value) {
      free(key);
      break;
    }

    /* Store in config, growing if needed */
    if (cfg->env_var_count >= cfg->env_var_capacity) {
      int new_cap = cfg->env_var_capacity ? cfg->env_var_capacity * 2 : 16;
      struct ds_env_var *new_vars =
          realloc(cfg->env_vars, new_cap * sizeof(struct ds_env_var));
      if (!new_vars) {
        ds_error("Out of memory while parsing env file");
        failed_count++;
        free(key);
        free(value);
        break;
      }
      cfg->env_vars = new_vars;
      cfg->env_var_capacity = new_cap;
    }

    cfg->env_vars[cfg->env_var_count].key = key;
    cfg->env_vars[cfg->env_var_count].value = value;
    cfg->env_var_count++;
  }

  free(line);
  fclose(f);

  if (failed_count > 0) {
    ds_log("Loaded %d environment variable(s) (%d failed)", cfg->env_var_count,
           failed_count);
  } else {
    ds_log("Loaded %d environment variable(s)", cfg->env_var_count);
  }
  ds_log_silent = prev_silent;
}

void free_config_env_vars(struct ds_config *cfg) {
  if (!cfg || !cfg->env_vars)
    return;

  for (int i = 0; i < cfg->env_var_count; i++) {
    free(cfg->env_vars[i].key);
    free(cfg->env_vars[i].value);
  }
  free(cfg->env_vars);
  cfg->env_vars = NULL;
  cfg->env_var_count = 0;
  cfg->env_var_capacity = 0;
}

/* Core Implementation */

/* PORT or START-END. end stays 0 for a single port, which is how the rest of
 * the tree tells the two apart. */
static int parse_port_range(const char *s, uint16_t *start, uint16_t *end) {
  const char *dash = strchr(s, '-');
  int a = atoi(s), b = dash ? atoi(dash + 1) : 0;
  if (a <= 0 || a > 65535 || (dash && (b < a || b > 65535)))
    return -1;
  *start = (uint16_t)a;
  *end = (uint16_t)b;
  return 0;
}

static void fmt_port_range(char *buf, size_t size, uint16_t start,
                           uint16_t end) {
  if (end)
    snprintf(buf, size, "%u-%u", start, end);
  else
    snprintf(buf, size, "%u", start);
}

/* [s1,e1] and [s2,e2] intersect; a single port carries end 0. */
static int ports_overlap(uint16_t s1, uint16_t e1, uint16_t s2, uint16_t e2) {
  if (!e1)
    e1 = s1;
  if (!e2)
    e2 = s2;
  return s1 <= e2 && s2 <= e1;
}

int ds_parse_port_forward(char *spec, struct ds_config *cfg, char *err,
                          size_t err_size) {
  while (*spec == ' ' || *spec == '\t')
    spec++;

  struct ds_port_forward rule = {0}, *pf = &rule;
  const char *proto = "tcp";
  char *slash = strchr(spec, '/');
  if (slash) {
    *slash = '\0';
    proto = slash + 1;
    if (strcmp(proto, "tcp") != 0 && strcmp(proto, "udp") != 0) {
      snprintf(err, err_size, "Invalid protocol '%s' (use tcp or udp)", proto);
      return -1;
    }
  }
  safe_strncpy(pf->proto, proto, sizeof(pf->proto));

  /* HOST[:CONTAINER]; no colon means the same spec on both sides. */
  char *cont = spec, *colon = strchr(spec, ':');
  if (colon) {
    *colon = '\0';
    cont = colon + 1;
  }
  if (parse_port_range(spec, &pf->host_port, &pf->host_port_end) < 0) {
    snprintf(err, err_size, "Invalid host port '%s'", spec);
    return -1;
  }
  if (parse_port_range(cont, &pf->container_port, &pf->container_port_end) <
      0) {
    snprintf(err, err_size, "Invalid container port '%s'", cont);
    return -1;
  }
  int hw = pf->host_port_end ? pf->host_port_end - pf->host_port : 0;
  int cw =
      pf->container_port_end ? pf->container_port_end - pf->container_port : 0;
  if (hw != cw) {
    snprintf(err, err_size,
             "Port range width mismatch (host %d ports, container %d)", hw + 1,
             cw + 1);
    return -1;
  }

  for (int i = 0; i < cfg->port_forward_count; i++) {
    const struct ds_port_forward *ex = &cfg->port_forwards[i];
    if (strcmp(ex->proto, pf->proto) != 0)
      continue;
    if (ex->host_port == pf->host_port &&
        ex->host_port_end == pf->host_port_end &&
        ex->container_port == pf->container_port &&
        ex->container_port_end == pf->container_port_end)
      return 1; /* the same rule twice, restart's second argv pass does this */
    int host_hit = ports_overlap(pf->host_port, pf->host_port_end,
                                 ex->host_port, ex->host_port_end);
    if (host_hit || ports_overlap(pf->container_port, pf->container_port_end,
                                  ex->container_port, ex->container_port_end)) {
      char a[16], b[16];
      fmt_port_range(a, sizeof(a),
                     host_hit ? pf->host_port : pf->container_port,
                     host_hit ? pf->host_port_end : pf->container_port_end);
      fmt_port_range(b, sizeof(b),
                     host_hit ? ex->host_port : ex->container_port,
                     host_hit ? ex->host_port_end : ex->container_port_end);
      ds_warn("%s port conflict: %s/%s overlaps with existing mapping %s/%s - "
              "skipping",
              host_hit ? "Host" : "Container", a, pf->proto, b, ex->proto);
      return 1;
    }
  }
  /* After the duplicate scan: restart's second argv pass resubmits every
   * saved rule, and that has to pass on a full table. */
  if (cfg->port_forward_count >= DS_MAX_PORT_FORWARDS) {
    snprintf(err, err_size, "Too many port forwards (max %d)",
             DS_MAX_PORT_FORWARDS);
    return -1;
  }
  cfg->port_forwards[cfg->port_forward_count++] = rule;
  return 0;
}

/* Bind Mount Sorting */

static int compare_bind_mounts(const void *a, const void *b) {
  const struct ds_bind_mount *ma = (const struct ds_bind_mount *)a;
  const struct ds_bind_mount *mb = (const struct ds_bind_mount *)b;
  return strcmp(ma->dest, mb->dest);
}

void sort_bind_mounts(struct ds_config *cfg) {
  if (!cfg || cfg->bind_count <= 1 || !cfg->binds)
    return;

  qsort(cfg->binds, cfg->bind_count, sizeof(struct ds_bind_mount),
        compare_bind_mounts);
}

int validate_bind_destination(const char *dest) {
  if (!dest || dest[0] != '/' || dest[1] == '\0')
    return 0;

  if (strlen(dest) >= PATH_MAX)
    return 0;

  /* "//" and "///" have no segment at all and land on the rootfs itself,
   * the same as "/", so a destination needs one real component. */
  int segments = 0;
  const char *p = dest;
  while (*p) {
    while (*p == '/')
      p++;
    const char *start = p;
    while (*p && *p != '/')
      p++;
    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;
    segments++;
    if ((len == 1 && start[0] == '.') ||
        (len == 2 && start[0] == '.' && start[1] == '.'))
      return 0;
    for (size_t i = 0; i < len; i++) {
      if (iscntrl((unsigned char)start[i]))
        return 0;
    }
  }

  return segments > 0;
}
