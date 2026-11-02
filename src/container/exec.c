/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

/* Namespace Entry (shared for enter and run) */

int enter_namespace(pid_t pid, struct ds_config *cfg) {
  /* Verify process is still alive before trying to enter namespaces */
  if (kill(pid, 0) < 0) {
    ds_error("Container PID %d is no longer alive.", pid);
    return -1;
  }

  const char *ns_names[] = {"mnt", "uts", "ipc", "pid", "cgroup", "net"};
  int ns_fds[6];
  char path[PATH_MAX];

  /* 1. Open all namespace descriptors first (CRITICAL: before any setns) */
  for (int i = 0; i < 6; i++) {
    snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, ns_names[i]);
    ns_fds[i] = open(path, O_RDONLY | O_CLOEXEC);
    if (ns_fds[i] < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("Failed to open mount namespace at %s: %s", path,
                 strerror(errno));
        /* Cleanup previous fds */
        for (int j = 0; j < i; j++)
          close(ns_fds[j]);
        return -1;
      }
      if (errno != ENOENT && i != 5) {
        ds_warn("Optional namespace %s (%s) is missing: %s", ns_names[i], path,
                strerror(errno));
      }
    }
  }

  /* 2. Enter namespaces */
  for (int i = 0; i < 6; i++) {
    if (ns_fds[i] < 0)
      continue;

    /* Skip entering the 'net' namespace (index 5) if host networking is enabled
     */
    if (i == 5 && cfg && cfg->net_mode == DS_NET_HOST) {
      close(ns_fds[i]);
      continue;
    }

    if (setns(ns_fds[i], 0) < 0) {
      if (i == 0) { /* mnt is mandatory */
        ds_error("setns(mnt) failed: %s", strerror(errno));
        for (int j = i; j < 6; j++)
          if (ns_fds[j] >= 0)
            close(ns_fds[j]);
        return -1;
      }
      if (i != 5) {
        ds_warn("setns(%s) failed (ignored): %s", ns_names[i], strerror(errno));
      }
    }
    close(ns_fds[i]);
  }

  return 0;
}

/* Enter / Run */

/* Shell convention, the same one lxc-attach follows: 128 plus the signal when
 * the child was killed, so `run -- sh -c 'kill -9 $$'` reports 137, not 1. */
static int exit_status(int st) {
  if (WIFEXITED(st))
    return WEXITSTATUS(st);
  if (WIFSIGNALED(st))
    return 128 + WTERMSIG(st);
  return EXIT_FAILURE;
}

/* What a shell reports after a failed exec: 127 when the binary is missing,
 * 126 when it is there but cannot be run. Read errno before logging. */
static int exec_exit_code(void) { return errno == ENOENT ? 127 : 126; }

