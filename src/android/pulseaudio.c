/*
 * Droidspaces v7 - PulseAudio Server and Socket Manager
 *
 * Manages the PulseAudio daemon lifecycle on Android (spawning, logging, and
 * stopping) and host-to-container socket bridging.
 *
 * Refactored for Android 15 / SM6150:
 *  - Reuses an existing unified UID 2000 OpenSL ES socket (/data/local/tmp/awl/pulse.sock)
 *    when present to prevent duplicate PulseAudio daemons.
 *  - Loads module-sles-sink + module-sles-source directly (-n), avoiding the
 *    module-aaudio-sink background futex deadlock and removing the extra pactl fork.
 *  - Drops privileges to UID 2000 (AID_SHELL, SYSTEM_FIXED RECORD_AUDIO) when
 *    spawning a new daemon so hardware microphone capture works natively.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspaces.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "internal.h"

#define AID_SHELL_UID 2000
#define UNIFIED_HOST_PULSE_SOCK "/data/local/tmp/awl/pulse.sock"

/* helpers */

static int probe_unix_socket(const char *path) {
  if (!path || access(path, F_OK) != 0)
    return -1;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
  int ok = (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) ? 0 : -1;
  close(fd);
  return ok;
}

static int pa_resolve_audio_uid(void) {
  if (access(TX11_PULSE_BIN, F_OK) != 0) {
    ds_warn("PulseAudio: binary not found at %s. Is pulseaudio installed in "
            "Termux?",
            TX11_PULSE_BIN);
    return -1;
  }
  /* Prefer AID_SHELL (UID 2000) which holds SYSTEM_FIXED RECORD_AUDIO on Android */
  return AID_SHELL_UID;
}

/*
 * Probe for libskcodec.so (needed by libskandroidcodec.so on Samsung One UI
 * 6.1+). lib64 takes priority; fall back to lib; skip if absent.
 */
#define SKCODEC_LIB64 "/system/lib64/libskcodec.so"
#define SKCODEC_LIB "/system/lib/libskcodec.so"

static const char *find_skcodec(void) {
  if (access(SKCODEC_LIB64, F_OK) == 0)
    return SKCODEC_LIB64;
  if (access(SKCODEC_LIB, F_OK) == 0)
    return SKCODEC_LIB;
  return NULL;
}

static void inject_skcodec_preload(void) {
  const char *lib = find_skcodec();
  if (!lib)
    return;

  const char *existing = getenv("LD_PRELOAD");
  if (existing && existing[0]) {
    char buf[PATH_MAX * 2];
    snprintf(buf, sizeof(buf), "%s:%s", lib, existing);
    setenv("LD_PRELOAD", buf, 1);
  } else {
    setenv("LD_PRELOAD", lib, 1);
  }
  ds_log("[PulseAudio] LD_PRELOAD += %s (Samsung OpenSL ES fix)", lib);
}

/* daemon child */

struct pulse_args {
  int uid;
};

static void pulse_child_wrapper(int ready_fd, void *user_data) {
  struct pulse_args *args = (struct pulse_args *)user_data;

  ds_daemon_child_preamble();

  setenv("TMPDIR", TX11_PREFIX "/tmp", 1);
  setenv("HOME", TX11_HOME, 1);
  setenv("PREFIX", TX11_PREFIX, 1);
  setenv("PULSE_SERVER", "unix:" TX11_PULSE_SOCKET, 1);

  mkdir_p(TX11_PREFIX "/tmp", 0755);
  chmod(TX11_PREFIX "/tmp", 01777);

  ds_selinux_enter_domain();

  if (ds_drop_privileges(args->uid) < 0) {
    perror("[PulseAudio] privilege drop failed");
    if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
    }
    _exit(1);
  }

  inject_skcodec_preload();

  fprintf(stdout, "[PulseAudio] uid=%d socket=%s (OpenSL ES sink+source)\n",
          (int)getuid(), TX11_PULSE_SOCKET);
  fflush(stdout);

  /* Launch PulseAudio directly with OpenSL ES sink + source (-n skips AAudio default.pa) */
  char *argv[] = {
      TX11_PULSE_BIN,
      "-n",
      "--load=module-sles-sink sink_name=android",
      "--load=module-sles-source source_name=android_mic",
      "--load=module-suspend-on-idle timeout=5",
      "--load=module-native-protocol-unix socket=" TX11_PULSE_SOCKET
      " auth-anonymous=1",
      "--exit-idle-time=-1",
      "--daemonize=no",
      "--log-target=stderr",
      "--use-pid-file=false",
      "--disallow-exit",
      NULL,
  };

  execv(argv[0], argv);
  perror("[PulseAudio] execv");
  if (write(ready_fd, "\x01", 1) < 0) { /* ignore */
  }
  _exit(1);
}

