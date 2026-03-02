/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
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
static int parse_bool(const char *val, const char *key_name) {
  if (!val)
    return 0;

  if (strcasecmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0)
    return 1;

  if (strcasecmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "no") == 0 || strcasecmp(val, "off") == 0)
    return 0;

  ds_warn("Config: Invalid boolean value '%s' for key '%s' (defaulting to 0)",
          val, key_name);
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
      if (ds_config_add_bind(cfg, src, dest) < 0) {
        ds_warn("Failed to add bind mount %s:%s from config", src, dest);
      }
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
      if (old_cap > INT_MAX / 2) {
        ds_error("Bind mount capacity overflow");
        return -1;
      }
      new_cap = old_cap * 2;
    }

    /* Check allocation size won't overflow */
    size_t alloc_size = (size_t)new_cap * sizeof(*cfg->binds);
    if (alloc_size / sizeof(*cfg->binds) != (size_t)new_cap) {
      ds_error("Bind mount allocation size overflow");
      return -1;
    }

    struct ds_bind_mount *new_binds = realloc(cfg->binds, alloc_size);
    if (!new_binds) {
      ds_error("Out of memory allocating bind mounts");
      return -1;
    }

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

void free_config_binds(struct ds_config *cfg) {
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
    ds_error("Failed to open config file '%s': %s", config_path,
             strerror(errno));
    return -1;
  }

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
      ds_warn("Config: Invalid syntax at %s:%d (missing '=')", config_path,
              line_num);
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
    } else if (strcmp(key, "enable_ipv6") == 0) {
      cfg->enable_ipv6 = parse_bool(val, key);
    } else if (strcmp(key, "enable_android_storage") == 0) {
      cfg->android_storage = parse_bool(val, key);
    } else if (strcmp(key, "enable_hw_access") == 0) {
      cfg->hw_access = parse_bool(val, key);
    } else if (strcmp(key, "enable_termux_x11") == 0) {
      cfg->termux_x11 = parse_bool(val, key);
    } else if (strcmp(key, "selinux_permissive") == 0) {
      cfg->selinux_permissive = parse_bool(val, key);
    } else if (strcmp(key, "volatile_mode") == 0) {
      cfg->volatile_mode = parse_bool(val, key);
    } else if (strcmp(key, "bind_mounts") == 0) {
      parse_bind_mounts(val, cfg);
    } else if (strcmp(key, "dns_servers") == 0) {
      safe_strncpy(cfg->dns_servers, val, sizeof(cfg->dns_servers));
    } else if (strcmp(key, "foreground") == 0) {
      cfg->foreground = parse_bool(val, key);
    } else if (strcmp(key, "pidfile") == 0) {
      safe_strncpy(cfg->pidfile, val, sizeof(cfg->pidfile));
    } else if (strcmp(key, "env_file") == 0) {
      if (strstr(val, "..") ||
          (val[0] == '/' && !is_subpath(get_workspace_dir(), val))) {
        ds_warn("Config: Invalid or unsafe env_file path: %s", val);
        continue;
      }
      safe_strncpy(cfg->env_file, val, sizeof(cfg->env_file));
    }
  }

  fclose(f);
  return 0;
}

/* List of keys managed by the C backend. Any other keys are preserved. */
static const char *KNOWN_KEYS[] = {"name",
                                   "hostname",
                                   "rootfs_path",
                                   "pidfile",
                                   "enable_ipv6",
                                   "enable_android_storage",
                                   "enable_hw_access",
                                   "enable_termux_x11",
                                   "selinux_permissive",
                                   "volatile_mode",
                                   "foreground",
                                   "bind_mounts",
                                   "dns_servers",
                                   "env_file",
                                   NULL};

/* Linked list to store unknown key-value pairs from existing config */
struct config_line {
  char line[2048];
  struct config_line *next;
};