int enter_rootfs(struct ds_config *cfg, const char *user) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }

  /* Parse environment file while host paths are reachable */
  if (cfg->env_file[0] != '\0') {
    free_config_env_vars(cfg);
    int prev_silent = ds_log_silent;
    ds_log_silent = 1;
    parse_env_file_to_config(cfg->env_file, cfg);
    ds_log_silent = prev_silent;
  }

  /* PTY allocation is deferred until after entering the container namespaces.
   * This ensures the slave PTY is part of the container's private devpts
   * instance. */
  struct ds_tty_info tty;
  memset(&tty, 0, sizeof(tty));
  tty.master = tty.slave = -1;

  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
    free_config_env_vars(cfg);
    return -1;
  }

  pid_t child = fork();
  if (child < 0) {
    close(sv[0]);
    close(sv[1]);
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    close(sv[0]);

    /* cgroup attach before entering namespaces */
    ds_log_silent = 1;
    ds_cgroup_attach(cfg->container_name);
    ds_log_silent = 0;
    ds_virtualize_join(pid);
    android_native_personality();

    if (enter_namespace(pid, cfg) < 0)
      _exit(EXIT_FAILURE);

    /* Now that we are in the container's mount namespace, allocate a PTY.
     * This ensures its path (e.g. /dev/pts/0) exists and is resolvable. */
    if (ds_terminal_create(&tty) < 0)
      _exit(EXIT_FAILURE);

    /* Size the pty before the shell exists, as lxc-attach does on its ptx, so
     * nothing ever reads a 0x0 window. The caller's stdin is still ours here;
     * only the grandchild swaps it for the slave. */
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
      ioctl(tty.master, TIOCSWINSZ, &ws);

    /* Send master FD back to parent (host monitor) */
    if (ds_send_fd(sv[1], tty.master) < 0)
      _exit(EXIT_FAILURE);
    close(tty.master);
    tty.master = -1;

    /* Apply identical security hardening as internal_boot().
     * Seccomp filters and capability bounding set drops are per-process and
     * inherited only via fork/exec from PID 1 - entering processes arrive via
     * setns() and are NOT children of init, so they inherit nothing. */
    ds_log_silent = 1;
    /* Neutralize the KSU container-escape path BEFORE seccomp is applied:
     * the ioctl needs the [ksu_driver] fd that the magic reboot() installs,
     * and the seccomp filter below denies that very magic reboot. */
    ds_ksu_neutralize_root_escape();
    ds_seccomp_apply_minimal(cfg->privileged_mask, cfg->sandboxing_allowed);
    ds_seccomp_keyring_shim(cfg->privileged_mask);
    ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask,
                                  cfg->sandboxing_allowed);
    ds_log_silent = 0;

    /* The intermediate, not the shell, owns the session and the terminal.
     * util-linux login (Ubuntu 24.04+) calls vhangup(), which SIGHUPs the
     * session leader: with the shell as leader that killed bash and took the
     * whole session with it. Here the leader ignores SIGHUP and the shell is a
     * plain member, so login's hangup passes through and the prompt returns
     * after logout. lxc-attach does the opposite, its attached shell calls
     * setsid() itself, which is why login ends an lxc-attach session. */
    if (setsid() < 0)
      _exit(EXIT_FAILURE);
    if (ioctl(tty.slave, TIOCSCTTY, 0) < 0)
      _exit(EXIT_FAILURE);
    signal(SIGHUP, SIG_IGN);

    close(sv[1]);

    /* Must fork again to actually be in the new PID namespace */
    pid_t shell_pid = fork();
    if (shell_pid < 0)
      _exit(EXIT_FAILURE);
    if (shell_pid == 0) {
      /* Only the guest shell loses the protection. The session leader above
       * holds the tty, and an OOM kill of it would end the session. */
      ds_oom_unprotect();
      /* Not the session leader on purpose, see above: no setsid, no
       * TIOCSCTTY, the slave is already our controlling terminal. */
      if (ds_terminal_set_stdfds(tty.slave) < 0)
        _exit(EXIT_FAILURE);

      if (tty.slave > STDERR_FILENO)
        close(tty.slave);

      if (chdir("/") < 0)
        _exit(EXIT_FAILURE);

      /* Apply fixed and user-defined environment */
      ds_env_boot_setup(cfg);
      load_etc_environment();

      extern char **environ;

      /* Primary path: proper login via su -l <user>.
       * This gives the correct home directory, shell, and login environment
       * from the container's /etc/passwd.  user is always non-NULL here
       * (main.c defaults to "root" when no argument is given). */
      char *shell_argv[] = {"su", "-l", (char *)(uintptr_t)user, NULL};
      execve("/bin/su", shell_argv, environ);
      execve("/usr/bin/su", shell_argv, environ);
      execve("/run/wrappers/bin/su", shell_argv, environ);

      /* Fallback: su not available - look up the shell from /etc/passwd */
      char user_shell[PATH_MAX] = {0};
      if (get_user_shell(user, user_shell, sizeof(user_shell)) == 0) {
        if (access(user_shell, X_OK) == 0) {
          const char *sh_name = strrchr(user_shell, '/');
          sh_name = sh_name ? sh_name + 1 : user_shell;
          char *sh_argv[] = {(char *)(uintptr_t)sh_name, "-l", NULL};
          execve(user_shell, sh_argv, environ);
        }
      }

      /* Last resort: try shells in priority order */
      const char *shells[] = {"/bin/bash", "/bin/ash", "/bin/sh", NULL};
      for (int i = 0; shells[i]; i++) {
        if (access(shells[i], X_OK) == 0) {
          const char *sh_name = strrchr(shells[i], '/');
          sh_name = sh_name ? sh_name + 1 : shells[i];
          char *sh_argv[] = {(char *)(uintptr_t)sh_name, "-l", NULL};
          execve(shells[i], sh_argv, environ);
        }
      }

      ds_error("Failed to find any usable shell");
      _exit(127);
    }
    /* Keep tty.slave open until the shell exits: it holds the pts alive
     * across login's vhangup()/reopen window. */
    int st;
    waitpid(shell_pid, &st, 0);
    _exit(exit_status(st));
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
  int st;
  waitpid(child, &st, 0);
  free_config_env_vars(cfg);
  return exit_status(st);
}

/* Append arg to buf as a single-quoted shell word, space-separated from any
 * prior content unless first.  Embedded single quotes use the '\'' idiom.
 * Writes at most size-1 chars and always NUL-terminates; returns the new
 * offset.  Preserves argv word boundaries across su -c's shell. */
static size_t shell_quote_append(char *buf, size_t off, size_t size,
                                 const char *arg, int first) {
  if (!first && off < size - 1)
    buf[off++] = ' ';
  if (off < size - 1)
    buf[off++] = '\'';
  for (const char *p = arg; *p; p++) {
    if (*p == '\'') {
      const char *esc = "'\\''"; /* close quote, escaped quote, reopen quote */
      for (const char *e = esc; *e && off < size - 1; e++)
        buf[off++] = *e;
    } else if (off < size - 1) {
      buf[off++] = *p;
    }
  }
  if (off < size - 1)
    buf[off++] = '\'';
  buf[off] = '\0';
  return off;
}

