/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* String helpers */

void safe_strncpy(char *dst, const char *src, size_t size) {
  if (!dst || size == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len >= size) {
    ds_warn("String truncation: src='%s' (len=%zu) to size=%zu", src, len,
            size);
  }
  snprintf(dst, size, "%s", src);
}

/* Append comma-separated interface names/wildcards from `val` to ifaces[],
 * trimming surrounding whitespace and skipping empty or over-long (>= IFNAMSIZ)
 * tokens.  Exact duplicates (already present in ifaces[0..*count)) are
 * silently skipped, mirroring the bind-mount/port-forward de-dup so re-parsing
 * the same list (e.g. on container restart) is a no-op instead of growing the
 * array.  *count is advanced up to `max`; the return value is the number of
 * tokens dropped because the array was already full (0 = all fit), letting
 * the caller warn or fail as it likes. */
int ds_parse_iface_csv(const char *val, char ifaces[][IFNAMSIZ], int *count,
                       int max) {
  char copy[1024];
  safe_strncpy(copy, val, sizeof(copy));
  int dropped = 0;
  char *sp;
  for (char *tok = strtok_r(copy, ",", &sp); tok;
       tok = strtok_r(NULL, ",", &sp)) {
    while (*tok == ' ' || *tok == '\t')
      tok++;
    char *end = tok + strlen(tok) - 1;
    while (end > tok && (*end == ' ' || *end == '\t'))
      *end-- = '\0';
    if (!tok[0] || strlen(tok) >= IFNAMSIZ)
      continue;
    int dup = 0;
    for (int i = 0; i < *count; i++) {
      if (strcmp(ifaces[i], tok) == 0) {
        dup = 1;
        break;
      }
    }
    if (dup)
      continue;
    if (*count >= max) {
      dropped++;
      continue;
    }
    safe_strncpy(ifaces[(*count)++], tok, IFNAMSIZ);
  }
  return dropped;
}

void ds_format_uptime(long uptime_sec, char *buf, size_t size) {
  if (uptime_sec < 0) {
    safe_strncpy(buf, "unknown", size);
    return;
  }

  int days = uptime_sec / 86400;
  int hours = (uptime_sec % 86400) / 3600;
  int mins = (uptime_sec % 3600) / 60;
  int secs = uptime_sec % 60;

  char tmp[128] = {0};
  int pos = 0;

  if (days > 0)
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%dd ", days);
  if (hours > 0 || days > 0)
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%dh ", hours);
  if (mins > 0 || hours > 0 || days > 0)
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%dm ", mins);
  snprintf(tmp + pos, sizeof(tmp) - pos, "%ds", secs);

  safe_strncpy(buf, tmp, size);
}

/* Parse human-readable size: "512M", "1G", "2048" (bytes). Returns -1 on error.
 *
 * Use integer and fractional parts separately to avoid precision loss
 * for large values (e.g. 8192G overflows double's 53-bit mantissa):
 *   - Integer part: strtoll → exact long long arithmetic.
 *   - Fractional part (e.g. "1.5G"): limited double multiplication only for
 *     the sub-unit portion, keeping precision loss < 1 byte.
 */
long long ds_parse_size(const char *str) {
  if (!str || !*str)
    return -1;

  errno = 0;
  char *end;
  /* Parse integer part exactly. */
  long long int_part = strtoll(str, &end, 10);
  if (errno || end == str || int_part < 0)
    return -1;

  /* Optional fractional part (e.g. ".5" in "1.5G"). */
  double frac = 0.0;
  if (*end == '.') {
    char *frac_end;
    frac = strtod(end, &frac_end);
    if (frac_end == end || frac < 0)
      return -1;
    end = frac_end;
  }

  long long factor = 1;
  switch (*end | 0x20) { /* tolower */
  case 'k':
    factor = 1024LL;
    break;
  case 'm':
    factor = 1024LL * 1024;
    break;
  case 'g':
    factor = 1024LL * 1024 * 1024;
    break;
  case 't':
    factor = 1024LL * 1024 * 1024 * 1024;
    break;
  case '\0':
    break;
  default:
    return -1;
  }

  /* Overflow check before multiplication. */
  if (factor > 1 && int_part > (long long)(9223372036854775807LL / factor))
    return -1;

  long long result = int_part * factor;
  if (frac != 0.0)
    result += (long long)(frac * (double)factor);
  return result;
}

