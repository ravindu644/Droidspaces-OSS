/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* Internal Helpers */

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

  /* Inject DISPLAY for Termux-X11 containers (written to /run/droidspaces.env
   * and sourced by /etc/profile.d/droidspaces_env.sh) */
  if (is_android() && cfg->x11)
    setenv("DISPLAY", TX11_DISPLAY_STR, 1);
  if (is_android() && cfg->virgl)
    setenv("GALLIUM_DRIVER", "virpipe", 1);
  if (is_android() && cfg->pulseaudio)
    setenv("PULSE_SERVER", "unix:" DS_PULSE_SOCKET, 1);
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

  /* Write DISPLAY for Termux-X11 containers so /etc/profile.d picks it up */
  if (is_android() && cfg->x11)
    fprintf(f, "export DISPLAY='" TX11_DISPLAY_STR "'\n");
  if (is_android() && cfg->virgl)
    fprintf(f, "export GALLIUM_DRIVER='virpipe'\n");
  if (is_android() && cfg->pulseaudio)
    fprintf(f, "export PULSE_SERVER='unix:" DS_PULSE_SOCKET "'\n");
  if (ds_mount_api_shim_wanted(cfg))
    fprintf(f, "export LIBMOUNT_FORCE_MOUNT2='always'\n");

  fclose(f);
  chmod(path, 0755);
}
