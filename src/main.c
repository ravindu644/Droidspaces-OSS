/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

int ds_log_silent = 0;
char ds_log_container_name[256] = "";

/* ---------------------------------------------------------------------------
 * Usage / Help
 * ---------------------------------------------------------------------------*/

void print_usage(void) {
  printf(C_BOLD
         "%s v%s — High-performance Container Runtime for Android/Linux" C_RESET
         "\n",
         DS_PROJECT_NAME, DS_VERSION);
  printf("by " C_CYAN "%s" C_RESET "\n", DS_AUTHOR);
  printf("\n" C_BLUE "%s" C_RESET "\n", DS_REPO);
  printf(C_DIM "Built on: %s %s" C_RESET "\n\n", __DATE__, __TIME__);
  printf(
      "Usage: droidspaces [options] <command> [args]\n\n" C_BOLD
      "Commands:" C_RESET "\n"
      "  start                     Start a new container\n"
      "  stop                      Stop one or more containers\n"
      "  restart                   Restart a container\n"
      "  enter [user]              Enter a running container\n"
      "  run <cmd> [args]          Run a command in a running container\n"
      "  usage                     Show container uptime, CPU and RAM usage\n"
      "  info                      Show detailed container info\n"
      "  pid                       Show the live PID of the container init\n"
      "  show                      List all running containers\n"
      "  scan                      Scan for untracked containers\n"
      "  check                     Check system requirements\n"
      "  docs                      Show interactive documentation\n"
      "  help                      Show this help message\n"
      "  version                   Show version information\n"
      "  daemon                    Run daemon mode (use --foreground for "
      "foreground execution)\n\n"

      C_BOLD "Options (Container Setup):" C_RESET "\n"
      "  -r, --rootfs=PATH         Path to rootfs directory\n"
      "  -i, --rootfs-img=PATH     Path to rootfs image (.img)\n"
      "  -n, --name=NAME           Container name (mandatory)\n"
      "  -h, --hostname=NAME       Set container hostname\n"
      "  -C, --conf=PATH           Load configuration from file\n\n"

      C_BOLD "Options (Networking):" C_RESET "\n"
      "      --net=MODE            Modes: host (default), nat, none\n"
      "      --nat-ip=IP           Assign a fixed IP in 172.28.*.* range (nat "
      "mode)\n"
      "      --upstream IFACE      Upstream interface(s) (supports wildcards, "
      "e.g. rmnet*)\n"
      "                            e.g. --upstream wlan0 or --upstream "
      "wlan0,rmnet*\n"
      "      --port [H:]C[/P]      Forward ports (supports ranges and "
      "symmetric ports)\n"
      "                            e.g. --port 22, 80:80/tcp, "
      "1000-2000:1000-2000/udp\n"
      "  -d, --dns=SERVERS         Set custom DNS servers (comma separated)\n"
      "                            e.g. --dns 1.1.1.1,8.8.8.8\n"
      "  -I, --disable-ipv6        Disable IPv6 inside the container\n\n"

      C_BOLD "Options (Integration & Hardware):" C_RESET "\n"
      "  -S, --enable-android-storage\n"
      "                            Mount Android internal storage (/sdcard)\n"
      "  -H, --hw-access           Enable direct hardware access (/dev nodes)\n"
      "      --gpu                 Enable GPU acceleration nodes\n"
      "  -X, --termux-x11          Configure Termux-X11 display support\n\n"

      C_BOLD "Options (Security & Boot):" C_RESET "\n"
      "  -P, --selinux-permissive  Set host SELinux to permissive mode\n"
      "  -V, --volatile            Discard changes on exit (OverlayFS)\n"
      "      --force-cgroupv1      Force legacy cgroup v1 hierarchy\n"
      "      --block-nested-namespaces\n"
      "                            Manual Deadlock Shield (no nested "
      "namespaces)\n"
      "      --memory=LIMIT        Memory limit (e.g. 512M, 2G)\n"
      "      --cpus=COUNT          CPU limit (e.g. 1.5, 2)\n"
      "      --pids-limit=N        Max number of PIDs\n"
      "      --privileged=TAGS     Relax security: nomask, nocaps, noseccomp, "
      "shared, unfiltered-dev, full\n\n"

      C_BOLD "Options (Advanced):" C_RESET "\n"
      "  -f, --foreground          Run in foreground (attach console)\n"
      "  -u, --user=USER           Run command as USER (for 'run' command "
      "only)\n"
      "  -E, --env=PATH            Load environment variables from file\n"
      "  -B, --bind=SRC:DEST       Bind mount host directory into container\n"
      "                            Supports multiple flags or "
      "comma-separation\n"
      "                            e.g. -B /data:/data -B /tmp:/tmp\n"
      "                            e.g. -B /data:/data,/tmp:/tmp\n"
      "      --reset               Reset config to defaults (keeps "
      "name/rootfs)\n"
      "      --format              Machine-parseable output (KEY=VALUE)\n"
      "      --help                Show this help message\n\n"

      C_BOLD "Examples:" C_RESET "\n"
      "  droidspaces --name=mycontainer --rootfs=/path/to/rootfs start\n"
      "  droidspaces --name=mycontainer enter\n"
      "  droidspaces --name=mycontainer stop\n\n");
}

/* ---------------------------------------------------------------------------
 * Validation Helpers
 * ---------------------------------------------------------------------------*/

