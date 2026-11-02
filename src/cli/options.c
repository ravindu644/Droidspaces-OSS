/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/*
 * Resolve every SRC component of a --bind-mount / -B value string.
 * Format: SRC:DEST[,SRC:DEST,...]
 * Only the SRC part of each pair is a host-side path; DEST lives inside the
 * container namespace and is always absolute by convention.
 */
static char *resolve_bind_src(const char *val) {
  char *copy = strdup(val), *out = NULL;
  size_t size = 0;
  FILE *f = copy ? open_memstream(&out, &size) : NULL;
  if (!f) {
    free(copy);
    return strdup(val);
  }
  char *sv;
  int first = 1;
  for (char *tok = strtok_r(copy, ",", &sv); tok;
       tok = strtok_r(NULL, ",", &sv), first = 0) {
    char *col = strchr(tok, ':');
    if (col)
      *col = '\0';
    char *src = ds_resolve_path_arg(tok);
    fprintf(f, "%s%s%s%s", first ? "" : ",", src ? src : tok, col ? ":" : "",
            col ? col + 1 : "");
    free(src);
  }
  free(copy);
  if (fclose(f) != 0) {
    free(out);
    return strdup(val);
  }
  return out;
}

/*
 * Table of options whose next argument (or = suffix) is a filesystem path.
 * Keeps ds_resolve_argv_paths() free of hard-coded option names.
 */
static const struct {
  const char *opt;
  int is_bind; /* 1 = --bind-mount: resolve the SRC component only */
} ds_path_opts[] = {
    {"--rootfs", 0}, {"-r", 0},           {"--rootfs-img", 0}, {"-i", 0},
    {"--conf", 0},   {"--config", 0},     {"-C", 0},           {"--env", 0},
    {"-E", 0},       {"--bind-mount", 1}, {"--bind", 1},       {"-B", 1},
    {NULL, 0},
};

void ds_resolve_argv_paths(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg || arg[0] != '-') /* fast skip: non-option args are not paths */
      continue;

    for (int j = 0; ds_path_opts[j].opt; j++) {
      const char *opt = ds_path_opts[j].opt;
      int bind = ds_path_opts[j].is_bind;
      size_t olen = strlen(opt);

      /* "--opt=VALUE" form */
      if (strncmp(arg, opt, olen) == 0 && arg[olen] == '=') {
        const char *val = arg + olen + 1;
        if (!*val || (val[0] == '/' && !bind))
          break; /* absolute paths (non-bind) don't need resolution */
        char *resolved =
            bind ? resolve_bind_src(val) : ds_resolve_path_arg(val);
        if (resolved) {
          char *new_arg = malloc(olen + 1 + strlen(resolved) + 1);
          if (new_arg) {
            memcpy(new_arg, opt, olen);
            new_arg[olen] = '=';
            strcpy(new_arg + olen + 1, resolved);
            argv[i] = new_arg; /* argv[i] was a kernel-provided pointer; safe to
                                  replace */
          }
          free(resolved);
        }
        break;
      }

      /* "--opt VALUE" form (value is the next element) */
      if (strcmp(arg, opt) == 0 && i + 1 < argc) {
        const char *val = argv[i + 1];
        if (!val || !*val || (val[0] == '/' && !bind))
          continue;
        char *resolved =
            bind ? resolve_bind_src(val) : ds_resolve_path_arg(val);
        if (resolved)
          argv[i + 1] = resolved; /* kernel-provided string; safe to replace */
        break;
      }
    }
  }
}

/* Validate each comma-separated name in optarg; store raw value in out_buf. */
int parse_and_validate_names(const char *optarg, char *out_buf,
                             size_t out_size) {
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", optarg);
  char *sp, *tok = strtok_r(tmp, ",", &sp);
  while (tok) {
    if (reject_container_name(tok) < 0)
      return -1;
    tok = strtok_r(NULL, ",", &sp);
  }
  snprintf(out_buf, out_size, "%s", optarg);
  return 0;
}
