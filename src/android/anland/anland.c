/*
 * Droidspaces - anland display daemon integration
 *
 * Embeds the anland broker daemon (libdisplay_daemon, vendored alongside this
 * file) into the container lifecycle. For a container with anland enabled we:
 *   - generate a per-container host socket path under <workspace>/Net,
 *   - fork a persistent process through ds_spawn_daemon that runs the
 *     daemon's epoll loop on it,
 *   - record the pid and the socket path in the Pids dir so the app can find
 *     and stop it, and
 *   - bind-mount the socket onto the container's /run/display.sock
 * (post-pivot).
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

#include "droidspaces.h"

#include "display_daemon.h"

/* Per-container record names in the Pids dir, keyed by the sanitized name
 * like every other file there. The extensions are deliberately not
 * DS_EXT_PID: every Pids scanner treats <x>.pid as a container named x, so a
 * daemon pid file named <name>.anland.pid showed up as a phantom container and
 * left a .lock behind when the daemon died. Siblings use .xpid and .vpid. */
#define ANLAND_PID_EXT ".apid"
#define ANLAND_SOCK_EXT ".asock"

static void anland_record_name(const struct ds_config *cfg, const char *ext,
                               char *buf, size_t n) {
  char safe[NAME_MAX + 1];
  sanitize_container_name(cfg->container_name, safe, sizeof(safe));
  snprintf(buf, n, "%s%s", safe, ext);
}

/* anland is per-container, so a daemon is never shared between containers:
 * ds_global_daemon_stop's "keep alive for others" check is always false. */
static int anland_never_needed(void) { return 0; }

/*
 * ds_spawn_daemon's child. The vendored daemon is linked into this binary, so
 * there is no exec: bind the socket, then report readiness on ready_fd the way
 * an exec would, closed on success and written on failure. daemon_run blocks
 * until SIGTERM from ds_global_daemon_stop ends the process.
 */
static void anland_child(int ready_fd, void *user_data) {
  const char *sock_path = user_data;

  ds_daemon_child_preamble();
  setsid();
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull >= 0) {
    dup2(devnull, STDIN_FILENO);
    close(devnull);
  }

  daemon_ctx *ctx = NULL;
  if (daemon_create(&ctx, sock_path) < 0) {
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }
  close(ready_fd);
  daemon_run(ctx);
  daemon_destroy(ctx);
  _exit(0);
}

/* Load the recorded per-container socket path (Pids/<name>.asock) into
 * cfg->anland_sock. No-op when the file is missing/empty. Needed because the
 * socket path is runtime-only (not persisted to container.config), so a cfg
 * loaded from disk at stop time has an empty anland_sock. */
void ds_anland_load_sock(struct ds_config *cfg) {
  char sockfile[NAME_MAX + 16], rec[PATH_MAX];
  anland_record_name(cfg, ANLAND_SOCK_EXT, sockfile, sizeof(sockfile));
  snprintf(rec, sizeof(rec), "%s/%s", get_pids_dir(), sockfile);
  if (read_file(rec, cfg->anland_sock, sizeof(cfg->anland_sock)) < 0) {
    cfg->anland_sock[0] = '\0';
    return;
  }
  cfg->anland_sock[strcspn(cfg->anland_sock, "\r\n")] = '\0';
}

/* public API */

