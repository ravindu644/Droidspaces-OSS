/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DROIDSPACE_H
#define DROIDSPACE_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
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

#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC 0x858458f6
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

/* Cgroup Namespace support (Linux 4.6+) */
#ifndef CLONE_NEWCGROUP
#define CLONE_NEWCGROUP 0x02000000
#endif

/* ---------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------*/

#define DS_PROJECT_NAME "Droidspaces"
#define DS_VERSION "6.0.0"
#define DS_MIN_KERNEL_MAJOR 3
#define DS_MIN_KERNEL_MINOR 10
#define DS_RECOMMENDED_KERNEL_MAJOR 4
#define DS_RECOMMENDED_KERNEL_MINOR 14
#define DS_AUTHOR "ravindu644"
#define DS_REPO "https://github.com/ravindu644/Droidspaces-OSS"
#define DS_MAX_TTYS 6
#define DS_UUID_LEN 32
#define DS_MAX_CONTAINERS 1024
#define DS_STOP_TIMEOUT 15 /* seconds */
#define DS_PID_SCAN_RETRIES 20
#define DS_PID_SCAN_DELAY_US 200000 /* 200ms */
#define DS_RETRY_DELAY_US 200000    /* 200ms */
#define DS_REBOOT_EXIT 249          /* exit code: in-container reboot */

/* Workspace paths */
#define DS_WORKSPACE_ANDROID "/data/local/Droidspaces"
#define DS_WORKSPACE_LINUX "/var/lib/Droidspaces"
#define DS_PIDS_SUBDIR "Pids"
#define DS_IMG_MOUNT_ROOT_UNIVERSAL "/mnt/Droidspaces"
#define DS_MAX_MOUNT_TRIES 1024
#define DS_BIND_INITIAL_CAP 4
#define DS_VOLATILE_SUBDIR "Volatile"
#define DS_LOGS_SUBDIR "Logs"
#define DS_NET_SUBDIR "Net"
#define DS_ANDROID_TMPFS_CONTEXT "u:object_r:tmpfs:s0"
#define DS_ANDROID_VOLD_CONTEXT "u:object_r:vold_data_file:s0"
#define DS_MAX_GPU_GROUPS 32

/* Device nodes to create in container /dev (when using tmpfs) */
#define DS_CONTAINER_MARKER "droidspaces"

/* Default DNS servers */
#define DS_DNS_DEFAULT_1 "1.1.1.1"
#define DS_DNS_DEFAULT_2 "8.8.8.8"

/* Common Paths & Patterns */
#define DS_PROC_ROOT_FMT "/proc/%d/root"
#define DS_PROC_CMDLINE_FMT "/proc/%d/cmdline"
#define DS_PROC_STATUS_FMT "/proc/%d/status"
#define DS_PROC_MOUNTINFO "/proc/self/mountinfo"
#define DS_OS_RELEASE "/etc/os-release"
#define DS_FW_PATH_FILE "/sys/module/firmware_class/parameters/path"
#define DS_SYSTEMD_CONTAINER_MARKER "/run/systemd/container"
#define DS_DROIDSPACES_MARKER "/run/droidspaces"

/* Hardening constants */
#define DS_DEFAULT_TTY_GID 5
#define DS_DEFAULT_SUBNET "172.28.0.0/16"
#define DS_MAX_TRACKED_ENTRIES 512

/* X11 Socket Paths (Host-side relative to /.old_root or absolute) */
#define DS_X11_PATH_DESKTOP "/.old_root/tmp/.X11-unix"
#define DS_TERMUX_TMP_DIR "/data/data/com.termux/files/usr/tmp"
#define DS_TERMUX_TMP_OLDROOT "/.old_root/data/data/com.termux/files/usr/tmp"
#define DS_X11_CONTAINER_DIR "/tmp/.X11-unix"

/* File Extensions */
#define DS_EXT_PID ".pid"
#define DS_EXT_MOUNT ".mount"
#define DS_EXT_LOCK ".lock"

