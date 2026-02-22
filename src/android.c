/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>

/* ---------------------------------------------------------------------------
 * Android detection
 * ---------------------------------------------------------------------------*/

int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  /* Check for Android-specific environments or devices */
  if (getenv("ANDROID_ROOT") || access("/system/bin/app_process", F_OK) == 0 ||
      access("/dev/binder", F_OK) == 0 || access("/dev/ashmem", F_OK) == 0)
    cached_result = 1;
  else
    cached_result = 0;

  return cached_result;
}

/* ---------------------------------------------------------------------------
 * Android optimizations
 * ---------------------------------------------------------------------------*/

void android_optimizations(int enable) {
  if (!is_android())
    return;

  if (enable) {
    ds_log("Applying Android system optimizations...");
    char *args1[] = {"cmd",
                     "device_config",
                     "put",
                     "activity_manager",
                     "max_phantom_processes",
                     "2147483647",
                     NULL};
    run_command_quiet(args1);
    char *args2[] = {"cmd", "device_config", "set_sync_disabled_for_tests",
                     "persistent", NULL};
    run_command_quiet(args2);
    char *args3[] = {"dumpsys", "deviceidle", "disable", NULL};
    run_command_quiet(args3);
  } else {
    char *args1[] = {"cmd",
                     "device_config",
                     "put",
                     "activity_manager",
                     "max_phantom_processes",
                     "32",
                     NULL};
    run_command_quiet(args1);
    char *args2[] = {"cmd", "device_config", "set_sync_disabled_for_tests",
                     "none", NULL};
    run_command_quiet(args2);
    char *args3[] = {"dumpsys", "deviceidle", "enable", NULL};
    run_command_quiet(args3);
  }
}

/* ---------------------------------------------------------------------------
 * SELinux management
 * ---------------------------------------------------------------------------*/

int android_get_selinux_status(void) {
  char buf[16];
  if (read_file("/sys/fs/selinux/enforce", buf, sizeof(buf)) < 0)
    return -1;
  return atoi(buf);
}

void android_set_selinux_permissive(void) {
  int status = android_get_selinux_status();
  if (status == -1) {
    ds_warn("SELinux not supported or interface missing. Skipping permissive "
            "mode.");
    return;
  }

  if (status == 1) {
    ds_log("Setting SELinux to permissive...");
    if (write_file("/sys/fs/selinux/enforce", "0") < 0) {
      /* Try setenforce command as fallback */
      char *args[] = {"setenforce", "0", NULL};
      run_command_quiet(args);
    }
  }
}

/* ---------------------------------------------------------------------------
 * Data partition remount (for suid support)
 * ---------------------------------------------------------------------------*/

void android_remount_data_suid(void) {
  if (!is_android())
    return;

  ds_log("Ensuring /data is mounted with suid support...");
  /* On some Android versions, /data is mounted nosuid. We need suid for
   * sudo/su/ping within the container if it's stored on /data. */
  char *args[] = {"mount", "-o", "remount,suid", "/data", NULL};
  if (run_command_quiet(args) != 0) {
    ds_warn(
        "Failed to remount /data with suid support. su/sudo might not work.");
  }
}

/* ---------------------------------------------------------------------------
 * DNS property retrieval
 * ---------------------------------------------------------------------------*/

int android_fill_dns_from_props(char *dns1, char *dns2, size_t size) {
  if (!is_android())
    return -1;

  dns1[0] = dns2[0] = '\0';

  /* Use fork+exec instead of popen to avoid shell injection */
  int pipefd[2];
  if (pipe(pipefd) < 0)
    return -1;

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }
    execlp("getprop", "getprop", (char *)NULL);
    _exit(127);
  }

  close(pipefd[1]);
  FILE *fp = fdopen(pipefd[0], "r");
  if (!fp) {
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    return -1;
  }

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    /* getprop output: [prop.name]: [value] */
    if (strstr(line, "dns") == NULL)
      continue;

    char *name_start = strchr(line, '[');
    char *name_end = strchr(line, ']');
    if (!name_start || !name_end)
      continue;

    char *val_start = strchr(name_end + 1, '[');
    char *val_end = strchr(name_end + 1, ']');
    if (!val_start || !val_end)
      continue;

    *val_end = '\0';
    char *val = val_start + 1;

    if (!dns1[0]) {
      safe_strncpy(dns1, val, size);
    } else if (!dns2[0] && strcmp(dns1, val) != 0) {
      safe_strncpy(dns2, val, size);
      break; /* Found both */
    }
  }
  fclose(fp);
  waitpid(pid, NULL, 0);

  return (dns1[0]) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Networking / Firewall
 * ---------------------------------------------------------------------------*/

void android_configure_iptables(void) {
  if (!is_android())
    return;

  ds_log("Configuring iptables for container networking...");

  char *cmds[][32] = {{"iptables", "-t", "filter", "-F", NULL},
                      {"ip6tables", "-t", "filter", "-F", NULL},
                      {"iptables", "-P", "FORWARD", "ACCEPT", NULL},
                      {"iptables", "-t", "nat", "-A", "POSTROUTING", "-s",
                       "10.0.3.0/24", "!", "-d", "10.0.3.0/24", "-j",
                       "MASQUERADE", NULL},
                      {"iptables", "-t", "nat", "-A", "OUTPUT", "-p", "tcp",
                       "-d", "127.0.0.1", "-m", "tcp", "--dport", "1:65535",
                       "-j", "REDIRECT", "--to-ports", "1-65535", NULL},
                      {"iptables", "-t", "nat", "-A", "OUTPUT", "-p", "udp",
                       "-d", "127.0.0.1", "-m", "udp", "--dport", "1:65535",
                       "-j", "REDIRECT", "--to-ports", "1-65535", NULL}};

  for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
    run_command_quiet(cmds[i]);
  }
}

/* ---------------------------------------------------------------------------
 * Storage
 * ---------------------------------------------------------------------------*/

int android_setup_storage(const char *rootfs_path) {
  if (!is_android())
    return 0;

  const char *storage_src = "/storage/emulated/0";
  struct stat st;

  if (stat(storage_src, &st) < 0 || !S_ISDIR(st.st_mode) ||
      access(storage_src, R_OK) < 0) {
    ds_warn("Android storage not found or not readable at %s", storage_src);
    return -1;
  }

  /* Create target directories inside rootfs: storage/, storage/emulated/,
   * storage/emulated/0 */
  char path[PATH_MAX];

  snprintf(path, sizeof(path), "%s/storage", rootfs_path);
  mkdir(path, 0755);

  snprintf(path, sizeof(path), "%s/storage/emulated", rootfs_path);
  mkdir(path, 0755);

  snprintf(path, sizeof(path), "%s/storage/emulated/0", rootfs_path);
  mkdir(path, 0755);

  ds_log("Mounting Android internal storage to /storage/emulated/0...");
  if (mount(storage_src, path, NULL, MS_BIND | MS_REC, NULL) < 0) {
    ds_warn("Failed to bind-mount Android storage %s -> %s: %s", storage_src,
            path, strerror(errno));
    return -1;
  }

  return 0;
}
