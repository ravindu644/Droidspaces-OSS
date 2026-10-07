/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ds_console_drain - Empty the container's console PTY master.
 *
 * In BACKGROUND mode the monitor is the only process holding the console PTY
 * master, and nothing reads it (foreground mode drives it from the parent via
 * console_monitor_loop). If it is never read, a chatty container init that
 * writes past the ~64 KiB PTY buffer blocks forever in n_tty_write and the
 * whole container wedges. So the monitor must keep this master drained for the
 * container's entire lifetime.
 *
 * Drained bytes are appended to the per-container console log when one is open
 * (log_fd >= 0), which also makes background boot/console output visible for
 * debugging. Draining continues even if the log write fails: keeping the PTY
 * buffer empty is the load-bearing part; logging is best-effort.
 *
 * master_fd must be O_NONBLOCK so read() returns EAGAIN once drained. */
static void ds_console_drain(int master_fd, int log_fd, size_t *logged) {
  char buf[4096];
  ssize_t n;

  while ((n = read(master_fd, buf, sizeof(buf))) > 0) {
    if (log_fd < 0)
      continue;

    /* Bound the console log: truncate at 2 MiB. The fd was opened O_APPEND,
     * so subsequent writes still land correctly after the truncation. */
    if (logged && *logged >= 2u * 1024 * 1024) {
      if (ftruncate(log_fd, 0) == 0)
        *logged = 0;
    }

    size_t off = 0;
    while (off < (size_t)n) {
      ssize_t w = write(log_fd, buf + off, (size_t)n - off);
      if (w <= 0)
        break;
      off += (size_t)w;
    }
    if (logged)
      *logged += off;
  }
}

/* ds_monitor_run - Supervisor process for a single container instance.
 *
 * Called immediately after fork() in start_rootfs(). Never returns - always
 * ends with _exit(). sync_pipe_write is the write-end of the parent sync
 * pipe; the monitor (or its intermediate child) writes the container init PID
 * through it on the first boot cycle, then closes it. */
/* Close a descriptor the monitor keeps a number for, and forget the number.
 * The handshake pipes live in cfg across boot cycles. Closing one without
 * resetting it leaves a stale number that the next cycle closes again, and by
 * then that number belongs to whatever was opened in between. */
static void close_and_forget(int *fd) {
  if (*fd >= 0)
    close(*fd);
  *fd = -1;
}

void ds_monitor_run(struct ds_config *cfg, int sync_pipe_write) {
  int sync_pipe[2];
  sync_pipe[0] = -1;
  sync_pipe[1] = sync_pipe_write;

  if (setsid() < 0 && errno != EPERM) {
    /* Fatal only if it's not EPERM (which means already leader) */
    ds_error("setsid failed: %s", strerror(errno));
    _exit(EXIT_FAILURE);
  }

  /* Held until we exit. It tells the pruners in pid.c that this container's
   * leftovers still have an owner, so they leave its pidfile alone. */
  ds_container_claim_supervision(cfg->container_name);

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

  /* The monitor takes new UTS and IPC namespaces now. The PID namespace is
   * NOT unshared here because unshare(CLONE_NEWPID) can only be called once
   * per process: each boot/reboot cycle forks an intermediate that creates a
   * fresh one. The cgroup namespace belongs to that intermediate too, the
   * monitor never enters the container's cgroups. */
  int ns_flags = CLONE_NEWUTS | CLONE_NEWIPC;

  if (unshare(ns_flags) < 0)
    ds_die("unshare failed: %s", strerror(errno));

  int stdio_redirected = 0;

  /* Background console-drain setup (see ds_console_drain).
   * Runs once, before the reboot loop, so a single log fd survives internal
   * reboot cycles. In background mode the monitor is the only reader of the
   * console PTY master: make it non-blocking and open a per-container console
   * log so the heartbeat loop below keeps the PTY buffer drained. Foreground
   * mode leaves the master untouched - the parent's console_monitor_loop
   * drives it. */
  int console_log_fd = -1;
  size_t console_logged = 0;
  if (!cfg->foreground && cfg->console.master >= 0) {
    int fl = fcntl(cfg->console.master, F_GETFL, 0);
    if (fl >= 0)
      (void)fcntl(cfg->console.master, F_SETFL, fl | O_NONBLOCK);

    char safe_name[256];
    char log_dir[PATH_MAX];
    char log_path[PATH_MAX];
    sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));
    snprintf(log_dir, sizeof(log_dir), "%.2048s/" DS_LOGS_SUBDIR "/%.256s",
             get_workspace_dir(), safe_name);
    mkdir_p(log_dir, 0755);
    snprintf(log_path, sizeof(log_path), "%.4080s/console", log_dir);
    rotate_log(log_path, 2 * 1024 * 1024);
    console_log_fd =
        open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  }

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
  /* The lifecycle lock, held while we act on our container's exit: through
   * an internal reboot until the new init is visible, or through cleanup. */
  int lifecycle_fd = -1;