/* Signals */
#define DS_SIG_STOP (SIGRTMIN + 3)

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
 * Logging macros & Centralized Engine
 * ---------------------------------------------------------------------------*/

extern int ds_log_silent;
extern char ds_log_container_name[256];

void ds_log_internal(const char *prefix, const char *color, int is_err,
                     const char *fmt, ...);
void ds_die_internal(const char *fmt, ...);
void rotate_log(const char *path, size_t max_size);
int check_ns(int flag, const char *name);

#define ds_log(fmt, ...) ds_log_internal("+", C_GREEN, 0, fmt, ##__VA_ARGS__)
#define ds_warn(fmt, ...) ds_log_internal("!", C_YELLOW, 1, fmt, ##__VA_ARGS__)
#define ds_error(fmt, ...) ds_log_internal("-", C_RED, 1, fmt, ##__VA_ARGS__)
#define ds_die(fmt, ...) ds_die_internal(fmt, ##__VA_ARGS__)

/* ---------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------------*/

/* Networking modes */

enum ds_net_mode {
  DS_NET_HOST = 0, /* share host network namespace (default) */
  DS_NET_NAT,      /* isolated netns + bridge + MASQUERADE      */
  DS_NET_NONE,     /* isolated netns with loopback only          */
};

/* Opaque RTNETLINK context - defined in ds_netlink.c */
typedef struct ds_nl_ctx ds_nl_ctx_t;

/* Handshake payload: Monitor → init via net_done_pipe */
struct ds_net_handshake {
  char peer_name[16]; /* e.g. "ds-p12345"        */
  char ip_str[32];    /* e.g. "172.28.4.47/16"  */
};

/* NAT networking constants */

#ifndef DS_NAT_BRIDGE
#define DS_NAT_BRIDGE "ds-br0"
#endif
#ifndef DS_NAT_GW_IP
#define DS_NAT_GW_IP "172.28.0.1"
#endif
#ifndef DS_NAT_PREFIX
#define DS_NAT_PREFIX 16
#endif

/* Android ip rule priorities for DS subnet routing.
 *
 * Must be < 10000 so they are evaluated BEFORE Android's VPN rule range
 * (RULE_PRIORITY_VPN_OVERRIDE_SYSTEM = 10000) - this ensures container
 * traffic bypasses any active VPN and that return traffic for the container
 * subnet always resolves via the main table even when a VPN is up.
 *
 * Must be > 1000 to avoid colliding with OEM/carrier reserved rules that
 * some vendors install below 1000.  The previous values (90, 100) were
 * dangerously close to 0 and triggered route re-evaluation races on
 * certain OEM kernels.
 *
 * DS_RULE_PRIO_TO_SUBNET  - "to 172.28.0.0/16 lookup main"
 *   Guarantees reply traffic to the container always resolves correctly.
 *
 * DS_RULE_PRIO_FROM_SUBNET - "from 172.28.0.0/16 lookup <gw_table>"
 *   Routes container-originated traffic through the active upstream table.
 */
#ifndef DS_RULE_PRIO_TO_SUBNET
#define DS_RULE_PRIO_TO_SUBNET 6090
#endif
#ifndef DS_RULE_PRIO_FROM_SUBNET
#define DS_RULE_PRIO_FROM_SUBNET 6100
#endif

/* Bind mount entry */
struct ds_bind_mount {
  char src[PATH_MAX];
  char dest[PATH_MAX];
};

struct ds_env_var {
  char *key;
  char *value;
};

struct ds_config_line {
  char line[2048];
  struct ds_config_line *next;
};

/* Terminal/TTY info - one per allocated PTY */

struct ds_tty_info {
  int master;          /* master fd (stays in parent/monitor) */
  int slave;           /* slave fd (bind-mounted into container) */
  char name[PATH_MAX]; /* slave device path (e.g. /dev/pts/3) */
};

/* Container configuration - replaces all global variables */
/* ---------------------------------------------------------------------------
 * Port forwarding (--port HOST:CONTAINER[/proto])
 * ---------------------------------------------------------------------------*/

