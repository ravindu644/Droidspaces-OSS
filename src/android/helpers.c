/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "internal.h"

/* Poll for a socket path to appear, bailing early if the server process dies.
 * Returns 0 on socket ready, -1 on server death or timeout. */
int wait_for_socket_or_death(pid_t pid, const char *path, int timeout_ms,
                             int interval_us) {
  int iters = timeout_ms * 1000 / interval_us;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (int i = 0; i < iters; i++) {
    if (access(path, F_OK) == 0) {
      clock_gettime(CLOCK_MONOTONIC, &t1);
      long ms =
          (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
      ds_log("[DEBUG] socket ready: %s (+%ldms)", path, ms);
      return 0;
    }
    int st;
    if (waitpid(pid, &st, WNOHANG) > 0) {
      ds_error("server pid=%d exited (status=%d) before socket appeared",
               (int)pid, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
      return -1;
    }
    usleep((unsigned int)interval_us);
  }
  ds_warn("timed out waiting for socket: %s", path);
  return -1;
}

/* Start the Android helper servers this config wants and stop the ones it
 * no longer does. A restart and an in-container reboot keep the helpers
 * alive for the next boot to reuse, so a toggle turned off between two boots
 * has to be honoured here, or the server outlives it: a stale Anland record
 * kept the app's launch button up on a container that no longer had a
 * display. The global servers stay up while another running container
 * needs them; the Anland broker is per container and goes. */
void ds_android_helpers_sync(struct ds_config *cfg) {
  if (!is_android())
    return;
  if (cfg->x11) {
    if (ds_x11_daemon_start(cfg) == 0)
      wait_for_socket_or_death(
          cfg->x11_pid, TX11_SOCK_DIR "/" TX11_DISPLAY_SOCK, 5000, 50000);
  } else {
    ds_x11_daemon_stop(cfg);
  }
  if (cfg->virgl) {
    /* The server runs as root and binds 0755 under our umask, but non-root
     * guest users and Termux must connect, which needs write permission. */
    if (ds_virgl_daemon_start(cfg) == 0 &&
        wait_for_socket_or_death(cfg->virgl_pid, TX11_VIRGL_SOCKET, 2000,
                                 20000) == 0)
      chmod(TX11_VIRGL_SOCKET, 0666);
  } else {
    ds_virgl_daemon_stop(cfg);
  }
  if (cfg->pulseaudio)
    ds_pulse_daemon_start(cfg);
  else
    ds_pulse_daemon_stop(cfg);
  /* The broker's socket path is generated here and recorded in the Pids dir,
   * never in container.config, so a reboot finds it through the record. */
  if (cfg->anland)
    ds_anland_daemon_start(cfg);
  else
    ds_anland_daemon_stop(cfg);
}
