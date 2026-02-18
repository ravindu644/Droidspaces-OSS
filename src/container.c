/*
 * Droidspaces v3 — Container lifecycle management
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Cleanup
 * ---------------------------------------------------------------------------*/

static void cleanup_container_resources(struct ds_config *cfg, pid_t pid,
                                        int skip_unmount) {
  /* Flush filesystem buffers */
  sync();

  if (is_android())
    android_optimizations(0);

  /* 1. Cleanup firmware path */
  if (cfg->rootfs_path[0]) {
    firmware_path_remove_rootfs(cfg->rootfs_path);
  } else if (pid > 0) {
    char rootfs[PATH_MAX];
    char root_link[PATH_MAX];
    snprintf(root_link, sizeof(root_link), "/proc/%d/root", pid);
    ssize_t rlen = readlink(root_link, rootfs, sizeof(rootfs) - 1);
    if (rlen > 0) {
      rootfs[rlen] = '\0'; /* readlink does NOT null-terminate */
      firmware_path_remove_rootfs(rootfs);
    }
  }

  /* 2. Resolve global PID file path */
  char global_pidfile[PATH_MAX];
  resolve_pidfile_from_name(cfg->container_name, global_pidfile,
                            sizeof(global_pidfile));

  /* 3. Handle rootfs image unmount */
  char mount_point[PATH_MAX];
  if (read_mount_path(cfg->pidfile, mount_point, sizeof(mount_point)) > 0) {
    if (!skip_unmount)
      unmount_rootfs_img(mount_point);
  }

  /* 4. Remove tracking info and unlink PID files */
  remove_mount_path(cfg->pidfile);
  if (cfg->pidfile[0])
    unlink(cfg->pidfile);
  if (strcmp(cfg->pidfile, global_pidfile) != 0)
    unlink(global_pidfile);
}

/* ---------------------------------------------------------------------------
 * Introspection
 * ---------------------------------------------------------------------------*/

int is_valid_container_pid(pid_t pid) {
  char path[PATH_MAX];
  build_proc_root_path(pid, "/run/systemd/container", path, sizeof(path));

  char buf[64];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return 0;

  return strstr(buf, "droidspaces") ? 1 : 0;
}

int check_status(struct ds_config *cfg, pid_t *pid_out) {
  if (auto_resolve_pidfile(cfg) < 0) {
    ds_error("Could not resolve PID file. Use --name or --pidfile.");
    return -1;
  }

  pid_t pid = 0;
  if (read_and_validate_pid(cfg->pidfile, &pid) < 0) {
    if (pid == 0) {
      /* PID no longer running, cleanup */
      cleanup_container_resources(cfg, 0, 0);
    }
    ds_error("Container %s is not running.", cfg->container_name);
    return -1;
  }

  if (pid_out)
    *pid_out = pid;
  return 0;
}

/* ---------------------------------------------------------------------------
 * Start
 * ---------------------------------------------------------------------------*/

