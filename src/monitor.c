/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * ds_monitor_run - Supervisor process for a single container instance.
 *
 * Called immediately after fork() in start_rootfs(). Never returns - always
 * ends with _exit(). sync_pipe_write is the write-end of the parent sync
 * pipe; the monitor (or its intermediate child) writes the container init PID
 * through it on the first boot cycle, then closes it.
 * ---------------------------------------------------------------------------*/
void ds_monitor_run(struct ds_config *cfg, int sync_pipe_write) {
  int sync_pipe[2];
  sync_pipe[0] = -1;
  sync_pipe[1] = sync_pipe_write;

  if (setsid() < 0 && errno != EPERM) {
    /* Fatal only if it's not EPERM (which means already leader) */
    ds_error("setsid failed: %s", strerror(errno));
    _exit(EXIT_FAILURE);
  }

  /* Monitor Hardening
   * Ignore common termination signals to prevent Android's process manager
   * from ending the supervisor prematurely. Monitor must only die via
   * SIGKILL or successful container exit. */
  signal(SIGTERM, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);

  /* Make monitor unkillable */
  ds_oom_protect();

  /* Enter droidspacesd domain. Best-effort: if policy not yet loaded
   * (user hasn't rebooted after module install), we stay in the inherited
   * domain -- still functional since droidspacesd is typepermissive. */
  ds_selinux_enter_domain();

  prctl(PR_SET_NAME, "[ds-monitor]", 0, 0, 0);

  /* Unshare namespaces - Monitor enters new UTS, IPC, and optionally Cgroup
   * namespaces immediately. PID namespace is NOT unshared here because
   * unshare(CLONE_NEWPID) can only be called once per process. Instead,
   * each boot/reboot cycle forks an intermediate that creates a fresh
   * PID namespace. */
  int ns_flags = CLONE_NEWUTS | CLONE_NEWIPC;

  /* Adaptive Cgroup Namespace (introduced in Linux 4.6).
   *
   * CGROUP SELECTION: Only enable cgroupns when V2 is active.
   * If --force-cgroupv1 is set, we skip cgroupns so setup_cgroups()
   * has full rights to create named V1 hierarchies from the host context. */
  int cg_ns_ok = (access("/proc/self/ns/cgroup", F_OK) == 0) &&
                 (ds_cgroup_host_is_v2() && !cfg->force_cgroupv1);
  if (cg_ns_ok) {
    /* To get isolation from a cgroup namespace, we must be in a sub-cgroup
     * BEFORE we unshare. If we are in the root '/', the namespace root
     * will be the host's root, providing zero isolation.
     * We use a container-specific path to avoid conflicts. */
    if (access("/sys/fs/cgroup/cgroup.procs", F_OK) == 0) {
      char safe_name[256];
      sanitize_container_name(cfg->container_name, safe_name,
                              sizeof(safe_name));

      /* v2: enable requested controllers top-down BEFORE mkdir.
       * Controllers only appear in a child cgroup if the parent's
       * subtree_control has them enabled first. Walk two levels:
       * /sys/fs/cgroup -> /sys/fs/cgroup/droidspaces */
      if (cfg->memory_limit || cfg->cpu_quota || cfg->pids_limit) {
        /* Build enable string with snprintf offsets instead of strncat to
         * avoid truncation. Use ds_cg_word_in_list() for exact word-boundary
         * matching to prevent false positives (e.g. matching "cpuset"
         * when looking for "cpu"). */
        char enable[64] = {0};
        char buf[256];
        int eoff = 0;
        if (read_file("/sys/fs/cgroup/cgroup.controllers", buf, sizeof(buf)) >
            0) {
          if (cfg->memory_limit && ds_cg_word_in_list(buf, "memory")) {
            int n = snprintf(enable + eoff, sizeof(enable) - (size_t)eoff,
                             "%s+memory", eoff ? " " : "");
            if (n > 0)
              eoff += n;
          }
          if (cfg->cpu_quota && ds_cg_word_in_list(buf, "cpu")) {
            int n = snprintf(enable + eoff, sizeof(enable) - (size_t)eoff,
                             "%s+cpu", eoff ? " " : "");
            if (n > 0)
              eoff += n;
          }
          if (cfg->pids_limit && ds_cg_word_in_list(buf, "pids")) {
            int n = snprintf(enable + eoff, sizeof(enable) - (size_t)eoff,
                             "%s+pids", eoff ? " " : "");
            if (n > 0)
              eoff += n;
          }
        }
        if (eoff > 0) {
          if (write_file("/sys/fs/cgroup/cgroup.subtree_control", enable) < 0)
            ds_warn("[CGROUP] subtree_control (root): %s", strerror(errno));
          mkdir_p("/sys/fs/cgroup/droidspaces", 0755);
          if (write_file("/sys/fs/cgroup/droidspaces/cgroup.subtree_control",
                         enable) < 0)
            ds_warn("[CGROUP] subtree_control (droidspaces): %s",
                    strerror(errno));
        }
      }

      char cg_path[PATH_MAX];
      snprintf(cg_path, sizeof(cg_path), "/sys/fs/cgroup/droidspaces/%s",
               safe_name);
      mkdir_p(cg_path, 0755);

      char cg_procs[PATH_MAX];
      safe_strncpy(cg_procs, cg_path, sizeof(cg_procs));
      strncat(cg_procs, "/cgroup.procs",
              sizeof(cg_procs) - strlen(cg_procs) - 1);
      FILE *f = fopen(cg_procs, "we");
      if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
      }
    }
    ns_flags |= CLONE_NEWCGROUP;
  } else {
    /* Legacy kernel without force flag - skip cgroupns, run in host
     * cgroupns with full rights so setup_cgroups() can create named
     * v1 hierarchies. */
  }

  /* Apply resource limits. On v2 hosts this writes memory.max / cpu.max /
   * pids.max into the delegated cgroup. On v1 or --force-cgroupv1 the
   * function skips with a warning since v1 delegation is unreliable. */
  if (ds_cgroup_apply_limits(cfg) < 0 &&
      (cfg->memory_limit || cfg->cpu_quota || cfg->pids_limit))
    ds_warn("[CGROUP] Some resource limits could not be enforced.");

  if (unshare(ns_flags) < 0)
    ds_die("unshare failed: %s", strerror(errno));

  int stdio_redirected = 0;

  /* Reboot-aware boot loop
   * Each iteration forks an intermediate child that creates a fresh PID
   * namespace (unshare(CLONE_NEWPID)) and then forks the container init.
   *
   * Reboot detection uses EXIT CODES ONLY (no signal interception):
   *   1. Init calls reboot(2) → kernel kills init with SIGHUP
   *   2. Intermediate sees WTERMSIG(init)==SIGHUP via waitpid()
   *   3. Intermediate exits with DS_REBOOT_EXIT (249)
   *   4. Monitor sees WEXITSTATUS(mid)==249 → loop back
   *
   * This eliminates ghost containers because the Monitor never handles
   * SIGHUP - it only checks a deterministic exit code. */
