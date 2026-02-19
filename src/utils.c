/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <ftw.h>

/* ---------------------------------------------------------------------------
 * String helpers
 * ---------------------------------------------------------------------------*/

void safe_strncpy(char *dst, const char *src, size_t size) {
  if (!dst || !src || size == 0)
    return;
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

int mkdir_p(const char *path, mode_t mode) {
  char tmp[PATH_MAX];
  char *p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
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

#if !defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 500
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#include <ftw.h>

int remove_recursive(const char *path) {
  return nftw(path, remove_recursive_handler, 64, FTW_DEPTH | FTW_PHYS);
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
  char buf[16384];
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

/* Internal helper to convert pidfile path to mount sidecar path: foo.pid ->
 * foo.mount */
static void pidfile_to_mountfile(const char *pidfile, char *buf, size_t size) {
  safe_strncpy(buf, pidfile, size);
  char *dot = strrchr(buf, '.');
  if (dot && strcmp(dot, ".pid") == 0) {
    /* If it ends in .pid, replace it */
    snprintf(dot, size - (size_t)(dot - buf), ".mount");
  } else {
    /* Otherwise just append */
    strncat(buf, ".mount", size - strlen(buf) - 1);
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
  char buf[CMSG_SPACE(sizeof(int))];
  struct iovec io = {.iov_base = buf, .iov_len = sizeof(buf)};

  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);

  if (recvmsg(sock, &msg, 0) < 0)
    return -1;

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS)
    return -1;

  return *((int *)CMSG_DATA(cmsg));
}