int start_rootfs(struct ds_config *cfg) {
  /* 1. Preparation */
  ensure_workspace();

  if (cfg->selinux_permissive)
    android_set_selinux_permissive();
  if (cfg->android_storage && !is_android())
    ds_warn("--enable-android-storage is only supported on Android hosts. "
            "Skipping.");

  if (cfg->rootfs_img_path[0]) {
    if (mount_rootfs_img(cfg->rootfs_img_path, cfg->rootfs_path,
                         sizeof(cfg->rootfs_path)) < 0)
      return -1;
    cfg->is_img_mount = 1;
    safe_strncpy(cfg->img_mount_point, cfg->rootfs_path,
                 sizeof(cfg->img_mount_point));
  }

  if (cfg->container_name[0] == '\0') {
    if (generate_container_name(cfg->rootfs_path, cfg->container_name,
                                sizeof(cfg->container_name)) < 0)
      return -1;
  }

  /* Always find an available name starting from the current base (allows
   * alpine, alpine-1, etc) */
  char final_name[256];
  if (find_available_name(cfg->container_name, final_name, sizeof(final_name)) <
      0)
    ds_die("Too many containers running with similar names");
  safe_strncpy(cfg->container_name, final_name, sizeof(cfg->container_name));

  /* cfg->hostname remains empty if not defined, letting container decide */

  generate_uuid(cfg->uuid, sizeof(cfg->uuid));

  /* 2. Parent-side PTY allocation (LXC Model) */
  cfg->tty_count = DS_MAX_TTYS;
  if (ds_terminal_create(&cfg->console) < 0)
    ds_die("Failed to allocate console PTY");
  for (int i = 0; i < cfg->tty_count; i++) {
    if (ds_terminal_create(&cfg->ttys[i]) < 0)
      break;
  }

  /* 3. Resolve target PID file names early so monitor inherits them */
  char global_pidfile[PATH_MAX];
  resolve_pidfile_from_name(cfg->container_name, global_pidfile,
                            sizeof(global_pidfile));

  /* If no pidfile specified, or we want to use the global one */
  if (!cfg->pidfile[0]) {
    safe_strncpy(cfg->pidfile, global_pidfile, sizeof(cfg->pidfile));
  }

  /* 4. Pipe for synchronization */
  int sync_pipe[2];
  if (pipe(sync_pipe) < 0)
    ds_die("pipe failed: %s", strerror(errno));

  /* 4. Fork Monitor Process */
  pid_t monitor_pid = fork();
  if (monitor_pid < 0)
    ds_die("fork failed: %s", strerror(errno));

  if (monitor_pid == 0) {
    /* MONITOR PROCESS */
    close(sync_pipe[0]);
    setsid();
    prctl(PR_SET_NAME, "[ds-monitor]", 0, 0, 0);

    /* Unshare namespaces - Monitor enters new UTS, IPC namespeces
     * immediately. PID namespace unshare means only CHILDREN of the monitor
     * will be in the new PID NS. Note: we no longer unshare MNT here so
     * monitor can cleanup host mounts. */
    if (unshare(CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID) < 0)
      ds_die("unshare failed: %s", strerror(errno));

    /* Fork Container Init (PID 1 inside) */
    pid_t init_pid = fork();
    if (init_pid < 0)
      exit(EXIT_FAILURE);

    if (init_pid == 0) {
      /* CONTAINER INIT */
      close(sync_pipe[1]);
      /* internal_boot will handle its own stdfds. */
      exit(internal_boot(cfg));
    }

    /* Write child PID to sync pipe so parent knows it */
    write(sync_pipe[1], &init_pid, sizeof(pid_t));
    close(sync_pipe[1]);

    /* Ensure monitor is not sitting inside any mount point */
    chdir("/");

    /* Stdio handling for monitor in background mode */
    if (!cfg->foreground) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(devnull);
      }
    }

    /* Wait for child to exit */
    int status;
    while (waitpid(init_pid, &status, 0) < 0 && errno == EINTR)
      ;

    /* Monitor cleans up resources before exiting */
    cleanup_container_resources(cfg, init_pid, 0);

    exit(WEXITSTATUS(status));
  }

  /* PARENT PROCESS */
  close(sync_pipe[1]);

  /* Wait for Monitor to send child PID */
  if (read(sync_pipe[0], &cfg->container_pid, sizeof(pid_t)) != sizeof(pid_t)) {
    ds_error("Monitor failed to send container PID.");
    return -1;
  }
  close(sync_pipe[0]);

  ds_log("Container started with PID %d (Monitor: %d)", cfg->container_pid,
         monitor_pid);

  /* 5. Configure host-side networking (NAT, ip_forward) */
  fix_networking_host(cfg);
  android_optimizations(1);

  if (cfg->hw_access)
    ds_log("Hardware access enabled: using host devtmpfs...");
  else
    ds_log("Hardware access disabled: using isolated tmpfs /dev...");

  ds_log("Booting %s (init: /sbin/init)...", cfg->container_name);

  /* 6. Save PID file */
  char pid_str[32];
  snprintf(pid_str, sizeof(pid_str), "%d", cfg->container_pid);

  /* Always save to global Pids directory (for --name lookups) */
  if (write_file(global_pidfile, pid_str) < 0) {
    ds_error("Failed to write PID file: %s", global_pidfile);
  }

  /* Also save to user-specified --pidfile if different */
  if (cfg->pidfile[0] && strcmp(cfg->pidfile, global_pidfile) != 0) {
    if (write_file(cfg->pidfile, pid_str) < 0) {
      ds_error("Failed to write PID file: %s", cfg->pidfile);
    }
  }

  if (cfg->is_img_mount)
    save_mount_path(cfg->pidfile, cfg->img_mount_point);

  /* 6. Foreground or background finish */
  if (cfg->foreground) {
    int ret = console_monitor_loop(cfg->console.master, monitor_pid,
                                   cfg->container_pid);
    return ret;
  } else {
    /* Wait for container to finish pivot_root before showing info.
     * The boot sequence writes /run/droidspaces after pivot_root,
     * so we poll for it via /proc/<pid>/root/run/droidspaces. */
    char marker[PATH_MAX];
    snprintf(marker, sizeof(marker), "/proc/%d/root/run/droidspaces",
             cfg->container_pid);
    for (int i = 0; i < 50; i++) { /* 5 seconds max */
      if (access(marker, F_OK) == 0)
        break;
      usleep(100000); /* 100ms */
    }
    show_info(cfg);
    ds_log("Container %s is running in background.", cfg->container_name);
    if (is_android()) {
      ds_log("Use 'su -c \"%s --name=%s enter\"' to connect.", cfg->prog_name,
             cfg->container_name);
    } else {
      ds_log("Use 'sudo %s --name=%s enter' to connect.", cfg->prog_name,
             cfg->container_name);
    }
  }

  return 0;
}

