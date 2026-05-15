/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <ftw.h>
#include <sys/xattr.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * String helpers
 * ---------------------------------------------------------------------------*/

void safe_strncpy(char *dst, const char *src, size_t size) {
  if (!dst || size == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len >= size) {
    ds_warn("String truncation: src='%s' (len=%zu) to size=%zu", src, len,
            size);
  }
  snprintf(dst, size, "%s", src);
}

/* Mirrors ContainerManager.sanitizeContainerName() in the Android app.
 * Replaces spaces with dashes so directory names are consistent. */
void sanitize_container_name(const char *name, char *out, size_t size) {
  size_t i;
  for (i = 0; i < size - 1 && name[i] != '\0'; i++)
    out[i] = (name[i] == ' ') ? '-' : name[i];
  out[i] = '\0';
}

/* ---------------------------------------------------------------------------
 * Relative-path resolution
 *
 * The daemon calls chdir("/") inside daemonize(), so any relative path
 * captured from the user's CWD must be made absolute BEFORE we reach the
 * daemonize()/reexec() boundary.  ds_resolve_argv_paths() is called once
 * in main() while CWD is still the user's directory.
 *
 * Strategy:
 *   1. Try realpath(3) - handles .., symlinks, and canonicalises the path.
 *      This works for paths that already exist on disk.
 *   2. For paths that do not exist yet (e.g. a new rootfs image being
 *      created), fall back to a plain cwd-join.  We still strip leading ./
 *      sequences so the result is always absolute.
 * ---------------------------------------------------------------------------*/

char *ds_resolve_path_arg(const char *path) {
  if (!path || !*path)
    return strdup("");

  const char *p = path;
  char *to_free = NULL;

  /* Handle ~/ expansion */
  if (p[0] == '~' && (p[1] == '/' || p[1] == '\0')) {
    const char *home = getenv("HOME");
    if (home) {
      size_t hlen = strlen(home);
      size_t plen = strlen(p + 1);
      to_free = malloc(hlen + plen + 1);
      if (to_free) {
        memcpy(to_free, home, hlen);
        memcpy(to_free + hlen, p + 1, plen + 1);
        p = to_free;
      }
    }
  }

  if (p[0] == '/') {
    char *res = strdup(p);
    if (res) {
      size_t len = strlen(res);
      while (len > 1 && res[len - 1] == '/') {
        res[len - 1] = '\0';
        len--;
      }
    }
    free(to_free);
    return res;
  }

  /* Fast path: realpath handles .., symlinks, and validates existence. */
  char resolved[PATH_MAX];
  if (realpath(p, resolved)) {
    free(to_free);
    return strdup(resolved);
  }

  /* Path does not exist yet - build an absolute path from the current CWD.
   * Strip leading ./ noise before joining so the result stays clean. */
  const char *suffix = p;
  while (suffix[0] == '.' && suffix[1] == '/')
    suffix += 2;
  if (!*suffix) {
    /* Input was pure "./" - resolve to CWD itself. */
    char cwd[PATH_MAX];
    char *res = strdup(getcwd(cwd, sizeof(cwd)) ? cwd : ".");
    free(to_free);
    return res;
  }

  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) {
    char *res = strdup(p);
    free(to_free);
    return res;
  }

  size_t clen = strlen(cwd), plen = strlen(suffix);
  if (clen + 1 + plen >= PATH_MAX) {
    char *res = strdup(p);
    free(to_free);
    return res;
  }

  char *out = malloc(clen + 1 + plen + 1);
  if (!out) {
    char *res = strdup(p);
    free(to_free);
    return res;
  }
  memcpy(out, cwd, clen);
  out[clen] = '/';
  memcpy(out + clen + 1, suffix, plen + 1); /* copies the NUL terminator */
  free(to_free);
  return out;
}

/*
 * Resolve every SRC component of a --bind-mount / -B value string.
 * Format: SRC:DEST[,SRC:DEST,...]
 * Only the SRC part of each pair is a host-side path; DEST lives inside the
 * container namespace and is always absolute by convention.
 */
static char *resolve_bind_src(const char *val) {
  if (!val)
    return strdup("");

  size_t len = strlen(val);
  size_t tokens = 1;
  for (size_t i = 0; i < len; i++) {
    if (val[i] == ',')
      tokens++;
  }

  if (tokens > (SIZE_MAX - len - 1) / PATH_MAX)
    return strdup(val);

  /* Worst case: every token expands to PATH_MAX.
   * Use the heap - not the stack - to avoid blowing the stack in the daemon
   * handler process (which may have a smaller stack than main). */
  size_t bufsz = len + PATH_MAX * tokens + 1;
  char *copy = malloc(bufsz);
  char *out = malloc(bufsz);
  if (!copy || !out) {
    free(copy);
    free(out);
    return strdup(val);
  }
  memcpy(copy, val, len + 1);
  out[0] = '\0';

  char *sv, *tok = strtok_r(copy, ",", &sv);
  int first = 1;
  size_t off = 0;

  while (tok) {
    char *col = strchr(tok, ':');
    const char *dest = col ? col + 1 : "";
    if (col)
      *col = '\0';

    char *abs_src = ds_resolve_path_arg(tok);
    const char *src = abs_src ? abs_src : tok;

    if (off >= bufsz) {
      free(abs_src);
      free(copy);
      free(out);
      return strdup(val);
    }

    size_t avail = bufsz - off;
    int n = snprintf(out + off, avail, "%s%s%s%s", first ? "" : ",", src,
                     col ? ":" : "", dest);
    if (n < 0 || (size_t)n >= avail) {
      free(abs_src);
      free(copy);
      free(out);
      return strdup(val);
    }
    off += (size_t)n;
    free(abs_src);
    first = 0;
    tok = strtok_r(NULL, ",", &sv);
  }

  free(copy);
  char *result = strdup(out);
  free(out);
  return result;
}

/*
 * Table of options whose next argument (or = suffix) is a filesystem path.
 * Keeps ds_resolve_argv_paths() free of hard-coded option names.
 */
static const struct {
  const char *opt;
  int is_bind; /* 1 = --bind-mount: resolve the SRC component only */
} ds_path_opts[] = {
    {"--rootfs", 0}, {"-r", 0},           {"--rootfs-img", 0}, {"-i", 0},
    {"--conf", 0},   {"--config", 0},     {"-C", 0},           {"--env", 0},
    {"-E", 0},       {"--bind-mount", 1}, {"--bind", 1},       {"-B", 1},
    {NULL, 0},
};