int ds_config_save(const char *config_path, struct ds_config *cfg) {
  char temp_path[PATH_MAX];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", config_path);

  struct config_line *unknown_head = NULL;
  struct config_line *unknown_tail = NULL;

  /* Step 1: Collect unknown keys from existing file */
  FILE *f_in = fopen(config_path, "re");
  if (f_in) {
    char line[2048];
    while (fgets(line, sizeof(line), f_in)) {
      char line_copy[2048];
      safe_strncpy(line_copy, line, sizeof(line_copy));
      char *trimmed = trim_whitespace(line_copy);

      /* Always skip header or comments in preservation to use new ones */
      if (trimmed[0] == '#' || trimmed[0] == '\0')
        continue;

      char *equals = strchr(trimmed, '=');
      if (!equals)
        continue;

      *equals = '\0';
      char *key = trim_whitespace(trimmed);

      int is_known = 0;
      for (int i = 0; KNOWN_KEYS[i]; i++) {
        if (strcmp(key, KNOWN_KEYS[i]) == 0) {
          is_known = 1;
          break;
        }
      }

      if (!is_known) {
        struct config_line *node = malloc(sizeof(*node));
        if (node) {
          safe_strncpy(node->line, line, sizeof(node->line));
          node->next = NULL;
          if (!unknown_head) {
            unknown_head = unknown_tail = node;
          } else {
            unknown_tail->next = node;
            unknown_tail = node;
          }
        }
      }
    }
    fclose(f_in);
  }

  /* Step 2: Write all configurations to temporary file */
  FILE *f_out = fopen(temp_path, "we");
  if (!f_out) {
    ds_warn("Failed to create temporary config '%s': %s", temp_path,
            strerror(errno));
    while (unknown_head) {
      struct config_line *next = unknown_head->next;
      free(unknown_head);
      unknown_head = next;
    }
    return -1;
  }

  fprintf(f_out, "# Droidspaces Container Configuration\n");
  fprintf(f_out, "# Generated automatically — Changes may be overwritten\n\n");

  /* Write managed keys */
  if (cfg->container_name[0])
    fprintf(f_out, "name=%s\n", cfg->container_name);
  if (cfg->hostname[0])
    fprintf(f_out, "hostname=%s\n", cfg->hostname);

  if (cfg->is_img_mount && cfg->rootfs_img_path[0]) {
    char abs_path[PATH_MAX];
    if (realpath(cfg->rootfs_img_path, abs_path))
      fprintf(f_out, "rootfs_path=%s\n", abs_path);
    else
      fprintf(f_out, "rootfs_path=%s\n", cfg->rootfs_img_path);
  } else if (cfg->rootfs_path[0]) {
    char abs_path[PATH_MAX];
    if (realpath(cfg->rootfs_path, abs_path))
      fprintf(f_out, "rootfs_path=%s\n", abs_path);
    else
      fprintf(f_out, "rootfs_path=%s\n", cfg->rootfs_path);
  }

  if (cfg->pidfile[0])
    fprintf(f_out, "pidfile=%s\n", cfg->pidfile);

  fprintf(f_out, "enable_ipv6=%d\n", cfg->enable_ipv6);
  if (is_android()) {
    fprintf(f_out, "enable_android_storage=%d\n", cfg->android_storage);
    fprintf(f_out, "enable_termux_x11=%d\n", cfg->termux_x11);
  }
  fprintf(f_out, "enable_hw_access=%d\n", cfg->hw_access);
  fprintf(f_out, "selinux_permissive=%d\n", cfg->selinux_permissive);
  fprintf(f_out, "volatile_mode=%d\n", cfg->volatile_mode);
  fprintf(f_out, "foreground=%d\n", cfg->foreground);

  if (cfg->env_file[0])
    fprintf(f_out, "env_file=%s\n", cfg->env_file);

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

  /* Step 3: Append preserved keys (Android App Config) */
  if (unknown_head) {
    fprintf(f_out, "\n# Android App Configuration\n");
    struct config_line *node = unknown_head;
    while (node) {
      fprintf(f_out, "%s", node->line);
      struct config_line *next = node->next;
      free(node);
      node = next;
    }
  }

  fclose(f_out);

  /* Step 4: Atomic rename commit */
  if (rename(temp_path, config_path) < 0) {
    ds_error("Failed to commit configuration to '%s': %s", config_path,
             strerror(errno));
    unlink(temp_path);
    return -1;
  }

  if (!cfg->config_file_existed) {
    ds_log("Configuration persisted to " C_BOLD "%s" C_RESET, config_path);
    cfg->config_file_existed = 1;
  }
  return 0;
}

int ds_config_validate(struct ds_config *cfg) {
  int errors = 0;

  if (cfg->rootfs_path[0] && cfg->rootfs_img_path[0]) {
    ds_error("Both rootfs directory and image specified simultaneously.");
    ds_log("Directory: %s", cfg->rootfs_path);
    ds_log("Image: %s", cfg->rootfs_img_path);
    ds_log("Override one using --rootfs or --rootfs-img.");
    errors++;
  }

  if (!cfg->container_name[0]) {
    ds_error("Container name is mandatory (--name).");
    errors++;
  }

  if (!cfg->rootfs_path[0] && !cfg->rootfs_img_path[0]) {
    ds_error("No rootfs target specified (requires -r or -i).");
    errors++;
  }

  /* Existence checks */
  if (cfg->rootfs_path[0] && access(cfg->rootfs_path, F_OK) != 0) {
    ds_error("Rootfs directory not found: '%s' (%s)", cfg->rootfs_path,
             strerror(errno));
    errors++;
  }

  if (cfg->rootfs_img_path[0] && access(cfg->rootfs_img_path, F_OK) != 0) {
    ds_error("Rootfs image not found: '%s' (%s)", cfg->rootfs_img_path,
             strerror(errno));
    errors++;
  }

  /* Image mode requires a name for the mount point */
  if (cfg->rootfs_img_path[0] && !cfg->container_name[0]) {
    ds_error("Rootfs image requires a container name (--name).");
    errors++;
  }

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