int stop_rootfs(struct ds_config *cfg, int skip_unmount) {
  pid_t pid;
  if (check_status(cfg, &pid) < 0) {
    return 0;
  }

  ds_log("Stopping container %s (PID %d)...", cfg->container_name, pid);

  /* Cleanup resources that need the process to be alive (like proc/pid/root) */
  /* Actually, we call the full cleanup at the end, but let's grab rootfs now */
  char rootfs[PATH_MAX] = "";
  char root_link[PATH_MAX];
  snprintf(root_link, sizeof(root_link), "/proc/%d/root", pid);
  ssize_t len = readlink(root_link, rootfs, sizeof(rootfs) - 1);
  if (len > 0)
    rootfs[len] = '\0';

  /* 1. Try graceful shutdown (SIGRTMIN+3 is systemd poweroff) */
  kill(pid, SIGRTMIN + 3);

  /* 2. Wait for exit */
  int stopped = 0;
  for (int i = 0; i < DS_STOP_TIMEOUT * 5; i++) {
    if (kill(pid, 0) < 0 && errno == ESRCH) {
      stopped = 1;
      break;
    }
    usleep(200000); /* 200ms */
    if (i == 10) {
      ds_log("Graceful stop in progress, sending SIGTERM...");
      kill(pid, SIGTERM); /* Fallback to SIGTERM after 2s */
    }
  }

  /* 3. Force kill if still running */
  if (!stopped) {
    ds_warn("Graceful stop timed out, sending SIGKILL...");
    kill(pid, SIGKILL);
    /* Really make sure it's gone */
    for (int i = 0; i < 10; i++) {
      if (kill(pid, 0) < 0)
        break;
      usleep(100000);
    }
  }

  /* 4. Firmware cleanup if we captured rootfs earlier */
  if (rootfs[0])
    firmware_path_remove_rootfs(rootfs);

  /* 5. Complete resource cleanup */
  cleanup_container_resources(cfg, 0, skip_unmount);

  ds_log("Container %s stopped.", cfg->container_name);
  return 0;
}

