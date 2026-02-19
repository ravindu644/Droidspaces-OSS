/*
 * Droidspaces v3 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DROIDSPACE_H
#define DROIDSPACE_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <pty.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------*/

#define DS_PROJECT_NAME "Droidspaces"
#define DS_VERSION "3.2.0"
#define DS_AUTHOR "ravindu644, Antigravity"
#define DS_REPO "https://github.com/ravindu644/Droidspaces-OSS"
#define DS_MAX_TTYS 6
#define DS_UUID_LEN 32
#define DS_MAX_CONTAINERS 1024
#define DS_STOP_TIMEOUT 8 /* seconds */
#define DS_PID_SCAN_RETRIES 20
#define DS_PID_SCAN_DELAY_US 200000 /* 200ms */

/* Workspace paths */
#define DS_WORKSPACE_ANDROID "/data/local/Droidspaces"
#define DS_WORKSPACE_LINUX "/var/lib/Droidspaces"
#define DS_PIDS_SUBDIR "Pids"
#define DS_IMG_MOUNT_ROOT_UNIVERSAL "/mnt/Droidspaces"
#define DS_MAX_MOUNT_TRIES 1024
#define DS_MAX_BINDS 16
#define DS_VOLATILE_SUBDIR "Volatile"

/* Device nodes to create in container /dev (when using tmpfs) */
#define DS_CONTAINER_MARKER "droidspaces"

/* Colors for output */
#define C_RESET "\033[0m"
#define C_RED "\033[1;31m"
#define C_GREEN "\033[1;32m"
#define C_YELLOW "\033[1;33m"
#define C_BLUE "\033[1;34m"
#define C_CYAN "\033[1;36m"
#define C_WHITE "\033[1;37m"
#define C_DIM "\033[2m"
#define C_BOLD "\033[1m"

/* ---------------------------------------------------------------------------
 * Logging macros
 * ---------------------------------------------------------------------------*/

