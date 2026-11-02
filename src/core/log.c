/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <time.h>

void rotate_log(const char *path, size_t max_size) {
  struct stat st;
  if (stat(path, &st) == 0 && (size_t)st.st_size >= max_size) {
    char old_path[PATH_MAX + 8];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    rename(path, old_path);
  }
}

/* <workspace>/Logs/<name>/<file>, with the directory in place and the file
 * rotated at 2 MB. */
void container_log_path(const char *name, const char *file, char *buf,
                        size_t size) {
  char safe[256], dir[PATH_MAX];
  sanitize_container_name(name, safe, sizeof(safe));
  snprintf(dir, sizeof(dir), "%.3800s/%.256s", get_logs_dir(), safe);
  mkdir_p(dir, 0755);
  snprintf(buf, size, "%.4080s/%.32s", dir, file);
  rotate_log(buf, 2 * 1024 * 1024);
}

static void write_to_log_file(const char *name, const char *component,
                              const char *raw_msg, int pre_opened_fd) {
  if (!name || !name[0])
    return;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm;
  localtime_r(&ts.tv_sec, &tm);

  /* Pre-opened FD path: survives pivot_root / mount namespace changes.
   * dprintf() writes directly to the fd - no dup/fdopen/fclose overhead.
   * O_APPEND (set at open time) makes each write atomic for small messages. */
  if (pre_opened_fd >= 0) {
    /* In-place rotation: truncate when over 2MB.
     * rename() is not possible since the FD follows the inode, not the path. */
    struct stat st;
    if (fstat(pre_opened_fd, &st) == 0 &&
        (size_t)st.st_size >= 2 * 1024 * 1024) {
      if (ftruncate(pre_opened_fd, 0) < 0) {
        /* best-effort, ignore */
      }
      if (lseek(pre_opened_fd, 0, SEEK_SET) == (off_t)-1) {
        /* best-effort, ignore */
      }
    }
    dprintf(pre_opened_fd, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] %s\n",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
            tm.tm_sec, ts.tv_nsec / 1000000, component, raw_msg);
    return;
  }

  /* Fallback: open by path (pre-pivot, monitor process, etc.) */
  char log_path[PATH_MAX];
  container_log_path(name, "log", log_path, sizeof(log_path));

  FILE *f = fopen(log_path, "ae"); /* append + close-on-exec */
  if (!f)
    return;

  fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] %s\n",
          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
          tm.tm_sec, ts.tv_nsec / 1000000, component, raw_msg);
  fclose(f);
}

void ds_log_internal(const char *prefix, const char *color, int is_err,
                     const char *fmt, ...) {
  /* A peek (ds_config_peek) at a config file prints none of the file's
   * complaints; errors are the command's own, so they still go out. */
  if (ds_log_silent == DS_LOG_PEEK && prefix[0] != '-')
    return;

  char raw_msg[8192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(raw_msg, sizeof(raw_msg), fmt, ap);
  va_end(ap);

  /* Always log to file if container name is known */
  if (ds_log_container_name[0]) {
    write_to_log_file(ds_log_container_name, "main", raw_msg,
                      ds_log_container_fd);
  }

  /* Decide if we should print to terminal */
  if (ds_log_silent && !is_err)
    return;

  /* Tagged lines, "[NET] ..." and the like, are subsystem chatter for the
   * log file; the terminal gets the untagged ones. */
  if (!is_err && raw_msg[0] == '[')
    return;

  FILE *out = is_err ? stderr : stdout;
  fprintf(out,
          "["
          "%s"
          "%s" C_RESET "] %s\r\n",
          color, prefix, raw_msg);
  fflush(out);
}

void ds_die_internal(const char *fmt, ...) {
  char raw_msg[8192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(raw_msg, sizeof(raw_msg), fmt, ap);
  va_end(ap);

  if (ds_log_container_name[0]) {
    write_to_log_file(ds_log_container_name, "fatal", raw_msg,
                      ds_log_container_fd);
  }

  fprintf(stderr, "[" C_RED "-" C_RESET "] %s\r\n", raw_msg);
  fflush(stderr);
  exit(EXIT_FAILURE);
}

void write_monitor_debug_log(const char *name, const char *fmt, ...) {
  if (!name || !name[0] || !fmt)
    return;

  char raw_msg[8192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(raw_msg, sizeof(raw_msg), fmt, ap);
  va_end(ap);

  write_to_log_file(name, "monitor", raw_msg, -1);
}

void ds_open_container_log(struct ds_config *cfg) {
  if (!cfg || !cfg->container_name[0])
    return;

  char log_path[PATH_MAX];
  container_log_path(cfg->container_name, "log", log_path, sizeof(log_path));

  int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd >= 0)
    ds_log_container_fd = fd;
}

void ds_close_container_log(void) {
  if (ds_log_container_fd >= 0) {
    close(ds_log_container_fd);
    ds_log_container_fd = -1;
  }
}