reboot_loop:;
  /* Close whatever is left of the previous cycle's pipes. The handshake
   * closes each end as it finishes with it, so normally nothing is. */
  close_and_forget(&cfg->net_ready_pipe[0]);
  close_and_forget(&cfg->net_ready_pipe[1]);
  close_and_forget(&cfg->net_done_pipe[0]);
  close_and_forget(&cfg->net_done_pipe[1]);

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

  /* This boot's cgroups, built from nothing, with the limits the config
   * holds now. Done here and not once up front so that a restart, an
   * internal reboot and a start after a crash all get the same clean slate. */
  ds_cgroup_setup(cfg);

  pid_t mid_pid = fork();
  if (mid_pid < 0)
    _exit(EXIT_FAILURE);

  if (mid_pid == 0) {
    /* INTERMEDIATE PROCESS
     * Join the container's cgroups, then create a fresh PID namespace (and
     * NET namespace for NAT/none modes) for this boot cycle.
     *
     * The cgroup namespace (Linux 4.6+) is unshared here, after the join: its
     * root is wherever we sit at that moment, and from the root cgroup it
     * would isolate nothing. */
    ds_cgroup_join(cfg->container_name);

    int clone_flags = CLONE_NEWPID;
    if (cfg->net_mode != DS_NET_HOST)
      clone_flags |= CLONE_NEWNET;
    if (access("/proc/self/ns/cgroup", F_OK) == 0)
      clone_flags |= CLONE_NEWCGROUP;

    if (unshare(clone_flags) < 0) {
      ds_error("unshare(PID|NET|CGROUP) failed: %s", strerror(errno));
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
      /* Init keeps the sync pipe's write end, and it is close-on-exec. The
       * command that started us reads end of file on it when init execs, which
       * is how it knows the boot is over. Do not close it here. */
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
      close_and_forget(&cfg->net_ready_pipe[1]); /* monitor reads */
      close_and_forget(&cfg->net_done_pipe[0]);  /* monitor writes */

      char rdy;
      if (read(cfg->net_ready_pipe[0], &rdy, 1) < 0) {
        ds_warn("[NET] Monitor: failed to read READY signal: %s",
                strerror(errno));
      } else {
        ds_log("[NET] Monitor: READY received from init (pid=%d)",
               (int)netns_pid);
      }
      close_and_forget(&cfg->net_ready_pipe[0]);

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

      /* Gateway self-heal: if any running client delegates to THIS container as
       * its gateway, (re)plug their LAN cable into our (possibly just-rebooted)
       * netns now.  Runs on every boot cycle - a cheap no-op when nobody routes
       * through us - so a gateway reboot re-wires its clients with no client
       * restart.
       *
       * This MUST run BEFORE the DONE handshake below.  The DONE write is what
       * unblocks init to proceed into pivot_root + exec of the real init
       * (procd/netifd).  A client's own eth0 is likewise wired before its DONE
       * (setup_gateway_veth_side above); plugging the gateway's LAN cable(s)
       * (eth1 ...) after DONE would hot-plug them into a gateway whose netifd
       * is already booting, racing its LAN bring-up.  Wire first, then unblock,
       * so the gateway execs its init with every client cable already present.
       */
      ds_net_rewire_gateway_clients(cfg->container_name, netns_pid);

      /* Send handshake to init */
      struct ds_net_handshake hs;
      ds_net_derive_handshake(netns_pid, cfg, &hs);
      if (cfg->net_mode == DS_NET_GATEWAY)
        ds_log("[NET] Monitor: sending DONE (gateway mode: eth0 is wired "
               "host-side, IP comes from the gateway's DHCP)");
      else
        ds_log("[NET] Monitor: sending DONE: peer=%s ip=%s", hs.peer_name,
               hs.ip_str);
      if (write(cfg->net_done_pipe[1], &hs, sizeof(hs)) != (ssize_t)sizeof(hs))
        ds_warn("[NET] Monitor: failed to write handshake to init");
      close_and_forget(&cfg->net_done_pipe[1]);
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

  /* An internal reboot kept the lifecycle lock across the gap where the old
   * init is dead and the new one is not yet visible as running. Without it a
   * command would read that gap as "not running" and boot a second instance.
   * Let go once the pidfile validates, or the boot has clearly failed. */
  if (lifecycle_fd >= 0) {
    for (int i = 0; i < 50; i++) {
      pid_t booted = 0;
      siginfo_t gone;
      memset(&gone, 0, sizeof(gone));
      if (read_and_validate_pid(cfg->pidfile, &booted) == 0)
        break;
      /* WNOWAIT: only look, the wait loop below does the reaping. */
      if (waitid(P_PID, (id_t)mid_pid, &gone, WEXITED | WNOHANG | WNOWAIT) ==
              0 &&
          gone.si_pid == mid_pid)
        break;
      usleep(100000); /* 100ms */
    }
    ds_container_unlock(lifecycle_fd);
    lifecycle_fd = -1;
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
    int gw_wired = 0, gw_tick = 0;
    struct timespec last_tick = {0, 0};

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

      /* The poll below returns at once whenever the console has output, so a
       * chatty guest (a shutdown prints hundreds of lines) would run the
       * refresh hundreds of times a second. It is meant once per heartbeat. */
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      long since_ms = (now.tv_sec - last_tick.tv_sec) * 1000 +
                      (now.tv_nsec - last_tick.tv_nsec) / 1000000;
      if (since_ms >= 500) {
        last_tick = now;
        ds_virtualize_update(cfg);

        /* A gateway client whose gateway was not up yet is still unwired.
         * Look again every couple of seconds until the cable is in. */
        if (cfg->net_mode == DS_NET_GATEWAY && !gw_wired && ++gw_tick % 4 == 0)
          gw_wired = ds_net_gateway_reconcile(cfg, cfg->container_pid);
      }

      /* Poll the signalfd and, in background mode, the console PTY master.
       * poll() wakes immediately when the master becomes readable, so draining
       * is effectively real-time - a container init that fills the PTY buffer
       * is unblocked at once instead of wedging forever in n_tty_write. */
      struct pollfd pfds[2];
      int npfd = 0;
      int sfd_slot = -1;
      int con_slot = -1;
      if (sfd >= 0) {
        pfds[npfd].fd = sfd;
        pfds[npfd].events = POLLIN;
        sfd_slot = npfd++;
      }
      if (!cfg->foreground && cfg->console.master >= 0) {
        pfds[npfd].fd = cfg->console.master;
        pfds[npfd].events = POLLIN;
        con_slot = npfd++;
      }
      if (npfd > 0) {
        poll(pfds, (nfds_t)npfd, 500);
        if (sfd_slot >= 0 && (pfds[sfd_slot].revents & POLLIN)) {
          struct signalfd_siginfo si;
          while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si))
            ; /* drain */
        }
        if (con_slot >= 0 &&
            (pfds[con_slot].revents & (POLLIN | POLLHUP | POLLERR)))
          ds_console_drain(cfg->console.master, console_log_fd,
                           &console_logged);
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

  /* Stop our helper threads before anything else. They serve a container that
   * no longer exists, and the route monitor in particular must not outlive
   * it: while we wait for the lock below, a stop or restart removes the
   * shared host rules, and a still-running route monitor would see them
   * missing and put them back from its own stale snapshot (old port forwards
   * included). A reboot cycle starts both again in setup_veth_host_side().
   * Joining the DHCP thread here also keeps the next cycle's
   * ds_dhcp_server_start() from resetting state under a live thread. */
  ds_net_stop_route_monitor();
  ds_dhcp_server_stop();

  /* Our container is gone. Take the lifecycle lock, waiting for any command
   * that is working on this container to finish, and only then look at what
   * is on disk. The pidfile, mount point and cgroup are keyed by name, so
   * acting on a guess here would tear down somebody else's instance: that is
   * how a restarted container used to lose its early services.
   *
   *   pidfile names a live container -> a successor owns the name (restart)
   *   pidfile is gone                -> a command already tore us down
   *   pidfile names a dead PID       -> nobody has claimed this exit: it is
   *                                     ours to reboot or to clean up
   *
   * If the lock cannot be taken at all, carry on with the same checks: they
   * are still right, just no longer race free. */
  lifecycle_fd = ds_container_lock(cfg->container_name, 1);
  {
    pid_t owner = 0;
    if (read_and_validate_pid(cfg->pidfile, &owner) == 0) {
      write_monitor_debug_log(cfg->container_name,
                              "Successor instance (PID %d) owns this "
                              "container now - nothing to do",
                              (int)owner);
      goto monitor_cleanup_and_exit;
    }
    if (access(cfg->pidfile, F_OK) != 0) {
      write_monitor_debug_log(cfg->container_name,
                              "Already torn down by a command");
      goto monitor_cleanup_and_exit;
    }
  }

  /* Reboot detection (internal reboot) */
  if (WIFEXITED(status) && WEXITSTATUS(status) == DS_REBOOT_EXIT) {
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
    if (is_android() && cfg->x11) {
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

    ds_socketd_record_core_event("restart", cfg->container_name, cfg->uuid);

    goto reboot_loop;
  }

  /* Normal exit - monitor does cleanup */
  write_monitor_debug_log(cfg->container_name, "Monitor performing cleanup");

  cleanup_container_resources(cfg, 0, 0, 0);

monitor_cleanup_and_exit:
  ds_container_unlock(lifecycle_fd);
  /* Capture any final console output, then close the background console log. */
  if (!cfg->foreground && cfg->console.master >= 0)
    ds_console_drain(cfg->console.master, console_log_fd, &console_logged);
  if (console_log_fd >= 0)
    close(console_log_fd);

  /* Free dynamically allocated configuration members before exit */
  ds_config_free(cfg);
  _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
}