reboot_loop:;
  /* Close existing pipes from previous cycle to prevent FD leaks */
  if (cfg->net_ready_pipe[0] >= 0) {
    close(cfg->net_ready_pipe[0]);
    close(cfg->net_ready_pipe[1]);
    cfg->net_ready_pipe[0] = cfg->net_ready_pipe[1] = -1;
  }
  if (cfg->net_done_pipe[0] >= 0) {
    close(cfg->net_done_pipe[0]);
    close(cfg->net_done_pipe[1]);
    cfg->net_done_pipe[0] = cfg->net_done_pipe[1] = -1;
  }

  /* Networking pipes (created fresh for every boot cycle) */
  int mid_sync_pipe[2] = {-1, -1};
  if (cfg->net_mode != DS_NET_HOST) {
    if (pipe(cfg->net_ready_pipe) < 0 || pipe(cfg->net_done_pipe) < 0 ||
        pipe(mid_sync_pipe) < 0) {
      ds_error("Failed to create NAT sync pipes: %s", strerror(errno));
      _exit(EXIT_FAILURE);
    }

    /* Set FD_CLOEXEC on all new pipe ends */
    fcntl(cfg->net_ready_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(cfg->net_ready_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(cfg->net_done_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(cfg->net_done_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(mid_sync_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(mid_sync_pipe[1], F_SETFD, FD_CLOEXEC);

    ds_log("[NET] Sync pipes created for net_mode=%d", cfg->net_mode);
  }

  /* First boot only: ensure no stale container with the same name is running
   */
  if (!cfg->reboot_cycle) {
    pid_t existing_pid = 0;
    if (is_container_running(cfg, &existing_pid)) {
      if (existing_pid != getpid()) {
        /*
         * Crucial Safety: Only kill the process if it's confirmed to be a
         * Droidspaces container. This prevents killing random processes that
         * might have recycled the PID after the container died without
         * cleanup.
         */
        if (is_valid_container_pid(existing_pid)) {
          ds_warn("Killing stale container with same name (PID %d)",
                  existing_pid);
          kill(existing_pid, SIGKILL);
          usleep(100000);
        }
      }
    }
  }

  /* Stdio handling for monitor in background mode (early redirection).
   * We must do this BEFORE forking the intermediate process, otherwise
   * the intermediate inherits the user's stdout/stderr (e.g. a pipe)
   * and holds it open indefinitely, causing CLI hangs in direct mode.
   * We only keep it open if we haven't reached the networking setup yet,
   * as setup_veth_host_side() might still need to print logs. */
  if (!cfg->foreground && !stdio_redirected) {
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, 0);
      /* Note: we don't redirect 1 and 2 here yet because we want to see
       * networking setup logs. We'll do a full redirect after the fork. */
      close(devnull);
    }
  }

  pid_t mid_pid = fork();
  if (mid_pid < 0)
    _exit(EXIT_FAILURE);

  if (mid_pid == 0) {
    /* INTERMEDIATE PROCESS
     * Create a fresh PID namespace (and NET namespace for NAT/none modes)
     * for this boot cycle. */
    int clone_flags = CLONE_NEWPID;
    if (cfg->net_mode != DS_NET_HOST)
      clone_flags |= CLONE_NEWNET;

    if (unshare(clone_flags) < 0) {
      ds_error("unshare(PID|NET) failed: %s", strerror(errno));
      _exit(EXIT_FAILURE);
    }

    pid_t init_pid = fork();
    if (init_pid < 0)
      _exit(EXIT_FAILURE);

    if (init_pid == 0) {
      /* CONTAINER INIT (PID 1 inside namespace) */
      /* Close pipe ends the init process doesn't use */
      if (cfg->net_mode != DS_NET_HOST) {
        if (mid_sync_pipe[0] >= 0)
          close(mid_sync_pipe[0]);
        if (mid_sync_pipe[1] >= 0)
          close(mid_sync_pipe[1]);
      }
      close(sync_pipe[1]);
      _exit(internal_boot(cfg));
    }

    /* Intermediate: redirect stdio to /dev/null NOW (after forking init).
     * It only exists to wait for init and has no business talking to the
     * user's terminal or holding pipes open.
     *
     * BUG FIX: this redirect was previously placed BEFORE the fork(), which
     * caused init_pid to inherit /dev/null for fd 1 and fd 2. Every
     * ds_log() call inside internal_boot() writes to stdout, so all boot
     * logs were silently swallowed by /dev/null - visible only in the log
     * file (which uses direct file I/O, not stdout). Moving the redirect
     * here means only the intermediate itself goes silent; internal_boot()
     * retains the original terminal fds until it redirects to /dev/console
     * at its own step 24. */
    if (!cfg->foreground) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        close(devnull);
      }
    }

    /* Send init PID to monitor so it can target /proc/<pid>/ns/net */
    if (cfg->net_mode != DS_NET_HOST && mid_sync_pipe[1] >= 0) {
      if (write(mid_sync_pipe[1], &init_pid, sizeof(pid_t)) != sizeof(pid_t)) {
        ds_warn(
            "[NET] Intermediate: failed to write init_pid to mid_sync_pipe");
      }
      close(mid_sync_pipe[1]);
      close(mid_sync_pipe[0]);
      mid_sync_pipe[0] = mid_sync_pipe[1] = -1;
    }

    /* Send init PID to parent via sync pipe (first boot only) */
    if (sync_pipe[1] >= 0) {
      if (write(sync_pipe[1], &init_pid, sizeof(pid_t)) != sizeof(pid_t)) {
        /* Reader will detect failure or handle empty/partial read */
      }
      close(sync_pipe[1]);
      sync_pipe[1] = -1;
    } else {
      /* Reboot cycle - update PID file directly so
       * 'droidspaces show/status' report the correct PID. */
      char pid_str[32];
      snprintf(pid_str, sizeof(pid_str), "%d", init_pid);
      write_file_atomic(cfg->pidfile, pid_str);

      char global_pf[PATH_MAX];
      resolve_pidfile_from_name(cfg->container_name, global_pf,
                                sizeof(global_pf));
      if (strcmp(cfg->pidfile, global_pf) != 0)
        write_file_atomic(global_pf, pid_str);
    }

    /* Wait for init to exit */
    int init_status;
    while (waitpid(init_pid, &init_status, 0) < 0 && errno == EINTR)
      ;

    /* Convert kernel signal to exit code:
     * SIGHUP from reboot(RESTART) → DS_REBOOT_EXIT (249)
     * Everything else → pass through as-is */
    if (WIFSIGNALED(init_status) && WTERMSIG(init_status) == SIGHUP) {
      _exit(DS_REBOOT_EXIT);
    }

    _exit(WIFEXITED(init_status) ? WEXITSTATUS(init_status) : EXIT_FAILURE);
  }

  /* MONITOR continues here */

  /* Close sync pipe write end (intermediate handles it) */
  if (sync_pipe[1] >= 0) {
    close(sync_pipe[1]);
    sync_pipe[1] = -1;
  }

  /* Monitor: NAT networking handshake
   *
   * Sequence (all non-blocking after pipes are ready):
   *   1. Read init_pid from mid_sync_pipe[0]
   *   2. Read "ready" byte from net_ready_pipe[0]  (init sent it)
   *   3. Call setup_veth_host_side → creates bridge/veth/rules
   *   4. Write ds_net_handshake to net_done_pipe[1] (init reads it)
   *
   * This handshake ensures the veth peer is moved into the container's
   * netns while the init process is alive and waiting, avoiding the race
   * where we try to open /proc/<pid>/ns/net before the process exists. */
  if (cfg->net_mode != DS_NET_HOST && mid_sync_pipe[0] >= 0) {
    close(mid_sync_pipe[1]); /* monitor is reader */

    pid_t netns_pid = -1;
    ssize_t nr = read(mid_sync_pipe[0], &netns_pid, sizeof(pid_t));
    close(mid_sync_pipe[0]);

    if (nr != sizeof(pid_t) || netns_pid <= 0) {
      ds_warn("[NET] Monitor: failed to read init_pid from mid_sync_pipe "
              "(nr=%zd pid=%d)",
              nr, (int)netns_pid);
    } else {
      ds_log("[NET] Monitor: received init_pid=%d, waiting for READY...",
             (int)netns_pid);
      cfg->container_pid = netns_pid;

      /* Close the ends we don't need */
      close(cfg->net_ready_pipe[1]); /* monitor reads, init writes */
      close(cfg->net_done_pipe[0]);  /* monitor writes, init reads  */

      char rdy;
      if (read(cfg->net_ready_pipe[0], &rdy, 1) < 0) {
        ds_warn("[NET] Monitor: failed to read READY signal: %s",
                strerror(errno));
      } else {
        ds_log("[NET] Monitor: READY received from init (pid=%d)",
               (int)netns_pid);
      }
      close(cfg->net_ready_pipe[0]);

      if (cfg->net_mode == DS_NET_NAT) {
        if (setup_veth_host_side(cfg, netns_pid) < 0) {
          ds_warn("[NET] Monitor: setup_veth_host_side failed - "
                  "container will have no internet");
        } else {
          /* Start the dynamic route monitor thread to handle WiFi/Mobile
           * switches */
          ds_net_start_route_monitor();
        }
      } else if (cfg->net_mode == DS_NET_GATEWAY) {
        if (setup_gateway_veth_side(cfg, netns_pid) < 0) {
          ds_warn("[NET] Monitor: setup_gateway_veth_side failed - "
                  "container will remain isolated");
        }
      }

      /* Send handshake to init */
      struct ds_net_handshake hs;
      ds_net_derive_handshake(netns_pid, cfg, &hs);
      ds_log("[NET] Monitor: sending DONE: peer=%s ip=%s", hs.peer_name,
             hs.ip_str);
      if (write(cfg->net_done_pipe[1], &hs, sizeof(hs)) != (ssize_t)sizeof(hs))
        ds_warn("[NET] Monitor: failed to write handshake to init");
      close(cfg->net_done_pipe[1]);
    }
  }

  /* Capture PID namespace inode for virtualization PID-recycling guard.
   * container_pid may be 0 on HOST mode until pidfile is written - that's
   * fine; ds_get_pid_ns_inode(0) returns 0 and update will skip safely. */
  cfg->ns_inode = ds_get_pid_ns_inode(cfg->container_pid);

  /* Ensure monitor is not sitting inside any mount point */
  if (chdir("/") < 0) {
    ds_warn("Failed to chdir to /: %s", strerror(errno));
  }

  /* Stdio handling for monitor in background mode (first boot only) */
  if (!cfg->foreground && !stdio_redirected) {
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, 0);
      dup2(devnull, 1);
      dup2(devnull, 2);
      close(devnull);
    }
    stdio_redirected = 1;
  }

  /* MONITOR waits for intermediate to complete */

  /* CRITICAL TIMING: Close sync pipe write end ONLY after intermediate
   * finishes. This ensures intermediate can write init PID to parent on first
   * boot. Closing too early causes parent's read() to return EOF, triggering
   * cleanup that deletes the PID file while container is still booting. See
   * commit 6f9f99a for details on the boot-at-boot race this prevents. */
  if (sync_pipe[1] >= 0) {
    close(sync_pipe[1]);
    sync_pipe[1] = -1;
  }

  /* Monitor heartbeat loop: 500ms poll + virtualization update.
   * WNOHANG lets us update virtual /proc files while waiting for mid_pid. */
  int status = 0;
  {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    while (1) {
      pid_t r = waitpid(mid_pid, &status, WNOHANG);
      if (r == mid_pid)
        break;
      if (r < 0 && errno != EINTR)
        break;

      /* HOST mode: monitor never gets container_pid via mid_sync_pipe.
       * Poll the pidfile (written by parent shortly after sync_pipe read)
       * until we have a valid PID, then capture ns_inode once. */
      if (cfg->container_pid <= 0 && cfg->pidfile[0]) {
        pid_t p = -1;
        if (read_and_validate_pid(cfg->pidfile, &p) == 0 && p > 0) {
          cfg->container_pid = p;
          cfg->ns_inode = ds_get_pid_ns_inode(p);
          write_monitor_debug_log(cfg->container_name,
                                  "[VIRT] resolved container_pid=%d "
                                  "ns_inode=%lu from pidfile",
                                  (int)p, cfg->ns_inode);
        }
      }

      ds_virtualize_update(cfg);

      if (sfd >= 0) {
        struct pollfd pfd = {.fd = sfd, .events = POLLIN};
        poll(&pfd, 1, 500);
        if (pfd.revents & POLLIN) {
          struct signalfd_siginfo si;
          while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si))
            ; /* drain */
        }
      } else {
        usleep(500000);
      }
    }

    if (sfd >= 0)
      close(sfd);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
  }

  /* Log what monitor saw */
  if (WIFEXITED(status)) {
    int code = WEXITSTATUS(status);
    if (code == DS_REBOOT_EXIT) {
      write_monitor_debug_log(cfg->container_name, "Detected internal REBOOT");
    } else {
      write_monitor_debug_log(cfg->container_name,
                              "Detected container SHUTDOWN (exit: %d)", code);
    }
  } else if (WIFSIGNALED(status)) {
    write_monitor_debug_log(cfg->container_name,
                            "Intermediate killed by signal: %d (%s)",
                            WTERMSIG(status), strsignal(WTERMSIG(status)));
  }

  /* Reboot detection (internal reboot) */
  if (WIFEXITED(status) && WEXITSTATUS(status) == DS_REBOOT_EXIT) {
    /* Check for external lock - if exists, abort reboot and let CLI handle it
     */
    if (is_external_lock_active(cfg->container_name)) {
      write_monitor_debug_log(
          cfg->container_name,
          "External command lock detected - aborting internal reboot");
      goto monitor_cleanup_and_exit;
    }

    if (cfg->foreground) {
      printf("\n" C_WHITE "Droidspaces v%s : Container " C_GREEN
             "%s" C_RESET C_WHITE " is now Rebooting...." C_RESET "\n",
             DS_VERSION, cfg->container_name);
      fflush(stdout);
    }

    /* Synchronize container_pid in Monitor */
    pid_t new_pid = -1;
    if (read_and_validate_pid(cfg->pidfile, &new_pid) == 0) {
      cfg->container_pid = new_pid;
    }

    /* Re-write the same UUID to sync file for the next boot cycle.
     * internal_boot reads this across the pivot_root boundary. */
    if (!cfg->volatile_mode && cfg->rootfs_path[0]) {
      char uuid_sync[PATH_MAX];
      snprintf(uuid_sync, sizeof(uuid_sync), "%.4060s/.droidspaces-uuid",
               cfg->rootfs_path);
      write_file(uuid_sync, cfg->uuid);
    }

    /* Reload from workspace (canonical path the user edits) */
    {
      free_config_binds(cfg);
      /* Preserve env_vars across the reboot */
      struct ds_env_var *saved_vars = cfg->env_vars;
      int saved_count = cfg->env_var_count;
      int saved_cap = cfg->env_var_capacity;
      int old_force_cgv1 = cfg->force_cgroupv1;

      struct ds_config reboot_cfg = *cfg;
      if (ds_config_load_by_name(cfg->container_name, &reboot_cfg) == 0) {
        reboot_cfg.env_vars = saved_vars;
        reboot_cfg.env_var_count = saved_count;
        reboot_cfg.env_var_capacity = saved_cap;
        if (strcmp(cfg->dns_servers, reboot_cfg.dns_servers) != 0) {
          reboot_cfg.dns_server_content[0] = '\0';
          ds_get_dns_servers(reboot_cfg.dns_servers,
                             reboot_cfg.dns_server_content,
                             sizeof(reboot_cfg.dns_server_content));
        }
        /* Cgroup namespace is locked at monitor startup - can't change */
        if (reboot_cfg.force_cgroupv1 != old_force_cgv1) {
          printf("\n" C_BOLD C_YELLOW "force_cgroupv1 changed but "
                 "requires a full stop/start to take effect" C_RESET "\n");
          reboot_cfg.force_cgroupv1 = old_force_cgv1;
        }
        *cfg = reboot_cfg;
        /* Restore mount point for img-based containers */
        if (cfg->is_img_mount && cfg->img_mount_point[0]) {
          safe_strncpy(cfg->rootfs_path, cfg->img_mount_point,
                       sizeof(cfg->rootfs_path));
        }
      }
    }

    cfg->reboot_cycle = 1;
    clock_gettime(CLOCK_BOOTTIME, &cfg->start_time);

    /* Mirror restart behavior: ensure X, VirGL, and PulseAudio servers are up
     * before next boot */
    if (is_android() && cfg->termux_x11) {
      if (ds_x11_daemon_start(cfg) == 0)
        wait_for_socket_or_death(
            cfg->x11_pid, TX11_SOCK_DIR "/" TX11_DISPLAY_SOCK, 5000, 50000);
    }

    if (is_android() && cfg->virgl) {
      if (ds_virgl_daemon_start(cfg) == 0)
        wait_for_socket_or_death(cfg->virgl_pid, TX11_VIRGL_SOCKET, 2000,
                                 20000);
    }

    if (is_android() && cfg->pulseaudio) {
      ds_pulse_daemon_start(cfg);
    }

    /* Refresh ns_inode: new container has a new PID namespace inode.
     * Without this, ds_virtualize_update's PID-recycling guard rejects
     * all writes after the first reboot cycle (stale inode != new pid ns). */
    cfg->ns_inode = ds_get_pid_ns_inode(cfg->container_pid);
    if (cfg->foreground)
      ds_log_silent = 1;

    /* ds_dhcp_server_start() memsets g_dhcp under g_dhcp_lock on the next
     * cycle, racing the still-running dhcp_server_loop thread that reads from
     * the same memory.  The DHCP thread is intentionally joinable so stop()
     * can join before memset. */
    ds_dhcp_server_stop();
    ds_socketd_record_core_event("restart", cfg->container_name, cfg->uuid);

    goto reboot_loop;
  }

  /* Not a reboot - check if external command is handling cleanup */
  if (is_external_lock_active(cfg->container_name)) {
    write_monitor_debug_log(cfg->container_name,
                            "External command lock detected - yielding "
                            "cleanup to CLI");
    goto monitor_cleanup_and_exit;
  }

  /* Normal exit - monitor does cleanup */
  write_monitor_debug_log(cfg->container_name, "Monitor performing cleanup");

  /* Before cleaning up the container's cgroup subtree, move the
   * monitor process itself back to the root cgroup.  The monitor wrote its
   * own PID into /sys/fs/cgroup/droidspaces/<name>/ at start (for cgroup
   * namespace isolation).  If it is still in that cgroup when
   * ds_cgroup_cleanup_container() calls rmdir, the kernel sees a non-empty
   * cgroup and returns EBUSY - the directory is never removed.
   *
   * Writing our PID to the root cgroup.procs atomically migrates us out.
   * This is safe: the monitor is about to _exit() anyway. */
  {
    int root_fd = open("/sys/fs/cgroup/cgroup.procs", O_WRONLY | O_CLOEXEC);
    if (root_fd >= 0) {
      char pid_s[32];
      int len = snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
      if (write(root_fd, pid_s, len) < 0) {
      }
      close(root_fd);
    }
  }

  cleanup_container_resources(cfg, 0, 0, 0);

monitor_cleanup_and_exit:
  /* Free dynamically allocated configuration members before exit */
  ds_config_free(cfg);
  _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
}
