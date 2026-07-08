/*
 * Droidspaces - anland display daemon integration
 *
 * Embeds the anland broker daemon (libdisplay_daemon, vendored alongside this
 * file) into the container lifecycle. For a container with anland enabled we:
 *   - generate a per-container host socket path under <workspace>/anland,
 *   - fork a persistent process that runs the daemon's epoll loop on it,
 *   - record the pid and the socket path in the Pids dir so the app can find
 *     and stop it, and
 *   - bind-mount the socket onto the container's /run/display.sock (post-pivot).
 *
 * The Android consumer app connects to the same socket (via its root fd-helper)
 * and a patched KWin ("producer") inside the container connects to
 * /run/display.sock; the daemon just brokers their handshake.
 *
 * Modeled on src/android/x11.c. Unlike X11 (a single global server) each
 * container gets its own daemon, so the pid / socket-path files are keyed by
 * container name.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspace.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "display_daemon.h"

/* Per-container Pids-dir filenames (keyed by container name). */
static void anland_pid_file(const struct ds_config *cfg, char *buf, size_t n) {
  snprintf(buf, n, "%s.anland.pid", cfg->container_name);
}
static void anland_sock_file(const struct ds_config *cfg, char *buf, size_t n) {
  snprintf(buf, n, "%s.anland", cfg->container_name);
}

/* anland is per-container, so a daemon is never shared between containers:
 * ds_global_daemon_stop's "keep alive for others" check is always false. */
static int anland_never_needed(void) { return 0; }

/* ---- daemon child ----------------------------------------------------- */

/*
 * The forked daemon process. Runs the vendored libdisplay_daemon epoll loop
 * in-process (the library is linked into this binary, so no execv is needed).
 * Never returns: always _exit()s.
 */
static void anland_daemon_child(int out_fd, const char *sock_path) {
  /* Detach from the launcher's session and make the daemon robust/persistent
   * (SIGTERM is left default so ds_global_daemon_stop can kill it). */
  setsid();
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  ds_oom_protect();

  /* stdout/stderr -> log pipe, stdin -> /dev/null */
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull >= 0) {
    dup2(devnull, STDIN_FILENO);
    close(devnull);
  }
  dup2(out_fd, STDOUT_FILENO);
  dup2(out_fd, STDERR_FILENO);
  close(out_fd);

  daemon_ctx *ctx = NULL;
  if (daemon_create(&ctx, sock_path) < 0) {
    fprintf(stderr, "failed to bind %s\n", sock_path);
    _exit(1);
  }
  daemon_run(ctx);      /* blocks until SIGTERM-triggered exit via the loop flag */
  daemon_destroy(ctx);  /* closes clients, unlinks the socket */
  _exit(0);
}

/* Fork the daemon child and a log-relay grandchild (Logs/anland.log).
 * Returns the daemon PID, or -1 on error. */
static pid_t spawn_anland_daemon(const char *sock_path) {
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    ds_warn("[anland] pipe: %s", strerror(errno));
    return -1;
  }

  pid_t child = fork();
  if (child < 0) {
    ds_warn("[anland] fork: %s", strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (child == 0) {
    close(pipefd[0]);
    anland_daemon_child(pipefd[1], sock_path); /* never returns */
    _exit(1);
  }

  /* Parent: hand the read end to a log relay; the daemon child holds the only
   * write end, so the relay sees EOF when the daemon exits. */
  close(pipefd[1]);
  ds_spawn_log_relay(pipefd[0], "anland.log", "anland");
  return child;
}

/* ---- public API ------------------------------------------------------- */

int ds_anland_daemon_start(struct ds_config *cfg) {
  if (!cfg || !cfg->anland || !is_android())
    return -1;
  if (getuid() != 0) {
    ds_error("[anland] not running as root");
    return -1;
  }

  char pidfile[NAME_MAX + 16], sockfile[NAME_MAX + 16];
  anland_pid_file(cfg, pidfile, sizeof(pidfile));
  anland_sock_file(cfg, sockfile, sizeof(sockfile));

  /* Reuse an existing live daemon (ds_daemon_read_pid verifies liveness). */
  pid_t existing = ds_daemon_read_pid(pidfile);
  if (existing > 0) {
    cfg->anland_pid = existing;
    char rec[PATH_MAX];
    snprintf(rec, sizeof(rec), "%s/%s", get_pids_dir(), sockfile);
    int fd = open(rec, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
      ssize_t r = read(fd, cfg->anland_sock, sizeof(cfg->anland_sock) - 1);
      close(fd);
      if (r > 0) {
        cfg->anland_sock[r] = '\0';
        cfg->anland_sock[strcspn(cfg->anland_sock, "\r\n")] = '\0';
      }
    }
    ds_log("[anland] daemon already running (PID %d)", (int)existing);
    return 1;
  }

  /* Socket dir + generated per-container socket path. */
  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s/anland", get_workspace_dir());
  mkdir_p(dir, 0771);

  char uuid[DS_UUID_LEN + 1];
  if (generate_uuid(uuid, sizeof(uuid)) < 0) {
    ds_error("[anland] failed to generate socket name");
    return -1;
  }
  snprintf(cfg->anland_sock, sizeof(cfg->anland_sock), "%s/%s.sock", dir, uuid);

  ds_log("[anland] launching display daemon on %s", cfg->anland_sock);
  pid_t child = spawn_anland_daemon(cfg->anland_sock);
  if (child <= 0)
    return -1;

  cfg->anland_pid = child;
  ds_daemon_write_pid(pidfile, child);

  /* Record the socket path so the app (and a later restart) can find it. */
  char rec[PATH_MAX];
  snprintf(rec, sizeof(rec), "%s/%s", get_pids_dir(), sockfile);
  write_file_atomic(rec, cfg->anland_sock);

  /* Give the daemon a moment to bind before we bind-mount the socket. */
  wait_for_socket_or_death(child, cfg->anland_sock, 2000, 20000);
  return 0;
}

void ds_anland_daemon_stop(struct ds_config *cfg) {
  if (!cfg)
    return;
  char pidfile[NAME_MAX + 16], sockfile[NAME_MAX + 16];
  anland_pid_file(cfg, pidfile, sizeof(pidfile));
  anland_sock_file(cfg, sockfile, sizeof(sockfile));

  ds_global_daemon_stop(anland_never_needed, cfg->anland_pid, &cfg->anland_pid,
                        pidfile, cfg->anland_sock, "[anland]");
  ds_daemon_remove_pid(sockfile);
}

/* ---- socket bridge ---------------------------------------------------- */

int ds_setup_anland_socket(struct ds_config *cfg) {
  if (!cfg || !cfg->anland || !is_android())
    return 0;
  if (cfg->anland_sock[0] == '\0') {
    ds_warn("[anland] no daemon socket recorded - skipping bind mount");
    return 0;
  }

  /* Post-pivot: the host socket is reachable under /.old_root. */
  char src[PATH_MAX];
  snprintf(src, sizeof(src), "/.old_root%s", cfg->anland_sock);
  if (access(src, F_OK) != 0) {
    ds_warn("[anland] host socket not found at %s - skipping bind mount", src);
    return 0;
  }

  mkdir_p("/run", 0755);
  if (ds_bind_mount_socket(src, "/run/display.sock", 0, "anland") < 0)
    return 0;

  ds_log("[anland] display socket bind-mounted to /run/display.sock");
  return 0;
}
