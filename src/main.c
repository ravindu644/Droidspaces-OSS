/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

int ds_log_silent = 0;

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
  printf("Usage: droidspaces [options] <command> [args]\n\n" C_BOLD
         "Commands:" C_RESET "\n");
  printf("  start                     Start a new container\n");
  printf("  stop                      Stop one or more containers\n");
  printf("  restart                   Restart a container\n");
  printf("  enter [user]              Enter a running container\n");
  printf("  run <cmd> [args]          Run a command in a running container\n");
  printf("  status                    Show container status\n");
  printf("  info                      Show detailed container info\n");
  printf("  show                      List all running containers\n");
  printf("  scan                      Scan for untracked containers\n");
  printf("  check                     Check system requirements\n");
  printf("  docs                      Show interactive documentation\n");
  printf("  help                      Show this help message\n");
  printf("  version                   Show version information\n");

  printf(C_BOLD "\nOptions:" C_RESET "\n");
  printf("  -r, --rootfs=PATH         Path to rootfs directory\n");
  printf("  -i, --rootfs-img=PATH     Path to rootfs image (.img)\n");
  printf("  -n, --name=NAME           Container name (auto-generated if "
         "omitted)\n");
  printf("  -h, --hostname=NAME       Set container hostname\n");
  printf(
      "  -d, --dns=SERVERS         Set custom DNS servers (comma separated)\n");
  printf("  -f, --foreground          Run in foreground (attach console)\n");
  printf("  -V, --volatile            Discard changes on exit (OverlayFS)\n");
  printf("  -E, --env=PATH            Load environment variables from file\n");
  printf(
      "  -X, --termux-x11          Enable Termux-X11 support (Android only)\n");
  printf(
      "  -B, --bind-mount=SRC:DEST Bind mount host directory into container\n");
  printf("  -C, --conf=PATH           Load configuration from file\n");
  printf("  --help                    Show this help message\n\n");

  printf(C_BOLD "Examples:" C_RESET "\n");
  printf("  droidspaces --rootfs=/path/to/rootfs start\n");
  printf("  droidspaces --name=mycontainer enter\n");
  printf("  droidspaces --name=mycontainer stop\n\n");
}

/* ---------------------------------------------------------------------------
 * Kernel Validation
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

/* ---------------------------------------------------------------------------
 * Command Dispatch
 * ---------------------------------------------------------------------------*/