static int validate_kernel_version(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) < 0) {
    ds_error("Failed to detect kernel version.");
    return -1;
  }

  if (major < DS_MIN_KERNEL_MAJOR ||
      (major == DS_MIN_KERNEL_MAJOR && minor < DS_MIN_KERNEL_MINOR)) {
    printf("\n" C_RED C_BOLD "[ FATAL: UNSUPPORTED KERNEL ]" C_RESET "\n\n");
    ds_error("Droidspaces requires at least Linux %d.%d.0.",
             DS_MIN_KERNEL_MAJOR, DS_MIN_KERNEL_MINOR);
    ds_log("Detected kernel: %d.%d", major, minor);
    printf("\n" C_DIM
           "Why? Droidspaces v3 relies on features like OverlayFS and mature\n"
           "namespace isolation that are only stable on kernels %d.%d+.\n"
           "Running on this kernel would lead to system instability or "
           "crashes." C_RESET "\n\n",
           DS_MIN_KERNEL_MAJOR, DS_MIN_KERNEL_MINOR);
    ds_log("You can still use " C_BOLD "check, info, help, scan" C_RESET
           " for diagnostics.");
    return -1;
  }

  return 0;
}

/**
 * CLI-level configuration validation with professional error reporting.
 * Deters configuration errors early before entering the runtime.
 */
static int validate_configuration_cli(struct ds_config *cfg) {
  int errors = 0;

  if (cfg->rootfs_path[0] && cfg->rootfs_img_path[0]) {
    ds_error("Both rootfs directory and image specified simultaneously.");
    ds_log("Directory: %s", cfg->rootfs_path);
    ds_log("Image: %s", cfg->rootfs_img_path);
    ds_log("Override one using --rootfs or --rootfs-img.");
    errors++;
  }

  if (!cfg->container_name[0]) {
    ds_error("Container name is mandatory (--name).");
    errors++;
  } else if (reject_container_name(cfg->container_name) < 0) {
    errors++;
  }

  if (!cfg->rootfs_path[0] && !cfg->rootfs_img_path[0]) {
    ds_error("No rootfs target specified (requires -r or -i).");
    errors++;
  }

  /* Existence checks */
  if (cfg->rootfs_path[0] && access(cfg->rootfs_path, F_OK) != 0) {
    ds_error("Rootfs directory not found: '%s' (%s)", cfg->rootfs_path,
             strerror(errno));
    errors++;
  }

  if (cfg->rootfs_img_path[0] && access(cfg->rootfs_img_path, F_OK) != 0) {
    ds_error("Rootfs image not found: '%s' (%s)", cfg->rootfs_img_path,
             strerror(errno));
    errors++;
  }

  /* Image mode requires a name for the mount point */
  if (cfg->rootfs_img_path[0] && !cfg->container_name[0]) {
    ds_error("Rootfs image requires a container name (--name).");
    errors++;
  }

  return (errors > 0) ? -1 : 0;
}

static int auto_resolve_container_name(struct ds_config *cfg) {
  if (cfg->container_name[0] != '\0')
    return 0;

  char first_name[256];
  int count = count_running_containers(first_name, sizeof(first_name));

  /* If 0 containers found, try a scan once if we aren't already silent
   * (prevents infinite scan loops) */
  if (count == 0 && !ds_log_silent) {
    ds_log_silent = 1;
    scan_containers();
    ds_log_silent = 0;
    count = count_running_containers(first_name, sizeof(first_name));
  }

  /* If still not found after scan, fail */
  if (count == 0) {
    ds_error("No containers are currently running.");
    return -1;
  }

  if (count > 1) {
    ds_error("Multiple containers running. Please specify " C_BOLD
             "--name" C_RESET ".");
    show_containers();
    return -1;
  }

  safe_strncpy(cfg->container_name, first_name, sizeof(cfg->container_name));
  return 0;
}

/* ---------------------------------------------------------------------------
 * Command Dispatch
 * ---------------------------------------------------------------------------*/