int run_in_rootfs(struct ds_config *cfg, char **argv, const char *as_user) {
  pid_t pid = 0;
  if (!is_container_running(cfg, &pid) || pid <= 0) {
    ds_error("Container '%s' is not running or invalid.", cfg->container_name);
    return -1;
  }

  /* Removed verbose status log to allow raw output stream */

  /* Parse environment file while host paths are reachable */
  if (cfg->env_file[0] != '\0') {
    free_config_env_vars(cfg);
    int prev_silent = ds_log_silent;
    ds_log_silent = 1;
    parse_env_file_to_config(cfg->env_file, cfg);
    ds_log_silent = prev_silent;
  }

  /* The terminal delivers ^C and ^\ to the whole foreground group, the
   * command included. Only the command should die of them; the two processes
   * that relay its exit status stay, the same as lxc-attach (its issue #313).
   * A command that traps them itself cannot be interrupted from the terminal
   * any more, also as in lxc-attach. The grandchild restores the defaults. */
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);

  pid_t child = fork();
  if (child < 0) {
    free_config_env_vars(cfg);
    return -1;
  }

  if (child == 0) {
    /* Mirror enter_rootfs: attach to the container's cgroup subtree before
     * crossing into its namespaces, so the command is accounted to the
     * container instead of leaking to the host's cgroup root. */
    ds_log_silent = 1;
    ds_cgroup_attach(cfg->container_name);
    ds_log_silent = 0;
    ds_virtualize_join(pid);
    android_native_personality();

    if (enter_namespace(pid, cfg) < 0)
      _exit(EXIT_FAILURE);

    /* Apply identical security hardening as internal_boot() and enter_rootfs().
     * Same reasoning: run processes are not children of container PID 1. */
    ds_log_silent = 1;
    /* Neutralize the KSU container-escape path BEFORE seccomp is applied:
     * the ioctl needs the [ksu_driver] fd that the magic reboot() installs,
     * and the seccomp filter below denies that very magic reboot. */
    ds_ksu_neutralize_root_escape();
    ds_seccomp_apply_minimal(cfg->privileged_mask, cfg->sandboxing_allowed);
    ds_seccomp_keyring_shim(cfg->privileged_mask);
    ds_apply_capability_hardening(cfg->hw_access, cfg->privileged_mask,
                                  cfg->sandboxing_allowed);
    ds_log_silent = 0;

    pid_t cmd_pid = fork();
    if (cmd_pid < 0)
      _exit(EXIT_FAILURE);
    if (cmd_pid == 0) {
      /* The waiter above relays the exit status and stays protected */
      ds_oom_unprotect();
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      if (chdir("/") < 0)
        _exit(EXIT_FAILURE);

      /* Setup environment */
      ds_env_boot_setup(cfg);
      load_etc_environment();

      /* Append NixOS binary path so Nix-managed tools (e.g. ip, hostname)
       * are available without requiring the caller to prefix PATH manually. */
      {
        const char *cur_path = getenv("PATH");
        if (cur_path) {
          char nix_path[4096];
          snprintf(nix_path, sizeof(nix_path), "%s:/run/current-system/sw/bin",
                   cur_path);
          setenv("PATH", nix_path, 1);
        } else {
          setenv("PATH", "/run/current-system/sw/bin", 1);
        }
      }

      /* Run the command directly as an alien process (instant results) */
      if (as_user != NULL) {
        /* Build the command string for su -c.
         * If argv[0] has no spaces and argv[1] is NULL, pass it directly.
         * Otherwise join all args into a single shell string. */
        char cmd_buf[4096];
        if (argv[1] == NULL) {
          /* Single argument is treated as a shell command string as-is, so a
           * caller can pass e.g. `-- "ls -la | grep foo"`. */
          safe_strncpy(cmd_buf, argv[0], sizeof(cmd_buf));
        } else {
          /* Multiple args: shell-quote each so word boundaries (spaces, globs,
           * metacharacters) survive su's shell -- e.g. `-- rm "a b"` stays two
           * arguments instead of being resplit into three. */
          size_t off = 0;
          for (int k = 0; argv[k]; k++)
            off = shell_quote_append(cmd_buf, off, sizeof(cmd_buf), argv[k],
                                     k == 0);
        }
        char *su_argv[] = {"su", "-",     (char *)(uintptr_t)as_user,
                           "-c", cmd_buf, NULL};
        execvp("/bin/su", su_argv);
        execvp("/usr/bin/su", su_argv);
        execvp("/run/wrappers/bin/su", su_argv);
        int code = exec_exit_code();
        ds_error("Failed to exec su for user '%s': %s", as_user,
                 strerror(errno));
        _exit(code);
      }

      if (argv[1] == NULL && strchr(argv[0], ' ') != NULL) {
        char *shell_argv[] = {"/bin/sh", "-c", argv[0], NULL};
        execvp("/bin/sh", shell_argv);
      } else {
        execvp(argv[0], argv);
      }

      int code = exec_exit_code();
      ds_error("Failed to execute command: %s", strerror(errno));
      _exit(code);
    }

    int status;
    waitpid(cmd_pid, &status, 0);
    _exit(exit_status(status));
  }

  int status;
  waitpid(child, &status, 0);
  free_config_env_vars(cfg);
  return exit_status(status);
}