void ds_resolve_argv_paths(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg || arg[0] != '-') /* fast skip: non-option args are not paths */
      continue;

    for (int j = 0; ds_path_opts[j].opt; j++) {
      const char *opt = ds_path_opts[j].opt;
      int bind = ds_path_opts[j].is_bind;
      size_t olen = strlen(opt);

      /* "--opt=VALUE" form */
      if (strncmp(arg, opt, olen) == 0 && arg[olen] == '=') {
        const char *val = arg + olen + 1;
        if (!*val || (val[0] == '/' && !bind))
          break; /* absolute paths (non-bind) don't need resolution */
        char *resolved =
            bind ? resolve_bind_src(val) : ds_resolve_path_arg(val);
        if (resolved) {
          char *new_arg = malloc(olen + 1 + strlen(resolved) + 1);
          if (new_arg) {
            memcpy(new_arg, opt, olen);
            new_arg[olen] = '=';
            strcpy(new_arg + olen + 1, resolved);
            argv[i] = new_arg; /* argv[i] was a kernel-provided pointer; safe to
                                  replace */
          }
          free(resolved);
        }
        break;
      }

      /* "--opt VALUE" form (value is the next element) */
      if (strcmp(arg, opt) == 0 && i + 1 < argc) {
        const char *val = argv[i + 1];
        if (!val || !*val || (val[0] == '/' && !bind))
          continue;
        char *resolved =
            bind ? resolve_bind_src(val) : ds_resolve_path_arg(val);
        if (resolved)
          argv[i + 1] = resolved; /* kernel-provided string; safe to replace */
        break;
      }
    }
  }
}

int is_ramfs(const char *path) {
  struct statfs sfs;
  if (statfs(path, &sfs) < 0)
    return 0;
  return (sfs.f_type == RAMFS_MAGIC || sfs.f_type == TMPFS_MAGIC);
}

int is_subpath(const char *parent, const char *child) {
  char *real_parent = ds_resolve_path_arg(parent);
  char *real_child = ds_resolve_path_arg(child);

  if (!real_parent || !real_child || !real_parent[0] || !real_child[0]) {
    free(real_parent);
    free(real_child);
    return 0;
  }

  size_t len = strlen(real_parent);

  /* Special case for the root directory */
  if (len == 1 && real_parent[0] == '/') {
    free(real_parent);
    free(real_child);
    return 1;
  }

  int result = 0;
  if (strncmp(real_parent, real_child, len) == 0) {
    if (real_child[len] == '\0' || real_child[len] == '/')
      result = 1;
  }

  free(real_parent);
  free(real_child);
  return result;
}

int is_running_in_termux(void) {
  if (getenv("TERMUX_VERSION") || getenv("TERMUX_APP__PACKAGE_NAME") ||
      getenv("TERMUX__PREFIX") || getenv("TERMUX_APP__APP_VERSION_CODE"))
    return 1;
  return 0;
}

int mkdir_p(const char *path, mode_t mode) {
  char tmp[PATH_MAX];
  char *p = NULL;
  size_t len;

  int r = snprintf(tmp, sizeof(tmp), "%s", path);
  if (r < 0 || (size_t)r >= sizeof(tmp)) {
    errno = ENAMETOOLONG;
    return -1;
  }

  len = strlen(tmp);
  if (len == 0)
    return 0;
  if (tmp[len - 1] == '/')
    tmp[len - 1] = '\0';

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, mode) < 0 && errno != EEXIST)
    return -1;
  return 0;
}

static int remove_recursive_handler(const char *fpath, const struct stat *sb,
                                    int tflag, struct FTW *ftwbuf) {
  (void)sb;
  (void)tflag;
  (void)ftwbuf;
  int r = remove(fpath);
  if (r)
    perror(fpath);
  return r;
}

int remove_recursive(const char *path) {
  return nftw(path, remove_recursive_handler, 64, FTW_DEPTH | FTW_PHYS);
}

/* ---------------------------------------------------------------------------
 * File I/O
 * ---------------------------------------------------------------------------*/

int write_file(const char *path, const char *content) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0)
    return -1;

  size_t len = strlen(content);
  ssize_t w = write_all(fd, content, len);
  int close_ret = close(fd);

  return (w == (ssize_t)len && close_ret == 0) ? 0 : -1;
}

int write_file_atomic(const char *path, const char *content) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);

  if (write_file(tmp, content) < 0)
    return -1;

  /* fsync before rename - ensures data hits disk on Android before reboot */
  int sync_fd = open(tmp, O_RDONLY | O_CLOEXEC);
  if (sync_fd >= 0) {
    fsync(sync_fd);
    close(sync_fd);
  }

  if (rename(tmp, path) < 0) {
    unlink(tmp);
    return -1;
  }
  return 0;
}

ssize_t write_all(int fd, const void *buf, size_t count) {
  const char *p = buf;
  size_t remaining = count;
  while (remaining > 0) {
    ssize_t w = write(fd, p, remaining);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    p += w;
    remaining -= (size_t)w;
  }
  return (ssize_t)count;
}

int read_file(const char *path, char *buf, size_t size) {
  if (size == 0)
    return -1;

  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  ssize_t total_read = 0;
  ssize_t r = 1;
  while ((size_t)total_read < size - 1 &&
         (r = read(fd, buf + total_read, size - 1 - (size_t)total_read)) > 0) {
    total_read += r;
  }

  close(fd);

  if (r < 0)
    return -1;

  buf[total_read] = '\0';

  /* strip trailing newline and carriage return */
  while (total_read > 0 &&
         (buf[total_read - 1] == '\n' || buf[total_read - 1] == '\r')) {
    buf[--total_read] = '\0';
  }

  return (int)total_read;
}

/* ---------------------------------------------------------------------------
 * UUID generation  - 32 hex chars from /dev/urandom
 * ---------------------------------------------------------------------------*/

