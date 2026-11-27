/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/* ds_console_drain - Empty the container's console PTY master.
 *
 * In BACKGROUND mode the monitor is the only process holding the console PTY
 * master, and nothing reads it (foreground mode drives it from the parent via
 * console_monitor_loop). If it is never read, a chatty container init that
 * writes past the ~64 KiB PTY buffer blocks forever in n_tty_write and the
 * whole container wedges. So the monitor must keep this master drained for the
 * container's entire lifetime.
 *
 * Drained bytes are appended to the per-container console log when one is open
 * (log_fd >= 0), which also makes background boot/console output visible for
 * debugging. Draining continues even if the log write fails: keeping the PTY
 * buffer empty is the load-bearing part; logging is best-effort.
 *
 * master_fd must be O_NONBLOCK so read() returns EAGAIN once drained. */
void ds_console_drain(int master_fd, int log_fd, size_t *logged) {
  char buf[4096];
  ssize_t n;

  while ((n = read(master_fd, buf, sizeof(buf))) > 0) {
    if (log_fd < 0)
      continue;

    /* Bound the console log: truncate at 2 MiB. The fd was opened O_APPEND,
     * so subsequent writes still land correctly after the truncation. */
    if (logged && *logged >= 2u * 1024 * 1024) {
      if (ftruncate(log_fd, 0) == 0)
        *logged = 0;
    }

    size_t off = 0;
    while (off < (size_t)n) {
      ssize_t w = write(log_fd, buf + off, (size_t)n - off);
      if (w <= 0)
        break;
      off += (size_t)w;
    }
    if (logged)
      *logged += off;
  }
}

int console_log_open(const struct ds_config *cfg) {
  char log_path[PATH_MAX];
  container_log_path(cfg->container_name, "console", log_path,
                     sizeof(log_path));
  return open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
}

/* Nobody is attached to the console: a background start, or a foreground one
 * whose command has gone. From here on the monitor reads the master itself,
 * so a chatty init never blocks on a full pty, and logs what it reads. */
void console_adopt(struct ds_config *cfg, int *log_fd) {
  if (cfg->console.master >= 0) {
    int fl = fcntl(cfg->console.master, F_GETFL, 0);
    if (fl >= 0)
      (void)fcntl(cfg->console.master, F_SETFL, fl | O_NONBLOCK);
  }
  if (*log_fd < 0)
    *log_fd = console_log_open(cfg);
}