/* ---------------------------------------------------------------------------
 * Namespace Entry (shared for enter and run)
 * ---------------------------------------------------------------------------*/

int enter_namespace(pid_t pid) {
  /* Verify process is still alive before trying to enter namespaces */
  if (kill(pid, 0) < 0) {
    ds_error("Container PID %d is no longer alive.", pid);
    return -1;
  }

  const char *ns_names[] = {"mnt", "uts", "ipc", "pid"};
  int ns_fds[4];
  char path[PATH_MAX];

  /* 1. Open all namespace descriptors first (CRITICAL: before any setns) */
  for (int i = 0; i < 4; i++) {
    snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, ns_names[i]);
    ns_fds[i] = open(path, O_RDONLY);
    if (ns_fds[i] < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("Failed to open mount namespace at %s: %s", path,
                 strerror(errno));
        /* Cleanup previous fds */
        for (int j = 0; j < i; j++)
          close(ns_fds[j]);
        return -1;
      }
      ds_warn("Optional namespace %s (%s) is missing: %s", ns_names[i], path,
              strerror(errno));
    }
  }

  /* 2. Enter namespaces */
  for (int i = 0; i < 4; i++) {
    if (ns_fds[i] < 0)
      continue;

    if (setns(ns_fds[i], 0) < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("setns(mnt) failed: %s", strerror(errno));
        for (int j = i; j < 4; j++)
          if (ns_fds[j] >= 0)
            close(ns_fds[j]);
        return -1;
      }
      ds_warn("setns(%s) failed (ignored): %s", ns_names[i], strerror(errno));
    }
    close(ns_fds[i]);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Enter / Run
 * ---------------------------------------------------------------------------*/

int enter_rootfs(struct ds_config *cfg, const char *user) {
  pid_t pid;
  if (check_status(cfg, &pid) < 0)
    return -1;

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    return -1;

  ds_log("Entering container %s...", cfg->container_name);

  pid_t child = fork();
  if (child < 0) {
    close(sv[0]);
    close(sv[1]);
    return -1;
  }

  if (child == 0) {
    close(sv[0]);
    if (enter_namespace(pid) < 0)
      exit(EXIT_FAILURE);

    /* Allocate TTY INSIDE the container namespaces */
    struct ds_tty_info tty;
    if (ds_terminal_create(&tty) < 0)
      exit(EXIT_FAILURE);

    /* Send master FD back to parent */
    if (ds_send_fd(sv[1], tty.master) < 0)
      exit(EXIT_FAILURE);

    close(tty.master);
    close(sv[1]);

    /* Establish controlling terminal using the native slave */
    if (ds_terminal_make_controlling(tty.slave) < 0)
      exit(EXIT_FAILURE);

    if (ds_terminal_set_stdfds(tty.slave) < 0)
      exit(EXIT_FAILURE);

    if (tty.slave > STDERR_FILENO)
      close(tty.slave);

    /* Must fork again to actually be in the new PID namespace */
    pid_t shell_pid = fork();
    if (shell_pid < 0)
      exit(EXIT_FAILURE);
    if (shell_pid == 0) {
      if (chdir("/") < 0)
        exit(EXIT_FAILURE);

      setup_container_env();
      setenv("LANG", "C.UTF-8", 1);
      load_etc_environment();

      extern char **environ;

      if (user && user[0]) {
        char *shell_argv[] = {"su", "-l", (char *)(uintptr_t)user, NULL};
        execve("/bin/su", shell_argv, environ);
        execve("/usr/bin/su", shell_argv, environ);
      }

      /* Try shells in order */
      const char *shells[] = {"/bin/bash", "/bin/ash", "/bin/sh", NULL};
      for (int i = 0; shells[i]; i++) {
        if (access(shells[i], X_OK) == 0) {
          const char *sh_name = strrchr(shells[i], '/');
          sh_name = sh_name ? sh_name + 1 : shells[i];
          char *shell_argv[] = {(char *)(uintptr_t)sh_name, "-l", NULL};
          execve(shells[i], shell_argv, environ);
        }
      }

      ds_error("Failed to find any usable shell");
      exit(EXIT_FAILURE);
    }
    waitpid(shell_pid, NULL, 0);
    exit(EXIT_SUCCESS);
  }

  close(sv[1]);

  /* Receive native PTY master from child */
  int master_fd = ds_recv_fd(sv[0]);
  close(sv[0]);

  if (master_fd < 0) {
    ds_error("Failed to receive PTY master from child");
    waitpid(child, NULL, 0);
    return -1;
  }

  /* Parent: setup host terminal and proxy I/O */
  struct termios old_tios;
  int has_tty = (ds_setup_tios(STDIN_FILENO, &old_tios) == 0);

  ds_terminal_proxy(master_fd);

  if (has_tty) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tios);
  }

  close(master_fd);
  waitpid(child, NULL, 0);
  return 0;
}