int generate_uuid(char *buf, size_t size) {
  if (!buf || size < DS_UUID_LEN + 1)
    return -1;

  unsigned char raw[DS_UUID_LEN / 2];

  /* Primary path: /dev/urandom */
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t r = read(fd, raw, sizeof(raw));
    close(fd);

    if (r == (ssize_t)sizeof(raw)) {
      for (int i = 0; i < (int)sizeof(raw); i++)
        snprintf(buf + i * 2, 3, "%02x", raw[i]);

      buf[DS_UUID_LEN] = '\0';
      return 0;
    }
  }

  /* Fallback path: seeded rand() */
  static int seeded = 0;
  if (!seeded) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    unsigned int seed =
        (unsigned int)(ts.tv_nsec ^ ts.tv_sec ^ getpid() ^ getppid());

    srand(seed);
    seeded = 1;
  }

  for (int i = 0; i < DS_UUID_LEN / 2; i++)
    raw[i] = (unsigned char)(rand() & 0xFF);

  for (int i = 0; i < (int)sizeof(raw); i++)
    snprintf(buf + i * 2, 3, "%02x", raw[i]);

  buf[DS_UUID_LEN] = '\0';
  return 0;
}

/* ---------------------------------------------------------------------------
 * PID collection - read numeric entries from /proc
 * ---------------------------------------------------------------------------*/

int collect_pids(pid_t **pids_out, size_t *count_out) {
  if (!pids_out || !count_out)
    return -1;

  *pids_out = NULL;
  *count_out = 0;

  DIR *d = opendir("/proc");
  if (!d)
    return -1;

  size_t cap = 256;
  size_t count = 0;

  pid_t *pids = malloc(cap * sizeof(pid_t));
  if (!pids) {
    closedir(d);
    return -1;
  }

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {

    /* Do NOT trust ent->d_type.
       Some filesystems (including Android /proc) return DT_UNKNOWN. */

    char *end;
    errno = 0;
    long val = strtol(ent->d_name, &end, 10);

    /* Must be a pure positive number */
    if (errno != 0 || *end != '\0' || val <= 0)
      continue;

    if (count >= cap) {
      cap *= 2;
      pid_t *tmp = realloc(pids, cap * sizeof(pid_t));
      if (!tmp) {
        free(pids);
        closedir(d);
        return -1;
      }
      pids = tmp;
    }

    pids[count++] = (pid_t)val;
  }

  closedir(d);

  *pids_out = pids;
  *count_out = count;
  return 0;
}

/* ---------------------------------------------------------------------------
 * /proc path helpers
 * ---------------------------------------------------------------------------*/