#define DS_MAX_PORT_FORWARDS 32
#define DS_MAX_UPSTREAM_IFACES 32

struct ds_port_forward {
  uint16_t host_port;          /* port on the Android/Linux host  */
  uint16_t host_port_end;      /* end of range (0 if single) */
  uint16_t container_port;     /* port inside the container       */
  uint16_t container_port_end; /* end of range (0 if single) */
  char proto[4];               /* "tcp" or "udp"                  */
};

/* ---------------------------------------------------------------------------
 * Privileged Mode Flags
 * ---------------------------------------------------------------------------*/
#define DS_PRIV_NOMASK (1 << 0) /* No jail masks (/proc, /sys) */
#define DS_PRIV_NOCAPS (1 << 1) /* No capability drops */
#define DS_PRIV_NOSEC (1 << 2)  /* Minimal seccomp only */
#define DS_PRIV_SHARED (1 << 3) /* MS_SHARED root propagation */
#define DS_PRIV_UNFILTERED                                                     \
  (1 << 4)                  /* No device node blocking (except PTYs)           \
                             */
#define DS_PRIV_FULL (0xFF) /* All above */

struct ds_config {
  /* Paths */
  char rootfs_path[PATH_MAX];     /* --rootfs=  */
  char rootfs_img_path[PATH_MAX]; /* --rootfs-img= */
  char pidfile[PATH_MAX];         /* --pidfile= or auto-resolved */
  char container_name[256];       /* --name= or auto-generated */
  char hostname[256];             /* --hostname= or container_name */
  char dns_servers[1024];         /* --dns= (comma/space separated) */
  enum ds_net_mode net_mode;      /* --net=host|nat|none */
  char dns_server_content[1024];  /* In-memory DNS config for boot */

  /* UUID for PID discovery */
  char uuid[DS_UUID_LEN + 1];

  /* Flags */
  int foreground;         /* --foreground */
  int hw_access;          /* --hw-access */
  int gpu_mode;           /* --gpu: mirror GPU nodes into isolated tmpfs /dev */
  int termux_x11;         /* --termux-x11 (Android only) */
  int volatile_mode;      /* --volatile */
  int disable_ipv6;       /* --disable-ipv6 */
  int android_storage;    /* --enable-android-storage */
  int selinux_permissive; /* --selinux-permissive */
  int net_bridgeless;     /* Probe result: no CONFIG_BRIDGE, use PTP NAT */
  int reboot_cycle;       /* 1 if we are in a reboot loop */
  int force_cgroupv1;     /* --force-cgroupv1: use v1 even if v2 is available */
  int block_nested_ns;    /* --block-nested-namespaces: fix VFS deadlock by
                               blocking nested namespace creation */
  int privileged_mask;    /* --privileged bitmask */
  int format_output;      /* --format: machine-parseable output (KEY=VALUE) */
  char prog_name[64];     /* argv[0] for logging */

  /* Runtime state */
  char volatile_dir[PATH_MAX];    /* temporary overlay dir */
  pid_t container_pid;            /* PID 1 of the container (host view) */
  pid_t intermediate_pid;         /* intermediate fork pid */
  int is_img_mount;               /* 1 if rootfs was loop-mounted from .img */
  char img_mount_point[PATH_MAX]; /* where the .img was mounted */

  /* NAT networking synchronization pipes
   * Both pairs are initialised to {-1,-1} in main() after memset.
   * Pipes are only created in container.c when net_mode != DS_NET_HOST. */
  int net_ready_pipe[2]; /* child → monitor: "I am in my new netns"  */
  int net_done_pipe[2];  /* monitor → child: "veth peer is in place" */

  /* Custom bind mounts (dynamically allocated) */
  struct ds_bind_mount *binds;
  int bind_count;
  int bind_capacity;

  /* Configuration persistence */
  char config_file[PATH_MAX];
  int config_file_specified;
  int config_file_existed;

  /* Terminal (console + ttys) */
  struct ds_tty_info console;
  struct ds_tty_info ttys[DS_MAX_TTYS];
  int tty_count; /* how many TTYs are active */

