/*
 * Droidspaces v5 — High-performance Container Runtime
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
  printf("      --net=MODE            Networking mode: host (default), nat, "
         "none\n");
  printf("      --upstream IFACE[,..] Upstream internet interface(s) for nat "
         "mode\n"
         "                            REQUIRED with --net=nat. Comma-separated "
         "list\n"
         "                            in priority order. Monitor tracks "
         "whichever\n"
         "                            is currently active (RUNNING + has a "
         "route).\n"
         "                            e.g. --upstream wlan0\n"
         "                            e.g. --upstream wlan0,rmnet0,ccmni1\n");
  printf(
      "      --port HOST:CONT[/proto] Forward host port to container (nat "
      "mode)\n"
      "                            e.g. --port 22:22 --port 8096:8096/tcp\n");
  printf(
      "  -B, --bind-mount=SRC:DEST Bind mount host directory into container\n");
  printf("  -C, --conf=PATH           Load configuration from file\n");
  printf("      --reset               Reset config to defaults (keeps "
         "name/rootfs)\n");
  printf(
      "      --force-cgroupv2      Use cgroupv2 even on kernels < 5.2.\n"
      "                            Allows modern systemd on legacy kernels\n"
      "                            that have cgroupv2 mounted but incomplete\n"
      "                            controllers. May cause instability.\n"
      "                            No-op if host lacks cgroupv2 support.\n");
  printf("  --help                    Show this help message\n\n");

  printf(C_BOLD "Examples:" C_RESET "\n");
  printf("  droidspaces --rootfs=/path/to/rootfs start\n");
  printf("  droidspaces --name=mycontainer enter\n");
  printf("  droidspaces --name=mycontainer stop\n\n");
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
  int is_ipv6 = cfg->enable_ipv6;

  /* Nuke config reliance: parse argv directly to guarantee the warning
   * triggers regardless of what ds_config_load() wiped during restart. */
  if (argv != NULL) {
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--net=nat") == 0)
        is_nat = 1;
      if (strcmp(argv[i], "--net") == 0 && i + 1 < argc &&
          strcmp(argv[i + 1], "nat") == 0)
        is_nat = 1;
      if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--enable-ipv6") == 0)
        is_ipv6 = 1;
    }
  }

  if (is_nat && is_ipv6) {
    printf("\n" C_YELLOW C_BOLD
           "[ WARNING: IPv6 UNSUPPORTED IN NAT MODE ]" C_RESET "\n");
    ds_log("IPv6 connectivity is currently not supported with --net=nat.");
    ds_log("IPv6 functionality will be disabled for this session.");
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
    ds_warn("--upstream is only valid with --net=nat — ignoring");
    cfg->upstream_iface_count = 0;
  }
  if (cfg->port_forward_count > 0 && cfg->net_mode != DS_NET_NAT) {
    ds_warn("--port is only valid with --net=nat — ignoring");
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

  cfg->enable_ipv6 = 0;

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

static void print_cgroup_status(struct ds_config *cfg) {
  if (cfg->force_cgroupv1) {
    ds_warn("Using legacy Cgroup V1 hierarchy (forced by --force-cgroupv1)");
  } else if (!ds_cgroup_host_is_v2()) {
    ds_warn("Host does not support Cgroup V2 (falling back to legacy V1)");
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
      {"enable-ipv6", no_argument, 0, 'I'},
      {"enable-android-storage", no_argument, 0, 'S'},
      {"selinux-permissive", no_argument, 0, 'P'},
      {"volatile", no_argument, 0, 'V'},
      {"bind-mount", required_argument, 0, 'B'},
      {"conf", required_argument, 0, 'C'},
      {"config", required_argument, 0, 'C'},
      {"env", required_argument, 0, 'E'},
      {"net", required_argument, 0, 257},
      {"port", required_argument, 0, 258},
      {"upstream", required_argument, 0, 259},
      {"force-cgroupv1", no_argument, 0, 260},
      {"reset", no_argument, 0, 256},
      {"help", no_argument, 0, 'v'},
      {0, 0, 0, 0}};

  extern int opterr;
  opterr = 0;

  /*
   * Multi-pass argument parsing:
   * 1. Discovery Pass: Find command and identity (name/rootfs/conf) anywhere.
   * 2. Load config.
   * 3. Override Pass: Apply CLI overrides on top of loaded config.
   */
  const char *discovered_cmd = NULL;
  char temp_r[PATH_MAX] = {0}, temp_i[PATH_MAX] = {0};
  int reset_config = 0;
  int cli_net_mode_set = 0;
  enum ds_net_mode cli_net_mode = DS_NET_HOST;
  int opt;

  /* 1. Discovery Pass: Capture identity and command without permuting argv.
   * Using '-' at the start of optstring returns non-options as '1'. */
  while ((opt = getopt_long(argc, argv, "-r:i:n:h:d:fHXPvVB:C:E:", long_options,
                            NULL)) != -1) {
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
      safe_strncpy(cfg.container_name, optarg, sizeof(cfg.container_name));
    } else if (opt == 'r') {
      safe_strncpy(temp_r, optarg, sizeof(temp_r));
    } else if (opt == 'i') {
      safe_strncpy(temp_i, optarg, sizeof(temp_i));
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
   * Unified Configuration Discovery and Loading
   * 1. Try to load from explicitly provided config file.
   * 2. Otherwise try to auto-detect config from rootfs paths.
   * 3. Ensure we have a container name for stateful commands.
   * 4. Perform a recovery scan to load from ~/.local/share/... if config hasn't
   * been loaded yet.
   */
  int is_stateful =
      (discovered_cmd && (strcmp(discovered_cmd, "stop") == 0 ||
                          strcmp(discovered_cmd, "restart") == 0 ||
                          strcmp(discovered_cmd, "status") == 0 ||
                          strcmp(discovered_cmd, "pid") == 0 ||
                          strcmp(discovered_cmd, "info") == 0 ||
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

    case 258: {
      /* --port HOST:CONTAINER[/proto]  (comma-separated list allowed) */
      char tmp[256];
      strncpy(tmp, optarg, sizeof(tmp) - 1);
      tmp[sizeof(tmp) - 1] = '\0';
      char *saveptr;
      char *tok = strtok_r(tmp, ",", &saveptr);
      while (tok) {
        if (cfg.port_forward_count >= DS_MAX_PORT_FORWARDS) {
          ds_error("Too many --port mappings (max %d)", DS_MAX_PORT_FORWARDS);
          ret = 1;
          goto cleanup;
        }
        struct ds_port_forward *pf = &cfg.port_forwards[cfg.port_forward_count];

        /* Default proto */
        strncpy(pf->proto, "tcp", sizeof(pf->proto));

        /* Optional /proto suffix */
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

        /* HOST:CONTAINER */
        char *colon = strchr(tok, ':');
        if (!colon) {
          ds_error(
              "Invalid --port format '%s' (expected HOST:CONTAINER[/proto])",
              tok);
          ret = 1;
          goto cleanup;
        }
        *colon = '\0';
        int hp = atoi(tok);
        int cp = atoi(colon + 1);
        if (hp <= 0 || hp > 65535 || cp <= 0 || cp > 65535) {
          ds_error("Invalid port numbers in --port '%s:%s'", tok, colon + 1);
          ret = 1;
          goto cleanup;
        }
        int dup = 0;
        for (int i = 0; i < cfg.port_forward_count; i++) {
          if (cfg.port_forwards[i].host_port == (uint16_t)hp &&
              cfg.port_forwards[i].container_port == (uint16_t)cp &&
              strcmp(cfg.port_forwards[i].proto, pf->proto) == 0) {
            dup = 1;
            break;
          }
        }
        if (!dup) {
          pf->host_port = (uint16_t)hp;
          pf->container_port = (uint16_t)cp;
          cfg.port_forward_count++;
        }
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

    case '?':
      /* Ignore unknown options during override if we already found a cmd */
      if (discovered_cmd)
        break;
      ret = 1;
      goto cleanup;
    default:
      break;
    }
  }

  if (optind >= argc) {
    ds_error(C_BOLD "Missing command" C_RESET);
    ret = 1;
    goto cleanup;
  }

  const char *cmd = argv[optind];

  /* Set up global logging context for centralized logging engine */
  if (cfg.container_name[0] != '\0') {
    safe_strncpy(ds_log_container_name, cfg.container_name,
                 sizeof(ds_log_container_name));
  }

  /* Prevent foreground mode in non-interactive environments for interactive
   * commands */
  if (cfg.foreground && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "restart") == 0 ||
        strcmp(cmd, "enter") == 0) {
      ds_error("Foreground mode requires a fully interactive terminal.");
      ret = 1;
      goto cleanup;
    }
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
    if (check_requirements() < 0) {
      ret = 1;
      goto cleanup;
    }
    if (reset_config)
      apply_reset_config(&cfg, cli_net_mode_set, cli_net_mode);
    enforce_nat_safety(&cfg, argc, argv);

    print_ds_banner();
    print_cgroup_status(&cfg);
    check_kernel_recommendation();
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
    if (check_requirements() < 0) {
      ret = 1;
      goto cleanup;
    }
    enforce_nat_safety(&cfg, argc, argv);
    print_ds_banner();
    print_cgroup_status(&cfg);
    ret = restart_rootfs(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "status") == 0) {
    if (is_container_running(&cfg, NULL)) {
      printf("Container '%s' is " C_GREEN "Running" C_RESET "\n",
             cfg.container_name);
      ret = 0;
    } else {
      printf("Container '%s' is " C_RED "Stopped" C_RESET "\n",
             cfg.container_name);
      ret = 1;
    }
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

  if (strcmp(cmd, "enter") == 0) {
    if (validate_kernel_version() < 0) {
      ret = 1;
      goto cleanup;
    }
    const char *user = (optind + 1 < argc) ? argv[optind + 1] : NULL;
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
    ret = run_in_rootfs(&cfg, argc - (optind + 1), argv + (optind + 1));
    goto cleanup;
  }

  ds_error("Unknown command: '%s'", cmd);
  ret = 1;

cleanup:
  free_config_env_vars(&cfg);
  free_config_binds(&cfg);
  return ret;
}