int build_proc_root_path(pid_t pid, const char *suffix, char *buf,
                         size_t size) {
  int r;
  if (suffix && suffix[0])
    r = snprintf(buf, size, DS_PROC_ROOT_FMT "%s", pid, suffix);
  else
    r = snprintf(buf, size, DS_PROC_ROOT_FMT, pid);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

int parse_os_release(const char *rootfs_path, char *id_out, char *ver_out,
                     size_t out_size) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%.4000s" DS_OS_RELEASE, rootfs_path);

  char buf[4096];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;

  /* Default values */
  safe_strncpy(id_out, "linux", out_size);
  if (ver_out)
    ver_out[0] = '\0';

  /* Parse ID */
  char *p = strstr(buf, "\nID=");
  if (!p && strncmp(buf, "ID=", 3) == 0)
    p = buf;

  if (p) {
    if (*p == '\n')
      p++;
    p += 3;
    if (*p == '"')
      p++;
    int i = 0;
    while (p[i] && p[i] != '"' && p[i] != '\n' && (size_t)i < out_size - 1) {
      id_out[i] = p[i];
      i++;
    }
    id_out[i] = '\0';
  }

  /* Parse VERSION_ID */
  if (ver_out) {
    p = strstr(buf, "VERSION_ID=");
    if (p) {
      p += 11;
      if (*p == '"')
        p++;
      int i = 0;
      while (p[i] && p[i] != '"' && p[i] != '\n' && (size_t)i < out_size - 1) {
        ver_out[i] = p[i];
        i++;
      }
      ver_out[i] = '\0';
    }
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Grep file for a pattern (simple substring search)
 * ---------------------------------------------------------------------------*/

int grep_file(const char *path, const char *pattern) {
  char buf[16384];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;
  return strstr(buf, pattern) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * PID file helpers
 * ---------------------------------------------------------------------------*/

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

/* ---------------------------------------------------------------------------
 * Mount sidecar files (.mount)
 * ---------------------------------------------------------------------------*/

/* Internal helper to convert pidfile path to mount sidecar path: foo.pid ->
 * foo.mount */
static void pidfile_to_mountfile(const char *pidfile, char *buf, size_t size) {
  safe_strncpy(buf, pidfile, size);
  char *dot = strrchr(buf, '.');
  if (dot && strcmp(dot, DS_EXT_PID) == 0) {
    /* If it ends in .pid, replace it */
    snprintf(dot, size - (size_t)(dot - buf), DS_EXT_MOUNT);
  } else {
    /* Otherwise just append */
    strncat(buf, DS_EXT_MOUNT, size - strlen(buf) - 1);
  }
}

/* Save mount path alongside a pidfile: foo.pid -> foo.mount */
int save_mount_path(const char *pidfile, const char *mount_path) {
  char mpath[PATH_MAX];
  pidfile_to_mountfile(pidfile, mpath, sizeof(mpath));
  return write_file(mpath, mount_path);
}

int read_mount_path(const char *pidfile, char *buf, size_t size) {
  char mpath[PATH_MAX];
  pidfile_to_mountfile(pidfile, mpath, sizeof(mpath));
  return read_file(mpath, buf, size);
}

int remove_mount_path(const char *pidfile) {
  char mpath[PATH_MAX];
  pidfile_to_mountfile(pidfile, mpath, sizeof(mpath));
  return unlink(mpath);
}

/* ---------------------------------------------------------------------------
 * Kernel firmware search path management
 *
 * Android kernels patch firmware_class.c to support a comma-separated list
 * of custom search paths in the single 256-byte fw_path_para buffer
 * (e.g. "/vendor/firmware,/efs/wifi").  Writing a newline to the sysfs node
 * pops the first entry but always preserves the tail - so the last path can
 * never be fully cleared.  We therefore never attempt a full clear; removal
 * is best-effort and skipped when it would leave an empty string.
 *
 * Only called when --hw-access is active AND /lib/firmware exists in the
 * rootfs - both conditions are enforced at every call site.
 * Not supported on desktop Linux - both functions are no-ops there.
 * ---------------------------------------------------------------------------*/

/* Android kernel fw_path_para is 256 bytes including the NUL terminator. */
#define FW_PATH_BUF_SIZE 256

/*
 * Token-aware removal: walk the comma-separated list and rebuild it without
 * the matching entry.  Matches on exact token boundaries (not substrings) to
 * avoid accidentally removing "/mnt/Droidspaces/Void" when removing
 * "/mnt/Droidspaces/Void2".
 * Returns the length of the rebuilt string (0 = only entry, do not write).
 */
static int fw_remove_token(const char *buf, const char *token, char *out,
                           size_t out_size) {
  size_t token_len = strlen(token);
  const char *p = buf;
  int first = 1;
  out[0] = '\0';

  while (*p) {
    const char *comma = strchr(p, ',');
    size_t seg_len = comma ? (size_t)(comma - p) : strlen(p);

    if (!(seg_len == token_len && memcmp(p, token, token_len) == 0)) {
      /* Not our token - keep it */
      if (!first)
        strncat(out, ",", out_size - strlen(out) - 1);
      strncat(out, p,
              (seg_len < out_size - strlen(out) - 1)
                  ? seg_len
                  : out_size - strlen(out) - 1);
      first = 0;
    }

    if (!comma)
      break;
    p = comma + 1;
  }

  return (int)strlen(out);
}

void firmware_path_add(const char *fw_path) {
  /* Firmware path manipulation is an Android-kernel-specific feature.
   * Desktop Linux firmware_class does not support this sysfs node in the
   * same way - skip entirely on non-Android hosts. */
  if (!is_android())
    return;

  /* Bail silently if /lib/firmware is absent in the rootfs. */
  struct stat st;
  if (stat(fw_path, &st) < 0)
    return;

  /* Read the current comma-separated path list.
   * read_file() already strips trailing newlines. */
  char current[FW_PATH_BUF_SIZE] = {0};
  read_file(DS_FW_PATH_FILE, current, sizeof(current));

  /* Idempotent - don't add if already present as an exact token. */
  size_t fw_len = strlen(fw_path);
  const char *p = current;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t seg_len = comma ? (size_t)(comma - p) : strlen(p);
    if (seg_len == fw_len && memcmp(p, fw_path, fw_len) == 0)
      return; /* already there */
    if (!comma)
      break;
    p = comma + 1;
  }

  /* Build "fw_path,existing" - prepend so container firmware wins over OEM
   * defaults.  Guard against the 255-char string limit of fw_path_para.
   * Pre-validate lengths so the compiler can confirm no truncation occurs. */
  char new_path[FW_PATH_BUF_SIZE] = {0};
  if (current[0]) {
    size_t needed =
        strlen(fw_path) + 1 /* comma */ + strlen(current) + 1 /* NUL */;
    if (needed > sizeof(new_path)) {
      ds_warn("[FW] firmware path too long to prepend '%s' - skipping",
              fw_path);
      return;
    }
    /* Lengths validated - safe to build without truncation. */
    safe_strncpy(new_path, fw_path, sizeof(new_path));
    strncat(new_path, ",", sizeof(new_path) - strlen(new_path) - 1);
    strncat(new_path, current, sizeof(new_path) - strlen(new_path) - 1);
  } else {
    safe_strncpy(new_path, fw_path, sizeof(new_path));
  }

  ds_log("[FW] Adding firmware path: %s", fw_path);
  write_file(DS_FW_PATH_FILE, new_path);
}

void firmware_path_remove(const char *fw_path) {
  if (!is_android())
    return;

  /* Read current list - read_file() strips trailing newlines. */
  char current[FW_PATH_BUF_SIZE] = {0};
  if (read_file(DS_FW_PATH_FILE, current, sizeof(current)) < 0)
    return;

  char new_path[FW_PATH_BUF_SIZE] = {0};
  int new_len = fw_remove_token(current, fw_path, new_path, sizeof(new_path));

  if (new_len == 0) {
    /* Our path was the only entry.  The Android kernel never allows a full
     * clear - writing empty would be a no-op anyway - so just leave it. */
    ds_log("[FW] Skipping firmware path removal (last entry): %s", fw_path);
    return;
  }

  ds_log("[FW] Removing firmware path: %s", fw_path);
  write_file(DS_FW_PATH_FILE, new_path);
}

/* ---------------------------------------------------------------------------
 * Safe Command Execution (fork + execvp)
 * ---------------------------------------------------------------------------*/

static int internal_run(char *const argv[], int quiet) {
  pid_t pid = fork();
  if (pid < 0)
    return -1;

  if (pid == 0) {
    if (quiet) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(devnull);
      }
    }
    execvp(argv[0], argv);
    _exit(127); /* exec failed */
  }

  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;

  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return -1;
}

int run_command(char *const argv[]) { return internal_run(argv, 0); }
int run_command_quiet(char *const argv[]) { return internal_run(argv, 1); }

/* run_command_log: runs argv, captures stderr and emits it via ds_log so
 * iptables error messages are visible in the droidspaces log on failure. */
int run_command_log(char *const argv[]) {
  int pipefd[2];
  if (pipe(pipefd) < 0)
    return internal_run(argv, 0);

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    execvp(argv[0], argv);
    _exit(127);
  }

  close(pipefd[1]);

  char buf[512];
  FILE *f = fdopen(pipefd[0], "r");
  if (f) {
    while (fgets(buf, sizeof(buf), f)) {
      size_t l = strlen(buf);
      while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
        buf[--l] = '\0';
      if (l > 0)
        ds_log("[IPT] %s", buf);
    }
    fclose(f);
  } else {
    close(pipefd[0]);
  }

  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---------------------------------------------------------------------------
 * FD Passing (SCM_RIGHTS)
 * ---------------------------------------------------------------------------*/

int ds_send_fd(int sock, int fd) {
  struct msghdr msg = {0};
  char buf[CMSG_SPACE(sizeof(int))];
  memset(buf, 0, sizeof(buf));

  struct iovec io = {.iov_base = "FD", .iov_len = 2};

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));

  *((int *)CMSG_DATA(cmsg)) = fd;

  if (sendmsg(sock, &msg, 0) < 0)
    return -1;

  return 0;
}

int ds_recv_fd(int sock) {
  struct msghdr msg = {0};
  char ctrl_buf[CMSG_SPACE(sizeof(int))];
  char data_buf[2];
  struct iovec io = {.iov_base = data_buf, .iov_len = sizeof(data_buf)};

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = ctrl_buf;
  msg.msg_controllen = sizeof(ctrl_buf);

  if (recvmsg(sock, &msg, 0) < 0)
    return -1;

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS)
    return -1;

  return *((int *)CMSG_DATA(cmsg));
}