  /* Environment variables (dynamically allocated) */
  char env_file[PATH_MAX];
  struct ds_env_var *env_vars;
  int env_var_count;
  int env_var_capacity;

  /* Unknown config lines (preserved from Android metadata) */
  struct ds_config_line *unknown_head;
  struct ds_config_line *unknown_tail;

  /* Port forwarding (--port HOST:CONTAINER[/proto]) */
  struct ds_port_forward port_forwards[DS_MAX_PORT_FORWARDS];
  int port_forward_count;
  char nat_container_ip[INET_ADDRSTRLEN]; /* assigned container IP, for cleanup
                                           */

  /* Static NAT IP (--nat-ip, or auto-assigned on first boot and persisted).
   * Once set in container.config, this IP is reused on every subsequent boot
   * instead of re-deriving a PID-hash IP.  Plain dotted-decimal, no CIDR. */
  char static_nat_ip[INET_ADDRSTRLEN];

  /* Upstream interfaces for NAT routing (--upstream wlan0,rmnet0,...) */
  char upstream_ifaces[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];
  int upstream_iface_count;
};

/* ---------------------------------------------------------------------------
 * utils.c
 * ---------------------------------------------------------------------------*/

void safe_strncpy(char *dst, const char *src, size_t size);
char *ds_resolve_path_arg(const char *path);
void ds_resolve_argv_paths(int argc, char **argv);
long ds_get_container_uptime(pid_t pid);
void ds_format_uptime(long uptime_sec, char *buf, size_t size);
int is_ramfs(const char *path);
int is_subpath(const char *parent, const char *child);
int is_running_in_termux(void);
int write_file(const char *path, const char *content);
int read_file(const char *path, char *buf, size_t size);
int write_file_atomic(const char *path, const char *content);
ssize_t write_all(int fd, const void *buf, size_t count);
int generate_uuid(char *buf, size_t size);
int get_kernel_version(int *major, int *minor);
int mkdir_p(const char *path, mode_t mode);
int remove_recursive(const char *path);
int collect_pids(pid_t **pids_out, size_t *count_out);
int build_proc_root_path(pid_t pid, const char *suffix, char *buf, size_t size);
int parse_os_release(const char *rootfs_path, char *id_out, char *ver_out,
                     size_t out_size);
int grep_file(const char *path, const char *pattern);
int read_and_validate_pid(const char *pidfile, pid_t *pid_out);
int save_mount_path(const char *pidfile, const char *mount_path);
int read_mount_path(const char *pidfile, char *buf, size_t size);
int remove_mount_path(const char *pidfile);
void firmware_path_add(const char *fw_path);
void firmware_path_remove(const char *fw_path);
int run_command(char *const argv[]);
int run_command_quiet(char *const argv[]);
int run_command_log(char *const argv[]);
int ds_get_selinux_status(void);
void ds_set_selinux_permissive(void);
int get_selinux_context(const char *path, char *buf, size_t size);
int set_selinux_context(const char *path, const char *context);
int ds_send_fd(int sock, int fd);
int ds_recv_fd(int sock);
void print_ds_banner(void);
void print_privileged_warning(int privileged_mask);
int is_systemd_rootfs(const char *path);
int get_user_shell(const char *user, char *shell_buf, size_t size);
void check_kernel_recommendation(void);
void write_monitor_debug_log(const char *name, const char *fmt, ...);
int copy_file(const char *src, const char *dst);
void sort_bind_mounts(struct ds_config *cfg);
void sanitize_container_name(const char *name, char *out, size_t size);
int validate_container_name(const char *name);
int reject_container_name(const char *name);
int validate_bind_destination(const char *dest);

/* ---------------------------------------------------------------------------
 * config.c
 * ---------------------------------------------------------------------------*/

int ds_config_load(const char *config_path, struct ds_config *cfg);
int ds_config_load_by_name(const char *name, struct ds_config *cfg);
int ds_config_save(const char *config_path, struct ds_config *cfg);
int ds_config_save_by_name(const char *name, struct ds_config *cfg);
int ds_config_validate(struct ds_config *cfg);
int ds_config_add_bind(struct ds_config *cfg, const char *src,
                       const char *dest);