#define ds_log(fmt, ...)                                                       \
  do {                                                                         \
    fprintf(stdout, "[" C_GREEN "+" C_RESET "] " fmt "\r\n", ##__VA_ARGS__);   \
    fflush(stdout);                                                            \
  } while (0)
#define ds_warn(fmt, ...)                                                      \
  do {                                                                         \
    fprintf(stderr, "[" C_YELLOW "!" C_RESET "] " fmt "\r\n", ##__VA_ARGS__);  \
    fflush(stderr);                                                            \
  } while (0)
#define ds_error(fmt, ...)                                                     \
  do {                                                                         \
    fprintf(stderr, "[" C_RED "-" C_RESET "] " fmt "\r\n", ##__VA_ARGS__);     \
    fflush(stderr);                                                            \
  } while (0)
#define ds_die(fmt, ...)                                                       \
  do {                                                                         \
    ds_error(fmt, ##__VA_ARGS__);                                              \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

/* ---------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------------*/

/* Bind mount entry */
struct ds_bind_mount {
  char src[PATH_MAX];
  char dest[PATH_MAX];
};

/* Terminal/TTY info — one per allocated PTY */
struct ds_tty_info {
  int master;          /* master fd (stays in parent/monitor) */
  int slave;           /* slave fd (bind-mounted into container) */
  char name[PATH_MAX]; /* slave device path (e.g. /dev/pts/3) */
};

/* Container configuration — replaces all global variables */
struct ds_config {
  /* Paths */
  char rootfs_path[PATH_MAX];     /* --rootfs=  */
  char rootfs_img_path[PATH_MAX]; /* --rootfs-img= */
  char pidfile[PATH_MAX];         /* --pidfile= or auto-resolved */
  char container_name[256];       /* --name= or auto-generated */
  char hostname[256];             /* --hostname= or container_name */

  /* UUID for PID discovery */
  char uuid[DS_UUID_LEN + 1];

  /* Flags */
  int foreground;         /* --foreground */
  int hw_access;          /* --hw-access */
  int volatile_mode;      /* --volatile */
  int enable_ipv6;        /* --enable-ipv6 */
  int android_storage;    /* --enable-android-storage */
  int selinux_permissive; /* --selinux-permissive */
  char prog_name[64];     /* argv[0] for logging */

  /* Runtime state */
  char volatile_dir[PATH_MAX];    /* temporary overlay dir */
  pid_t container_pid;            /* PID 1 of the container (host view) */
  pid_t intermediate_pid;         /* intermediate fork pid */
  int is_img_mount;               /* 1 if rootfs was loop-mounted from .img */
  char img_mount_point[PATH_MAX]; /* where the .img was mounted */

  /* Custom bind mounts */
  struct ds_bind_mount binds[DS_MAX_BINDS];
  int bind_count;

  /* Terminal (console + ttys) */
  struct ds_tty_info console;
  struct ds_tty_info ttys[DS_MAX_TTYS];
  int tty_count; /* how many TTYs are active */
};

/* ---------------------------------------------------------------------------
 * utils.c
 * ---------------------------------------------------------------------------*/

void safe_strncpy(char *dst, const char *src, size_t size);
int write_file(const char *path, const char *content);
int read_file(const char *path, char *buf, size_t size);
ssize_t write_all(int fd, const void *buf, size_t count);
int generate_uuid(char *buf, size_t size);
int mkdir_p(const char *path, mode_t mode);
int remove_recursive(const char *path);
int collect_pids(pid_t **pids_out, size_t *count_out);
int build_proc_root_path(pid_t pid, const char *suffix, char *buf, size_t size);
int grep_file(const char *path, const char *pattern);
int read_and_validate_pid(const char *pidfile, pid_t *pid_out);
int save_mount_path(const char *pidfile, const char *mount_path);
int read_mount_path(const char *pidfile, char *buf, size_t size);
int remove_mount_path(const char *pidfile);
void firmware_path_add_rootfs(const char *rootfs);
void firmware_path_remove_rootfs(const char *rootfs);
int run_command(char *const argv[]);
int run_command_quiet(char *const argv[]);
int ds_send_fd(int sock, int fd);
int ds_recv_fd(int sock);

/* ---------------------------------------------------------------------------
 * android.c
 * ---------------------------------------------------------------------------*/

int is_android(void);
void android_optimizations(int enable);
void android_set_selinux_permissive(void);
int android_get_selinux_status(void);
void android_remount_data_suid(void);
int android_fill_dns_from_props(char *dns1, char *dns2, size_t size);
void android_configure_iptables(void);
void android_setup_paranoid_network_groups(void);
int android_setup_storage(const char *rootfs_path);

/* ---------------------------------------------------------------------------
 * mount.c
 * ---------------------------------------------------------------------------*/

int domount(const char *src, const char *tgt, const char *fstype,
            unsigned long flags, const char *data);
int bind_mount(const char *src, const char *tgt);
int setup_dev(const char *rootfs, int hw_access);
int create_devices(const char *rootfs, int hw_access);
int setup_devpts(int hw_access);
int setup_cgroups(void);
int setup_volatile_overlay(struct ds_config *cfg);
int cleanup_volatile_overlay(struct ds_config *cfg);
int setup_custom_binds(struct ds_config *cfg, const char *rootfs);
int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size);
int unmount_rootfs_img(const char *mount_point);
int get_container_mount_fstype(pid_t pid, const char *path, char *fstype,
                               size_t size);
int detect_android_storage_in_container(pid_t pid);
int detect_hw_access_in_container(pid_t pid);

/* ---------------------------------------------------------------------------
 * network.c
 * ---------------------------------------------------------------------------*/

int fix_networking_host(struct ds_config *cfg);
int fix_networking_rootfs(struct ds_config *cfg);
int ds_get_dns_servers(char *dns1, char *dns2, size_t size);
int detect_ipv6_in_container(pid_t pid);

/* ---------------------------------------------------------------------------
 * terminal.c
 * ---------------------------------------------------------------------------*/

int ds_terminal_create(struct ds_tty_info *tty);
int ds_terminal_set_stdfds(int fd);
int ds_terminal_make_controlling(int fd);
int ds_setup_tios(int fd, struct termios *old);
void build_container_ttys_string(struct ds_tty_info *ttys, int count, char *buf,
                                 size_t size);
int ds_terminal_proxy(int master_fd);

/* ---------------------------------------------------------------------------
 * console.c
 * ---------------------------------------------------------------------------*/

int console_monitor_loop(int console_master_fd, pid_t intermediate_pid,
                         pid_t container_pid);

/* ---------------------------------------------------------------------------
 * pid.c
 * ---------------------------------------------------------------------------*/

const char *get_workspace_dir(void);
const char *get_pids_dir(void);
int ensure_workspace(void);
int generate_container_name(const char *rootfs_path, char *name, size_t size);
int find_available_name(const char *base_name, char *final_name, size_t size);
int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size);
int auto_resolve_pidfile(struct ds_config *cfg);
int count_running_containers(char *first_name, size_t size);
pid_t find_container_init_pid(const char *uuid);
int sync_pidfile(const char *src_pidfile, const char *name);
int show_containers(void);
int scan_containers(void);

/* ---------------------------------------------------------------------------
 * boot.c
 * ---------------------------------------------------------------------------*/

int internal_boot(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * environment.c
 * ---------------------------------------------------------------------------*/

void setup_container_env(void);
void load_etc_environment(void);
void ds_env_boot_setup(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * container.c
 * ---------------------------------------------------------------------------*/

int is_valid_container_pid(pid_t pid);
int check_status(struct ds_config *cfg, pid_t *pid_out);
int start_rootfs(struct ds_config *cfg);
int stop_rootfs(struct ds_config *cfg, int skip_unmount);
int enter_namespace(pid_t pid);
int enter_rootfs(struct ds_config *cfg, const char *user);
int run_in_rootfs(struct ds_config *cfg, int argc, char **argv);
int show_info(struct ds_config *cfg);
int restart_rootfs(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * documentation.c
 * ---------------------------------------------------------------------------*/

void print_documentation(const char *argv0);

/* ---------------------------------------------------------------------------
 * check.c
 * ---------------------------------------------------------------------------*/

int check_requirements(void);
int check_requirements_detailed(void);

#endif /* DROIDSPACE_H */