/* ---------------------------------------------------------------------------
 * System helpers
 * ---------------------------------------------------------------------------*/

int get_kernel_version(int *major, int *minor) {
  struct utsname uts;
  if (uname(&uts) < 0)
    return -1;

  if (sscanf(uts.release, "%d.%d", major, minor) != 2)
    return -1;

  return 0;
}

void check_kernel_recommendation(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) < 0)
    return;

  if (major < DS_RECOMMENDED_KERNEL_MAJOR ||
      (major == DS_RECOMMENDED_KERNEL_MAJOR &&
       minor < DS_RECOMMENDED_KERNEL_MINOR)) {
    ds_warn("Your kernel (%d.%d) is below recommended %d.%d - "
            "some functions might be unstable.",
            major, minor, DS_RECOMMENDED_KERNEL_MAJOR,
            DS_RECOMMENDED_KERNEL_MINOR);
    fflush(stdout);
  }
}

void rotate_log(const char *path, size_t max_size) {
  struct stat st;
  if (stat(path, &st) == 0 && (size_t)st.st_size >= max_size) {
    char old_path[PATH_MAX + 8];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    rename(path, old_path);
  }
}

static void write_to_log_file(const char *name, const char *component,
                              const char *raw_msg) {
  if (!name || !name[0])
    return;

  char log_dir[PATH_MAX];
  char safe_log_name[256];
  sanitize_container_name(name, safe_log_name, sizeof(safe_log_name));
  snprintf(log_dir, sizeof(log_dir), "%.2048s/" DS_LOGS_SUBDIR "/%.256s",
           get_workspace_dir(), safe_log_name);
  mkdir_p(log_dir, 0755);

  char log_path[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "%.4090s/log", log_dir);

  rotate_log(log_path, 2 * 1024 * 1024);

  FILE *f = fopen(log_path, "ae"); /* append + close-on-exec */
  if (!f)
    return;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm;
  localtime_r(&ts.tv_sec, &tm);
  fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] %s\n",
          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
          tm.tm_sec, ts.tv_nsec / 1000000, component, raw_msg);
  fclose(f);
}

void ds_log_internal(const char *prefix, const char *color, int is_err,
                     const char *fmt, ...) {
  char raw_msg[8192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(raw_msg, sizeof(raw_msg), fmt, ap);
  va_end(ap);

  /* Always log to file if container name is known */
  if (ds_log_container_name[0]) {
    write_to_log_file(ds_log_container_name, "main", raw_msg);
  }

  /* Decide if we should print to terminal */
  if (ds_log_silent && !is_err)
    return;

  /* Filter out [DEBUG] and [IPT] prefixes from terminal output */
  if (!is_err) {
    if (strncmp(raw_msg, "[DEBUG]", 7) == 0 ||
        strncmp(raw_msg, "[CGROUP]", 8) == 0 ||
        strncmp(raw_msg, "[IPT]", 5) == 0 ||
        strncmp(raw_msg, "[NET]", 5) == 0 ||
        strncmp(raw_msg, "[SEC]", 5) == 0 ||
        strncmp(raw_msg, "[GPU]", 5) == 0 || strncmp(raw_msg, "[FW]", 4) == 0 ||
        strncmp(raw_msg, "[DHCP]", 6) == 0) {
      return;
    }
  }

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
    write_to_log_file(ds_log_container_name, "fatal", raw_msg);
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

  write_to_log_file(name, "monitor", raw_msg);
}

void print_ds_banner(void) {
  printf(C_CYAN C_BOLD "— Welcome to " C_WHITE DS_PROJECT_NAME
                       " v" DS_VERSION C_CYAN " ! —" C_RESET "\r\n\r\n");
  fflush(stdout);
}

void print_privileged_warning(int privileged_mask) {
  if (privileged_mask <= 0)
    return;

  printf(C_BOLD C_RED "WARNING: PRIVILEGED MODE ACTIVE - DEVICE SECURITY "
                      "COMPROMISED" C_RESET "\r\n\r\n");
  fflush(stdout);
}

int is_systemd_rootfs(const char *path) {
  if (!path)
    return 0;

  char buf[PATH_MAX];
  struct stat st;
  size_t path_len = strlen(path);

  /* Standard systemd binary locations (not present on NixOS -- nix store). */
  const char *check_paths[] = {"/lib/systemd/systemd",
                               "/usr/lib/systemd/systemd", "/bin/systemd",
                               "/usr/bin/systemd"};

  for (size_t i = 0; i < sizeof(check_paths) / sizeof(check_paths[0]); i++) {
    size_t check_len = strlen(check_paths[i]);
    if (path_len + check_len >= sizeof(buf))
      continue;

    memcpy(buf, path, path_len);
    memcpy(buf + path_len, check_paths[i], check_len + 1);
    if (stat(buf, &st) == 0 && S_ISREG(st.st_mode)) {
      return 1;
    }
  }

  /* Fallback: /sbin/init symlink target -- covers systemd and NixOS. */
  if (path_len + 12 <= sizeof(buf)) { /* 10 chars + '/' prefix + '\0' = 12 */
    memcpy(buf, path, path_len);
    memcpy(buf + path_len, "/sbin/init", 11);
    char link_target[PATH_MAX];
    ssize_t len = readlink(buf, link_target, sizeof(link_target) - 1);
    if (len != -1) {
      link_target[len] = '\0';
      if (strstr(link_target, "systemd"))
        return 1;
      /* NixOS: /sbin/init -> /nix/store/<hash>-nixos-system-.../init
       * That wrapper execs systemd. The nix store path is unique enough. */
      if (strstr(link_target, "/nix/store"))
        return 1;
    }
  }

  return 0;
}

/* Probe the rootfs at `path` to identify its init system.
 * Uses the same stat+readlink pattern as is_systemd_rootfs. */
ds_init_type_t detect_container_init(const char *path) {
  if (!path || path[0] == '\0')
    return DS_INIT_UNKNOWN;

  char buf[PATH_MAX];
  struct stat st;
  size_t plen = strlen(path);

  /* Helper: build "path + suffix" into buf, return 1 on success */
#define PROBE_PATH(suffix)                                                     \
  (plen + sizeof(suffix) - 1 < sizeof(buf) &&                                  \
   (memcpy(buf, path, plen), memcpy(buf + plen, suffix, sizeof(suffix)), 1))

  /* systemd -- reuse existing logic via is_systemd_rootfs */
  if (is_systemd_rootfs(path))
    return DS_INIT_SYSTEMD;

  /* procd (OpenWrt) */
  if ((PROBE_PATH("/sbin/procd") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)) ||
      (PROBE_PATH("/usr/sbin/procd") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)))
    return DS_INIT_PROCD;

  /* openrc */
  if ((PROBE_PATH("/sbin/openrc-init") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)) ||
      (PROBE_PATH("/usr/bin/openrc-init") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)) ||
      (PROBE_PATH("/sbin/openrc") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)))
    return DS_INIT_OPENRC;

  /* runit */
  if ((PROBE_PATH("/sbin/runit") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)) ||
      (PROBE_PATH("/usr/bin/runit") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)))
    return DS_INIT_RUNIT;

  /* s6 */
  if ((PROBE_PATH("/bin/s6-svscan") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)) ||
      (PROBE_PATH("/usr/bin/s6-svscan") && stat(buf, &st) == 0 &&
       S_ISREG(st.st_mode)))
    return DS_INIT_S6;

  /* sysvinit: /sbin/init is NOT a symlink, or symlink points to sysvinit */
  if (PROBE_PATH("/sbin/init")) {
    char target[PATH_MAX];
    ssize_t len = readlink(buf, target, sizeof(target) - 1);
    if (len == -1) {
      /* not a symlink -> classic sysvinit binary */
      if (stat(buf, &st) == 0 && S_ISREG(st.st_mode))
        return DS_INIT_SYSVINIT;
    } else {
      target[len] = '\0';
      if (strstr(target, "busybox"))
        return DS_INIT_BUSYBOX;
      if (strstr(target, "sysvinit") || strstr(target, "init.sysv"))
        return DS_INIT_SYSVINIT;
    }
  }