/* stale config nuke */

#define PA_CONFIG_DIR TX11_HOME "/.config/pulse"

static void nuke_pulse_config(void) {
  if (access(PA_CONFIG_DIR, F_OK) != 0)
    return;

  ds_log("[PulseAudio] nuking stale config: %s", PA_CONFIG_DIR);
  remove_recursive(PA_CONFIG_DIR);
}

/* spawn */

static pid_t spawn_pulse(int uid) {
  struct pulse_args args = {.uid = uid};
  return ds_spawn_daemon(pulse_child_wrapper, &args, "pulse.log", "PulseAudio",
                         "PulseAudio");
}

/* public API */

int ds_pulse_daemon_start(struct ds_config *cfg) {
  if (!cfg || !cfg->pulseaudio || !is_android())
    return -1;
  if (getuid() != 0) {
    ds_error("[PulseAudio] not running as root");
    return -1;
  }

  /* Zero-duplicate optimization: reuse live unified UID 2000 OpenSL ES socket if reachable */
  if (probe_unix_socket(UNIFIED_HOST_PULSE_SOCK) == 0) {
    mkdir_p(TX11_PREFIX "/tmp", 0755);
    unlink(TX11_PULSE_SOCKET);
    if (symlink(UNIFIED_HOST_PULSE_SOCK, TX11_PULSE_SOCKET) == 0) {
      ds_log("[PulseAudio] reusing live unified host socket %s (0 duplicate daemons)",
             UNIFIED_HOST_PULSE_SOCK);
      return 1;
    }
  }

  /* Reuse existing managed daemon if still alive */
  pid_t existing = ds_daemon_read_pid("pulse.ppid");
  if (existing > 0) {
    ds_log("PulseAudio: daemon already running (PID %d)", (int)existing);
    cfg->pulse_pid = existing;
    return 1;
  }

  int uid = pa_resolve_audio_uid();
  if (uid < 0)
    return -1;

  unlink(TX11_PULSE_SOCKET);
  nuke_pulse_config();

  ds_log("[PulseAudio] launching OpenSL ES daemon (uid=%d)", uid);
  pid_t child = spawn_pulse(uid);
  if (child <= 0)
    return -1;

  cfg->pulse_pid = child;
  ds_daemon_write_pid("pulse.ppid", child, NULL);

  if (wait_for_socket_or_death(child, TX11_PULSE_SOCKET, 3000, 100000) == 0) {
    chmod(TX11_PULSE_SOCKET, 0777);
    ds_log("[PulseAudio] OpenSL ES socket ready (no pactl fork needed)");
  } else {
    ds_warn("PulseAudio: socket did not appear in 3s");
  }

  return 0;
}

void ds_pulse_daemon_stop(struct ds_config *cfg) {
  if (!cfg)
    return;
  /* Do not tear down an externally supervised unified socket symlink */
  struct stat st;
  if (lstat(TX11_PULSE_SOCKET, &st) == 0 && S_ISLNK(st.st_mode))
    return;
  ds_global_daemon_stop(check_pulse_needs, cfg->pulse_pid, &cfg->pulse_pid,
                        "pulse.ppid", TX11_PULSE_SOCKET, "[PulseAudio]");
}

/* socket bridge */

int ds_setup_pulse_socket(struct ds_config *cfg) {
  if (!is_android() || !cfg->pulseaudio)
    return 0;

  return ds_bridge_termux_socket(".pulse-socket", DS_PULSE_SOCKET,
                                 "PULSE_SERVER", "unix:" DS_PULSE_SOCKET,
                                 "PulseAudio");
}