void free_config_binds(struct ds_config *cfg);
void free_config_env_vars(struct ds_config *cfg);
void free_config_unknown_lines(struct ds_config *cfg);
char *ds_config_auto_path(const char *rootfs_path);
void apply_reset_config(struct ds_config *cfg, int cli_net_mode_set,
                        enum ds_net_mode cli_net_mode);
void parse_privileged(const char *value, struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * android.c
 * ---------------------------------------------------------------------------*/

int is_android(void);
void android_optimizations(int enable);
void ds_set_selinux_permissive(void);
int ds_get_selinux_status(void);
void android_remount_data_suid(void);
int android_setup_storage(const char *rootfs_path);
int android_seccomp_setup(int is_systemd, int block_nested_ns);
int ds_seccomp_apply_minimal(int hw_access, int privileged_mask);

/* ---------------------------------------------------------------------------
 * mount.c
 * ---------------------------------------------------------------------------*/

int domount(const char *src, const char *tgt, const char *fstype,
            unsigned long flags, const char *data);
int domount_silent(const char *src, const char *tgt, const char *fstype,
                   unsigned long flags, const char *data);
int bind_mount(const char *src, const char *tgt);
int ds_apply_jail_mask(int hw_access, int privileged_mask);
int setup_dev(const char *rootfs, int hw_access, int gpu_mode,
              int privileged_mask);
int create_devices(const char *rootfs, int hw_access, int privileged_mask);
int setup_devpts(int hw_access);
int ds_fix_host_ptys(void);
int setup_volatile_overlay(struct ds_config *cfg);
int cleanup_volatile_overlay(struct ds_config *cfg);
int check_volatile_mode(struct ds_config *cfg);
int setup_custom_binds(struct ds_config *cfg, const char *rootfs);
int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size,
                     const char *name);
int unmount_rootfs_img(const char *mount_point, int silent);
int is_mountpoint(const char *path);

/* ---------------------------------------------------------------------------
 * cgroup.c
 * ---------------------------------------------------------------------------*/