#undef PROBE_PATH

  return DS_INIT_UNKNOWN;
}

int get_user_shell(const char *user, char *shell_buf, size_t size) {
  if (!user || user[0] == '\0' || !shell_buf || size == 0)
    return -1;

  FILE *f = fopen("/etc/passwd", "re");
  if (!f)
    return -1;

  char line[1024];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    /* Format: user:pw:uid:gid:gecos:home:shell */
    char line_copy[1024];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    char *saveptr;
    char *name = strtok_r(line_copy, ":", &saveptr);
    if (!name || strcmp(name, user) != 0)
      continue;

    /* Skip 5 fields to reach the shell (field 7) */
    for (int i = 0; i < 5; i++) {
      if (!strtok_r(NULL, ":", &saveptr))
        break;
    }
    char *shell = strtok_r(NULL, ":\n", &saveptr);

    if (shell) {
      safe_strncpy(shell_buf, shell, size);
      found = 1;
      break;
    }
  }

  fclose(f);
  return found ? 0 : -1;
}

int get_selinux_context(const char *path, char *buf, size_t size) {
  if (!path || !buf || size == 0)
    return -1;

  /* Use lgetxattr to read the security.selinux attribute */
  ssize_t len = lgetxattr(path, "security.selinux", buf, size - 1);
  if (len < 0) {
#ifdef SYS_lgetxattr
    len = syscall(SYS_lgetxattr, path, "security.selinux", buf, size - 1);
#endif
  }

  /* FIX: Check bounds before writing null terminator */
  if (len < 0 || len >= (ssize_t)(size - 1)) {
    return -1;
  }

  buf[len] = '\0';
  return 0;
}

int ds_get_selinux_status(void) {
  char buf[16];
  if (read_file("/sys/fs/selinux/enforce", buf, sizeof(buf)) < 0)
    return -1;
  return atoi(buf);
}

void ds_set_selinux_permissive(int enable) {
  int status = ds_get_selinux_status();
  if (status == -1) {
    if (enable)
      ds_warn("SELinux not supported or interface missing. Skipping permissive "
              "mode.");
    return;
  }

  if (enable) {
    if (status == 1) {
      ds_log("Setting SELinux to permissive...");
      if (write_file("/sys/fs/selinux/enforce", "0") < 0) {
        /* Try setenforce command as fallback */
        char *args[] = {"setenforce", "0", NULL};
        run_command_quiet(args);
      }
    }
  } else {
    /* Set back to Enforcing if it's currently Permissive */
    if (status == 0) {
      if (write_file("/sys/fs/selinux/enforce", "1") < 0) {
        char *args[] = {"setenforce", "1", NULL};
        run_command_quiet(args);
      }
    }
  }
}

int set_selinux_context(const char *path, const char *context) {
  if (!path || !context)
    return -1;

  size_t len = strlen(context);
  if (lsetxattr(path, "security.selinux", context, len, 0) < 0) {
#ifdef SYS_lsetxattr
    if (syscall(SYS_lsetxattr, path, "security.selinux", context, len, 0) < 0) {
      return -1;
    }
#else
    return -1;
#endif
  }

  return 0;
}

int copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "re");
  if (!in)
    return -1;
  FILE *out = fopen(dst, "we");
  if (!out) {
    fclose(in);
    return -1;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return -1;
    }
  fclose(in);
  fclose(out);
  return 0;
}

/* ---------------------------------------------------------------------------
 * show_container_usage
 *
 * Prints uptime, CPU%, and RAM usage for a running container.
 * Works entirely from the host side - no namespace entry required.
 * Compatible with kernel 3.10+.
 *
 * Method:
 *   UPTIME  - field 22 (starttime) of /proc/<init_pid>/stat converted to
 *             seconds, subtracted from /proc/uptime.
 *   MEMORY  - PID namespace walk: any PID whose ns/pid matches container
 *             init's namespace is in the container. Sum VmRSS.
 *   CPU     - same walk, sum utime+stime jiffies, two samples 1s apart.
 *             Divide delta by host CPU delta for percentage.
 *             Per-mille avoids integer floor on sub-1% values.
 *
 *   OPTIMISATION: walk 1 collects RAM + CPU sample 1 simultaneously.
 *   walk 2 (after sleep) collects CPU sample 2 only. Total: 2 walks.
 *
 * Output (machine-parseable key=value):
 *   UPTIME_SEC=<seconds>
 *   UPTIME=<Xd Xh Xm Xs | Xh Xm Xs>
 *   RAM_USED_KB=<kb>
 *   RAM_TOTAL_KB=<kb>
 *   CPU_PERMILL=<0-1000>
 * ---------------------------------------------------------------------------*/
