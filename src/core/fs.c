/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <ftw.h>
#include <sys/random.h>

int is_ramfs(const char *path) {
  struct statfs sfs;
  if (statfs(path, &sfs) < 0)
    return 0;
  return (sfs.f_type == RAMFS_MAGIC || sfs.f_type == TMPFS_MAGIC);
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

/* File I/O */

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
  /* Randomized name + O_EXCL (via mkstemp) so a symlink pre-planted at a
   * predictable "<path>.tmp" cannot redirect this (often root-owned) write or
   * let it truncate an arbitrary followed target. */
  char tmp[PATH_MAX];
  int n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
  if (n < 0 || n >= (int)sizeof(tmp))
    return -1;

  int fd = mkstemp(tmp);
  if (fd < 0)
    return -1;
  (void)fcntl(fd, F_SETFD, FD_CLOEXEC);

  size_t len = strlen(content);
  int ok = (write_all(fd, content, len) == (ssize_t)len);

  /* mkstemp creates the file 0600; preserve the previous 0644 semantics. */
  if (ok && fchmod(fd, 0644) < 0)
    ok = 0;

  /* Without the fsync the rename can reach disk before the data, and a hard
   * reboot leaves an empty file. EINVAL means the filesystem has no fsync,
   * the write itself still landed. */
  if (ok && fsync(fd) < 0 && errno != EINVAL)
    ok = 0;
  close(fd);

  if (!ok || rename(tmp, path) < 0) {
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

/* UUID generation  - 32 hex chars from /dev/urandom */

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

  /* Fallback path: getrandom(2).  Unlike /dev/urandom it needs no device node
   * (e.g. when /dev is not yet populated).  If it too is unavailable, fail
   * closed rather than emit a predictable time+pid-seeded rand() identifier. */
  ssize_t g = getrandom(raw, sizeof(raw), 0);
  if (g == (ssize_t)sizeof(raw)) {
    for (int i = 0; i < (int)sizeof(raw); i++)
      snprintf(buf + i * 2, 3, "%02x", raw[i]);

    buf[DS_UUID_LEN] = '\0';
    return 0;
  }

  return -1;
}

/* Grep file for a pattern (simple substring search) */

int grep_file(const char *path, const char *pattern) {
  char buf[16384];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;
  return strstr(buf, pattern) ? 1 : 0;
}

/*
 * count_folders : function to count the number of folders in the passed path
 * and return the number of folder it can be used the get the total number of
 * containers from the get_workspace_dir directory
 */
int count_folders(const char *path) {
  DIR *dir = opendir(path);
  struct dirent *entry;
  struct stat st;
  char fullpath[PATH_MAX];
  int count = 0;

  if (!dir)
    return 0;

  size_t base_len = strlen(path);

  while ((entry = readdir(dir)) != NULL) {

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    /* Skip entries whose full path would exceed PATH_MAX */
    if (base_len + 1 + strlen(entry->d_name) >= sizeof(fullpath))
      continue;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

    if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
      count++;
  }

  closedir(dir);
  return count;
}
