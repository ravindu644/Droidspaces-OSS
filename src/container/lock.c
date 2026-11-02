/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <sys/file.h>

#include "container/internal.h"

/* Container lifecycle lock
 *
 * Everything a container owns on disk is keyed by its name: pidfile, mount
 * point, cgroup. So at most one actor may change a container's state
 * at a time, and that includes the monitor, not only commands. Whoever holds
 * <pids>/<name>.lock owns the transition, and decides what to do from the
 * state it finds while holding it.
 *
 * It is a flock(), so taking it is atomic and the kernel drops it when the
 * holder dies. There is no stale lock to detect and no PID to recycle.
 *
 * A second file, <name>.monitor, is held shared by a container's monitor for
 * as long as it lives. It answers one question for the pidfile pruners in
 * container/: is somebody still responsible for this container's leftovers? */

#define DS_EXT_MONITOR ".monitor"
#define DS_EXT_HELPERS ".helpers"

/* Four is enough: a monitor holds at most supervision, helpers and, while it
 * acts on its container's exit, the lifecycle lock. The path is kept so that
 * release can unlink the file. */
static struct held_lock {
  int fd;
  char path[PATH_MAX];
} g_held_locks[4] = {{-1, ""}, {-1, ""}, {-1, ""}, {-1, ""}};

#define N_HELD_LOCKS (sizeof(g_held_locks) / sizeof(g_held_locks[0]))

static struct held_lock *find_held(int fd) {
  for (size_t i = 0; i < N_HELD_LOCKS; i++)
    if (g_held_locks[i].fd == fd)
      return &g_held_locks[i];
  return NULL;
}

/* A lock belongs to the process that took it. flock() locks live on the open
 * file description, which fork() shares with the child, so without this any
 * long-lived child (the monitor, a log relay) would keep its parent's lock
 * held long after the parent let go. Close only: the parent still holds them.
 */
static void drop_inherited_locks(void) {
  for (size_t i = 0; i < N_HELD_LOCKS; i++) {
    if (g_held_locks[i].fd >= 0)
      close(g_held_locks[i].fd);
    g_held_locks[i].fd = -1;
  }
}

/* Build <pids>/<safe_name><ext> with defensive truncation.
 * Precision: 2048 (pids_dir) + 256 (name) + ext < PATH_MAX (4096). */
static int get_lock_path(const char *name, const char *ext, char *buf,
                         size_t size) {
  if (!name || !buf || size == 0 || !validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));
  int r =
      snprintf(buf, size, "%.2048s/%.256s%s", get_pids_dir(), safe_name, ext);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* open + flock. Returns the fd, or -1 with errno set (EWOULDBLOCK when op has
 * LOCK_NB and somebody else holds it).
 *
 * unlock_file() removes a lock file whose last holder lets go, so the file we
 * locked may no longer be the one the path names. A lock on an unlinked file
 * guards nothing, so after locking check the path still names our file, and
 * start over if it does not. Bounded: on a filesystem where fstat and stat
 * disagree (overlayfs before 4.19) every attempt fails, and the callers all
 * tolerate -1. */
static int lock_file(const char *path, int op) {
  static int atfork_registered;
  if (!atfork_registered) {
    pthread_atfork(NULL, NULL, drop_inherited_locks);
    atfork_registered = 1;
  }

  for (int attempt = 0; attempt < 8; attempt++) {
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0)
      return -1;

    int r;
    do {
      r = flock(fd, op);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
      int err = errno;
      close(fd);
      errno = err;
      return -1;
    }

    struct stat held, named;
    if (fstat(fd, &held) == 0 && stat(path, &named) == 0 &&
        held.st_ino == named.st_ino && held.st_dev == named.st_dev) {
      struct held_lock *slot = find_held(-1);
      if (slot) {
        slot->fd = fd;
        safe_strncpy(slot->path, path, sizeof(slot->path));
      }
      return fd;
    }
    close(fd);
  }
  errno = EAGAIN;
  return -1;
}

/* Release, and remove the file when we are its last holder: an exclusive lock
 * means nobody else has one. Unlinking while still holding the lock is what
 * makes this safe, see lock_file(). The same pattern as systemd's
 * release_lock_file(). Converting a shared lock drops it first and, on
 * EWOULDBLOCK, does not put it back, which is fine only because we are
 * releasing anyway: never call this on a shared lock you still need. */
static void unlock_file(int fd) {
  struct held_lock *slot = find_held(fd);
  if (slot) {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
      unlink(slot->path);
    slot->fd = -1;
  }
  flock(fd, LOCK_UN);
  close(fd);
}

/* Take the lifecycle lock for a container. wait=0 fails at once with
 * errno == EWOULDBLOCK when another actor holds it, which is what a command
 * wants. wait=1 blocks until it is free, which is what the monitor wants: it
 * must not act on its container's exit until any command has finished.
 * Returns an fd for ds_container_unlock(), or -1. */