long ds_get_container_uptime(pid_t pid) {
  if (pid <= 0)
    return -1;

  long clk_tck = sysconf(_SC_CLK_TCK);
  if (clk_tck <= 0)
    clk_tck = 100;

  char stat_path[PATH_MAX];
  snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);

  FILE *f = fopen(stat_path, "r");
  if (!f)
    return -1;

  unsigned long long start_ticks = 0;
  /* starttime is the 22nd field */
  for (int i = 1; i <= 21; i++) {
    if (fscanf(f, "%*s") == EOF)
      break;
  }
  if (fscanf(f, "%llu", &start_ticks) != 1)
    start_ticks = 0;
  fclose(f);

  if (start_ticks == 0)
    return -1;

  f = fopen("/proc/uptime", "r");
  if (!f)
    return -1;

  double host_uptime_sec = 0.0;
  if (fscanf(f, "%lf", &host_uptime_sec) != 1)
    host_uptime_sec = 0.0;
  fclose(f);

  long uptime_sec =
      (long)(host_uptime_sec - (double)start_ticks / (double)clk_tck);
  return (uptime_sec < 0) ? 0 : uptime_sec;
}

void ds_format_uptime(long uptime_sec, char *buf, size_t size) {
  if (uptime_sec < 0) {
    safe_strncpy(buf, "unknown", size);
    return;
  }

  int days = uptime_sec / 86400;
  int hours = (uptime_sec % 86400) / 3600;
  int mins = (uptime_sec % 3600) / 60;
  int secs = uptime_sec % 60;

  char tmp[128] = {0};
  int pos = 0;

  if (days > 0)
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%dd ", days);
  if (hours > 0 || days > 0)
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%dh ", hours);
  if (mins > 0 || hours > 0 || days > 0)
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%dm ", mins);
  snprintf(tmp + pos, sizeof(tmp) - pos, "%ds", secs);

  safe_strncpy(buf, tmp, size);
}

int show_container_usage(struct ds_config *cfg) {
  pid_t pid = 0;

  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running.", cfg->container_name);
    return -1;
  }

  /* -----------------------------------------------------------------------
   * UPTIME
   * -----------------------------------------------------------------------*/
  long uptime_sec = ds_get_container_uptime(pid);
  char uptime_str[128];
  ds_format_uptime(uptime_sec, uptime_str, sizeof(uptime_str));

  /* -----------------------------------------------------------------------
   * PID namespace of container init
   * -----------------------------------------------------------------------*/
  char ns_init_path[PATH_MAX];
  snprintf(ns_init_path, sizeof(ns_init_path), "/proc/%d/ns/pid", (int)pid);
  char container_ns[256] = {0};
  ssize_t ns_len =
      readlink(ns_init_path, container_ns, sizeof(container_ns) - 1);
  if (ns_len <= 0) {
    ds_error("Failed to read PID namespace of container init: %s",
             strerror(errno));
    return -1;
  }
  container_ns[ns_len] = '\0';

  /* -----------------------------------------------------------------------
   * WALK 1: collect RAM + CPU sample 1 in a single /proc pass
   * -----------------------------------------------------------------------*/
  long ram_used_kb = 0;
  long long cpu_t1 = 0;
  long long cpu_host_t1 = 0;
  FILE *f = NULL;

  DIR *proc_dir = opendir("/proc");
  if (!proc_dir) {
    ds_error("Failed to open /proc: %s", strerror(errno));
    return -1;
  }
  struct dirent *de;
  while ((de = readdir(proc_dir)) != NULL) {
    if (de->d_name[0] < '1' || de->d_name[0] > '9')
      continue;

    /* check PID namespace */
    char ns_path[64];
    snprintf(ns_path, sizeof(ns_path), "/proc/%s/ns/pid", de->d_name);
    char ns_buf[256] = {0};
    ssize_t r = readlink(ns_path, ns_buf, sizeof(ns_buf) - 1);
    if (r <= 0)
      continue;
    ns_buf[r] = '\0';
    if (strcmp(ns_buf, container_ns) != 0)
      continue;

    /* RAM: VmRSS from /proc/<pid>/status */
    char status_path[64];
    snprintf(status_path, sizeof(status_path), "/proc/%s/status", de->d_name);
    FILE *sf = fopen(status_path, "r");
    if (sf) {
      char line[128];
      while (fgets(line, sizeof(line), sf)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
          long rss = 0;
          if (sscanf(line + 6, "%ld", &rss) == 1)
            ram_used_kb += rss;
          break;
        }
      }
      fclose(sf);
    }

    /* CPU sample 1: utime+stime from /proc/<pid>/stat fields 14+15 */
    char pstat_path[64];
    snprintf(pstat_path, sizeof(pstat_path), "/proc/%s/stat", de->d_name);
    FILE *pf = fopen(pstat_path, "r");
    if (pf) {
      long long utime = 0, stime = 0;
      for (int i = 1; i <= 13; i++)
        if (fscanf(pf, "%*s") == EOF)
          break;
      if (fscanf(pf, "%lld %lld", &utime, &stime) == 2)
        cpu_t1 += utime + stime;
      fclose(pf);
    }
  }
  closedir(proc_dir);

  /* host CPU total sample 1 */
  f = fopen("/proc/stat", "r");
  if (f) {
    long long u, n, s, i, iow, irq, sirq;
    if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld", &u, &n, &s, &i,
               &iow, &irq, &sirq) == 7)
      cpu_host_t1 = u + n + s + i + iow + irq + sirq;
    fclose(f);
  }

  /* total device RAM from /proc/meminfo */
  long ram_total_kb = 0;
  f = fopen("/proc/meminfo", "r");
  if (f) {
    char line[128];
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "MemTotal:", 9) == 0) {
        sscanf(line + 9, "%ld", &ram_total_kb);
        break;
      }
    }
    fclose(f);
  }

  /* 250ms measurement window - short enough for a responsive UI,
   * long enough for a meaningful CPU delta (1 jiffie = 10ms at HZ=100,
   * so 250ms gives 25-jiffie resolution = ~0.4% minimum granularity). */
  struct timespec ts = {0, 250000000L};
  nanosleep(&ts, NULL);

  /* -----------------------------------------------------------------------
   * WALK 2: CPU sample 2 only
   * -----------------------------------------------------------------------*/
  long long cpu_t2 = 0;
  long long cpu_host_t2 = 0;

  proc_dir = opendir("/proc");
  if (proc_dir) {
    while ((de = readdir(proc_dir)) != NULL) {
      if (de->d_name[0] < '1' || de->d_name[0] > '9')
        continue;
      char ns_path[64];
      snprintf(ns_path, sizeof(ns_path), "/proc/%s/ns/pid", de->d_name);
      char ns_buf[256] = {0};
      ssize_t r = readlink(ns_path, ns_buf, sizeof(ns_buf) - 1);
      if (r <= 0)
        continue;
      ns_buf[r] = '\0';
      if (strcmp(ns_buf, container_ns) != 0)
        continue;

      char pstat_path[64];
      snprintf(pstat_path, sizeof(pstat_path), "/proc/%s/stat", de->d_name);
      FILE *pf = fopen(pstat_path, "r");
      if (pf) {
        long long utime = 0, stime = 0;
        for (int i = 1; i <= 13; i++)
          if (fscanf(pf, "%*s") == EOF)
            break;
        if (fscanf(pf, "%lld %lld", &utime, &stime) == 2)
          cpu_t2 += utime + stime;
        fclose(pf);
      }
    }
    closedir(proc_dir);
  }

  f = fopen("/proc/stat", "r");
  if (f) {
    long long u, n, s, i, iow, irq, sirq;
    if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld", &u, &n, &s, &i,
               &iow, &irq, &sirq) == 7)
      cpu_host_t2 = u + n + s + i + iow + irq + sirq;
    fclose(f);
  }

  long long delta_container = cpu_t2 - cpu_t1;
  long long delta_host = cpu_host_t2 - cpu_host_t1;
  if (delta_container < 0)
    delta_container = 0;
  long cpu_permill =
      (delta_host > 0) ? (long)(delta_container * 1000 / delta_host) : 0;
  if (cpu_permill > 1000)
    cpu_permill = 1000;

  /* -----------------------------------------------------------------------
   * Output - machine-parseable key=value, one per line
   * -----------------------------------------------------------------------*/
  printf("UPTIME_SEC=%ld\n", uptime_sec);
  printf("UPTIME=%s\n", uptime_str);
  printf("RAM_USED_KB=%ld\n", ram_used_kb);
  printf("RAM_TOTAL_KB=%ld\n", ram_total_kb);
  printf("CPU_PERMILL=%ld\n", cpu_permill);

  return 0;
}

