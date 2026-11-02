/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONTAINER_CONTAINER_H
#define DS_CONTAINER_CONTAINER_H

/* cleanup.c */
void cleanup_container_resources(struct ds_config *cfg, pid_t pid,
                                 int skip_unmount, int force_cleanup);

/* command.c */
/* Requests on a monitor's command socket, answered from the config the
 * container booted with. The host asks these instead of reading anything
 * under the guest's /run, which root inside the container controls. */
/* reply: the init pid (u32 big-endian, 0 if none), then the config as its
 * file holds it, so both come from the same boot cycle */
#define DS_CMD_CONFIG ((uint8_t)'C')

/* Monitor side. serve_start starts the thread that answers on the
 * container's command socket; a forked child never keeps its listener.
 * publish sets what it answers with, the booted config and init pid of this
 * boot cycle; NULL withdraws it and frees the name. */
void ds_cmd_serve_start(const char *name);
void ds_cmd_publish(struct ds_config *cfg);

/* Client side. Both fail (-1, 0) when no monitor of ours answers, for example
 * one started by an older build: callers fall back to the host config. */
int ds_cmd_get_config(const char *name, struct ds_config *cfg, pid_t *pid);
pid_t ds_cmd_get_pid(const char *name);
int ds_cmd_for_each(void (*visit)(struct ds_config *booted, pid_t pid,
                                  void *arg),
                    void *arg);

/* exec.c */
int enter_namespace(pid_t pid, struct ds_config *cfg);
int enter_rootfs(struct ds_config *cfg, const char *user);
int run_in_rootfs(struct ds_config *cfg, char **argv, const char *as_user);

/* info.c */
int show_info(struct ds_config *cfg, int trust_cfg_pid);

/* inspect.c */
int is_systemd_rootfs(const char *path);

ds_init_type_t detect_container_init(const char *path);
int get_user_shell(const char *user, char *shell_buf, size_t size);

/* lock.c */
int ds_container_lock(const char *name, int wait);
void ds_container_unlock(int fd);
int ds_container_lock_orphan(const char *name);
void ds_container_claim_supervision(const char *name);
void ds_container_release_supervision(void);
void ds_container_prune_supervision(const char *name);
int ds_container_lock_helpers(const char *name);
void ds_container_wait_helpers(const char *name, int timeout_ms);

/* recovery.c */
int scan_containers(void);

/* registry.c */
int read_and_validate_pid(const char *pidfile, pid_t *pid_out);

int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size);
int is_container_running(struct ds_config *cfg, pid_t *pid_out);
int is_container_init(pid_t pid);
int count_running_containers(char *first_name, size_t size);
int check_selinux_permissive_needs(void);
int check_x11_needs(void);
int check_virgl_needs(void);
int check_pulse_needs(void);

/*
 * ds_feature_needs - generic feature-needs scanner.
 * Pass offsetof(struct ds_config, <field>) for the boolean flag to check.
 * Returns: 1=running, 0=installed-but-idle, -1=not-installed
 */
int ds_feature_needs(size_t cfg_flag_offset);

int next_running(DIR *d, struct ds_config *cfg, pid_t *pid);

int is_valid_container_pid(pid_t pid);

/* restart.c */
/* argc/argv: the process's original arguments, so restart can re-apply CLI
 * overrides after its post-stop config reload. */
int restart_rootfs(struct ds_config *cfg, int argc, char **argv);

/* start.c */
int start_rootfs(struct ds_config *cfg);

/* stop.c */
int stop_rootfs(struct ds_config *cfg, int skip_unmount);
int stop_rootfs_with_timeout(struct ds_config *cfg, int skip_unmount,
                             int timeout_seconds);

#endif /* DS_CONTAINER_CONTAINER_H */
