/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* Validation Helpers */

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

  if (cfg->custom_init[0]) {
    if (cfg->custom_init[0] != '/') {
      ds_error("Custom init path must be absolute: %s", cfg->custom_init);
      errors++;
    } else if (strchr(cfg->custom_init, ' ')) {
      ds_error("Custom init path cannot contain spaces: %s", cfg->custom_init);
      errors++;
    }
  }

  if (cfg->net_mode == DS_NET_GATEWAY) {
    if (!cfg->gateway_container[0]) {
      ds_error("--net=gateway requires --gateway=<container>.");
      errors++;
    } else if (reject_container_name(cfg->gateway_container) < 0) {
      errors++;
    } else if (strcmp(cfg->gateway_container, cfg->container_name) == 0) {
      ds_error("A container cannot use itself as --gateway.");
      errors++;
    }

    if (cfg->gateway_bridge[0] && strlen(cfg->gateway_bridge) >= IFNAMSIZ) {
      ds_error("--gateway-bridge interface name is too long: %s",
               cfg->gateway_bridge);
      errors++;
    }
    if (cfg->gateway_lan_ifname[0] &&
        strlen(cfg->gateway_lan_ifname) >= IFNAMSIZ) {
      ds_error("--gateway-iface interface name is too long: %s",
               cfg->gateway_lan_ifname);
      errors++;
    }
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
    show_containers(cfg);
    return -1;
  }

  safe_strncpy(cfg->container_name, first_name, sizeof(cfg->container_name));
  return 0;
}

/* Command Dispatch */

static void enforce_nat_safety(struct ds_config *cfg) {
  if (cfg->net_mode != DS_NET_HOST && !check_ns(CLONE_NEWNET, "net"))
    ds_die("Kernel does not support CLONE_NEWNET (network namespaces), so "
           "--net=nat, none, gateway and macvlan cannot work. Use --net=host "
           "for shared host networking.");

  /* --upstream and --port are only meaningful with --net=nat.  --upstream is an
   * optional override (auto-detection is the default) - never mandatory. */
  if (cfg->upstream_iface_count > 0 && cfg->net_mode != DS_NET_NAT) {
    ds_warn("--upstream is only valid with --net=nat - ignoring");
    cfg->upstream_iface_count = 0;
  }
  if (cfg->port_forward_count > 0 && cfg->net_mode != DS_NET_NAT) {
    ds_warn("--port is only valid with --net=nat - ignoring");
    cfg->port_forward_count = 0;
  }

  if (cfg->net_mode != DS_NET_NAT && cfg->net_mode != DS_NET_GATEWAY)
    return;

  /* Gateway mode has no bridgeless fallback: the gateway is the bridge. */
  int gateway = cfg->net_mode == DS_NET_GATEWAY;
  const char *name = gateway ? "gateway" : "nat";
  char reason[512];
  int probe = ds_nl_probe_nat_capability(reason, sizeof(reason));
  if (probe < 0 || (gateway && probe == 1))
    ds_die("--net=%s is not supported on this kernel: %s. Use --net=host for "
           "shared host networking, or rebuild your kernel with "
           "CONFIG_BRIDGE=y and CONFIG_VETH=y.",
           name, reason);
  cfg->net_bridgeless = !gateway && probe == 1;
  ds_log("[NET] Kernel capability probe passed for --net=%s%s.", name,
         cfg->net_bridgeless ? " (FALLBACK: No BRIDGE)" : "");
}

/* stop with a comma-separated name list: stop each one in turn, 1 if any
 * of them failed. */
static int ds_multi_stop(const char *raw_names) {
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", raw_names);
  int ret = 0;
  char *sp, *tok = strtok_r(tmp, ",", &sp);
  while (tok) {
    struct ds_config c;
    ds_config_init(&c);
    safe_strncpy(c.container_name, tok, sizeof(c.container_name));
    /* ds_log() keys the log file by this global, not by the config it is
     * handed, so point it at the container being stopped. */
    safe_strncpy(ds_log_container_name, tok, sizeof(ds_log_container_name));
    if (stop_rootfs(&c, 0) != 0)
      ret = 1;
    tok = strtok_r(NULL, ",", &sp);
  }
  ds_log_container_name[0] = '\0';
  return ret;
}

