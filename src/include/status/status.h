/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_STATUS_STATUS_H
#define DS_STATUS_STATUS_H

/* collect.c */
struct ds_status {
  char name[128];
  pid_t pid;
  char hostname[256];    /* from the container config, same source as info */
  char os[256];          /* PRETTY_NAME from /proc/<pid>/root/etc/os-release */
  char ip[256];          /* non-loopback IPv4 addresses, comma separated */
  char ip6[512];         /* global and ULA IPv6 addresses, comma separated */
  char anland_sock[108]; /* host socket of the anland daemon, or "" */
  int x11;               /* booted with Termux:X11 */
  long uptime_sec;
  long ram_used_kb;
  long cpu_permill;
  /* Limits in force, 0 when unlimited. cpu_limit_permill is the share of the
   * host the container may use, the same scale as cpu_permill. */
  long ram_limit_kb;
  long cpu_limit_permill;
  int swap_allowed; /* 0 only while a memory limit holds swap at 0 */
};

/* Caller fills name, pid and hostname. Fills the rest for every entry in one
 * /proc walk pair (one 250 ms CPU window total). Returns host MemTotal KB. */
long ds_collect_status(struct ds_status *st, int n);
void get_os_pretty(const char *osrelease_path, char *buf, size_t size);
void ds_json_status(const struct ds_status *st, int *first);

/* show.c */
int show_containers(struct ds_config *cfg);

#endif /* DS_STATUS_STATUS_H */
