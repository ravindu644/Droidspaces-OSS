/*
 * Droidspaces v3 — Android-specific helpers
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Android detection
 * ---------------------------------------------------------------------------*/

int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  /* Check for ANDROID_ROOT env var or presence of /system/bin/app_process */
  if (getenv("ANDROID_ROOT") || access("/system/bin/app_process", F_OK) == 0)
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
    system("cmd device_config put activity_manager max_phantom_processes "
           "2147483647 >/dev/null 2>&1");
    system("cmd device_config set_sync_disabled_for_tests persistent "
           ">/dev/null 2>&1");
    system("dumpsys deviceidle disable >/dev/null 2>&1");
  } else {
    system("cmd device_config put activity_manager max_phantom_processes 32 "
           ">/dev/null 2>&1");
    system(
        "cmd device_config set_sync_disabled_for_tests none >/dev/null 2>&1");
    system("dumpsys deviceidle enable >/dev/null 2>&1");
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
  if (!is_android())
    return;

  int status = android_get_selinux_status();
  if (status == 1) {
    ds_log("Setting SELinux to permissive...");
    if (write_file("/sys/fs/selinux/enforce", "0") < 0) {
      /* Try setenforce command as fallback */
      system("setenforce 0 2>/dev/null");
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
  system("mount -o remount,suid /data 2>/dev/null");
}

/* ---------------------------------------------------------------------------
 * DNS property retrieval
 * ---------------------------------------------------------------------------*/

int android_fill_dns_from_props(char *dns1, char *dns2, size_t size) {
  if (!is_android())
    return -1;

  char cmd1[128], cmd2[128];
  FILE *fp;

  /* Try common Android DNS properties */
  const char *props[] = {"net.dns1",
                         "net.dns2",
                         "net.eth0.dns1",
                         "net.eth0.dns2",
                         "net.wlan0.dns1",
                         "net.wlan0.dns2",
                         NULL};

  dns1[0] = dns2[0] = '\0';

  for (int i = 0; props[i] && !dns1[0]; i += 2) {
    snprintf(cmd1, sizeof(cmd1), "getprop %s", props[i]);
    fp = popen(cmd1, "r");
    if (fp) {
      if (fgets(dns1, size, fp)) {
        /* remove newline */
        char *nl = strchr(dns1, '\n');
        if (nl)
          *nl = '\0';
      }
      pclose(fp);
    }

    if (dns1[0] && props[i + 1]) {
      snprintf(cmd2, sizeof(cmd2), "getprop %s", props[i + 1]);
      fp = popen(cmd2, "r");
      if (fp) {
        if (fgets(dns2, size, fp)) {
          char *nl = strchr(dns2, '\n');
          if (nl)
            *nl = '\0';
        }
        pclose(fp);
      }
    }
  }

  return (dns1[0]) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Networking / Firewall
 * ---------------------------------------------------------------------------*/

void android_configure_iptables(void) {
  if (!is_android())
    return;

  ds_log("Configuring iptables for container networking...");
  /* Configure iptables for container networking */
  system("iptables -t filter -F 2>/dev/null");
  system("ip6tables -t filter -F 2>/dev/null");
  system("iptables -P FORWARD ACCEPT 2>/dev/null");
  system("iptables -t nat -A POSTROUTING -s 10.0.3.0/24 ! -d 10.0.3.0/24 -j "
         "MASQUERADE 2>/dev/null");
  system("iptables -t nat -A OUTPUT -p tcp -d 127.0.0.1 -m tcp --dport 1:65535 "
         "-j REDIRECT --to-ports 1-65535 2>/dev/null");
  system("iptables -t nat -A OUTPUT -p udp -d 127.0.0.1 -m udp --dport 1:65535 "
         "-j REDIRECT --to-ports 1-65535 2>/dev/null");
}

void android_setup_paranoid_network_groups(void) {
  if (!is_android())
    return;

  /* Android's "Paranoid Network" (CONFIG_ANDROID_PARANOID_NETWORK)
   * requires specific GIDs to access internet.
   * AID_INET (3003), AID_NET_RAW (3004), AID_NET_ADMIN (3005) */

  /* This is usually handled by adding the user to these groups inside the
   * rootfs. We can do it broadly for the process here if needed, but it's
   * better to use 'fix_networking_rootfs' to ensure /etc/group is correct. */
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
  if (domount(storage_src, path, NULL, MS_BIND | MS_REC, NULL) < 0) {
    ds_warn("Failed to bind-mount Android storage %s -> %s", storage_src, path);
    return -1;
  }

  return 0;
}
