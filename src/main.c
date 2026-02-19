/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

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
  printf("  -p, --pidfile=PATH        Path to pidfile\n");
  printf("  -h, --hostname=NAME       Set container hostname\n");
  printf("  -f, --foreground          Run in foreground (attach console)\n");
  printf("  -V, --volatile            Discard changes on exit (OverlayFS)\n");
  printf(
      "  -B, --bind-mount=SRC:DEST Bind mount host directory into container\n");
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
  struct ds_config cfg = {0};
  safe_strncpy(cfg.prog_name, argv[0], sizeof(cfg.prog_name));

  static struct option long_options[] = {
      {"rootfs", required_argument, 0, 'r'},
      {"rootfs-img", required_argument, 0, 'i'},
      {"name", required_argument, 0, 'n'},
      {"pidfile", required_argument, 0, 'p'},
      {"hostname", required_argument, 0, 'h'},
      {"foreground", no_argument, 0, 'f'},
      {"hw-access", no_argument, 0, 'H'},
      {"enable-ipv6", no_argument, 0, 'I'},
      {"enable-android-storage", no_argument, 0, 'S'},
      {"selinux-permissive", no_argument, 0, 'P'},
      {"volatile", no_argument, 0, 'V'},
      {"bind-mount", required_argument, 0, 'B'},
      {"help", no_argument, 0, 'v'},
      {0, 0, 0, 0}};

  extern int opterr;
  opterr = 0;

  /*
   * Two-pass argument parsing:
   * 1. Pass 1 finds the command strictly (using '+' prefix).
   * 2. Based on the command, Pass 2 decides whether to allow flag permutation.
   *    Permutation is allowed for life-cycle commands (e.g., start -f) but
   *    forbidden for execution commands (e.g., run ls -l) to protect sub-flags.
   */
  const char *discovered_cmd = NULL;
  int temp_optind = optind;
  while (getopt_long(argc, argv, "+r:i:n:p:h:fHISPvVB:", long_options, NULL) !=
         -1)
    ;
  if (optind < argc)
    discovered_cmd = argv[optind];
  optind = temp_optind; /* Reset for Pass 2 */

  int strict = (discovered_cmd && (strcmp(discovered_cmd, "run") == 0));
  const char *optstring =
      strict ? "+r:i:n:p:h:fHISPvVB:" : "r:i:n:p:h:fHISPvVB:";

  int opt;
  while ((opt = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
    switch (opt) {
    case 'r':
      safe_strncpy(cfg.rootfs_path, optarg, sizeof(cfg.rootfs_path));
      break;
    case 'i':
      safe_strncpy(cfg.rootfs_img_path, optarg, sizeof(cfg.rootfs_img_path));
      break;
    case 'n':
      safe_strncpy(cfg.container_name, optarg, sizeof(cfg.container_name));
      break;
    case 'p':
      safe_strncpy(cfg.pidfile, optarg, sizeof(cfg.pidfile));
      break;
    case 'h':
      safe_strncpy(cfg.hostname, optarg, sizeof(cfg.hostname));
      break;
    case 'f':
      cfg.foreground = 1;
      break;
    case 'H':
      cfg.hw_access = 1;
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
    case 'B': {
      char *saveptr;
      char *token = strtok_r(optarg, ",", &saveptr);
      while (token) {
        if (cfg.bind_count >= DS_MAX_BINDS) {
          ds_error("Too many bind mounts (max %d)", DS_MAX_BINDS);
          return 1;
        }

        char *sep = strchr(token, ':');
        if (!sep) {
          ds_error("Invalid bind mount format: %s (expected SRC:DEST)", token);
          return 1;
        }
        *sep = '\0';
        const char *src = token;
        const char *dest = sep + 1;

        if (dest[0] != '/') {
          ds_error("Bind destination must be an absolute path: %s", dest);
          return 1;
        }
        if (strstr(dest, "..")) {
          ds_error("Path traversal detected in bind destination: %s", dest);
          return 1;
        }

        safe_strncpy(cfg.binds[cfg.bind_count].src, src,
                     sizeof(cfg.binds[cfg.bind_count].src));
        safe_strncpy(cfg.binds[cfg.bind_count].dest, dest,
                     sizeof(cfg.binds[cfg.bind_count].dest));
        cfg.bind_count++;

        token = strtok_r(NULL, ",", &saveptr);
      }
      break;
    }
    case 'v':
      print_usage();
      return 0;
    case '?':
      if (optopt)
        ds_error(C_BOLD "Unrecognized option:" C_RESET " -%c", optopt);
      else
        ds_error(C_BOLD "Unrecognized option:" C_RESET " %s", argv[optind - 1]);
      printf("\n");
      ds_log("Use " C_BOLD "%s help" C_RESET " or " C_BOLD "--help" C_RESET
             " for usage information.",
             argv[0]);
      return 1;
    default:
      return 1;
    }
  }

  if (optind >= argc) {
    ds_error(C_BOLD "Missing command" C_RESET
                    " (e.g., start, stop, enter, show)");
    printf("\n");
    ds_log("Use " C_BOLD "%s help" C_RESET " or " C_BOLD "--help" C_RESET
           " for usage information.",
           argv[0]);
    return 1;
  }

  const char *cmd = argv[optind];

  /* Commands that don't need root or config */
  if (strcmp(cmd, "check") == 0)
    return check_requirements_detailed();
  if (strcmp(cmd, "version") == 0) {
    printf("v%s\n", DS_VERSION);
    return 0;
  }
  if (strcmp(cmd, "help") == 0) {
    print_usage();
    return 0;
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
    return 1;
  }

  if (strcmp(cmd, "show") == 0)
    return show_containers();
  if (strcmp(cmd, "docs") == 0) {
    print_documentation(argv[0]);
    return 0;
  }

  /* Commands that need root or workspace */
  if (getuid() != 0)
    ds_die("Root privileges required for '%s'", cmd);
  ensure_workspace();

  if (strcmp(cmd, "scan") == 0)
    return scan_containers();

  /* Start command */
  if (strcmp(cmd, "start") == 0) {
    if (validate_kernel_version() < 0)
      return 1;
    if (cfg.rootfs_path[0] == '\0' && cfg.rootfs_img_path[0] == '\0')
      ds_die("--rootfs or --rootfs-img is required for start");
    if (cfg.rootfs_path[0] != '\0' && cfg.rootfs_img_path[0] != '\0')
      ds_die("--rootfs and --rootfs-img are mutually exclusive");
    if (cfg.container_name[0] != '\0' && cfg.pidfile[0] != '\0')
      ds_die("--name and --pidfile are mutually exclusive");

    if (check_requirements() < 0)
      return 1;

    return start_rootfs(&cfg);
  }

  /* Other lifestyle commands */
  if (strcmp(cmd, "stop") == 0) {
    /* Support multi-stop via comma separated names in --name */
    if (strchr(cfg.container_name, ',')) {
      char *name = strtok(cfg.container_name, ",");
      while (name) {
        struct ds_config subcfg = cfg;
        safe_strncpy(subcfg.container_name, name,
                     sizeof(subcfg.container_name));
        stop_rootfs(&subcfg, 0);
        name = strtok(NULL, ",");
      }
      return 0;
    }
    return stop_rootfs(&cfg, 0);
  }

  if (strcmp(cmd, "restart") == 0) {
    if (validate_kernel_version() < 0)
      return 1;
    return restart_rootfs(&cfg);
  }

  if (strcmp(cmd, "status") == 0) {
    if (check_status(&cfg, NULL) == 0) {
      printf("Container '%s' is " C_GREEN "Running" C_RESET "\n",
             cfg.container_name);
      return 0;
    } else {
      printf("Container '%s' is " C_RED "Stopped" C_RESET "\n",
             cfg.container_name);
      return 1;
    }
  }

  /* Machine-readable PID query — safe, never triggers cleanup.
   * Prints just the integer PID, or the literal string "NONE".
   * App uses this instead of 'status' to avoid PID file deletion races. */
  if (strcmp(cmd, "pid") == 0) {
    if (auto_resolve_pidfile(&cfg) < 0) {
      printf("NONE\n");
      return 1;
    }
    pid_t pid = 0;
    if (read_and_validate_pid(cfg.pidfile, &pid) < 0 || pid <= 0) {
      printf("NONE\n");
      return 1;
    }
    /* Only confirm alive — do NOT call cleanup on failure */
    if (kill(pid, 0) < 0) {
      printf("NONE\n");
      return 1;
    }
    printf("%d\n", (int)pid);
    return 0;
  }
  if (strcmp(cmd, "info") == 0)
    return show_info(&cfg);

  if (strcmp(cmd, "enter") == 0) {
    if (validate_kernel_version() < 0)
      return 1;
    /* Optional: we could validate container exists here,
     * but enter_rootfs already does it. */
    const char *user = (optind + 1 < argc) ? argv[optind + 1] : NULL;
    return enter_rootfs(&cfg, user);
  }

  if (strcmp(cmd, "run") == 0) {
    if (validate_kernel_version() < 0)
      return 1;
    if (optind + 1 >= argc) {
      ds_error("Command required for 'run' (e.g., run ls -l)");
      return 1;
    }
    return run_in_rootfs(&cfg, argc - (optind + 1), argv + (optind + 1));
  }

  return 0;
}
