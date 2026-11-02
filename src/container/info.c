/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

static const char *get_architecture(void) {
  static struct utsname uts;
  if (uname(&uts) != 0)
    return "unknown";

  if (strcmp(uts.machine, "x86_64") == 0)
    return "x86_64";
  if (strcmp(uts.machine, "aarch64") == 0 || strcmp(uts.machine, "arm64") == 0)
    return "aarch64";
  /* A 64-bit backend only runs on a 64-bit kernel. A 32-bit name here is the
   * PER_LINUX32 personality a 32-bit Android hands every process. */
  if (strncmp(uts.machine, "arm", 3) == 0)
    return UINTPTR_MAX > 0xffffffffu ? "aarch64" : "arm";
  if (strcmp(uts.machine, "i686") == 0 || strcmp(uts.machine, "i386") == 0)
    return UINTPTR_MAX > 0xffffffffu ? "x86_64" : "x86";
  return uts.machine;
}

int show_info(struct ds_config *cfg, int trust_cfg_pid) {
  /* Case 1: No container name specified - try auto-resolution or listing */
  if (cfg->container_name[0] == '\0') {
    char first_name[256];
    int count = count_running_containers(first_name, sizeof(first_name));

    if (count == 0) {
      const char *host = is_android() ? "Android" : "Linux";
      const char *arch = get_architecture();
      printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);
      printf("\n" C_YELLOW "Container:" C_RESET " No containers running.\n\n");
      return 0;
    }

    if (count == 1) {
      /* Auto-resolve to the only running container */
      safe_strncpy(cfg->container_name, first_name,
                   sizeof(cfg->container_name));
      resolve_pidfile_from_name(first_name, cfg->pidfile, sizeof(cfg->pidfile));
    } else if (cfg->json_output) {
      return show_containers(cfg);
    } else {
      /* Multiple containers running, show Host info and list */
      const char *host = is_android() ? "Android" : "Linux";
      const char *arch = get_architecture();
      printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);
      printf("\n" C_YELLOW "Multiple containers running:" C_RESET "\n");
      show_containers(cfg);
      printf("\nUse '" C_GREEN "--name <NAME> info" C_RESET
             "' for detailed information.\n\n");
      return 0;
    }
  }

  /* Now we have a container name. Ensure its config is loaded from the source
   * of truth (container.config) so we show accurate feature info without
   * expensive live probing. */
  if (!trust_cfg_pid) {
    ds_config_load_by_name(cfg->container_name, cfg);
  }

  /* Case 2: Ensure pidfile resolved. */
  if (cfg->pidfile[0] == '\0' && cfg->container_name[0] != '\0') {
    resolve_pidfile_from_name(cfg->container_name, cfg->pidfile,
                              sizeof(cfg->pidfile));
  }

  /* Case 3: Validate running status */
  pid_t pid = 0;
  if (trust_cfg_pid && cfg->container_pid > 0) {
    /* Trust the PID we just got from the sync pipe.
     * We assume it's running because parent waited for boot marker. */
    pid = cfg->container_pid;
  } else {
    /* For other calls (e.g., info command), read and validate from pidfile. */
    is_container_running(cfg, &pid);
  }

  if (pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }

  long long lim_mem, lim_quota, lim_period, lim_pids;
  int swap_capped;
  ds_cgroup_get_limits(cfg->container_name, &lim_mem, &lim_quota, &lim_period,
                       &lim_pids, &swap_capped);
  int swap_allowed = !swap_capped;

  /* The acronym stays upper case in the output, the config file has nat. */
  const char *net =
      cfg->net_mode == DS_NET_NAT ? "NAT" : ds_net_mode_name(cfg->net_mode);

  /* Success - print Host and detailed Container info */
  if (cfg->json_output) {
    struct ds_status st = {0};
    safe_strncpy(st.name, cfg->container_name, sizeof(st.name));
    st.pid = pid;
    safe_strncpy(st.hostname, cfg->hostname, sizeof(st.hostname));
    ds_anland_load_sock(cfg);
    safe_strncpy(st.anland_sock, cfg->anland_sock, sizeof(st.anland_sock));
    st.x11 = cfg->x11;
    long ram_total = ds_collect_status(&st, 1);

    int first = 1;
    printf("{");
    ds_json_str("host_platform", is_android() ? "Android" : "Linux", &first);
    ds_json_str("host_arch", get_architecture(), &first);
    ds_json_status(&st, &first);
    ds_json_int("ram_total_kb", ram_total, &first);

    ds_json_str("networking_mode", net, &first);

    char buf[1024];
    size_t pos;
    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0])
        ds_json_str("nat_ip", ip, &first);

      if (cfg->upstream_iface_count > 0) {
        pos = 0;
        for (int i = 0; i < cfg->upstream_iface_count && pos < sizeof(buf); i++)
          pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                                  i ? "," : "", cfg->upstream_ifaces[i]);
        ds_json_str("upstream_interfaces", buf, &first);
      }
    } else if (cfg->net_mode == DS_NET_GATEWAY) {
      ds_json_str("gateway_container", cfg->gateway_container, &first);
      ds_json_str("gateway_net", cfg->gateway_net[0] ? cfg->gateway_net : "lan",
                  &first);
      if (cfg->gateway_bridge[0])
        ds_json_str("gateway_bridge", cfg->gateway_bridge, &first);
      ds_json_str("gateway_iface",
                  cfg->gateway_lan_ifname[0] ? cfg->gateway_lan_ifname : "eth1",
                  &first);
    } else if (cfg->net_mode == DS_NET_MACVLAN) {
      ds_json_str("macvlan_parent", cfg->macvlan_parent, &first);
      ds_json_str("macvlan_mode", ds_macvlan_mode_name(cfg->macvlan_mode),
                  &first);
    }

    ds_json_int("disable_ipv6", cfg->disable_ipv6, &first);
    if (is_android())
      ds_json_int("android_storage", cfg->android_storage, &first);

    ds_json_str("hw_access",
                cfg->hw_access ? "full" : (cfg->gpu_mode ? "GPU" : "none"),
                &first);

    if (is_android()) {
      if (cfg->tx11_extra_flags)
        ds_json_str("tx11_flags", cfg->tx11_extra_flags, &first);
      ds_json_int("virgl", cfg->virgl, &first);
      if (cfg->virgl_extra_flags)
        ds_json_str("virgl_flags", cfg->virgl_extra_flags, &first);
      ds_json_int("pulseaudio", cfg->pulseaudio, &first);
      ds_json_int("anland", cfg->anland, &first);
    }

    if (access("/sys/fs/selinux/enforce", R_OK) == 0)
      ds_json_str("selinux",
                  ds_get_selinux_status() == 0 ? "Permissive" : "Enforcing",
                  &first);

    ds_json_int("volatile_mode", cfg->volatile_mode, &first);
    ds_json_int("force_cgroup_v1", cfg->force_cgroupv1, &first);
    /* 0 is unlimited */
    ds_json_int("memory_limit", lim_mem, &first);
    ds_json_int("cpu_quota", lim_quota, &first);
    ds_json_int("cpu_period", lim_period, &first);
    ds_json_int("pids_limit", lim_pids, &first);
    ds_json_int("swap_allowed", swap_allowed, &first);
    ds_json_int("sandboxing_allowed", cfg->sandboxing_allowed, &first);
    ds_json_int("vts_allowed", cfg->allow_vts, &first);
    ds_json_int("foreground_mode", cfg->foreground, &first);
    ds_json_str("dns_servers", cfg->dns_servers, &first);

    pos = 0;
    for (int i = 0; i < cfg->port_forward_count && pos < sizeof(buf); i++) {
      struct ds_port_forward *pf = &cfg->port_forwards[i];
      if (pf->host_port_end == 0)
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%d:%d/%s",
                                i ? "," : "", pf->host_port, pf->container_port,
                                pf->proto);
      else
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                "%s%d-%d:%d-%d/%s", i ? "," : "", pf->host_port,
                                pf->host_port_end, pf->container_port,
                                pf->container_port_end, pf->proto);
    }
    ds_json_str("port_forwards", cfg->port_forward_count ? buf : "", &first);

    if (cfg->privileged_mask > 0) {
      ds_privileged_names(cfg->privileged_mask, buf, sizeof(buf), ",");
      ds_json_str("privileged_mode", buf, &first);
    }

    ds_json_int("bind_mount_count", cfg->bind_count, &first);
    ds_json_int("env_var_count", cfg->env_var_count, &first);
    printf("}\n");
  } else {
    /* Human-readable output */
    const char *host = is_android() ? "Android" : "Linux";
    const char *arch = get_architecture();
    printf(C_GREEN "Host:" C_RESET " %s %s\n", host, arch);

    printf("\n" C_GREEN "Container:" C_RESET " %s (RUNNING)\n",
           cfg->container_name);
    printf("  PID: %d\n", pid);

    char pretty[256];
    char osr_path[PATH_MAX];
    if (build_proc_root_path(pid, "/etc/os-release", osr_path,
                             sizeof(osr_path)) == 0) {
      get_os_pretty(osr_path, pretty, sizeof(pretty));
      if (pretty[0])
        printf("  OS: %s\n", pretty);
    }

    if (cfg->hostname[0])
      printf("  Hostname: %s\n", cfg->hostname);

    /* Uptime (only if called from info command) */
    if (!trust_cfg_pid) {
      long uptime_sec = ds_get_container_uptime(pid);
      if (uptime_sec >= 0) {
        char uptime_str[128];
        ds_format_uptime(uptime_sec, uptime_str, sizeof(uptime_str));
        printf("  Uptime: %s\n", uptime_str);
      }
    }

    printf("\n" C_GREEN "Features:" C_RESET "\n");
    int feat_count = 0;

    /* 1. Networking Mode */
    printf("  Networking: %s\n", net);
    feat_count++;

    /* 2. NAT/Gateway Configuration */
    if (cfg->net_mode == DS_NET_GATEWAY) {
      printf("  Gateway: %s (%s)\n", cfg->gateway_container,
             cfg->gateway_net[0] ? cfg->gateway_net : "lan");
      feat_count++;
    }

    if (cfg->net_mode == DS_NET_MACVLAN) {
      printf("  Macvlan: %s (%s)\n", cfg->macvlan_parent,
             ds_macvlan_mode_name(cfg->macvlan_mode));
      feat_count++;
    }

    if (cfg->net_mode == DS_NET_NAT) {
      const char *ip =
          cfg->static_nat_ip[0] ? cfg->static_nat_ip : cfg->nat_container_ip;
      if (ip[0]) {
        printf("  NAT IP: %s\n", ip);
        feat_count++;
      }

      if (cfg->upstream_iface_count > 0) {
        printf("  Upstream (pinned): ");
        for (int i = 0; i < cfg->upstream_iface_count; i++)
          printf("%s%s", cfg->upstream_ifaces[i],
                 (i < cfg->upstream_iface_count - 1) ? ", " : "");
        printf("\n");
        feat_count++;
      }

      if (cfg->port_forward_count > 0) {
        printf("  Port forwards: ");
        for (int i = 0; i < cfg->port_forward_count; i++) {
          struct ds_port_forward *pf = &cfg->port_forwards[i];
          if (pf->host_port_end == 0) {
            printf("%d:%d", pf->host_port, pf->container_port);
          } else {
            printf("%d-%d:%d-%d", pf->host_port, pf->host_port_end,
                   pf->container_port, pf->container_port_end);
          }
          printf("/%s%s", pf->proto,
                 (i < cfg->port_forward_count - 1) ? ", " : "");
        }
        printf("\n");
        feat_count++;
      }
    }

    /* 3. DNS */
    if (cfg->dns_servers[0]) {
      printf("  DNS Servers: %s\n", cfg->dns_servers);
      feat_count++;
    }

    /* 4. IPv6 */
    if (cfg->disable_ipv6) {
      printf("  Disable IPv6: yes\n");
      feat_count++;
    }

    /* 5. Android Storage */
    if (is_android() && cfg->android_storage) {
      printf("  Android storage: enabled\n");
      feat_count++;
    }

    /* 6. HW/GPU Access */
    if (cfg->hw_access) {
      printf("  " C_RED "HW access:" C_RESET " full\n");
      feat_count++;
    } else if (cfg->gpu_mode) {
      printf("  HW access: GPU\n");
      feat_count++;
    }

    /* 7. X11 */
    if (cfg->x11) {
      printf("  X11: enabled\n");
      feat_count++;
    }

    /* 8. VirGL */
    if (is_android() && cfg->virgl) {
      printf("  VirGL: enabled\n");
      feat_count++;
    }

    /* 9. PulseAudio */
    if (is_android() && cfg->pulseaudio) {
      printf("  PulseAudio: enabled\n");
      feat_count++;
    }

    /* 9b. Anland */
    if (is_android() && cfg->anland) {
      printf("  Anland: enabled\n");
      feat_count++;
    }

    /* 10. SELinux Status */
    if (access("/sys/fs/selinux/enforce", R_OK) == 0) {
      int status = ds_get_selinux_status();
      if (status == 0) {
        printf("  " C_RED "SELinux:" C_RESET " Permissive\n");
      } else {
        printf("  SELinux: Enforcing\n");
      }
      feat_count++;
    }

    /* 11. Volatile Mode */
    if (cfg->volatile_mode) {
      printf("  Volatile mode: enabled\n");
      feat_count++;
    }

    /* 12. Cgroup v1 */
    if (cfg->force_cgroupv1) {
      printf("  " C_RED "Force Cgroup V1:" C_RESET " yes\n");
      feat_count++;
    }

    /* 14. Sandboxing (user namespaces) */
    if (cfg->sandboxing_allowed) {
      printf("  " C_RED "Sandboxing:" C_RESET " enabled\n");
      feat_count++;
    }

    /* 14b. Host virtual terminals */
    if (cfg->allow_vts) {
      printf("  " C_RED "Host VTs:" C_RESET " tty1-6 unmasked\n");
      feat_count++;
    }

    /* 15. Privileged Mode */
    if (cfg->privileged_mask > 0) {
      char tags[64];
      ds_privileged_names(cfg->privileged_mask, tags, sizeof(tags), ", ");
      printf("  " C_RED "Privileged mode:" C_RESET " %s\n", tags);
      feat_count++;
    }

    /* 16. Bind Mounts */
    if (cfg->bind_count > 0) {
      printf("  Bind mounts: %d active\n", cfg->bind_count);
      feat_count++;
    }

    /* 17. Custom Init */
    if (cfg->custom_init[0]) {
      printf("  " C_RED "Custom Init:" C_RESET " %s\n", cfg->custom_init);
      feat_count++;
    }

    /* 18. Environment Variables */
    if (cfg->env_var_count > 0) {
      printf("  Env variables: %d loaded\n", cfg->env_var_count);
      feat_count++;
    }

    if (feat_count == 0) {
      printf("  None\n");
    }

    /* Resource limits in force. Live usage only for the info command: right
     * after a start or restart there is nothing meaningful to report yet. */
    if (lim_mem || lim_quota || lim_pids) {
      long long mu = -1, cache = 0, cu = -1, pu = -1;
      if (!trust_cfg_pid)
        ds_cgroup_get_usage(cfg->container_name, &mu, &cache, &cu, &pu);
      /* Used as free(1) counts it: without the reclaimable file cache */
      if (mu >= cache)
        mu -= cache;
      printf("\n" C_GREEN "Resources:" C_RESET "\n");

      if (lim_mem) {
        char used[32], lim[32];
        ds_format_size(lim_mem, lim, sizeof(lim));
        if (mu >= 0) {
          ds_format_size(mu, used, sizeof(used));
          printf("  Memory : %s / %s\n", used, lim);
        } else {
          printf("  Memory : %s\n", lim);
        }
        printf("  Swap   : %s\n", swap_allowed ? "allowed" : "not allowed");
      }
      if (lim_quota) {
        double cores = (double)lim_quota / (double)lim_period;
        printf("  CPU    : %.2f cores", cores);
        long uptime = cu >= 0 ? ds_get_container_uptime(pid) : 0;
        /* Average use of the allowed cores since boot. cu is in usec. */
        if (uptime > 0)
          printf(" (Avg usage: %.1f%%)",
                 ((double)cu / 1e6 / (double)uptime) / cores * 100.0);
        printf("\n");
      }
      if (lim_pids) {
        printf("  PIDs   : %lld", lim_pids);
        if (pu >= 0)
          printf(" (current: %lld)", pu);
        printf("\n");
      }
    }
    printf("\n");
  }
  return 0;
}