int ds_cgroup_v2_usable(void);
int ds_cgroup_kernel_supports_v2(void);
int ds_cgroup_host_is_v2(void);
int setup_cgroups(int is_systemd, int force_cgroupv1);
void ds_cgroup_host_bootstrap(int force_cgroupv1);
int ds_cgroup_attach(pid_t target_pid);
/* Remove the ds-enter-<child_pid> leaf cgroup after an enter/run session. */
void ds_cgroup_detach(pid_t child_pid, const char *container_name);
/* Remove the entire /sys/fs/cgroup/droidspaces/<name>/ subtree on stop. */
void ds_cgroup_cleanup_container(const char *container_name);
void print_cgroup_status(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * hardware.c
 * ---------------------------------------------------------------------------*/

int scan_host_gpu_gids(gid_t *gids, int max_gids);
void mirror_gpu_nodes(const char *dev_path);
int setup_gpu_groups(void);
void stop_termux_if_running(void);
int setup_unified_tmpfs(void);
void cleanup_unified_tmpfs(void);
int setup_x11_and_virgl_sockets(struct ds_config *cfg);
int setup_hardware_access(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * network.c
 * ---------------------------------------------------------------------------*/

int fix_networking_host(struct ds_config *cfg);
int fix_networking_rootfs(struct ds_config *cfg);

/* NAT veth/bridge lifecycle */
int setup_veth_host_side(struct ds_config *cfg, pid_t child_pid);
int setup_veth_child_side_named(struct ds_config *cfg, const char *peer_name,
                                const char *ip_str);
/* Populate a ds_net_handshake from a container init PID + resolved config.
 * peer_name is derived from the PID (stable per-boot interface name).
 * ip_str    is taken from cfg->static_nat_ip (stable across boots). */
void ds_net_derive_handshake(pid_t init_pid, struct ds_config *cfg,
                             struct ds_net_handshake *hs);
void ds_net_cleanup(struct ds_config *cfg, pid_t container_pid);
void ds_net_start_route_monitor(void);
int ds_net_disable_tx_checksum(const char *ifname);
void parse_cidr(const char *cidr, uint32_t *ip_out, uint32_t *mask_out);

int ds_get_dns_servers(const char *custom_dns, char *out, size_t size);
int detect_ipv6_in_container(pid_t pid);

/* ---------------------------------------------------------------------------
 * ds_netlink.c
 * ---------------------------------------------------------------------------*/

ds_nl_ctx_t *ds_nl_open(void);
void ds_nl_close(ds_nl_ctx_t *ctx);
int ds_nl_link_exists(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_get_ifindex(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_create_bridge(ds_nl_ctx_t *ctx, const char *name);
int ds_nl_create_veth(ds_nl_ctx_t *ctx, const char *host, const char *peer);
int ds_nl_set_master(ds_nl_ctx_t *ctx, const char *ifname, const char *master);
int ds_nl_link_up(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_link_down(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_del_link(ds_nl_ctx_t *ctx, const char *ifname);
int ds_nl_rename(ds_nl_ctx_t *ctx, const char *ifname, const char *newname);
int ds_nl_add_addr4(ds_nl_ctx_t *ctx, const char *ifname, uint32_t ip_be,
                    uint8_t prefix);
int ds_nl_add_route4(ds_nl_ctx_t *ctx, uint32_t dst_be, uint8_t dst_len,
                     uint32_t gw_be, int oif_idx);
int ds_nl_move_to_netns(ds_nl_ctx_t *ctx, const char *ifname, int netns_fd);
int ds_nl_get_iface_table(ds_nl_ctx_t *ctx, const char *ifname, int *table_out);
int ds_nl_add_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority);
int ds_nl_del_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority);
void ds_nl_flush_stale_veths(ds_nl_ctx_t *ctx, const char *prefix);
int ds_nl_count_ifaces_with_prefix(ds_nl_ctx_t *ctx, const char *prefix);
int ds_nl_list_ifaces(ds_nl_ctx_t *ctx, char names[][IFNAMSIZ], int max);
/* Kernel capability probe - call before any NAT setup */
int ds_nl_probe_nat_capability(char *reason, size_t rsz);

/* ---------------------------------------------------------------------------
 * ds_iptables.c
 * ---------------------------------------------------------------------------*/

int ds_ipt_ensure_masquerade(const char *src_cidr);
int ds_ipt_ensure_forward_accept(const char *iface);
int ds_ipt_ensure_input_accept(const char *iface);
int ds_ipt_ensure_mss_clamp(void);
int ds_ipt_remove_iface_rules(const char *iface);
int ds_ipt_remove_ds_rules(void);
int ds_ipt_add_portforwards(struct ds_config *cfg, const char *container_ip);
int ds_ipt_remove_portforwards(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * Static NAT IP management (network.c)
 * ---------------------------------------------------------------------------*/

/* Validate a user-supplied static NAT IP string.
 * Must be a valid IPv4 address inside DS_DEFAULT_SUBNET (172.28.0.0/16),
 * with octet3 in 1-254 and octet4 in 1-254 (excludes gateway row .0.x).
 * Returns 0 on success, -1 on failure (reason written to errbuf). */
int ds_net_validate_static_ip(const char *ip_str, char *errbuf, size_t errsize);

/* Scan all container.config files in the workspace for a static_nat_ip
 * collision.  exclude_name (the current container) is skipped.
 * Returns 0 if ip_str is unique across all other configs, 1 if collision. */
int ds_net_check_ip_collision(const char *ip_str, const char *exclude_name);

/* Called from start_rootfs() before the first config save and before fork.
 * Resolves cfg->static_nat_ip:
 *   - If user provided --nat-ip: validate + uniqueness check; fall back to
 *     auto-assign on failure.
 *   - If not provided (or fell back): derive a name-based deterministic IP,
 *     then walk forward until a unique slot is found.
 * After this call cfg->static_nat_ip is always a valid, unique IP string.
 * Callers must save config after this returns to persist the result. */
void ds_net_resolve_static_ip(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * ds_dhcp.c
 * ---------------------------------------------------------------------------*/

/* Start a single-lease DHCP server on veth_host (detached monitor thread).
 * Offers offer_ip_be to any DHCP client that broadcasts on the interface.
 * gw_ip_be becomes the router/server-id option (typically DS_NAT_GW_IP).
 * Isolation is enforced by AF_PACKET bind to veth_host's ifindex; no MAC
 * filter is needed or used (see ds_dhcp.c for rationale). */
void ds_dhcp_server_start(struct ds_config *cfg, const char *veth_host,
                          uint32_t offer_ip_be, uint32_t gw_ip_be);

/* Stop the DHCP server and unblock its recv() loop. Call before veth teardown.
 */
void ds_dhcp_server_stop(void);

/* ---------------------------------------------------------------------------
 * terminal.c
 * ---------------------------------------------------------------------------*/

int ds_openpty(int *master, int *slave, char *name);
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

int console_monitor_loop(int console_master_fd, pid_t monitor_pid,
                         struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * pid.c
 * ---------------------------------------------------------------------------*/

const char *get_workspace_dir(void);
const char *get_pids_dir(void);
const char *get_net_dir(void);
const char *get_logs_dir(void);
int ensure_workspace(void);
int generate_container_name(const char *rootfs_path, char *name, size_t size);
int find_available_name(const char *base_name, char *final_name, size_t size);
int resolve_pidfile_from_name(const char *name, char *pidfile, size_t size);
int auto_resolve_pidfile(struct ds_config *cfg);
int is_container_running(struct ds_config *cfg, pid_t *pid_out);
int is_container_init(pid_t pid);
int ds_metadata_sync(pid_t pid);
int count_running_containers(char *first_name, size_t size);
pid_t find_container_init_pid(const char *uuid);
pid_t find_container_by_name(const char *name);
int sync_pidfile(const char *src_pidfile, const char *name);
int show_containers(void);
int scan_containers(void);
void write_plain_env_file(const char *src, const char *dst);

/* ---------------------------------------------------------------------------
 * boot.c
 * ---------------------------------------------------------------------------*/

void ds_apply_capability_hardening(int hw_access, int privileged_mask);
int internal_boot(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * environment.c
 * ---------------------------------------------------------------------------*/

void load_etc_environment(void);
void ds_env_boot_setup(struct ds_config *cfg);
void ds_env_save(const char *path, struct ds_config *cfg);
void parse_env_file_to_config(const char *path, struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * container.c
 * ---------------------------------------------------------------------------*/

int is_valid_container_pid(pid_t pid);
int start_rootfs(struct ds_config *cfg);
int stop_rootfs(struct ds_config *cfg, int skip_unmount);
int enter_namespace(pid_t pid, struct ds_config *cfg);
int enter_rootfs(struct ds_config *cfg, const char *user);
int run_in_rootfs(struct ds_config *cfg, int argc, char **argv,
                  const char *as_user);
int show_info(struct ds_config *cfg, int trust_cfg_pid);
int show_container_usage(struct ds_config *cfg);
int restart_rootfs(struct ds_config *cfg);

/* ---------------------------------------------------------------------------
 * documentation.c
 * ---------------------------------------------------------------------------*/

void print_documentation(const char *argv0);

/* ---------------------------------------------------------------------------
 * check.c
 * ---------------------------------------------------------------------------*/

int is_dangerous_node(const char *name);
int check_requirements(void);
int check_requirements_hw(int hw_access);
int check_requirements_detailed(void);

/* ---------------------------------------------------------------------------
 * daemon.c - daemon, client, and probe entry points
 * ---------------------------------------------------------------------------*/

int ds_daemon_run(int foreground, char **argv);
int ds_client_run(int argc, char **argv);
int ds_daemon_probe(void);

#endif /* DROIDSPACE_H */
