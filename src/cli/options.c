/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/*
 * Resolve every SRC component of a --bind-mount / -B value string.
 * Format: SRC:DEST[,SRC:DEST,...]
 * Only the SRC part of each pair is a host-side path; DEST lives inside the
 * container namespace and is always absolute by convention.
 */
static char *resolve_bind_src(const char *val) {
  char *copy = strdup(val), *out = NULL;
  size_t size = 0;
  FILE *f = copy ? open_memstream(&out, &size) : NULL;
  if (!f) {
    free(copy);
    return strdup(val);
  }
  char *sv;
  int first = 1;
  for (char *tok = strtok_r(copy, ",", &sv); tok;
       tok = strtok_r(NULL, ",", &sv), first = 0) {
    char *col = strchr(tok, ':');
    if (col)
      *col = '\0';
    char *src = ds_resolve_path_arg(tok);
    fprintf(f, "%s%s%s%s", first ? "" : ",", src ? src : tok, col ? ":" : "",
            col ? col + 1 : "");
    free(src);
  }
  free(copy);
  if (fclose(f) != 0) {
    free(out);
    return strdup(val);
  }
  return out;
}

/*
 * Table of options whose next argument (or = suffix) is a filesystem path.
 * Keeps ds_resolve_argv_paths() free of hard-coded option names.
 */
static const struct {
  const char *opt;
  int is_bind; /* 1 = --bind-mount: resolve the SRC component only */
} ds_path_opts[] = {
    {"--rootfs", 0}, {"-r", 0},           {"--rootfs-img", 0}, {"-i", 0},
    {"--conf", 0},   {"--config", 0},     {"-C", 0},           {"--env", 0},
    {"-E", 0},       {"--bind-mount", 1}, {"--bind", 1},       {"-B", 1},
    {NULL, 0},
};

void ds_resolve_argv_paths(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg || arg[0] != '-') /* fast skip: non-option args are not paths */
      continue;

    for (int j = 0; ds_path_opts[j].opt; j++) {
      const char *opt = ds_path_opts[j].opt;
      int bind = ds_path_opts[j].is_bind;
      size_t olen = strlen(opt);

      /* "--opt=VALUE" form */
      if (strncmp(arg, opt, olen) == 0 && arg[olen] == '=') {
        const char *val = arg + olen + 1;
        if (!*val || (val[0] == '/' && !bind))
          break; /* absolute paths (non-bind) don't need resolution */
        char *resolved =
            bind ? resolve_bind_src(val) : ds_resolve_path_arg(val);
        if (resolved) {
          char *new_arg = malloc(olen + 1 + strlen(resolved) + 1);
          if (new_arg) {
            memcpy(new_arg, opt, olen);
            new_arg[olen] = '=';
            strcpy(new_arg + olen + 1, resolved);
            argv[i] = new_arg; /* argv[i] was a kernel-provided pointer; safe to
                                  replace */
          }
          free(resolved);
        }
        break;
      }

      /* "--opt VALUE" form (value is the next element) */
      if (strcmp(arg, opt) == 0 && i + 1 < argc) {
        const char *val = argv[i + 1];
        if (!val || !*val || (val[0] == '/' && !bind))
          continue;
        char *resolved =
            bind ? resolve_bind_src(val) : ds_resolve_path_arg(val);
        if (resolved)
          argv[i + 1] = resolved; /* kernel-provided string; safe to replace */
        break;
      }
    }
  }
}

/* Validate each comma-separated name in optarg; store raw value in out_buf. */
int parse_and_validate_names(const char *optarg, char *out_buf,
                             size_t out_size) {
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", optarg);
  char *sp, *tok = strtok_r(tmp, ",", &sp);
  while (tok) {
    if (reject_container_name(tok) < 0)
      return -1;
    tok = strtok_r(NULL, ",", &sp);
  }
  snprintf(out_buf, out_size, "%s", optarg);
  return 0;
}