/* ---------------------------------------------------------------------------
 * Bind Mount Sorting
 * ---------------------------------------------------------------------------*/

static int compare_bind_mounts(const void *a, const void *b) {
  const struct ds_bind_mount *ma = (const struct ds_bind_mount *)a;
  const struct ds_bind_mount *mb = (const struct ds_bind_mount *)b;
  return strcmp(ma->dest, mb->dest);
}

void sort_bind_mounts(struct ds_config *cfg) {
  if (!cfg || cfg->bind_count <= 1 || !cfg->binds)
    return;

  qsort(cfg->binds, cfg->bind_count, sizeof(struct ds_bind_mount),
        compare_bind_mounts);
}

int validate_container_name(const char *name) {
  if (!name || !name[0])
    return 0;

  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 0;

  size_t len = strlen(name);
  if (len >= 256)
    return 0;

  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' '))
      return 0;
  }

  return 1;
}

int reject_container_name(const char *name) {
  if (!validate_container_name(name)) {
    ds_error("Invalid container name '%s'. Use only letters, numbers, "
             "'.', '_', '-' and spaces.",
             name);
    return -1;
  }
  return 0;
}

int validate_bind_destination(const char *dest) {
  if (!dest || dest[0] != '/' || dest[1] == '\0')
    return 0;

  if (strlen(dest) >= PATH_MAX)
    return 0;

  const char *p = dest;
  while (*p) {
    while (*p == '/')
      p++;
    const char *start = p;
    while (*p && *p != '/')
      p++;
    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;
    if ((len == 1 && start[0] == '.') ||
        (len == 2 && start[0] == '.' && start[1] == '.'))
      return 0;
    for (size_t i = 0; i < len; i++) {
      if (iscntrl((unsigned char)start[i]))
        return 0;
    }
  }

  return 1;
}

/* Parse human-readable size: "512M", "1G", "2048" (bytes). Returns -1 on error.
 *
 * Use integer and fractional parts separately to avoid precision loss
 * for large values (e.g. 8192G overflows double's 53-bit mantissa):
 *   - Integer part: strtoll → exact long long arithmetic.
 *   - Fractional part (e.g. "1.5G"): limited double multiplication only for
 *     the sub-unit portion, keeping precision loss < 1 byte.
 */
long long ds_parse_size(const char *str) {
  if (!str || !*str)
    return -1;

  errno = 0;
  char *end;
  /* Parse integer part exactly. */
  long long int_part = strtoll(str, &end, 10);
  if (errno || end == str || int_part < 0)
    return -1;

  /* Optional fractional part (e.g. ".5" in "1.5G"). */
  double frac = 0.0;
  if (*end == '.') {
    char *frac_end;
    frac = strtod(end, &frac_end);
    if (frac_end == end || frac < 0)
      return -1;
    end = frac_end;
  }

  long long factor = 1;
  switch (*end | 0x20) { /* tolower */
  case 'k':
    factor = 1024LL;
    break;
  case 'm':
    factor = 1024LL * 1024;
    break;
  case 'g':
    factor = 1024LL * 1024 * 1024;
    break;
  case 't':
    factor = 1024LL * 1024 * 1024 * 1024;
    break;
  case '\0':
    break;
  default:
    return -1;
  }

  /* Overflow check before multiplication. */
  if (factor > 1 && int_part > (long long)(9223372036854775807LL / factor))
    return -1;

  long long result = int_part * factor;
  if (frac != 0.0)
    result += (long long)(frac * (double)factor);
  return result;
}

void ds_format_size(long long bytes, char *buf, size_t sz) {
  if (bytes <= 0) {
    snprintf(buf, sz, "N/A");
    return;
  }
  static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int u = 0;
  double d = (double)bytes;
  while (d >= 1024 && u < 4) {
    d /= 1024;
    u++;
  }
  snprintf(buf, sz, "%.2f %s", d, units[u]);
}