int ds_container_lock(const char *name, int wait) {
  char path[PATH_MAX];
  if (get_lock_path(name, DS_EXT_LOCK, path, sizeof(path)) < 0) {
    errno = EINVAL;
    return -1;
  }
  ensure_workspace();

  int fd = lock_file(path, LOCK_EX | (wait ? 0 : LOCK_NB));
  if (fd < 0)
    return -1;

  /* Monitors started by a build older than this lock cannot see a flock().
   * They read "pid boot-id" from this file and yield to a live holder, so
   * keep writing it. Drop this once no such monitor can still be running. */
  char boot_id[64] = "", stamp[96];
  read_file("/proc/sys/kernel/random/boot_id", boot_id, sizeof(boot_id));
  int len = snprintf(stamp, sizeof(stamp), "%d %s", getpid(), boot_id);
  if (ftruncate(fd, 0) < 0 || pwrite(fd, stamp, (size_t)len, 0) < 0)
    ds_log("[DEBUG] could not stamp %s: %s", path, strerror(errno));
  return fd;
}

void ds_container_unlock(int fd) {
  if (fd < 0)
    return;
  if (ftruncate(fd, 0) < 0) {
    /* Only the compatibility stamp; the lock itself is released below. */
  }
  unlock_file(fd);
}

/* Called once by a monitor, which then holds it until it exits. */
static int g_supervision_fd = -1;

void ds_container_claim_supervision(const char *name) {
  char path[PATH_MAX];
  if (get_lock_path(name, DS_EXT_MONITOR, path, sizeof(path)) == 0)
    g_supervision_fd = lock_file(path, LOCK_SH);
}

/* The monitor's last act. Its pidfile is gone by now, so no pruner can be
 * looking for it, and a successor monitor (restart) holds its own shared lock
 * on the same file, in which case the file stays. */
void ds_container_release_supervision(void) {
  if (g_supervision_fd >= 0)
    unlock_file(g_supervision_fd);
  g_supervision_fd = -1;
}

/* Take and release a lock that only a monitor ever holds. Returns 0 when it
 * was free, and then the file is gone too (see unlock_file), or -1 when a
 * live monitor has it. */
static int probe_free(const char *name, const char *ext) {
  char path[PATH_MAX];
  if (get_lock_path(name, ext, path, sizeof(path)) < 0)
    return -1;
  /* No file, no holder. Also keeps a 20 ms wait loop and a scan over stray
   * directory names from minting and deleting lock files. */
  if (access(path, F_OK) != 0)
    return 0;
  int fd = lock_file(path, LOCK_EX | LOCK_NB);
  if (fd < 0)
    return -1;
  unlock_file(fd);
  return 0;
}

/* For a stop, which holds the lifecycle lock: a monitor killed with -9 never
 * released its supervision file, and nothing else takes it. A live monitor
 * holds it shared, so this is a no-op while one exists. */
void ds_container_prune_supervision(const char *name) {
  probe_free(name, DS_EXT_MONITOR);
}

/* Held by the monitor for exactly the life of its route monitor thread. That
 * thread reinstalls the shared NAT rules whenever it finds them missing, and
 * it lives in the monitor process, so a stop command cannot join it; it waits
 * for this lock instead. Non-blocking: a monitor left behind by an unkillable
 * init still holds its lock, and a new instance must boot past it rather than
 * hang in its handshake. Returns an fd for ds_container_unlock(), or -1. */
int ds_container_lock_helpers(const char *name) {
  char path[PATH_MAX];
  if (get_lock_path(name, DS_EXT_HELPERS, path, sizeof(path)) < 0)
    return -1;
  return lock_file(path, LOCK_EX | LOCK_NB);
}

/* Wait until the helpers lock is free, or timeout_ms has passed. A free lock
 * (no monitor, or one from a build before this lock) returns at once. On
 * timeout carry on anyway: that is no worse than before the lock existed, and
 * the holder is then a monitor that is wedged, not one winding down. */
void ds_container_wait_helpers(const char *name, int timeout_ms) {
  for (int waited = 0;; waited += 20) {
    if (probe_free(name, DS_EXT_HELPERS) == 0)
      return;
    if (waited >= timeout_ms) {
      ds_warn("Could not confirm that the monitor for '%s' stopped its "
              "network helpers - proceeding with cleanup",
              name);
      return;
    }
    usleep(20000);
  }
}

/* For the pruners: take the lifecycle lock only if nobody is in the middle of
 * a transition AND no monitor is alive for this name. A live monitor cleans
 * up after its own container, and deleting its pidfile first would make it
 * conclude a command had already done so. Returns an fd for
 * ds_container_unlock(), or -1, in which case leave the container alone.
 * Anything we cannot determine counts as "not ours to touch". */
int ds_container_lock_orphan(const char *name) {
  int fd = ds_container_lock(name, 0);
  if (fd < 0)
    return -1;

  if (probe_free(name, DS_EXT_MONITOR) < 0) {
    ds_container_unlock(fd);
    return -1;
  }
  /* No monitor, so nobody holds the helpers lock either: a monitor killed
   * with -9 while its route monitor ran leaves the file, and only a stop
   * would otherwise ever look at it. */
  probe_free(name, DS_EXT_HELPERS);
  return fd;
}

int lifecycle_lock_or_complain(struct ds_config *cfg, const char *verb) {
  int fd = ds_container_lock(cfg->container_name, 0);
  if (fd >= 0)
    return fd;

  if (errno == EWOULDBLOCK) {
    ds_error("Cannot %s '%s': another operation is in progress on this "
             "container",
             verb, cfg->container_name);
    ds_error("Wait for it to complete, or use 'droidspaces show' to check "
             "status");
  } else {
    ds_error("Cannot %s '%s': failed to take the container lock: %s", verb,
             cfg->container_name, strerror(errno));
  }
  return -1;
}