int run_in_rootfs(struct ds_config *cfg, int argc, char **argv) {
  (void)argc;
  pid_t pid;
  if (check_status(cfg, &pid) < 0)
    return -1;

  pid_t child = fork();
  if (child < 0)
    return -1;

  if (child == 0) {
    if (enter_namespace(pid) < 0)
      exit(EXIT_FAILURE);

    pid_t cmd_pid = fork();
    if (cmd_pid < 0)
      exit(EXIT_FAILURE);
    if (cmd_pid == 0) {
      if (chdir("/") < 0)
        exit(EXIT_FAILURE);

      setup_container_env();
      load_etc_environment();

      /* If single argument with spaces, run via /bin/sh -c */
      if (argv[1] == NULL && strchr(argv[0], ' ') != NULL) {
        char *shell_argv[] = {"/bin/sh", "-c", argv[0], NULL};
        execvp("/bin/sh", shell_argv);
      } else {
        execvp(argv[0], argv);
      }

      ds_error("Failed to execute command: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }

    int status;
    waitpid(cmd_pid, &status, 0);
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
  }

  int status;
  waitpid(child, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---------------------------------------------------------------------------
 * Other operations
 * ---------------------------------------------------------------------------*/

static const char *get_architecture(void) {
  static struct utsname uts;
  if (uname(&uts) != 0)
    return "unknown";

  if (strcmp(uts.machine, "x86_64") == 0)
    return "x86_64";
  if (strcmp(uts.machine, "aarch64") == 0 || strcmp(uts.machine, "arm64") == 0)
    return "aarch64";
  if (strncmp(uts.machine, "arm", 3) == 0)
    return "arm";
  if (strcmp(uts.machine, "i686") == 0 || strcmp(uts.machine, "i386") == 0)
    return "x86";
  return uts.machine;
}

static void get_container_os_pretty(pid_t pid, char *buf, size_t size) {
  if (!buf || size == 0)
    return;
  buf[0] = '\0';

  char path[PATH_MAX];
  if (build_proc_root_path(pid, "/etc/os-release", path, sizeof(path)) != 0)
    return;

  FILE *fp = fopen(path, "r");
  if (!fp)
    return;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
      char *val = line + 12;
      size_t len = strlen(val);
      while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '"'))
        val[--len] = '\0';
      if (val[0] == '"') {
        val++;
        len--;
      }
      if (len >= size)
        len = size - 1;
      snprintf(buf, size, "%.*s", (int)len, val);
      break;
    }
  }
  fclose(fp);
}