const struct option long_options[] = {
    {"rootfs", required_argument, 0, 'r'},
    {"rootfs-img", required_argument, 0, 'i'},
    {"name", required_argument, 0, 'n'},
    {"hostname", required_argument, 0, 'h'},
    {"dns", required_argument, 0, 'd'},
    {"foreground", no_argument, 0, 'f'},
    {"hw-access", no_argument, 0, 'H'},
    {"x11", no_argument, 0, 'X'},
    {"termux-x11", no_argument, 0, 'X'}, /* old name, kept for scripts */
    {"tx11-flags", required_argument, 0, 271},
    {"disable-ipv6", no_argument, 0, 'I'},
    {"enable-android-storage", no_argument, 0, 'S'},
    {"selinux-permissive", no_argument, 0, 'P'},
    {"allow-sandboxing", no_argument, 0, 279},
    {"allow-userns", no_argument, 0, 279}, /* old name, kept for scripts */
    {"allow-vts", no_argument, 0, 278},
    {"allow-swap", no_argument, 0, 284},
    {"volatile", no_argument, 0, 'V'},
    {"bind-mount", required_argument, 0, 'B'},
    {"bind", required_argument, 0, 'B'},
    {"conf", required_argument, 0, 'C'},
    {"config", required_argument, 0, 'C'},
    {"env", required_argument, 0, 'E'},
    {"user", required_argument, 0, 'u'},
    {"stdin", no_argument, 0, 280},
    {"net", required_argument, 0, 257},
    {"port", required_argument, 0, 258},
    {"upstream", required_argument, 0, 259},
    {"force-cgroupv1", no_argument, 0, 260},
    {"privileged", required_argument, 0, 264},
    {"nat-ip", required_argument, 0, 262},
    {"gpu", no_argument, 0, 263},
    {"virgl", no_argument, 0, 270},
    {"virgl-flags", required_argument, 0, 272},
    {"pulse-audio", no_argument, 0, 273},
    {"anland", no_argument, 0, 283},
    {"gateway", required_argument, 0, 274},
    {"gateway-container", required_argument, 0, 274},
    {"gateway-net", required_argument, 0, 275},
    {"gateway-iface", required_argument, 0, 276},
    {"gateway-bridge", required_argument, 0, 277},
    {"macvlan-parent", required_argument, 0, 281},
    {"macvlan-mode", required_argument, 0, 282},
    {"reset", no_argument, 0, 256},
    {"json", no_argument, 0, 265},
    {"memory", required_argument, 0, 266},
    {"cpus", required_argument, 0, 267},
    {"pids-limit", required_argument, 0, 268},
    {"init", required_argument, 0, 269},
    {"help", no_argument, 0, 'v'},
    {0, 0, 0, 0}};

/* The command word in argv: the first argument that is neither an option nor
 * the value one takes, found with the real option table so a flag without a
 * value cannot hide it. NULL when there is none. Resets getopt. */
const char *ds_command_word(int argc, char **argv) {
  const char *cmd = NULL;
  int opt;
  optind = 0;
  while (!cmd && (opt = getopt_long(argc, argv, "-" DS_OPTSTRING, long_options,
                                    NULL)) != -1)
    if (opt == 1)
      cmd = optarg;
  optind = 0;
  return cmd;
}

/* The CLI override pass: apply every flag in argv on top of whatever cfg
 * already holds (defaults or a disk load). main runs it as pass 3 of its
 * three-pass scheme, and restart runs it a SECOND time over the same argv
 * after its post-stop workspace config reload, so every case here must stay
 * idempotent and must never write into argv. Returns 0 on success, -1 on a
 * bad flag value, 1 when help was printed. */
