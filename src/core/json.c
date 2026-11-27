/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* JSON output. We only ever write flat objects of strings and integers, so
 * these two functions and ds_json_status() in status/collect.c are the whole
 * encoder. */

void ds_json_str(const char *key, const char *val, int *first) {
  printf("%s\"%s\":\"", *first ? "" : ",", key);
  *first = 0;
  for (const unsigned char *p = (const unsigned char *)val; val && *p; p++) {
    if (*p == '"' || *p == '\\')
      printf("\\%c", *p);
    else if (*p < 0x20)
      printf("\\u%04x", *p);
    else
      putchar(*p);
  }
  putchar('"');
}

void ds_json_int(const char *key, long long val, int *first) {
  printf("%s\"%s\":%lld", *first ? "" : ",", key, val);
  *first = 0;
}
