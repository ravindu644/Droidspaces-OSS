/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Internal Helpers
 * ---------------------------------------------------------------------------*/

static void set_container_defaults(const char *term) {
  setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
         1);
  setenv("TERM", term ? term : "xterm-256color", 1);
  setenv("HOME", "/root", 1);
  setenv("container", "droidspaces", 1);
}

void load_etc_environment(void) {
  FILE *envf = fopen("/etc/environment", "re");
  if (!envf)
    return;

  char line[512];
  while (fgets(line, sizeof(line), envf)) {
    /* Strip newline */
    char *nl = strchr(line, '\n');
    if (nl)
      *nl = '\0';
    /* Skip comments and empty lines */
    if (line[0] == '#' || line[0] == '\0')
      continue;
    /* Parse KEY=VALUE */
    char *eq = strchr(line, '=');
    if (eq) {
      *eq = '\0';
      char *val = eq + 1;
      /* Strip quotes */
      size_t vlen = strlen(val);
      if (vlen >= 2 && ((val[0] == '"' && val[vlen - 1] == '"') ||
                        (val[0] == '\'' && val[vlen - 1] == '\''))) {
        val[vlen - 1] = '\0';
        val++;
      }
      setenv(line, val, 1);
    }
  }
  fclose(envf);
}

void ds_env_boot_setup(struct ds_config *cfg) {
  /* Capture TERM before clearenv() */
  const char *saved_term = getenv("TERM");
  char term_buf[64] = "xterm-256color";

  if (saved_term && saved_term[0]) {
    /* Bug fix: TWRP terminal leaks internal variables like 'bg1.25' as TERM.
     * Some recovery environments/kernels also set TERM to version strings.
     * If it contains dots or appears to be a garbage value, use the default. */
    if (strchr(saved_term, '.') || strncmp(saved_term, "bg", 2) == 0) {
      /* likely garbage - keep default xterm-256color */
    } else {
      safe_strncpy(term_buf, saved_term, sizeof(term_buf));
    }
  }

  clearenv();
  set_container_defaults(term_buf);

  /* Standard Linux LANG default */
  setenv("LANG", "en_US.UTF-8", 0);

  /* User-defined variables - applied LAST so they override our defaults above
   */
  for (int i = 0; i < cfg->env_var_count; i++) {
    setenv(cfg->env_vars[i].key, cfg->env_vars[i].value, 1);
  }
}

void ds_env_save(const char *path, struct ds_config *cfg) {
  if (!path || !cfg)
    return;

  FILE *f = fopen(path, "we");
  if (!f) {
    ds_warn("Failed to save environment to %s: %s", path, strerror(errno));
    return;
  }

  for (int i = 0; i < cfg->env_var_count; i++) {
    const char *key = cfg->env_vars[i].key;
    const char *val = cfg->env_vars[i].value;

    /* Escape single quotes for shell safety in export format */
    fprintf(f, "export %s='", key);
    for (const char *p = val; *p; p++) {
      if (*p == '\'') {
        fprintf(f, "'\\''");
      } else {
        fputc(*p, f);
      }
    }
    fprintf(f, "'\n");
  }

  fclose(f);
  chmod(path, 0755);
}

/* ---------------------------------------------------------------------------
 * parse_env_file_to_config() - parse user environment variables into memory
 *
 * Called before fork() while host paths are still accessible.
 * Supports unlimited line length and variable count via dynamic allocation.
 * ---------------------------------------------------------------------------*/
void parse_env_file_to_config(const char *path, struct ds_config *cfg) {
  if (!path || path[0] == '\0' || !cfg)
    return;

  FILE *f = fopen(path, "re");
  if (!f) {
    /* Non-fatal: env file missing is fine, container still boots */
    if (errno != ENOENT) {
      ds_warn("Failed to open env file: %s (%s)", path, strerror(errno));
    }
    return;
  }

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
                         (val[0] == '\'' && val[val_len - 1] == '\''))) {
      value = malloc(val_len - 1);
      if (value) {
        memcpy(value, val + 1, val_len - 2);
        value[val_len - 2] = '\0';
      }
    } else {
      value = strdup(val);
    }

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