int ds_apply_cli_overrides(int argc, char **argv, struct ds_config *cfg,
                           int strict) {
  int opt;

  /* Strict mode for 'run' prevents stealing arguments from the sub-command. */
  const char *optstring = strict ? "+" DS_OPTSTRING : DS_OPTSTRING;

  /* --reset must wipe the config BEFORE the other flags land on top, but
   * getopt hands us options in argv order. Scan for it first so
   * reset-then-override holds no matter where --reset sits, on start and on
   * restart's second pass alike. */
  optind = 0;
  while ((opt = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
    if (opt == 256) {
      apply_reset_config(cfg);
      break;
    }
  }

  optind = 0;
  while ((opt = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
    switch (opt) {
    case 'r':
      safe_strncpy(cfg->rootfs_path, optarg, sizeof(cfg->rootfs_path));
      cfg->rootfs_img_path[0] = '\0';
      cfg->is_img_mount = 0;
      break;
    case 'i':
      safe_strncpy(cfg->rootfs_img_path, optarg, sizeof(cfg->rootfs_img_path));
      cfg->rootfs_path[0] = '\0';
      cfg->is_img_mount = 1;
      break;
    case 'n': {
      /* Names were already captured by main's discovery pass; re-validate for
       * identical error behavior and keep only the cfg copy. */
      char scratch[4096];
      if (parse_and_validate_names(optarg, scratch, sizeof(scratch)) < 0)
        return -1;
      /* "-n Ubuntu-24.04" finds the folder of "Ubuntu 24.04". Keep the name=
       * spelling already loaded, or the next save renames the container. */
      char want[256], have[256];
      sanitize_container_name(optarg, want, sizeof(want));
      sanitize_container_name(cfg->container_name, have, sizeof(have));
      if (strcmp(want, have) != 0)
        safe_strncpy(cfg->container_name, optarg, sizeof(cfg->container_name));
      break;
    }
    case 'h':
      safe_strncpy(cfg->hostname, optarg, sizeof(cfg->hostname));
      break;
    case 'E':
      safe_strncpy(cfg->env_file, optarg, sizeof(cfg->env_file));
      break;
    case 'u':
      /* Captured by main's discovery pass; nothing to re-apply here. */
      break;
    case 'd':
      safe_strncpy(cfg->dns_servers, optarg, sizeof(cfg->dns_servers));
      break;
    case 'f':
      cfg->foreground = 1;
      break;
    case 'H':
      cfg->hw_access = 1;
      break;
    case 'X':
      cfg->x11 = 1;
      break;
    case 271:
      free(cfg->tx11_extra_flags);
      cfg->tx11_extra_flags = optarg[0] ? strdup(optarg) : NULL;
      break;
    case 270:
      cfg->virgl = 1;
      break;
    case 272:
      free(cfg->virgl_extra_flags);
      cfg->virgl_extra_flags = optarg[0] ? strdup(optarg) : NULL;
      break;
    case 273:
      cfg->pulseaudio = 1;
      break;
    case 283:
      cfg->anland = 1;
      break;
    case 274:
      safe_strncpy(cfg->gateway_container, optarg,
                   sizeof(cfg->gateway_container));
      break;
    case 275:
      safe_strncpy(cfg->gateway_net, optarg, sizeof(cfg->gateway_net));
      break;
    case 276:
      safe_strncpy(cfg->gateway_lan_ifname, optarg,
                   sizeof(cfg->gateway_lan_ifname));
      break;
    case 277:
      safe_strncpy(cfg->gateway_bridge, optarg, sizeof(cfg->gateway_bridge));
      break;
    case 281:
      if (strlen(optarg) >= IFNAMSIZ) {
        ds_error("--macvlan-parent interface name is too long: %s", optarg);
        return -1;
      }
      safe_strncpy(cfg->macvlan_parent, optarg, sizeof(cfg->macvlan_parent));
      break;
    case 282:
      cfg->macvlan_mode = ds_parse_macvlan_mode(optarg);
      if (!cfg->macvlan_mode) {
        ds_error("Unknown --macvlan-mode '%s'. Valid options: bridge, "
                 "private, vepa, passthru",
                 optarg);
        return -1;
      }
      break;
    case 'I':
      cfg->disable_ipv6 = 1;
      break;
    case 'S':
      cfg->android_storage = 1;
      break;
    case 'P':
      cfg->selinux_permissive = 1;
      break;
    case 279:
      cfg->sandboxing_allowed = 1;
      break;
    case 278:
      cfg->allow_vts = 1;
      break;
    case 284:
      cfg->allow_swap = 1;
      break;
    case 'V':
      cfg->volatile_mode = 1;
      break;
    case 'B': {
      /* Tokenize a copy: strtok_r on optarg writes NULs into argv, which
       * would corrupt the second pass restart runs over the same argv. */
      char *dup = strdup(optarg);
      if (!dup)
        return -1;
      char err[256], *saveptr;
      int rc = 0;
      for (char *tok = strtok_r(dup, ",", &saveptr); tok && rc == 0;
           tok = strtok_r(NULL, ",", &saveptr))
        if ((rc = ds_parse_bind_spec(tok, cfg, err, sizeof(err))) < 0)
          ds_error("%s", err);
      free(dup);
      if (rc < 0)
        return -1;
      break;
    }
    case 'v':
      print_usage();
      return 1;
    case 257: {
      int mode = ds_parse_net_mode(optarg);
      if (mode < 0) {
        ds_error("Unknown network mode: '%s'. Valid options: host, nat, none, "
                 "gateway, macvlan",
                 optarg);
        return -1;
      }
      cfg->net_mode = (enum ds_net_mode)mode;
      break;
    }
    case 264:
      if (parse_privileged(optarg, cfg) < 0) {
        ds_error("--privileged refused");
        return -1;
      }
      break;

    case 258: {
      /* --port HOST[:CONTAINER][/proto], comma-separated, ranges allowed */
      char tmp[1024], err[128], *saveptr;
      safe_strncpy(tmp, optarg, sizeof(tmp));
      for (char *tok = strtok_r(tmp, ",", &saveptr); tok;
           tok = strtok_r(NULL, ",", &saveptr))
        if (ds_parse_port_forward(tok, cfg, err, sizeof(err)) < 0) {
          ds_error("%s in --port", err);
          return -1;
        }
      break;
    }

    case 259: {
      /* --upstream wlan0,rmnet*  (comma-separated, wildcards allowed).  Pins
       * NAT WAN to this list in priority order and disables auto-detection. */
      if (ds_parse_iface_csv(optarg, cfg->upstream_ifaces,
                             &cfg->upstream_iface_count,
                             DS_MAX_UPSTREAM_IFACES) > 0) {
        ds_error("Too many --upstream interfaces (max %d)",
                 DS_MAX_UPSTREAM_IFACES);
        return -1;
      }
      break;
    }

    case 260:
      /* --force-cgroupv1: escape hatch to legacy hierarchy */
      cfg->force_cgroupv1 = 1;
      break;

    case 262: {
      /* --nat-ip: static container IP inside the NAT subnet.
       * Only a basic format check here - subnet + uniqueness validation
       * happens in ds_net_resolve_static_ip() inside start_rootfs(). */
      char _errbuf[128];
      if (ds_net_validate_static_ip(optarg, _errbuf, sizeof(_errbuf)) != 0) {
        ds_error("--nat-ip '%s' is invalid: %s", optarg, _errbuf);
        return -1;
      }
      safe_strncpy(cfg->static_nat_ip, optarg, sizeof(cfg->static_nat_ip));
      break;
    }

    case 263:
      /* --gpu: enable GPU acceleration in isolated tmpfs mode.
       * Scans the host /dev for known GPU nodes and mknods them into the
       * container's isolated /dev.  Safe to combine with --hw-access (which
       * already does full GPU wiring). */
      cfg->gpu_mode = 1;
      break;
    case 265:
      /* --json: JSON output */
      cfg->json_output = 1;
      break;

    case 266: {
      long long v = ds_parse_size(optarg);
      if (v < 4 * 1024 * 1024) {
        ds_error("--memory: invalid or too small (min 4M): %s", optarg);
        return -1;
      }
      cfg->memory_limit = v;
      break;
    }
    case 267: {
      char *end;
      errno = 0;
      double cpus = strtod(optarg, &end);
      if (errno || end == optarg || *end != '\0' || cpus < 0.01) {
        ds_error("--cpus: invalid value: %s", optarg);
        return -1;
      }
      cfg->cpu_period = 100000;
      cfg->cpu_quota = (long long)(cpus * cfg->cpu_period);
      /* Enforce a strict minimum floor of 1000us (1ms) for the quota.
       * Most kernels reject values smaller than this with EINVAL to prevent
       * excessive scheduling overhead. */
      if (cfg->cpu_quota < 1000) {
        ds_error("--cpus: quota too small (min 0.01 cores / 1000us): %s",
                 optarg);
        return -1;
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
      if (errno || end == optarg || *end != '\0' || p < DS_MIN_PIDS_LIMIT ||
          p > 4194304LL) {
        ds_error("--pids-limit: invalid value (must be %d..4194304): %s",
                 DS_MIN_PIDS_LIMIT, optarg);
        return -1;
      }
      cfg->pids_limit = p;
      break;
    }
    case 269:
      if (strchr(optarg, ' ')) {
        ds_error("--init: path cannot contain spaces: %s", optarg);
        return -1;
      }
      safe_strncpy(cfg->custom_init, optarg, sizeof(cfg->custom_init));
      break;
    default:
      break;
    }
  }

  return 0;
}
