/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* PID file helpers */

int read_and_validate_pid(const char *pidfile, pid_t *pid_out) {
  if (pid_out)
    *pid_out = 0;

  char buf[64];
  if (read_file(pidfile, buf, sizeof(buf)) < 0) {
    /* If file is gone or empty, count as stopped without error logging */
    return -1;
  }

  char *end;
  long val = strtol(buf, &end, 10);
  if (*end != '\0' || val <= 0) {
    /* Stale/invalid data in pidfile */
    ds_error("Invalid/stale PID in %s: '%s'", pidfile, buf);
    return -1;
  }

  /* check if process exists. Atomic check: if kill fails with ESRCH, we KNOW
   * it's dead without racing between exist checking and acting. */
  if (kill((pid_t)val, 0) < 0) {
    if (errno == ESRCH) {
      return -1;
    }
    /* Permissive check: if EPERM, it exists but we can't signal it.
     * Likely still running if it was ours. */
  }

  /*
   * Crucial Fix: Distinguish between "process is gone" (-1) and
   * "process exists but is not a Droidspaces container" (-2).
   * Pruning logic must ONLY nuke files when the process is truly gone.
   */
  if (!is_valid_container_pid((pid_t)val)) {
    if (pid_out)
      *pid_out = (pid_t)val; /* Return the PID so caller knows it exists */
    return -2;
  }

  if (pid_out)
    *pid_out = (pid_t)val;
  return 0;
}