void ds_format_size(long long bytes, char *buf, size_t sz) {
  if (bytes <= 0) {
    snprintf(buf, sz, "N/A");
    return;
  }
  static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int u = 0;
  double d = (double)bytes;
  while (d >= 1024 && u < 4) {
    d /= 1024;
    u++;
  }
  snprintf(buf, sz, "%.2f %s", d, units[u]);
}

/* Shell metacharacters that must never appear in a flag token */
static const char ds_shell_metachars[] = "$`;&|><(){}\\";

/* Append c to a growing token, doubling its buffer as needed. */
static int push(char **tok, size_t *len, size_t *cap, char c) {
  if (*len + 1 >= *cap) {
    size_t ncap = *cap ? *cap * 2 : 64;
    char *tmp = realloc(*tok, ncap);
    if (!tmp)
      return -1;
    *tok = tmp;
    *cap = ncap;
  }
  (*tok)[(*len)++] = c;
  return 0;
}

/*
 * Split a flags string into an argv array suitable for appending to execv.
 * Tokens are split on whitespace; single and double quotes are supported for
 * grouping tokens with embedded spaces.  Any token containing a shell
 * metacharacter is rejected and -1 is returned.
 *
 * On success, *out_argv is a heap-allocated NULL-terminated array of
 * heap-allocated strings and *out_argc is the count.
 * Caller must free with ds_free_split_flags().
 * Returns 0 on success, -1 on error (nothing allocated on error).
 */
int ds_split_flags(const char *str, char ***out_argv, int *out_argc) {
  if (!str || !out_argv || !out_argc)
    return -1;

  *out_argv = NULL;
  *out_argc = 0;

  int cap = 0, count = 0;
  char **argv = NULL;
  const char *p = str;

  while (*p) {
    while (isspace((unsigned char)*p))
      p++;
    if (!*p)
      break;

    /* One token; a quoted run copies literally up to its closing quote. */
    char *tok = NULL;
    size_t tok_len = 0, tok_cap = 0;
    while (*p && !isspace((unsigned char)*p)) {
      char q = (*p == '\'' || *p == '"') ? *p++ : 0;
      if (q) {
        while (*p && *p != q)
          if (push(&tok, &tok_len, &tok_cap, *p++) < 0) {
            free(tok);
            goto oom;
          }
        if (*p == q)
          p++;
      } else if (push(&tok, &tok_len, &tok_cap, *p++) < 0) {
        free(tok);
        goto oom;
      }
    }
    if (!tok_len) {
      free(tok);
      continue;
    }
    tok[tok_len] = '\0'; /* push always leaves room for it */

    if (strpbrk(tok, ds_shell_metachars)) {
      ds_error("flags: rejected token with shell metachar: %s", tok);
      free(tok);
      ds_free_split_flags(argv, count);
      return -1;
    }

    if (count >= cap) {
      int new_cap = cap ? cap * 2 : 8;
      char **tmp = realloc(argv, (size_t)(new_cap + 1) * sizeof(char *));
      if (!tmp) {
        free(tok);
        goto oom;
      }
      argv = tmp;
      cap = new_cap;
    }
    argv[count++] = tok;
  }

  if (argv)
    argv[count] = NULL;

  *out_argv = argv;
  *out_argc = count;
  return 0;

oom:
  ds_free_split_flags(argv, count);
  return -1;
}

void ds_free_split_flags(char **argv, int argc) {
  if (!argv)
    return;
  for (int i = 0; i < argc; i++)
    free(argv[i]);
  free(argv);
}
