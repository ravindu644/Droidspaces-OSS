/*
 * Droidspaces v3 — Utility functions
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * String helpers
 * ---------------------------------------------------------------------------*/

void safe_strncpy(char *dst, const char *src, size_t size) {
  if (!dst || !src || size == 0)
    return;
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

/* ---------------------------------------------------------------------------
 * File I/O
 * ---------------------------------------------------------------------------*/

int write_file(const char *path, const char *content) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return -1;
  size_t len = strlen(content);
  ssize_t w = write(fd, content, len);
  close(fd);
  return (w == (ssize_t)len) ? 0 : -1;
}

int read_file(const char *path, char *buf, size_t size) {
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  ssize_t r = read(fd, buf, size - 1);
  close(fd);
  if (r < 0)
    return -1;
  buf[r] = '\0';
  /* strip trailing newline */
  while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r'))
    buf[--r] = '\0';
  return (int)r;
}

/* ---------------------------------------------------------------------------
 * UUID generation  — 32 hex chars from /dev/urandom
 * ---------------------------------------------------------------------------*/

int generate_uuid(char *buf, size_t size) {
  if (size < DS_UUID_LEN + 1)
    return -1;

  unsigned char raw[DS_UUID_LEN / 2];
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    /* fallback: use pid + time */
    snprintf(buf, size, "%08x%08x%08x%08x", (unsigned)getpid(),
             (unsigned)time(NULL), (unsigned)getppid(), (unsigned)rand());
    return 0;
  }

  ssize_t r = read(fd, raw, sizeof(raw));
  close(fd);
  if (r != (ssize_t)sizeof(raw))
    return -1;

  for (int i = 0; i < (int)sizeof(raw); i++)
    snprintf(buf + i * 2, 3, "%02x", raw[i]);
  buf[DS_UUID_LEN] = '\0';
  return 0;
}

/* ---------------------------------------------------------------------------
 * PID collection — read numeric entries from /proc
 * ---------------------------------------------------------------------------*/

int collect_pids(pid_t **pids_out, size_t *count_out) {
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
    if (ent->d_type != DT_DIR)
      continue;
    char *end;
    long val = strtol(ent->d_name, &end, 10);
    if (*end != '\0' || val <= 0)
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
    r = snprintf(buf, size, "/proc/%d/root%s", pid, suffix);
  else
    r = snprintf(buf, size, "/proc/%d/root", pid);
  return (r > 0 && (size_t)r < size) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Grep file for a pattern (simple substring search)
 * ---------------------------------------------------------------------------*/

int grep_file(const char *path, const char *pattern) {
  char buf[4096];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;
  return strstr(buf, pattern) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * PID file helpers
 * ---------------------------------------------------------------------------*/

int read_and_validate_pid(const char *pidfile, pid_t *pid_out) {
  char buf[64];
  if (read_file(pidfile, buf, sizeof(buf)) < 0)
    return -1;

  char *end;
  long val = strtol(buf, &end, 10);
  if (*end != '\0' || val <= 0) {
    ds_error("Invalid PID in %s: '%s'", pidfile, buf);
    return -1;
  }

  /* check if process exists */
  if (kill((pid_t)val, 0) < 0 && errno == ESRCH) {
    *pid_out = 0;
    return -1;
  }

  *pid_out = (pid_t)val;
  return 0;
}

/* ---------------------------------------------------------------------------
 * Mount sidecar files (.mount)
 * ---------------------------------------------------------------------------*/

/* Save mount path alongside a pidfile: foo.pid -> foo.mount */
int save_mount_path(const char *pidfile, const char *mount_path) {
  char mpath[PATH_MAX];
  safe_strncpy(mpath, pidfile, sizeof(mpath));

  /* replace .pid with .mount */
  char *dot = strrchr(mpath, '.');
  if (dot)
    safe_strncpy(dot, ".mount", sizeof(mpath) - (size_t)(dot - mpath));
  else
    strncat(mpath, ".mount", sizeof(mpath) - strlen(mpath) - 1);

  return write_file(mpath, mount_path);
}

int read_mount_path(const char *pidfile, char *buf, size_t size) {
  char mpath[PATH_MAX];
  safe_strncpy(mpath, pidfile, sizeof(mpath));

  char *dot = strrchr(mpath, '.');
  if (dot)
    safe_strncpy(dot, ".mount", sizeof(mpath) - (size_t)(dot - mpath));
  else
    strncat(mpath, ".mount", sizeof(mpath) - strlen(mpath) - 1);

  return read_file(mpath, buf, size);
}

int remove_mount_path(const char *pidfile) {
  char mpath[PATH_MAX];
  safe_strncpy(mpath, pidfile, sizeof(mpath));

  char *dot = strrchr(mpath, '.');
  if (dot)
    safe_strncpy(dot, ".mount", sizeof(mpath) - (size_t)(dot - mpath));
  else
    strncat(mpath, ".mount", sizeof(mpath) - strlen(mpath) - 1);

  return unlink(mpath);
}

/* ---------------------------------------------------------------------------
 * Kernel firmware search path management
 * ---------------------------------------------------------------------------*/

#define FW_PATH_FILE "/sys/module/firmware_class/parameters/path"

void firmware_path_add_rootfs(const char *rootfs) {
  char fw_path[PATH_MAX];
  snprintf(fw_path, sizeof(fw_path), "%s/lib/firmware", rootfs);

  struct stat st;
  if (stat(fw_path, &st) < 0)
    return;

  /* Read current firmware path */
  char current[PATH_MAX] = {0};
  read_file(FW_PATH_FILE, current, sizeof(current));

  /* Don't add if already present */
  if (current[0] && strstr(current, fw_path))
    return;

  /* Prepend our path */
  char new_path[PATH_MAX * 2];
  if (current[0])
    snprintf(new_path, sizeof(new_path), "%s:%s", fw_path, current);
  else
    safe_strncpy(new_path, fw_path, sizeof(new_path));

  write_file(FW_PATH_FILE, new_path);
}

void firmware_path_remove_rootfs(const char *rootfs) {
  char fw_path[PATH_MAX];
  snprintf(fw_path, sizeof(fw_path), "%s/lib/firmware", rootfs);

  char current[PATH_MAX * 2] = {0};
  if (read_file(FW_PATH_FILE, current, sizeof(current)) < 0)
    return;

  /* Remove our path from the firmware search path */
  char *pos = strstr(current, fw_path);
  if (!pos)
    return;

  char new_path[PATH_MAX * 2] = {0};
  size_t prefix_len = (size_t)(pos - current);
  if (prefix_len > 0) {
    memcpy(new_path, current, prefix_len);
    /* remove trailing colon */
    if (new_path[prefix_len - 1] == ':')
      new_path[prefix_len - 1] = '\0';
  }

  char *after = pos + strlen(fw_path);
  if (*after == ':')
    after++;
  if (*after) {
    if (new_path[0])
      strncat(new_path, ":", sizeof(new_path) - strlen(new_path) - 1);
    strncat(new_path, after, sizeof(new_path) - strlen(new_path) - 1);
  }

  write_file(FW_PATH_FILE, new_path);
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
    exit(127); /* exec failed */
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
