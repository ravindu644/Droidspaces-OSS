/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* Status reporting */

int show_containers(struct ds_config *cfg) {
  char container_dir[1024];
  snprintf(container_dir, sizeof(container_dir), "%s/%s", get_workspace_dir(),
           DS_CONTAINERS_DIR);
  int totalcount = count_folders(container_dir);

  int count = 0;
  int cap = 32;
  size_t max_name_len = 4; /* "NAME" */
  struct ds_status *containers = malloc((size_t)cap * sizeof(*containers));
  if (!containers)
    return -1;

  DIR *d = opendir(get_pids_dir());
  if (!d && errno != ENOENT) {
    ds_error("Failed to open PIDs directory: %s", strerror(errno));
    free(containers);
    return -1;
  }

  struct ds_config tmp_cfg;
  pid_t pid;
  while (d && next_running(d, &tmp_cfg, &pid)) {
    if (count >= cap) {
      struct ds_status *tmp =
          cap > 8192 ? NULL
                     : realloc(containers, (size_t)cap * 2 * sizeof(*tmp));
      if (!tmp) {
        free(containers);
        closedir(d);
        return -1;
      }
      cap *= 2;
      containers = tmp;
    }
    struct ds_status *st = &containers[count++];
    memset(st, 0, sizeof(*st));
    safe_strncpy(st->name, tmp_cfg.container_name, sizeof(st->name));
    st->pid = pid;
    /* The pidfile only carries the sanitized name. The display name and the
     * hostname the container actually got come from what it booted with,
     * which its monitor holds, or from the host config when no monitor
     * answers: ds_config_peek, the same source a by-name load uses. The table
     * needs it: a name copied from it into -n is what gets saved on the next
     * start. */
    struct ds_config booted;
    ds_config_init(&booted);
    if (ds_config_peek(tmp_cfg.container_name, &booted) == 0 &&
        booted.container_name[0]) {
      safe_strncpy(st->name, booted.container_name, sizeof(st->name));
      safe_strncpy(st->hostname, booted.hostname, sizeof(st->hostname));
      ds_anland_load_sock(&booted);
      safe_strncpy(st->anland_sock, booted.anland_sock,
                   sizeof(st->anland_sock));
      st->x11 = booted.x11;
    }
    ds_config_free(&booted);
    ds_config_free(&tmp_cfg);
    if (!st->hostname[0])
      safe_strncpy(st->hostname, st->name, sizeof(st->hostname));
    size_t nlen = strlen(st->name);
    if (nlen > max_name_len)
      max_name_len = nlen;
  }
  if (d)
    closedir(d);

  if (cfg->json_output) {
    long ram_total = ds_collect_status(containers, count);
    printf("{\"total\":%d,\"ram_total_kb\":%ld,\"running\":[", totalcount,
           ram_total);
    for (int i = 0; i < count; i++) {
      int first = 1;
      printf(i ? ",{" : "{");
      ds_json_status(&containers[i], &first);
      printf("}");
    }
    printf("]}\n");
  } else if (count == 0) {
    printf("\n(No containers running)\n\n");
  } else {
    if (max_name_len > 60)
      max_name_len = 60;

/* Helper to print horizontal line */
#define PRINT_LINE(start, mid, end)                                            \
  do {                                                                         \
    printf("%s", start);                                                       \
    for (size_t i = 0; i < max_name_len + 2; i++)                              \
      printf("─");                                                             \
    printf("%s", mid);                                                         \
    for (size_t i = 0; i < 10; i++)                                            \
      printf("─");                                                             \
    printf("%s\n", end);                                                       \
  } while (0)

    printf("\n");
    PRINT_LINE("┌", "┬", "┐");
    printf("│ %-*s │ %-8s │\n", (int)max_name_len, "NAME", "PID");
    PRINT_LINE("├", "┼", "┤");

    for (int i = 0; i < count; i++) {
      printf("│ %-*s │ %-8d │\n", (int)max_name_len, containers[i].name,
             containers[i].pid);
    }

    PRINT_LINE("└", "┴", "┘");
    printf("\n");
#undef PRINT_LINE
  }
  free(containers);
  return 0;
}