int main(int argc, char **argv) {
  int ret = 0;
  struct ds_config cfg;
  /* CRITICAL: Zero all fields to avoid garbage pointer in dynamic arrays */
  memset(&cfg, 0, sizeof(cfg));

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
      {"enable-ipv6", no_argument, 0, 'I'},
      {"enable-android-storage", no_argument, 0, 'S'},
      {"selinux-permissive", no_argument, 0, 'P'},
      {"volatile", no_argument, 0, 'V'},
      {"bind-mount", required_argument, 0, 'B'},
      {"conf", required_argument, 0, 'C'},
      {"config", required_argument, 0, 'C'},
      {"env", required_argument, 0, 'E'},
      {"help", no_argument, 0, 'v'},
      {0, 0, 0, 0}};

  extern int opterr;
  opterr = 0;

  /*
   * Multi-pass argument parsing:
   * 1. Find command and look for explicit --conf flag.
   * 2. Load config (explicit or auto-detected).
   * 3. Re-parse CLI to apply overrides on top of config.
   */
  const char *discovered_cmd = NULL;
  int temp_optind = optind;
  int opt;
  while ((opt = getopt_long(argc, argv, "+r:i:n:h:d:fHXPvVB:C:E:", long_options,
                            NULL)) != -1) {
    if (opt == 'C') {
      safe_strncpy(cfg.config_file, optarg, sizeof(cfg.config_file));
      cfg.config_file_specified = 1;
    }
  }
  if (optind < argc)
    discovered_cmd = argv[optind];
  optind = temp_optind; /* Reset for config loading */

  /* Load configuration if specified or available */
  if (cfg.config_file_specified) {
    ds_config_load(cfg.config_file, &cfg);
  } else {
    /* Auto-detect config from CLI rootfs arguments (preview only) */
    char temp_r[PATH_MAX] = {0}, temp_i[PATH_MAX] = {0};
    int t_optind = optind;
    while ((opt = getopt_long(argc, argv, "+r:i:n:h:d:fHXPvVB:C:E:",
                              long_options, NULL)) != -1) {
      if (opt == 'r')
        safe_strncpy(temp_r, optarg, sizeof(temp_r));
      if (opt == 'i')
        safe_strncpy(temp_i, optarg, sizeof(temp_i));
    }
    optind = t_optind;

    char *auto_p = ds_config_auto_path(temp_r[0] ? temp_r : temp_i);
    if (auto_p) {
      safe_strncpy(cfg.config_file, auto_p, sizeof(cfg.config_file));
      ds_config_load(cfg.config_file, &cfg);
      free(auto_p);
    }
  }

  /* Re-parse CLI to apply overrides on top of loaded configuration */
  int strict = (discovered_cmd && (strcmp(discovered_cmd, "run") == 0));
  const char *optstring =
      strict ? "+r:i:n:h:d:fHXPvVB:C:E:" : "r:i:n:h:d:fHXPvVB:C:E:";

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
      safe_strncpy(cfg.container_name, optarg, sizeof(cfg.container_name));
      break;
    case 'h':
      safe_strncpy(cfg.hostname, optarg, sizeof(cfg.hostname));
      break;
    case 'E':
      safe_strncpy(cfg.env_file, optarg, sizeof(cfg.env_file));
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
      cfg.enable_ipv6 = 1;
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
    case 'C':
      /* Already handled in first pass, but included here for consistency */
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
        if (strstr(dest, "..")) {
          ds_error("Path traversal detected in bind destination: %s", dest);
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
    case '?':
      if (optopt)
        ds_error(C_BOLD "Unrecognized option:" C_RESET " -%c", optopt);
      else
        ds_error(C_BOLD "Unrecognized option:" C_RESET " %s", argv[optind - 1]);
      printf("\n");
      ds_log("Use " C_BOLD "%s help" C_RESET " or " C_BOLD "--help" C_RESET
             " for usage information.",
             argv[0]);
      ret = 1;
      goto cleanup;
    default:
      ret = 1;
      goto cleanup;
    }
  }

  /* Prevent foreground mode in non-interactive environments (pipes, CI/CD) */
  if (cfg.foreground && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
    ds_die("Foreground mode (-f/--foreground) requires a fully interactive "
           "terminal (STDIN and STDOUT must be TTYs).");
  }

  if (optind >= argc) {
    ds_error(C_BOLD "Missing command" C_RESET
                    " (e.g., start, stop, enter, show)");
    printf("\n");
    ds_log("Use " C_BOLD "%s help" C_RESET " or " C_BOLD "--help" C_RESET
           " for usage information.",
           argv[0]);
    ret = 1;
    goto cleanup;
  }

  const char *cmd = argv[optind];

  /* Commands that don't need root or config */
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

  /* Validate if command exists at all before root check */
  const char *valid_cmds[] = {"start", "stop",   "restart", "enter",
                              "run",   "status", "pid",     "info",
                              "show",  "scan",   "docs",    NULL};
  int found = 0;
  for (int i = 0; valid_cmds[i]; i++) {
    if (strcmp(cmd, valid_cmds[i]) == 0) {
      found = 1;
      break;
    }
  }

  if (!found) {
    ds_error(C_BOLD "Unknown command:" C_RESET " '%s'", cmd);
    printf("\n");
    ds_log("Use " C_BOLD "%s help" C_RESET " or " C_BOLD "--help" C_RESET
           " for usage information.",
           argv[0]);
    ret = 1;
    goto cleanup;
  }

  if (strcmp(cmd, "docs") == 0) {
    print_documentation(argv[0]);
    ret = 0;
    goto cleanup;
  }

  /* Commands that need root or workspace */
  if (getuid() != 0)
    ds_die("Root privileges required for '%s'", cmd);
  ensure_workspace();

  if (strcmp(cmd, "show") == 0) {
    ret = show_containers();
    goto cleanup;
  }
  if (strcmp(cmd, "scan") == 0) {
    ret = scan_containers();
    goto cleanup;
  }

  /* Start command */
  if (strcmp(cmd, "start") == 0) {
    if (ds_config_validate(&cfg) < 0) {
      ret = 1;
      goto cleanup;
    }

    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    if (check_requirements() < 0) {
      ret = 1;
      goto cleanup;
    }

    print_ds_banner();
    check_kernel_recommendation();

    if (cfg.config_file[0]) {
      /* Resolve mandatory name/hostname early for saving */
      if (cfg.container_name[0] == '\0' && cfg.rootfs_path[0]) {
        generate_container_name(cfg.rootfs_path, cfg.container_name,
                                sizeof(cfg.container_name));
      }
      if (cfg.hostname[0] == '\0' && cfg.container_name[0]) {
        safe_strncpy(cfg.hostname, cfg.container_name, sizeof(cfg.hostname));
      }
    }

    ret = start_rootfs(&cfg);
    goto cleanup;
  }

  /* Other lifestyle commands */
  if (strcmp(cmd, "stop") == 0) {
    /* Support multi-stop via comma separated names in --name */
    if (strchr(cfg.container_name, ',')) {
      char *name = strtok(cfg.container_name, ",");
      while (name) {
        struct ds_config subcfg = cfg;
        if (load_config_with_recovery(name, &subcfg) == 0) {
          stop_rootfs(&subcfg, 0);
        }
        name = strtok(NULL, ",");
      }
      ret = 0;
      goto cleanup;
    }

    if (load_config_with_recovery(cfg.container_name, &cfg) < 0) {
      ret = 1;
      goto cleanup;
    }
    ret = stop_rootfs(&cfg, 0);
    goto cleanup;
  }

  if (strcmp(cmd, "restart") == 0) {
    if (load_config_with_recovery(cfg.container_name, &cfg) < 0) {
      ret = 1;
      goto cleanup;
    }

    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    if (check_requirements() < 0) {
      ret = 1;
      goto cleanup;
    }

    print_ds_banner();
    check_kernel_recommendation();

    if (cfg.config_file[0]) {
      /* Resolve mandatory name/hostname early for saving */
      if (cfg.container_name[0] == '\0' && cfg.rootfs_path[0]) {
        generate_container_name(cfg.rootfs_path, cfg.container_name,
                                sizeof(cfg.container_name));
      }
      if (cfg.hostname[0] == '\0' && cfg.container_name[0]) {
        safe_strncpy(cfg.hostname, cfg.container_name, sizeof(cfg.hostname));
      }
    }

    ret = restart_rootfs(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "status") == 0) {
    /* Quiet recovery lookup */
    load_config_with_recovery(cfg.container_name, &cfg);

    if (is_container_running(&cfg, NULL)) {
      printf("Container '%s' is " C_GREEN "Running" C_RESET "\n",
             cfg.container_name);
      ret = 0;
      goto cleanup;
    } else {
      printf("Container '%s' is " C_RED "Stopped" C_RESET "\n",
             cfg.container_name);
      ret = 1;
      goto cleanup;
    }
  }

  /* Machine-readable PID query — safe, never triggers cleanup.
   * Prints just the integer PID, or the literal string "NONE".
   * App uses this instead of 'status' to avoid PID file deletion races. */
  if (strcmp(cmd, "pid") == 0) {
    /* Run a silent full scan first to recover ALL missing containers
     * before checking the PID. This ensures any container whose pidfile
     * was nuked gets restored before we query it. */
    int prev_silent = ds_log_silent;
    ds_log_silent = 1;
    scan_containers();
    ds_log_silent = prev_silent;

    load_config_with_recovery(cfg.container_name, &cfg);

    pid_t pid = 0;
    if (is_container_running(&cfg, &pid) && pid > 0) {
      printf("%d\n", (int)pid);
      ret = 0;
      goto cleanup;
    }
    printf("NONE\n");
    ret = 1;
    goto cleanup;
  }

  if (strcmp(cmd, "info") == 0) {
    if (load_config_with_recovery(cfg.container_name, &cfg) < 0) {
      ret = 1;
      goto cleanup;
    }
    ret = show_info(&cfg, 0);
    goto cleanup;
  }

  if (strcmp(cmd, "enter") == 0) {
    if (load_config_with_recovery(cfg.container_name, &cfg) < 0) {
      ret = 1;
      goto cleanup;
    }

    /* Verify the container is actually running before attempting enter */
    pid_t running_pid = 0;
    if (!is_container_running(&cfg, &running_pid) || running_pid <= 0) {
      ds_error("Container '%s' is not running or invalid.", cfg.container_name);
      ret = 1;
      goto cleanup;
    }

    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    const char *user = (optind + 1 < argc) ? argv[optind + 1] : NULL;
    ret = enter_rootfs(&cfg, user);
    goto cleanup;
  }

  if (strcmp(cmd, "run") == 0) {
    if (load_config_with_recovery(cfg.container_name, &cfg) < 0) {
      ret = 1;
      goto cleanup;
    }
    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    if (optind + 1 >= argc) {
      ds_error("Command required for 'run' (e.g., run ls -l)");
      ret = 1;
      goto cleanup;
    }
    ret = run_in_rootfs(&cfg, argc - (optind + 1), argv + (optind + 1));
    goto cleanup;
  }

cleanup:
  free_config_env_vars(&cfg);
  free_config_binds(&cfg);
  return ret;
}
