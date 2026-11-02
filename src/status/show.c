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
    /* The booted snapshot carries the display name and hostname the container
     * actually got, the same source ds_config_load_by_name() prefers for a
     * running one. The table needs it too: a name copied from it into -n is
     * what gets saved on the next start. */
    char snap[PATH_MAX];
    if (build_proc_root_path(pid, "/run/droidspaces/container.config", snap,
                             sizeof(snap)) == 0 &&
        ds_config_peek_path(snap, &tmp_cfg) == 0) {
      safe_strncpy(st->name, tmp_cfg.container_name, sizeof(st->name));
      safe_strncpy(st->hostname, tmp_cfg.hostname, sizeof(st->hostname));
      ds_anland_load_sock(&tmp_cfg);
      safe_strncpy(st->anland_sock, tmp_cfg.anland_sock,
                   sizeof(st->anland_sock));
      st->x11 = tmp_cfg.x11;
    }
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
