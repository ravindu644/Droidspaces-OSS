/*
 * Droidspaces v3 — Container boot sequence (PID 1)
 */

#include "droidspace.h"

int internal_boot(struct ds_config *cfg, int sock_fd) {
  (void)sock_fd; /* No longer used */

  /* 1. Isolated mount namespace */
  if (unshare(CLONE_NEWNS) < 0)
    ds_die("Failed to unshare mount namespace: %s", strerror(errno));

  /* 2. Make root filesystem private to prevent mount leakage to host */
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
    ds_die("Failed to remount / as private: %s", strerror(errno));

  /* 2. Bind mount rootfs to itself (required for pivot_root) */
  if (mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL) <
      0)
    ds_die("Failed to bind mount rootfs: %s", strerror(errno));

  /* 3. Change directory to rootfs */
  if (chdir(cfg->rootfs_path) < 0)
    ds_die("Failed to chdir to rootfs: %s", strerror(errno));

  /* 4. Prepare .old_root for pivot_root */
  mkdir(".old_root", 0755);

  /* 5. Setup /dev (tmpfs or devtmpfs) */
  if (setup_dev(".", cfg->hw_access) < 0)
    ds_die("Failed to setup /dev");

  /* 6. Mount virtual filesystems */
  mkdir("proc", 0755);
  domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);

  mkdir("sys", 0755);
  domount("sysfs", "sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);

  mkdir("run", 0755);
  domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755");

  /* 7. Bind-mount TTYs BEFORE pivot_root so we can still see /dev/pts/N from
   * host */
  /* We use relative paths to the current directory (rootfs) */
  if (mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL) < 0)
    ds_warn("Failed to bind mount console %s: %s", cfg->console.name,
            strerror(errno));

  char tty_target[32];
  for (int i = 0; i < cfg->tty_count; i++) {
    snprintf(tty_target, sizeof(tty_target), "dev/tty%d", i + 1);
    if (mount(cfg->ttys[i].name, tty_target, NULL, MS_BIND, NULL) < 0)
      ds_warn("Failed to bind mount %s: %s", tty_target, strerror(errno));
  }

  /* 8. Write UUID marker for PID discovery */
  char marker[PATH_MAX];
  snprintf(marker, sizeof(marker), "run/%s", cfg->uuid);
  write_file(marker, "init");
  write_file("run/droidspaces", DS_VERSION);

  /* 9. Setup Cgroups */
  setup_cgroups();

  /* 10. Android-specific storage */
  if (cfg->android_storage) {
    android_setup_storage(".");
  }

  /* 12. PIXOT_ROOT */
  if (syscall(SYS_pivot_root, ".", ".old_root") < 0)
    ds_die("pivot_root failed: %s", strerror(errno));

  /* 13. Switch to new root */
  if (chdir("/") < 0)
    ds_die("chdir / failed: %s", strerror(errno));

  /* 14. Setup devpts (must be after pivot_root for newinstance) */
  setup_devpts();

  /* 15. Configure rootfs networking (hostname, resolv.conf, etc) */
  fix_networking_rootfs(cfg);

  /* 16. Cleanup .old_root */
  if (umount2("/.old_root", MNT_DETACH) < 0)
    ds_warn("Failed to unmount .old_root: %s", strerror(errno));
  else
    rmdir("/.old_root");

  /* 17. Set container identity for systemd/openrc */
  write_file("/run/systemd/container", "droidspaces");

  /* 18. Clear environment and set container defaults */
  clearenv();
  setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
         1);
  setenv("TERM", getenv("TERM") ? getenv("TERM") : "xterm-256color", 1);
  setenv("HOME", "/root", 1);
  setenv("container", "droidspaces", 1);

  char ttys_str[256];
  build_container_ttys_string(cfg->ttys, cfg->tty_count, ttys_str,
                              sizeof(ttys_str));
  setenv("container_ttys", ttys_str, 1);

  /* 19. Redirect standard I/O to /dev/console */
  int console_fd = open("/dev/console", O_RDWR);
  if (console_fd >= 0) {
    ds_terminal_set_stdfds(console_fd);
    ds_terminal_make_controlling(console_fd);
    if (console_fd > 2)
      close(console_fd);
  }

  /* 21. EXEC INIT */
  char *argv[] = {"/sbin/init", NULL};

  execve("/sbin/init", argv, environ);

  /* Fallback if /sbin/init is missing */
  ds_warn("/sbin/init not found, trying /bin/sh...");
  argv[0] = "/bin/sh";
  execve("/bin/sh", argv, environ);

  ds_die("Final execve failed: %s", strerror(errno));
  return -1;
}