static void enforce_nat_safety(struct ds_config *cfg, int argc, char **argv) {
  int is_nat = (cfg->net_mode == DS_NET_NAT);
  int is_disable_ipv6 = cfg->disable_ipv6;

  /* Nuke config reliance: parse argv directly to guarantee the warning
   * triggers regardless of what ds_config_load() wiped during restart. */
  if (argv != NULL) {
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--net=nat") == 0)
        is_nat = 1;
      if (strcmp(argv[i], "--net") == 0 && i + 1 < argc &&
          strcmp(argv[i + 1], "nat") == 0)
        is_nat = 1;
      if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--disable-ipv6") == 0)
        is_disable_ipv6 = 1;
    }
  }

  if (is_nat && is_disable_ipv6) {
    ds_log(
        "IPv6 is already inactive in NAT mode - --disable-ipv6 has no effect.");
  }

  if (cfg->net_mode == DS_NET_NAT || cfg->net_mode == DS_NET_NONE) {
    if (!check_ns(CLONE_NEWNET, "net")) {
      printf("\n" C_RED C_BOLD
             "[ FATAL: NETWORK NAMESPACE UNSUPPORTED ]" C_RESET "\n\n");
      ds_error("Kernel does not support CLONE_NEWNET (network namespaces).");
      ds_log("Cannot use --net=nat or --net=none.");
      ds_log("Tip: Use --net=host (default) for shared host networking.");
      exit(EXIT_FAILURE);
    }
  }

  if (cfg->net_mode != DS_NET_NAT)
    return;

  /* --upstream and --port are only meaningful with --net=nat */
  if (cfg->upstream_iface_count > 0 && cfg->net_mode != DS_NET_NAT) {
    ds_warn("--upstream is only valid with --net=nat - ignoring");
    cfg->upstream_iface_count = 0;
  }
  if (cfg->port_forward_count > 0 && cfg->net_mode != DS_NET_NAT) {
    ds_warn("--port is only valid with --net=nat - ignoring");
    cfg->port_forward_count = 0;
  }

  /* --upstream is mandatory when using --net=nat */
  if (cfg->upstream_iface_count == 0) {
    printf("\n" C_RED C_BOLD "[ FATAL: --upstream REQUIRED ]" C_RESET "\n\n");
    ds_error("--net=nat requires --upstream <interface(s)>\n"
             "\n"
             "  Specify the host interface(s) that provide internet access.\n"
             "  The monitor will track whichever is currently active.\n"
             "\n"
             "  Examples:\n"
             "    --upstream wlan0\n"
             "    --upstream wlan0,rmnet0\n"
             "    --upstream wlan0,rmnet0,ccmni1,v4-ccmni1");
    exit(EXIT_FAILURE);
  }

  char reason[512];
  int probe = ds_nl_probe_nat_capability(reason, sizeof(reason));
  if (probe < 0) {
    printf("\n" C_RED C_BOLD "[ FATAL: NAT NETWORKING UNSUPPORTED ]" C_RESET
           "\n\n");
    ds_error("--net=nat is not supported on this kernel:\n  %s", reason);
    ds_log("\nTip: Use --net=host (default) for shared host networking,");
    ds_log("or rebuild your kernel with CONFIG_BRIDGE=y and CONFIG_VETH=y.");
    exit(1);
  }

  if (probe == 1) {
    cfg->net_bridgeless = 1;
    ds_log("[NET] Kernel capability probe passed for --net=nat (FALLBACK: No "
           "BRIDGE)");
  } else {
    cfg->net_bridgeless = 0;
    ds_log("[NET] Kernel capability probe passed for --net=nat (Full BRIDGE)");
  }
}