int ds_anland_daemon_start(struct ds_config *cfg) {
  if (!cfg || !cfg->anland || !is_android())
    return -1;
  if (getuid() != 0) {
    ds_error("[Anland] not running as root");
    return -1;
  }

  char pidfile[NAME_MAX + 16], sockfile[NAME_MAX + 16];
  anland_record_name(cfg, ANLAND_PID_EXT, pidfile, sizeof(pidfile));
  anland_record_name(cfg, ANLAND_SOCK_EXT, sockfile, sizeof(sockfile));

  /* Reuse an existing live daemon (ds_daemon_read_pid verifies liveness). */
  pid_t existing = ds_daemon_read_pid(pidfile);
  if (existing > 0) {
    cfg->anland_pid = existing;
    ds_anland_load_sock(cfg);
    ds_log("[Anland] daemon already running (PID %d)", (int)existing);
    return 1;
  }

  /* The socket lives in the workspace's Net directory, which only root can
   * write. /data/local/tmp belongs to the shell user with no sticky bit, so a
   * socket there could be swapped for a symlink before the chmod and the bind
   * mount below, both of which follow symlinks as root. The directory is 0755,
   * so the consumer can still reach the socket. */
  char uuid[DS_UUID_LEN + 1];
  if (generate_uuid(uuid, sizeof(uuid)) < 0) {
    ds_error("[Anland] failed to generate socket name");
    return -1;
  }
  snprintf(cfg->anland_sock, sizeof(cfg->anland_sock), "%s/anland-%s.sock",
           get_net_dir(), uuid);

  ds_log("[Anland] launching display daemon on %s", cfg->anland_sock);

  /* The log relay resolves its file relative to get_logs_dir() and creates
   * only the final component; the container's own log directory is already
   * there from ds_open_container_log. */
  char safe_log_name[NAME_MAX + 1], log_rel[NAME_MAX + 16];
  sanitize_container_name(cfg->container_name, safe_log_name,
                          sizeof(safe_log_name));
  snprintf(log_rel, sizeof(log_rel), "%s/anland", safe_log_name);

  pid_t child = ds_spawn_daemon(anland_child, cfg->anland_sock, log_rel,
                                "anland", "Anland");
  if (child <= 0) {
    cfg->anland_sock[0] = '\0';
    return -1;
  }

  /* The socket is bound once ds_spawn_daemon returns. Loosen its mode so the
   * consumer app and the compositor inside the container can connect. */
  chmod(cfg->anland_sock, 0666);

  cfg->anland_pid = child;
  ds_daemon_write_pid(pidfile, child, NULL);

  /* Record the socket path so the app (and a later restart) can find it. */
  char rec[PATH_MAX];
  snprintf(rec, sizeof(rec), "%s/%s", get_pids_dir(), sockfile);
  write_file_atomic(rec, cfg->anland_sock);
  return 0;
}

void ds_anland_daemon_stop(struct ds_config *cfg) {
  if (!cfg)
    return;
  char pidfile[NAME_MAX + 16], sockfile[NAME_MAX + 16];
  anland_record_name(cfg, ANLAND_PID_EXT, pidfile, sizeof(pidfile));
  anland_record_name(cfg, ANLAND_SOCK_EXT, sockfile, sizeof(sockfile));

  /* Recover the socket path from the Pids file if this cfg was loaded from disk
   * (anland_sock is runtime-only), so ds_global_daemon_stop can unlink it. */
  if (cfg->anland_sock[0] == '\0')
    ds_anland_load_sock(cfg);

  ds_global_daemon_stop(anland_never_needed, cfg->anland_pid, &cfg->anland_pid,
                        pidfile, cfg->anland_sock[0] ? cfg->anland_sock : NULL,
                        "[Anland]");
  ds_daemon_remove_pid(sockfile);
}

/* socket bridge */

int ds_setup_anland_socket(struct ds_config *cfg) {
  if (!cfg || !cfg->anland || !is_android())
    return 0;
  if (cfg->anland_sock[0] == '\0') {
    ds_warn("[Anland] no daemon socket recorded - skipping bind mount");
    return 0;
  }

  /* Post-pivot: the host socket is reachable under /.old_root. */
  char src[PATH_MAX + 32];
  snprintf(src, sizeof(src), "/.old_root%s", cfg->anland_sock);
  if (access(src, F_OK) != 0) {
    ds_warn("[Anland] host socket not found at %s - skipping bind mount", src);
    return 0;
  }

  mkdir_p("/run", 0755);
  if (ds_bind_mount_socket(src, "/run/display.sock", 0, "anland") < 0)
    return 0;

  ds_log("[Anland] display socket bind-mounted to /run/display.sock");
  return 0;
}