static void get_os_pretty_from_path(const char *osrelease_path, char *buf,
                                    size_t size) {
  if (!buf || size == 0)
    return;
  buf[0] = '\0';

  FILE *fp = fopen(osrelease_path, "r");
  if (!fp)
    return;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
      char *val = line + 12;
      size_t len = strlen(val);
      while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '"'))
        val[--len] = '\0';
      if (val[0] == '"') {
        val++;
        len--;
      }
      if (len >= size)
        len = size - 1;
      snprintf(buf, size, "%.*s", (int)len, val);
      break;
    }
  }
  fclose(fp);
}

int show_info(struct ds_config *cfg) {
  /* Host info */
  const char *host = is_android() ? "Android" : "Linux";
  const char *arch = get_architecture();
  printf("\n" C_GREEN "Host:" C_RESET " %s %s\n", host, arch);

  /* Case 1: No container name specified */
  if (cfg->container_name[0] == '\0') {
    char first_name[256];
    int count = count_running_containers(first_name, sizeof(first_name));

    if (count == 0) {
      printf("\n" C_YELLOW "Container:" C_RESET " No containers running.\n\n");
      return 0;
    }

    if (count == 1) {
      /* Auto-resolve to the only running container */
      safe_strncpy(cfg->container_name, first_name,
                   sizeof(cfg->container_name));
      resolve_pidfile_from_name(first_name, cfg->pidfile, sizeof(cfg->pidfile));
    } else {
      /* Multiple containers running, show list */
      printf("\n" C_YELLOW "Multiple containers running:" C_RESET "\n");
      show_containers();
      printf("\nUse '" C_GREEN "--name <NAME> info" C_RESET
             "' for detailed information.\n\n");
      return 0;
    }
  }

  /* Case 2: Specific name specified or auto-resolved */
  if (cfg->pidfile[0] == '\0' && cfg->container_name[0] != '\0') {
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
  }

  pid_t pid = 0;
  read_and_validate_pid(cfg->pidfile, &pid);

  printf("\n" C_GREEN "Container:" C_RESET " %s (%s)\n", cfg->container_name,
         pid > 0 ? "RUNNING" : "STOPPED");

  if (pid > 0) {
    printf("  PID: %d\n", pid);

    char pretty[256];
    get_container_os_pretty(pid, pretty, sizeof(pretty));
    if (pretty[0])
      printf("  OS: %s\n", pretty);

    printf("\n" C_GREEN "Features:" C_RESET "\n");

    /* SELinux */
    if (access("/sys/fs/selinux/enforce", R_OK) == 0) {
      const char *sel =
          android_get_selinux_status() == 0 ? "Permissive" : "Enforcing";
      printf("  SELinux: %s\n", sel);
    }

    /* IPv6 */
    printf("  IPv6: %s\n",
           detect_ipv6_in_container(pid) ? "enabled" : "disabled");

    /* Android storage */
    printf("  Android storage: %s\n",
           detect_android_storage_in_container(pid) ? "enabled" : "disabled");

    /* HW access */
    int hw = detect_hw_access_in_container(pid);
    if (hw)
      printf("  " C_RED "HW access:" C_RESET " enabled\n");
    else
      printf("  HW access: disabled\n");
  } else {
    /* Best effort: read os-release from rootfs path */
    if (cfg->rootfs_path[0]) {
      char osr_path[PATH_MAX];
      snprintf(osr_path, sizeof(osr_path), "%.4070s/etc/os-release",
               cfg->rootfs_path);
      char pretty[256];
      get_os_pretty_from_path(osr_path, pretty, sizeof(pretty));
      if (pretty[0])
        printf("  Rootfs OS: %s\n", pretty);
    }
  }
  printf("\n");

  return 0;
}

int restart_rootfs(struct ds_config *cfg) {
  ds_log("Restarting container %s...", cfg->container_name);
  stop_rootfs(cfg, 1); /* skip unmount to keep rootfs.img attached */
  return start_rootfs(cfg);
}