int main(int argc, char **argv) {
  int ret = 0;
  struct ds_config cfg;
  /* CRITICAL: Zero all fields to avoid garbage pointer in dynamic arrays */
  memset(&cfg, 0, sizeof(cfg));

  /* Initialise pipe fds to -1 so accidental close(-1) is harmless */
  cfg.net_ready_pipe[0] = cfg.net_ready_pipe[1] = -1;
  cfg.net_done_pipe[0] = cfg.net_done_pipe[1] = -1;

  safe_strncpy(cfg.prog_name, argv[0], sizeof(cfg.prog_name));

  static struct option long_options[] = {
      {"rootfs", required_argument, 0, 'r'},
      {"rootfs-img", required_argument, 0, 'i'},
      {"name", required_argument, 0, 'n'},
      {"hostname", required_argument, 0, 'h'},
      {"dns", required_argument, 0, 'd'},
      {"foreground", no_argument, 0, 'f'},
      {"hw-access", no_argument, 0, 'H'},
      {"termux-x11", no_argument, 0, 'X'},
      {"disable-ipv6", no_argument, 0, 'I'},
      {"enable-android-storage", no_argument, 0, 'S'},
      {"selinux-permissive", no_argument, 0, 'P'},
      {"volatile", no_argument, 0, 'V'},
      {"bind-mount", required_argument, 0, 'B'},
      {"bind", required_argument, 0, 'B'},
      {"conf", required_argument, 0, 'C'},
      {"config", required_argument, 0, 'C'},
      {"env", required_argument, 0, 'E'},
      {"user", required_argument, 0, 'u'},
      {"net", required_argument, 0, 257},
      {"port", required_argument, 0, 258},
      {"upstream", required_argument, 0, 259},
      {"force-cgroupv1", no_argument, 0, 260},
      {"block-nested-namespaces", no_argument, 0, 261},
      {"privileged", required_argument, 0, 264},
      {"nat-ip", required_argument, 0, 262},
      {"gpu", no_argument, 0, 263},
      {"reset", no_argument, 0, 256},
      {"format", no_argument, 0, 265},
      {"memory", required_argument, 0, 266},
      {"cpus", required_argument, 0, 267},
      {"pids-limit", required_argument, 0, 268},
      {"help", no_argument, 0, 'v'},
      {0, 0, 0, 0}};

  extern int opterr;
  opterr = 0;

  /* Resolve relative path arguments to absolute before any parsing.
   * The daemon runs from CWD='/' (daemonize calls chdir("/")), so a relative
   * path like --conf=./file.conf would resolve against '/' in the re-exec'd
   * child.  Doing this here - while we still own the user's CWD - means every
   * subsequent getopt pass reads absolute paths, covering all execution modes.
   */
  ds_resolve_argv_paths(argc - 1, argv + 1);

  /*
   * Multi-pass argument parsing:
   * 1. Discovery Pass: Find command and identity (name/rootfs/conf) anywhere.
   * 2. Load config.
   * 3. Override Pass: Apply CLI overrides on top of loaded config.
   */
  const char *discovered_cmd = NULL;
  char temp_r[PATH_MAX] = {0}, temp_i[PATH_MAX] = {0};
  char run_user[256] = {0};
  int reset_config = 0;
  int cli_net_mode_set = 0;
  enum ds_net_mode cli_net_mode = DS_NET_HOST;
  int opt;

  /* 1. Discovery Pass: Capture identity and command without permuting argv.
   * Using '-' at the start of optstring returns non-options as '1'. */
  while ((opt = getopt_long(argc, argv, "-r:i:n:h:d:fHXPvVB:C:E:u:",
                            long_options, NULL)) != -1) {
    if (opt == 1) { /* Non-option argument */
      if (!discovered_cmd) {
        discovered_cmd = optarg;
        /* If the command is 'run', following arguments are for the container.
         * Stop discovering here to avoid misinterpreting sub-command flags. */
        if (strcmp(discovered_cmd, "run") == 0)
          break;
      }
    } else if (opt == 'C') {
      safe_strncpy(cfg.config_file, optarg, sizeof(cfg.config_file));
      cfg.config_file_specified = 1;
    } else if (opt == 'n') {
      if (reject_container_name(optarg) < 0) {
        ret = 1;
        goto cleanup;
      }
      safe_strncpy(cfg.container_name, optarg, sizeof(cfg.container_name));
    } else if (opt == 'r') {
      safe_strncpy(temp_r, optarg, sizeof(temp_r));
    } else if (opt == 'i') {
      safe_strncpy(temp_i, optarg, sizeof(temp_i));
    } else if (opt == 'u') {
      safe_strncpy(run_user, optarg, sizeof(run_user));
    } else if (opt == 256) {
      reset_config = 1;
    }
    /* Discover --net early so kernel probe can run before config load */
    if (opt == 257) {
      if (strcmp(optarg, "nat") == 0)
        cfg.net_mode = DS_NET_NAT;
      else if (strcmp(optarg, "none") == 0)
        cfg.net_mode = DS_NET_NONE;
      else if (strcmp(optarg, "host") == 0)
        cfg.net_mode = DS_NET_HOST;
      else {
        ds_error("Unknown network mode: '%s'. Valid options: host, nat, none",
                 optarg);
        ret = 1;
        goto cleanup;
      }
    }
  }
  optind = 0; /* Reset for next steps */

  /*
   * Daemon Proxying:
   * Optimistically attempt to proxy commands to the background daemon.
   * If the daemon is not reachable, fall back to direct execution.
   */
  int is_daemon_cmd = (discovered_cmd && strcmp(discovered_cmd, "daemon") == 0);

  /*
   * Commands that do not require root access (docs, help, version) or
   * must be run locally to avoid recursive loops (mode) are never proxied.
   */
  int is_stateless_cmd =
      (discovered_cmd && (strcmp(discovered_cmd, "docs") == 0 ||
                          strcmp(discovered_cmd, "help") == 0 ||
                          strcmp(discovered_cmd, "version") == 0 ||
                          strcmp(discovered_cmd, "mode") == 0));

  if (!is_daemon_cmd && !is_stateless_cmd && getenv("DS_NO_PROXY") == NULL) {
    int proxy_ret = ds_client_run(argc - 1, argv + 1);
    if (proxy_ret != -2) {
      ret = proxy_ret;
      goto cleanup;
    }
  }

  /*
   * Unified Configuration Discovery and Loading
   * 1. Try to load from explicitly provided config file.
   * 2. Otherwise try to auto-detect config from rootfs paths.
   * 3. Ensure we have a container name for stateful commands.
   * 4. Perform a recovery scan to load from
   *    <workspace dir>/Containers/<name>/container.config if config hasn't
   *    been loaded yet.
   */
  int is_stateful =
      (discovered_cmd && (strcmp(discovered_cmd, "stop") == 0 ||
                          strcmp(discovered_cmd, "restart") == 0 ||
                          strcmp(discovered_cmd, "pid") == 0 ||
                          strcmp(discovered_cmd, "info") == 0 ||
                          strcmp(discovered_cmd, "usage") == 0 ||
                          strcmp(discovered_cmd, "enter") == 0 ||
                          strcmp(discovered_cmd, "run") == 0));

  int loaded = 0;
  if (cfg.config_file_specified) {
    if (ds_config_load(cfg.config_file, &cfg) < 0) {
      ds_error("Failed to load configuration from '%s': %s", cfg.config_file,
               strerror(errno));
      ret = 1;
      goto cleanup;
    }
    loaded = 1;
  } else {
    char *auto_p = ds_config_auto_path(temp_r[0] ? temp_r : temp_i);
    if (auto_p) {
      safe_strncpy(cfg.config_file, auto_p, sizeof(cfg.config_file));
      if (ds_config_load(cfg.config_file, &cfg) == 0) {
        loaded = 1;
      } else if (errno != ENOENT) {
        ds_warn("Failed to load auto-detected config from '%s': %s",
                cfg.config_file, strerror(errno));
      }
      free(auto_p);
    }
  }

  /* For stateful commands, we absolutely need a container name.
   * If we don't have one by now, try to guess the active container. */
  if (is_stateful && cfg.container_name[0] == '\0') {
    if (auto_resolve_container_name(&cfg) < 0) {
      ret = 1;
      goto cleanup;
    }
  }

  /* If we have a name but haven't successfully loaded a config file yet, load
   * by name. */
  if (!loaded && cfg.container_name[0] != '\0') {
    if (ds_config_load_by_name(cfg.container_name, &cfg) < 0) {
      /* If loading by name fails and it's a stateful command, maybe the
       * container was moved or renamed. Perform a recovery scan of running
       * systems as a last resort. */
      if (is_stateful) {
        int prev = ds_log_silent;
        ds_log_silent = 1;
        scan_containers();
        ds_log_silent = prev;

        if (ds_config_load_by_name(cfg.container_name, &cfg) < 0) {
          ds_error("Container '%s' not found or metadata missing.",
                   cfg.container_name);
          ret = 1;
          goto cleanup;
        }
      }
    }
  }

  /* Apply configuration reset immediately AFTER disk load, BEFORE CLI overrides
   */
  if (reset_config) {
    apply_reset_config(&cfg, cli_net_mode_set, cli_net_mode);
  }

  /* 2. Override Pass: Apply CLI flags on top of config.
   * Strict mode for 'run' prevents stealing arguments from the sub-command. */
  int strict = (discovered_cmd && (strcmp(discovered_cmd, "run") == 0));
  const char *optstring =
      strict ? "+r:i:n:h:d:fHXPvVB:C:E:u:" : "r:i:n:h:d:fHXPvVB:C:E:u:";

  while ((opt = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
    switch (opt) {
    case 'r':
      safe_strncpy(cfg.rootfs_path, optarg, sizeof(cfg.rootfs_path));
      cfg.rootfs_img_path[0] = '\0';
      cfg.is_img_mount = 0;
      break;
    case 'i':
      safe_strncpy(cfg.rootfs_img_path, optarg, sizeof(cfg.rootfs_img_path));
      cfg.rootfs_path[0] = '\0';
      cfg.is_img_mount = 1;
      break;
    case 'n':
      if (reject_container_name(optarg) < 0) {
        ret = 1;
        goto cleanup;
      }
      safe_strncpy(cfg.container_name, optarg, sizeof(cfg.container_name));
      break;
    case 'h':
      safe_strncpy(cfg.hostname, optarg, sizeof(cfg.hostname));
      break;
    case 'E':
      safe_strncpy(cfg.env_file, optarg, sizeof(cfg.env_file));
      break;
    case 'u':
      safe_strncpy(run_user, optarg, sizeof(run_user));
      break;
    case 'd':
      safe_strncpy(cfg.dns_servers, optarg, sizeof(cfg.dns_servers));
      break;
    case 'f':
      cfg.foreground = 1;
      break;
    case 'H':
      cfg.hw_access = 1;
      break;
    case 'X':
      cfg.termux_x11 = 1;
      break;
    case 'I':
      cfg.disable_ipv6 = 1;
      break;
    case 'S':
      cfg.android_storage = 1;
      break;
    case 'P':
      cfg.selinux_permissive = 1;
      break;
    case 'V':
      cfg.volatile_mode = 1;
      break;
    case 'B': {
      char *saveptr;
      char *token = strtok_r(optarg, ",", &saveptr);
      while (token) {
        char *sep = strchr(token, ':');
        if (!sep) {
          ds_error("Invalid bind mount format: %s (expected SRC:DEST)", token);
          ret = 1;
          goto cleanup;
        }
        *sep = '\0';
        const char *src = token;
        const char *dest = sep + 1;

        if (dest[0] != '/') {
          ds_error("Bind destination must be an absolute path: %s", dest);
          ret = 1;
          goto cleanup;
        }
        if (!validate_bind_destination(dest)) {
          ds_error("Unsafe bind destination '%s': path traversal or control "
                   "characters not allowed.",
                   dest);
          ret = 1;
          goto cleanup;
        }
        if (ds_config_add_bind(&cfg, src, dest) < 0) {
          ret = 1;
          goto cleanup;
        }
        token = strtok_r(NULL, ",", &saveptr);
      }
      break;
    }
    case 'v':
      print_usage();
      ret = 0;
      goto cleanup;
    case 257:
      if (strcmp(optarg, "nat") == 0)
        cli_net_mode = DS_NET_NAT;
      else if (strcmp(optarg, "none") == 0)
        cli_net_mode = DS_NET_NONE;
      else if (strcmp(optarg, "host") == 0)
        cli_net_mode = DS_NET_HOST;
      else {
        ds_error("Unknown network mode: '%s'. Valid options: host, nat, none",
                 optarg);
        ret = 1;
        goto cleanup;
      }
      cfg.net_mode = cli_net_mode;
      cli_net_mode_set = 1;
      break;
    case 264:
      parse_privileged(optarg, &cfg);
      break;

    case 258: {
      /* --port HOST:CONTAINER[/proto]  (comma-separated list allowed), supports
       * ranges */
      char tmp[1024];
      safe_strncpy(tmp, optarg, sizeof(tmp));
      char *saveptr;
      char *tok = strtok_r(tmp, ",", &saveptr);
      while (tok) {
        if (cfg.port_forward_count >= DS_MAX_PORT_FORWARDS) {
          ds_error("Too many --port mappings (max %d)", DS_MAX_PORT_FORWARDS);
          ret = 1;
          goto cleanup;
        }

        while (*tok == ' ' || *tok == '\t')
          tok++;

        struct ds_port_forward *pf = &cfg.port_forwards[cfg.port_forward_count];
        memset(pf, 0, sizeof(*pf));
        strncpy(pf->proto, "tcp", sizeof(pf->proto));

        /* Strip optional /proto suffix */
        char *slash = strchr(tok, '/');
        if (slash) {
          *slash = '\0';
          strncpy(pf->proto, slash + 1, sizeof(pf->proto) - 1);
          pf->proto[sizeof(pf->proto) - 1] = '\0';
          if (strcmp(pf->proto, "tcp") != 0 && strcmp(pf->proto, "udp") != 0) {
            ds_error("Invalid protocol '%s' in --port (use tcp or udp)",
                     pf->proto);
            ret = 1;
            goto cleanup;
          }
        }

        /* Split HOST:CONTAINER or symmetric PORT */
        char *host_side = tok;
        char *cont_side = tok;
        char *colon = strchr(tok, ':');
        if (colon) {
          *colon = '\0';
          cont_side = colon + 1;
        }

        int valid = 1;
        /* Host side parsing */
        {
          char *dash = strchr(host_side, '-');
          if (dash) {
            int a = atoi(host_side), b = atoi(dash + 1);
            if (a <= 0 || a > 65535 || b < a || b > 65535) {
              ds_error("Invalid host port range '%s' in --port", host_side);
              valid = 0;
            } else {
              pf->host_port = (uint16_t)a;
              pf->host_port_end = (uint16_t)b;
            }
          } else {
            int p = atoi(host_side);
            if (p <= 0 || p > 65535) {
              ds_error("Invalid host port '%s' in --port", host_side);
              valid = 0;
            } else {
              pf->host_port = (uint16_t)p;
              pf->host_port_end = 0;
            }
          }
        }

        /* Container side parsing */
        if (valid) {
          char *dash = strchr(cont_side, '-');
          if (dash) {
            int a = atoi(cont_side), b = atoi(dash + 1);
            if (a <= 0 || a > 65535 || b < a || b > 65535) {
              ds_error("Invalid container port range '%s' in --port",
                       cont_side);
              valid = 0;
            } else {
              pf->container_port = (uint16_t)a;
              pf->container_port_end = (uint16_t)b;
            }
          } else {
            int p = atoi(cont_side);
            if (p <= 0 || p > 65535) {
              ds_error("Invalid container port '%s' in --port", cont_side);
              valid = 0;
            } else {
              pf->container_port = (uint16_t)p;
              pf->container_port_end = 0;
            }
          }
        }

        if (!valid) {
          ret = 1;
          goto cleanup;
        }

        /* Width mismatch check */
        int hw = pf->host_port_end ? (pf->host_port_end - pf->host_port) : 0;
        int cw = pf->container_port_end
                     ? (pf->container_port_end - pf->container_port)
                     : 0;
        if (hw != cw) {
          ds_error(
              "Port range width mismatch in --port: host %d vs container %d",
              hw + 1, cw + 1);
          ret = 1;
          goto cleanup;
        }

        /* Conflict/Intersection check - Venn diagram logic:
         * Reject if host OR container ranges overlap with any existing rule
         * of the same protocol.  Two ranges [s1,e1] and [s2,e2] overlap
         * iff s1 <= e2 && s2 <= e1. */
        for (int i = 0; i < cfg.port_forward_count; i++) {
          struct ds_port_forward *ex = &cfg.port_forwards[i];
          if (strcmp(ex->proto, pf->proto) != 0)
            continue;

          /* Exact duplicate - silently skip */
          if (pf->host_port == ex->host_port &&
              pf->host_port_end == ex->host_port_end &&
              pf->container_port == ex->container_port &&
              pf->container_port_end == ex->container_port_end) {
            goto skip_tok;
          }

          /* Host-side overlap */
          uint16_t hs1 = pf->host_port,
                   he1 = pf->host_port_end ? pf->host_port_end : pf->host_port;
          uint16_t hs2 = ex->host_port,
                   he2 = ex->host_port_end ? ex->host_port_end : ex->host_port;
          int host_overlap = (hs1 <= he2 && hs2 <= he1);

          /* Container-side overlap */
          uint16_t cs1 = pf->container_port, ce1 = pf->container_port_end
                                                       ? pf->container_port_end
                                                       : pf->container_port;
          uint16_t cs2 = ex->container_port, ce2 = ex->container_port_end
                                                       ? ex->container_port_end
                                                       : ex->container_port;
          int cont_overlap = (cs1 <= ce2 && cs2 <= ce1);

          if (host_overlap || cont_overlap) {
            char pf_str[32], ex_str[32];
            const char *side = host_overlap ? "Host" : "Container";

            if (host_overlap) {
              snprintf(pf_str, sizeof(pf_str), "%u%s%u", pf->host_port,
                       pf->host_port_end ? "-" : "", pf->host_port_end);
              snprintf(ex_str, sizeof(ex_str), "%u%s%u", ex->host_port,
                       ex->host_port_end ? "-" : "", ex->host_port_end);
            } else {
              snprintf(pf_str, sizeof(pf_str), "%u%s%u", pf->container_port,
                       pf->container_port_end ? "-" : "",
                       pf->container_port_end);
              snprintf(ex_str, sizeof(ex_str), "%u%s%u", ex->container_port,
                       ex->container_port_end ? "-" : "",
                       ex->container_port_end);
            }

            ds_warn("%s port conflict: %s/%s overlaps with existing "
                    "mapping %s/%s - skipping",
                    side, pf_str, pf->proto, ex_str, ex->proto);
            goto skip_tok;
          }
        }

        cfg.port_forward_count++;

      skip_tok:
        tok = strtok_r(NULL, ",", &saveptr);
      }
      break;
    }

    case 259: {
      /* --upstream wlan0,rmnet0,ccmni1  (comma-separated list) */
      char tmp[256];
      strncpy(tmp, optarg, sizeof(tmp) - 1);
      tmp[sizeof(tmp) - 1] = '\0';
      char *saveptr2;
      char *tok2 = strtok_r(tmp, ",", &saveptr2);
      while (tok2) {
        /* Trim leading/trailing whitespace */
        while (*tok2 == ' ' || *tok2 == '\t')
          tok2++;
        char *end2 = tok2 + strlen(tok2) - 1;
        while (end2 > tok2 && (*end2 == ' ' || *end2 == '\t'))
          *end2-- = '\0';

        if (tok2[0] == '\0') {
          tok2 = strtok_r(NULL, ",", &saveptr2);
          continue;
        }
        if (cfg.upstream_iface_count >= DS_MAX_UPSTREAM_IFACES) {
          ds_error("Too many --upstream interfaces (max %d)",
                   DS_MAX_UPSTREAM_IFACES);
          ret = 1;
          goto cleanup;
        }
        if (strlen(tok2) >= IFNAMSIZ) {
          ds_error("Interface name too long: '%s' (max %d chars)", tok2,
                   IFNAMSIZ - 1);
          ret = 1;
          goto cleanup;
        }
        int dup = 0;
        for (int i = 0; i < cfg.upstream_iface_count; i++) {
          if (strcmp(cfg.upstream_ifaces[i], tok2) == 0) {
            dup = 1;
            break;
          }
        }
        if (!dup) {
          safe_strncpy(cfg.upstream_ifaces[cfg.upstream_iface_count++], tok2,
                       IFNAMSIZ);
        }
        tok2 = strtok_r(NULL, ",", &saveptr2);
      }
      break;
    }
    case 260:
      /* --force-cgroupv1: escape hatch to legacy hierarchy */
      cfg.force_cgroupv1 = 1;
      break;
    case 261:
      /* --block-nested-namespaces: fix VFS deadlock manually */
      cfg.block_nested_ns = 1;
      break;

    case 262: {
      /* --nat-ip: static container IP inside the NAT subnet.
       * Only a basic format check here - subnet + uniqueness validation
       * happens in ds_net_resolve_static_ip() inside start_rootfs(). */
      char _errbuf[128];
      if (ds_net_validate_static_ip(optarg, _errbuf, sizeof(_errbuf)) != 0) {
        ds_error("--nat-ip '%s' is invalid: %s", optarg, _errbuf);
        ret = 1;
        goto cleanup;
      }
      safe_strncpy(cfg.static_nat_ip, optarg, sizeof(cfg.static_nat_ip));
      break;
    }

    case 263:
      /* --gpu: enable GPU acceleration in isolated tmpfs mode.
       * Scans the host /dev for known GPU nodes and mknods them into the
       * container's isolated /dev.  Safe to combine with --hw-access (which
       * already does full GPU wiring). */
      cfg.gpu_mode = 1;
      break;
    case 265:
      /* --format: machine-parseable output */
      cfg.format_output = 1;
      break;

    case 266: {
      long long v = ds_parse_size(optarg);
      if (v < 4 * 1024 * 1024) {
        ds_error("--memory: invalid or too small (min 4M): %s", optarg);
        ret = 1;
        goto cleanup;
      }
      cfg.memory_limit = v;
      break;
    }
    case 267: {
      char *end;
      errno = 0;
      double cpus = strtod(optarg, &end);
      if (errno || end == optarg || *end != '\0' || cpus < 0.01) {
        ds_error("--cpus: invalid value: %s", optarg);
        ret = 1;
        goto cleanup;
      }
      cfg.cpu_period = 100000;
      cfg.cpu_quota = (long long)(cpus * cfg.cpu_period);
      /* Enforce a strict minimum floor of 1000us (1ms) for the quota.
       * Most kernels reject values smaller than this with EINVAL to prevent
       * excessive scheduling overhead. */
      if (cfg.cpu_quota < 1000) {
        ds_error("--cpus: quota too small (min 0.01 cores / 1000us): %s",
                 optarg);
        ret = 1;
        goto cleanup;
      }
      break;
    }
    case 268: {
      char *end;
      errno = 0;
      long long p = strtoll(optarg, &end, 10);
      /* Add a sane upper bound (4194304 = 2^22) matching the Linux kernel's
       * default pid_max ceiling. Values above this are almost certainly
       * user errors and would be rejected by the kernel with EINVAL. */
      if (errno || end == optarg || *end != '\0' || p <= 0 ||
          p > 4194304LL) {
        ds_error("--pids-limit: invalid value (must be 1..4194304): %s",
                 optarg);
        ret = 1;
        goto cleanup;
      }
      cfg.pids_limit = p;
      break;
    }

    case '?':
      break;
    default:
      break;
    }
  }

  if (optind >= argc) {
    ds_error(C_BOLD "Missing command" C_RESET);
    ds_log("Run '" C_BOLD "%s help" C_RESET "' for usage information.",
           cfg.prog_name);
    ret = 1;
    goto cleanup;
  }

  const char *cmd = argv[optind];

  /* Set up global logging context for centralized logging engine */
  if (cfg.container_name[0] != '\0') {
    safe_strncpy(ds_log_container_name, cfg.container_name,
                 sizeof(ds_log_container_name));
  }

  /* Prevent Termux suicide when --termux-x11 is used inside Termux */
  if (is_android() && cfg.termux_x11 && is_running_in_termux() &&
      (strcmp(cmd, "start") == 0 || strcmp(cmd, "restart") == 0)) {
    printf("\n" C_RED C_BOLD "[ FATAL: Termux X11 Conflict ]" C_RESET "\n\n");
    ds_error(
        "Droidspaces cannot enable --termux-x11 when running inside Termux.");
    ds_log("Why? Setting up Termux X11 requires restarting the Termux app,");
    ds_log(
        "which would terminate this terminal session and Droidspaces itself.");
    printf("\n" C_CYAN
           "To use X11 support, please start this container from:" C_RESET
           "\n");
    printf("  1. The Droidspaces Android App\n");
    printf("  2. Using an another Terminal (adb shell, Reterminal, etc)\n\n");
    ret = 1;
    goto cleanup;
  }

  /* Basic info commands */
  if (strcmp(cmd, "check") == 0) {
    ret = check_requirements_detailed();
    goto cleanup;
  }
  if (strcmp(cmd, "version") == 0) {
    printf("v%s\n", DS_VERSION);
    ret = 0;
    goto cleanup;
  }
  if (strcmp(cmd, "help") == 0) {
    print_usage();
    ret = 0;
    goto cleanup;
  }
  if (strcmp(cmd, "docs") == 0) {
    print_documentation(argv[0]);
    ret = 0;
    goto cleanup;
  }

  if (strcmp(cmd, "mode") == 0) {
    printf("%s\n", ds_daemon_probe() ? "daemon" : "direct");
    ret = 0;
    goto cleanup;
  }

  /* Root required commands */
  if (getuid() != 0) {
    ds_error("Root privileges required for '%s'", cmd);
    ret = 1;
    goto cleanup;
  }
  ensure_workspace();

  if (strcmp(cmd, "show") == 0) {
    ret = show_containers();
    goto cleanup;
  }

  if (strcmp(cmd, "scan") == 0) {
    scan_containers();
    ret = 0;
    goto cleanup;
  }

  /* Lifestyle commands */
  if (strcmp(cmd, "start") == 0) {
    if (validate_configuration_cli(&cfg) < 0) {
      ret = 1;
      goto cleanup;
    }
    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    if (check_requirements_hw(cfg.hw_access) < 0) {
      ret = 1;
      goto cleanup;
    }
    enforce_nat_safety(&cfg, argc, argv);

    print_privileged_warning(cfg.privileged_mask);

    if ((cfg.privileged_mask & DS_PRIV_NOSEC) && cfg.block_nested_ns) {
      ds_warn("--privileged=noseccomp is active: --block-nested-namespaces "
              "is now a NO-OP.");
    }

    ds_cgroup_host_bootstrap(cfg.force_cgroupv1);
    if (cfg.container_name[0] == '\0' && cfg.rootfs_path[0]) {
      generate_container_name(cfg.rootfs_path, cfg.container_name,
                              sizeof(cfg.container_name));
    }
    ret = start_rootfs(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "stop") == 0) {
    ret = stop_rootfs(&cfg, 0);
    goto cleanup;
  }

  if (strcmp(cmd, "restart") == 0) {
    if (check_requirements_hw(cfg.hw_access) < 0) {
      ret = 1;
      goto cleanup;
    }
    enforce_nat_safety(&cfg, argc, argv);

    print_privileged_warning(cfg.privileged_mask);

    if ((cfg.privileged_mask & DS_PRIV_NOSEC) && cfg.block_nested_ns) {
      ds_warn("--privileged=noseccomp is active: --block-nested-namespaces "
              "is now a NO-OP.");
    }

    ds_cgroup_host_bootstrap(cfg.force_cgroupv1);
    ret = restart_rootfs(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "pid") == 0) {
    pid_t pid = 0;
    if (is_container_running(&cfg, &pid) && pid > 0) {
      printf("%d\n", (int)pid);
      ret = 0;
    } else {
      printf("NONE\n");
      ret = 1;
    }
    goto cleanup;
  }

  if (strcmp(cmd, "info") == 0) {
    ret = show_info(&cfg, 0);
    goto cleanup;
  }

  if (strcmp(cmd, "usage") == 0) {
    ret = show_container_usage(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "enter") == 0) {
    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    const char *user = (optind + 1 < argc) ? argv[optind + 1] : "root";
    ret = enter_rootfs(&cfg, user);
    goto cleanup;
  }

  if (strcmp(cmd, "run") == 0) {
    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    if (optind + 1 >= argc) {
      ds_error("Command required for 'run'");
      ret = 1;
      goto cleanup;
    }
    const char *as_user = (run_user[0] != '\0') ? run_user : NULL;
    ret =
        run_in_rootfs(&cfg, argc - (optind + 1), argv + (optind + 1), as_user);
    goto cleanup;
  }

  if (strcmp(cmd, "daemon") == 0) {
    if (getuid() != 0) {
      ds_error("Root privileges required for daemon mode");
      ret = 1;
      goto cleanup;
    }
    ret = ds_daemon_run(cfg.foreground, argv);
    goto cleanup;
  }

  ds_error("Unknown command: '%s'", cmd);
  ds_log("Run '" C_BOLD "%s help" C_RESET "' for usage information.",
         cfg.prog_name);
  ret = 1;

cleanup:
  free_config_unknown_lines(&cfg);
  free_config_env_vars(&cfg);
  free_config_binds(&cfg);
  return ret;
}