int main(int argc, char **argv) {
  int ret = 0;
  struct ds_config cfg;
  char raw_names[4096] = "";
  ds_config_init(&cfg);

  safe_strncpy(cfg.prog_name, argv[0], sizeof(cfg.prog_name));

  extern int opterr;
  opterr = 0;

  /* Resolve relative path arguments to absolute before any parsing.
   * The daemon runs from CWD='/' (daemonize calls chdir("/")), so a relative
   * path like --conf=./file.conf would resolve against '/' in the re-exec'd
   * child.  Doing this here - while we still own the user's CWD - means every
   * subsequent getopt pass reads absolute paths, covering all execution modes.
   * Everything after `run` is the container's command, whose -i, -E or -C
   * mean something else entirely: `ffmpeg -i x` reached the container as
   * `ffmpeg -i //x`. Only the words before it are ours to resolve.
   */
  int own_args = argc - 1;
  const char *word = ds_command_word(argc, argv);
  if (word && strcmp(word, "run") == 0)
    for (int k = 1; k < argc; k++)
      if (argv[k] == word)
        own_args = k - 1;
  ds_resolve_argv_paths(own_args, argv + 1);

  /*
   * Multi-pass argument parsing:
   * 1. Discovery Pass: Find command and identity (name/rootfs/conf) anywhere.
   * 2. Load config.
   * 3. Override Pass: Apply CLI overrides on top of loaded config.
   */
  const char *discovered_cmd = NULL;
  char temp_r[PATH_MAX] = {0}, temp_i[PATH_MAX] = {0};
  char run_user[256] = {0};
  int run_stdin = 0;
  int opt;

  /* 1. Discovery Pass: Capture identity and command without permuting argv.
   * Using '-' at the start of optstring returns non-options as '1'. */
  while ((opt = getopt_long(argc, argv, "-" DS_OPTSTRING, long_options,
                            NULL)) != -1) {
    if (opt == '?') {
      /* opterr is 0, so this is the only word the user gets. optopt names a
       * bad short letter; for a long option it is 0 and optind has moved
       * past the word. */
      if (optopt)
        ds_error("Unknown or incomplete option: -%c", optopt);
      else
        ds_error("Unknown or incomplete option: %s", argv[optind - 1]);
      ret = 1;
      goto cleanup;
    }
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
      if (parse_and_validate_names(optarg, raw_names, sizeof(raw_names)) < 0) {
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
    } else if (opt == 280) {
      run_stdin = 1;
    }
    /* Discover --net early so kernel probe can run before config load */
    if (opt == 257) {
      int mode = ds_parse_net_mode(optarg);
      if (mode < 0) {
        ds_error("Unknown network mode: '%s'. Valid options: host, nat, none, "
                 "gateway, macvlan",
                 optarg);
        ret = 1;
        goto cleanup;
      }
      cfg.net_mode = (enum ds_net_mode)mode;
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
  int is_no_root_cmd =
      (discovered_cmd && (strcmp(discovered_cmd, "docs") == 0 ||
                          strcmp(discovered_cmd, "help") == 0 ||
                          strcmp(discovered_cmd, "version") == 0 ||
                          strcmp(discovered_cmd, "mode") == 0 ||
                          strcmp(discovered_cmd, "check") == 0));

  if (!is_daemon_cmd && !is_no_root_cmd && getenv("DS_NO_PROXY") == NULL) {
    int proxy_ret =
        ds_client_run(argc - 1, argv + 1, discovered_cmd, run_stdin);
    if (proxy_ret != -2) {
      ret = proxy_ret;
      goto cleanup;
    }
  }

  /* Unified root gate: block all non-exempt commands before any work begins */
  if (!is_no_root_cmd && getuid() != 0) {
    ds_error("Root privileges required for '%s'",
             discovered_cmd ? discovered_cmd : "(unknown)");
    ret = 1;
    goto cleanup;
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
   * by name. Skip for comma-separated names - ds_multi_* handles those. */
  int multi = strchr(raw_names, ',') != NULL;
  if (!loaded && cfg.container_name[0] != '\0' && !multi) {
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

  /* 2. Override Pass: Apply CLI flags on top of config. It also handles
   * --reset internally, wiping the loaded config before the flags land. */
  int strict = (discovered_cmd && (strcmp(discovered_cmd, "run") == 0));
  int orc = ds_apply_cli_overrides(argc, argv, &cfg, strict);
  if (orc != 0) {
    ret = (orc < 0) ? 1 : 0;
    goto cleanup;
  }
  if (optind >= argc) {
    ds_error(C_BOLD "Missing command" C_RESET);
    ds_log("Run '" C_BOLD "%s help" C_RESET "' for usage information.",
           cfg.prog_name);
    ret = 1;
    goto cleanup;
  }

  const char *cmd = argv[optind];

  /* Set up global logging context for centralized logging engine. A
   * comma-separated list is not a container; ds_multi_stop() sets the name
   * per token, and a copy here would create a log folder named after the
   * whole list. */
  if (cfg.container_name[0] != '\0' && !multi) {
    safe_strncpy(ds_log_container_name, cfg.container_name,
                 sizeof(ds_log_container_name));
  }

  /* The NAT probe check runs stamps its verdict under Net/, so the workspace
   * comes first. A non-root check cannot create it and mkdir_p stays quiet. */
  ensure_workspace();

  /* Basic info commands */
  if (strcmp(cmd, "check") == 0) {
    ret = check_requirements_detailed(cfg.json_output);
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

  if (strcmp(cmd, "show") == 0) {
    ret = show_containers(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "scan") == 0) {
    scan_containers();
    ret = 0;
    goto cleanup;
  }

  /* Pinned like runc does, not the caller's: setup creates the container's /
   * (volatile), /run/droidspaces, bind parents and host cgroup dirs with
   * literal modes, and init hands this to every service. A 077 root shell
   * would lock every non-root user out. */
  if (strcmp(cmd, "start") == 0 || strcmp(cmd, "restart") == 0)
    umask(022);

  /* start/restart: single container only */
  if (strcmp(cmd, "start") == 0) {
    if (multi) {
      ds_error("start does not support multiple containers.");
      ret = 1;
      goto cleanup;
    }
    if (validate_configuration_cli(&cfg) < 0) {
      ret = 1;
      goto cleanup;
    }
    if (check_requirements_hw(cfg.hw_access) < 0) {
      ret = 1;
      goto cleanup;
    }
    enforce_nat_safety(&cfg);
    print_ds_banner();
    ds_cgroup_host_bootstrap(cfg.force_cgroupv1);
    ret = start_rootfs(&cfg);
    goto cleanup;
  }

  if (strcmp(cmd, "stop") == 0) {
    ret = multi ? ds_multi_stop(raw_names) : stop_rootfs(&cfg, 0);
    goto cleanup;
  }

  if (strcmp(cmd, "restart") == 0) {
    if (multi) {
      ds_error("restart does not support multiple containers.");
      ret = 1;
      goto cleanup;
    }
    if (check_requirements_hw(cfg.hw_access) < 0) {
      ret = 1;
      goto cleanup;
    }
    enforce_nat_safety(&cfg);
    ds_cgroup_host_bootstrap(cfg.force_cgroupv1);
    ret = restart_rootfs(&cfg, argc, argv);
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
    const char *user = (optind + 1 < argc) ? argv[optind + 1] : "root";
    ret = enter_rootfs(&cfg, user);
    goto cleanup;
  }

  if (strcmp(cmd, "run") == 0) {
    /* run [--] <cmd>: a leading "--" fences off a command that itself starts
     * with a dash, the way env(1) takes it. */
    char **run_argv = argv + optind + 1;
    if (*run_argv && strcmp(*run_argv, "--") == 0)
      run_argv++;
    if (!*run_argv) {
      ds_error("Command required for 'run'");
      ret = 1;
      goto cleanup;
    }
    const char *as_user = (run_user[0] != '\0') ? run_user : NULL;
    ret = run_in_rootfs(&cfg, run_argv, as_user);
    goto cleanup;
  }

  if (strcmp(cmd, "daemon") == 0) {
    ret = ds_daemon_run(cfg.foreground, argv);
    goto cleanup;
  }

  ds_error("Unknown command: '%s'", cmd);
  ds_log("Run '" C_BOLD "%s help" C_RESET "' for usage information.",
         cfg.prog_name);
  ret = 1;

cleanup:
  ds_config_free(&cfg);
  return ret;
}
