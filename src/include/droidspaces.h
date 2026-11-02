/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DROIDSPACES_H
#define DROIDSPACES_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <grp.h>
#include <limits.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <poll.h>
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
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif
#ifndef BINDERFS_SUPER_MAGIC
#define BINDERFS_SUPER_MAGIC 0x6c6f6f70
#endif

/* Cgroup Namespace support (Linux 4.6+) */
#ifndef CLONE_NEWCGROUP
#define CLONE_NEWCGROUP 0x02000000
#endif

/* Constants */

#define DS_PROJECT_NAME "Droidspaces"
#define DS_VERSION "7.1.0"
#define DS_MIN_KERNEL_MAJOR 3
#define DS_MIN_KERNEL_MINOR 10

/* The smallest --pids-limit we accept. The limit counts every task in the
 * container's cgroup: the per-boot intermediate and init are always there,
 * and an enter or run session adds three before its shell forks anything.
 * Below this a container may boot, but there is no room to get a shell. */
#define DS_MIN_PIDS_LIMIT 16
#define DS_RECOMMENDED_KERNEL_MAJOR 4
#define DS_RECOMMENDED_KERNEL_MINOR 14
#define DS_AUTHOR "ravindu644"
#define DS_REPO "https://github.com/ravindu644/Droidspaces-OSS"
#define DS_UUID_LEN 32
#define DS_STOP_TIMEOUT 15       /* seconds */
#define DS_RETRY_DELAY_US 200000 /* 200ms */
#define DS_REBOOT_EXIT 249       /* exit code: in-container reboot */

/* Workspace paths */
#define DS_WORKSPACE_ANDROID "/data/local/Droidspaces"
#define DS_WORKSPACE_LINUX "/var/lib/Droidspaces"
#define DS_CONTAINERS_DIR "Containers"
#define DS_PIDS_SUBDIR "Pids"
#define DS_IMG_MOUNT_ROOT_UNIVERSAL "/mnt/Droidspaces"
#define DS_BIND_INITIAL_CAP 4
#define DS_DEFAULT_INIT "/sbin/init"
#define DS_VOLATILE_SUBDIR "Volatile"
#define DS_LOGS_SUBDIR "Logs"
#define DS_NET_SUBDIR "Net"
#define DS_ANDROID_TMPFS_CONTEXT "u:object_r:tmpfs:s0"
#define DS_ANDROID_VOLD_CONTEXT "u:object_r:vold_data_file:s0"

/* Default DNS servers */
#define DS_DNS_DEFAULT_1 "1.1.1.1"
#define DS_DNS_DEFAULT_2 "8.8.8.8"
/* The only resolver a container can reach when the uplink has no IPv4. One
 * is all there is room for: glibc reads three nameserver lines. */
#define DS_DNS_DEFAULT_6 "2606:4700:4700::1111"

/* Common Paths & Patterns */
#define DS_PROC_ROOT_FMT "/proc/%d/root"
#define DS_PROC_STATUS_FMT "/proc/%d/status"
#define DS_FW_PATH_FILE "/sys/module/firmware_class/parameters/path"
#define DS_SYSTEMD_CONTAINER_MARKER "/run/systemd/container"
#define DS_DROIDSPACES_MARKER "/run/droidspaces"

/* Hardening constants */
#define DS_DEFAULT_TTY_GID 5
#define DS_DEFAULT_SUBNET "172.28.0.0/16"

/* X11 Display Number - change this one value to relocate everything */
#define TX11_DISPLAY_NUM 5
#define _DS_STR(x) #x
#define _DS_XSTR(x) _DS_STR(x)
#define TX11_DISPLAY_STR ":" _DS_XSTR(TX11_DISPLAY_NUM)
#define TX11_DISPLAY_SOCK "X" _DS_XSTR(TX11_DISPLAY_NUM)

/* X11 Socket Paths (Host-side relative to /.old_root or absolute) */
#define DS_X11_PATH_DESKTOP "/.old_root/tmp/.X11-unix"
#define DS_TERMUX_TMP_OLDROOT "/.old_root/data/data/com.termux/files/usr/tmp"
#define DS_X11_CONTAINER_DIR "/tmp/.X11-unix"

/* Termux paths (used by x11.c; kept here for single-source-of-truth) */
#define TX11_DATA_DIR "/data/user/0/com.termux"
#define TX11_DATA_ALT "/data/data/com.termux"
#define TX11_PREFIX "/data/data/com.termux/files/usr"
#define TX11_HOME "/data/data/com.termux/files/home"
#define TX11_LOADER TX11_PREFIX "/libexec/termux-x11/loader.apk"
#define TX11_SOCK_DIR TX11_PREFIX "/tmp/.X11-unix"
#define TX11_PACKAGES "/data/system/packages.list"

/* VirGL Paths (Android only) */
#define DS_VIRGL_SOCKET "/tmp/.virgl_test"
#define TX11_VIRGL_SOCKET TX11_PREFIX "/tmp/.virgl_test"
#define TX11_VIRGL_BIN TX11_PREFIX "/bin/virgl_test_server_android"

/* PulseAudio paths (Android only) */
#define DS_PULSE_SOCKET "/tmp/.pulse-socket"
#define TX11_PULSE_SOCKET TX11_PREFIX "/tmp/.pulse-socket"
#define TX11_PULSE_BIN TX11_PREFIX "/bin/pulseaudio"
#define TX11_PACTL_BIN TX11_PREFIX "/bin/pactl"
#define TX11_PULSE_DEFAULT_SINK "AAudio_sink"

/* File Extensions */
#define DS_EXT_PID ".pid"
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

/* Also mutes this container's log, see ds_log_internal. */
#define DS_LOG_PEEK 2
int check_ns(int flag, const char *name);

/* Data structures */

/* Networking modes */

/* NAT is the zero value so a zeroed config already holds the default */
enum ds_net_mode {
  DS_NET_NAT = 0, /* isolated netns + bridge + MASQUERADE      */
  DS_NET_HOST,    /* share host network namespace               */
  DS_NET_NONE,    /* isolated netns with loopback only          */
  DS_NET_GATEWAY, /* isolated netns attached to gateway LAN     */
  DS_NET_MACVLAN, /* isolated netns, macvlan on a host NIC      */
};

/* Opaque RTNETLINK context - defined in net/nl/internal.h */
typedef struct ds_nl_ctx ds_nl_ctx_t;

/* Handshake payload: Monitor → init via net_done_pipe */
struct ds_net_handshake {
  char peer_name[16]; /* e.g. "ds-p12345"        */
  char ip_str[32];    /* e.g. "172.28.4.47/16"  */
};

/* NAT networking constants */

#define DS_NAT_BRIDGE "ds-br0"
#define DS_NAT_GW_IP "172.28.0.1"
#define DS_NAT_PREFIX 16

/* NAT66. One ULA /48 for everything we masquerade. RFC 4193 wants the global
 * ID random; ours is fixed (64:73:70 is "dsp") for the same reason the IPv4
 * side is a fixed 172.28.0.0/16: the monitor, the cleanup path and the docs
 * all need to name it without reading state.
 *   Bridge mode:  ds-br0 is fd64:7370::1 and containers share fd64:7370::/64.
 *   Bridgeless:   each container gets fd64:7370:0:XXYY::/64, where XX.YY are
 *                 the last two octets of its 172.28.XX.YY address.
 * Containers always route through fe80::1, which sits on the bridge or on the
 * bridgeless host veth. */
#define DS_NAT6_SUBNET "fd64:7370::/48"
#define DS_NAT6_PREFIX "fd64:7370::"
#define DS_NAT6_PREFIX_LEN 48
#define DS_NAT6_GW_LL "fe80::1"

/* Router Advertisement frame: ethernet + IPv6 + RA with a prefix option. The
 * router lifetime is 30 minutes and the DHCP thread re-announces every 5. */
#define DS_RA_FRAME_LEN 102
#define DS_RA_ROUTER_LIFETIME 1800
#define DS_RA_INTERVAL_SEC 300

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
 * DS_RULE_PRIO_TETHER - "from 172.28.0.0/16 lookup local_network(97)"
 *   Returns container replies to hotspot/USB-tether clients out the tether
 *   interface.  Reply packets of DNAT'd port-forward connections still carry
 *   the container source address at routing time (conntrack un-DNATs in
 *   POSTROUTING, after the route lookup), so without this rule they would
 *   match DS_RULE_PRIO_FROM_SUBNET and leak out the WAN uplink instead of
 *   reaching the tethered client.  netd keeps the connected route of every
 *   downstream interface (swlan*, ap*, rndis*, ncm*, bt-pan) in the
 *   local_network table, which has no default route - so this rule only
 *   matches tether-bound traffic and falls through to FROM_SUBNET otherwise.
 *   Must sit between TO_SUBNET and FROM_SUBNET.
 *
 * DS_RULE_PRIO_FROM_SUBNET - "from 172.28.0.0/16 lookup <gw_table>"
 *   Routes container-originated traffic through the active uplink table.
 */
#define DS_RULE_PRIO_TO_SUBNET 6090
#define DS_RULE_PRIO_TETHER 6095
#define DS_RULE_PRIO_FROM_SUBNET 6100

/* netd's fixed local_network routing table
 * (RouteController::ROUTE_TABLE_LOCAL_NETWORK, constant since Android 5.x). */
#define DS_ANDROID_TABLE_LOCAL_NETWORK 97

/* Bind mount entry */
struct ds_bind_mount {
  char src[PATH_MAX];
  char dest[PATH_MAX];
  int ro; /* 1 = remount read-only after bind */
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

/* Port forwarding (--port HOST:CONTAINER[/proto]) */

#define DS_MAX_PORT_FORWARDS 32
#define DS_MAX_UPSTREAM_IFACES 32

struct ds_port_forward {
  uint16_t host_port;          /* port on the Android/Linux host  */
  uint16_t host_port_end;      /* end of range (0 if single) */
  uint16_t container_port;     /* port inside the container       */
  uint16_t container_port_end; /* end of range (0 if single) */
  char proto[4];               /* "tcp" or "udp"                  */
};

/* Privileged Mode Flags */
#define DS_PRIV_NOMASK (1 << 0) /* No jail masks (/proc, /sys) */
#define DS_PRIV_NOCAPS (1 << 1) /* No capability drops */
#define DS_PRIV_NOSEC (1 << 2)  /* Minimal seccomp only */
#define DS_PRIV_SHARED (1 << 3) /* MS_SHARED root propagation */
#define DS_PRIV_FULL (0xFF)     /* All above */

typedef enum {
  DS_INIT_UNKNOWN = 0,
  DS_INIT_SYSTEMD,  /* SIGRTMIN+3 */
  DS_INIT_PROCD,    /* SIGUSR2    -- OpenWrt; SIGTERM = reboot there! */
  DS_INIT_OPENRC,   /* SIGTERM    */
  DS_INIT_RUNIT,    /* stopit flag + SIGCONT */
  DS_INIT_S6,       /* SIGUSR2    */
  DS_INIT_BUSYBOX,  /* SIGUSR2    */
  DS_INIT_SYSVINIT, /* SIGTERM    */
} ds_init_type_t;

/* Container configuration - replaces all global variables */
struct ds_config {
  /* Paths */
  char rootfs_path[PATH_MAX];        /* --rootfs=  */
  char rootfs_img_path[PATH_MAX];    /* --rootfs-img= */
  char pidfile[PATH_MAX];            /* Pids/<name>.pid */
  char container_name[256];          /* --name= (mandatory) */
  char hostname[256];                /* --hostname= or container_name */
  char dns_servers[1024];            /* --dns= (comma/space separated) */
  enum ds_net_mode net_mode;         /* --net=host|nat|none|gateway|macvlan */
  char dns_server_content[1024];     /* In-memory DNS config for boot */
  char gateway_container[256];       /* --gateway=NAME for gateway mode */
  char gateway_net[64];              /* --gateway-net=NAME (default: lan) */
  char gateway_bridge[IFNAMSIZ];     /* optional host bridge name */
  char gateway_lan_ifname[IFNAMSIZ]; /* ifname inside gateway (default: eth1) */
  char macvlan_parent[IFNAMSIZ];     /* --macvlan-parent= host NIC */
  uint32_t macvlan_mode;             /* MACVLAN_MODE_*, 0 = bridge */

  /* UUID for PID discovery */
  char uuid[DS_UUID_LEN + 1];

  /* Flags */
  int foreground; /* --foreground */
  int hw_access;  /* --hw-access */
  int gpu_mode;   /* --gpu: mirror GPU nodes into isolated tmpfs /dev */
  int x11; /* -X, --x11: bridge host X11 socket (launches Termux:X11 on Android)
            */
  char *tx11_extra_flags;  /* --tx11-flags "..." (heap, NULL if unset) */
  int virgl;               /* --virgl (Android only) */
  char *virgl_extra_flags; /* --virgl-flags "..." (heap, NULL if unset) */
  int pulseaudio;          /* --pulse-audio (Android only) */
  int anland;              /* --anland: embed anland display daemon (Android) */
  int volatile_mode;       /* --volatile */
  int disable_ipv6;        /* --disable-ipv6 */
  int android_storage;     /* --enable-android-storage */
  int selinux_permissive;  /* --selinux-permissive */
  int sandboxing_allowed;  /* --allow-sandboxing */
  int allow_vts;           /* --allow-vts: leave host VTs unmasked in hw mode */
  int allow_swap;    /* --allow-swap: a memory-limited container may use swap */
  int swap_uncapped; /* runtime, never saved: no swap cap is in force */
  int net_bridgeless;  /* Probe result: no CONFIG_BRIDGE, use PTP NAT */
  int reboot_cycle;    /* 1 if we are in a reboot loop */
  int force_cgroupv1;  /* --force-cgroupv1: use v1 even if v2 is available */
  int privileged_mask; /* --privileged bitmask */
  /* A privileged= value with an unknown tag: the mask stays empty and this is
   * written back as it was until a valid --privileged replaces it. */
  char privileged_bad[256];
  int json_output;    /* --json: JSON output (show, info, check) */
  char prog_name[64]; /* argv[0] for logging */

  /* Runtime state */
  char volatile_dir[PATH_MAX]; /* temporary overlay dir */
  pid_t container_pid;         /* PID 1 of the container (host view) */
  pid_t x11_pid;               /* PID of the Termux-X11 server process */
  pid_t virgl_pid;             /* PID of the VirGL server process */
  pid_t pulse_pid;             /* PID of the PulseAudio daemon process */
  pid_t anland_pid;            /* PID of the anland display daemon process */
  char anland_sock[108]; /* host socket of the anland daemon, sun_path size */
  int is_img_mount;      /* 1 if rootfs was loop-mounted from .img */
  char img_mount_point[PATH_MAX]; /* where the .img was mounted */
  ds_init_type_t init_type;       /* detected container PID 1 init family */
  char custom_init[PATH_MAX]; /* --init=PATH override (default: /sbin/init) */

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
  /* The console travels on these socketpairs. Init allocates it from the
   * container's own devpts and sends the pair up console_sock[1]; the monitor
   * reads [0] and keeps a copy. A foreground start also gets the pair, over
   * console_fwd[1]; only the start command holds console_fwd[0], so once it
   * is gone the monitor sees end of file there and adopts the console. */
  int console_sock[2];
  int console_fwd[2];

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

  /* User-pinned upstream interfaces (--upstream wlan0,rmnet*,...).  When set,
   * NAT WAN is forced through this list (priority order, literals + wildcards)
   * and automatic uplink detection is disabled entirely.  Empty = auto-detect.
   */
  char upstream_ifaces[DS_MAX_UPSTREAM_IFACES][IFNAMSIZ];
  int upstream_iface_count;

  /* Resource limits (0 = unlimited) */
  long long memory_limit; /* bytes */
  long long cpu_quota;    /* us per period */
  long long cpu_period;   /* us (default 100000) */
  long long pids_limit;

  /* Resource virtualization (auto-enabled when limits are set) */
  struct timespec start_time; /* container start time (CLOCK_MONOTONIC) */
  unsigned long ns_inode;     /* PID namespace inode for PID-recycling guard */
};

#include "android/firmware.h"
#include "android/socket.h"
#include "android/spawn.h"
#include "cgroup/discover.h"
#include "cgroup/lifecycle.h"
#include "cgroup/limits.h"
#include "cgroup/mount.h"
#include "cgroup/stats.h"
#include "cli/options.h"
#include "cli/usage.h"
#include "config/config.h"
#include "config/load.h"
#include "config/parse.h"
#include "config/save.h"
#include "container/inspect.h"
#include "container/recovery.h"
#include "container/registry.h"
#include "core/exec.h"
#include "core/fdpass.h"
#include "core/fs.h"
#include "core/json.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/proc.h"
#include "core/string.h"
#include "dev/dev.h"
#include "dev/devpts.h"
#include "env/environment.h"
#include "hw/gpu.h"
#include "hw/hardware.h"
#include "net/cleanup.h"
#include "net/dhcp/server.h"
#include "net/dns.h"
#include "net/gateway/lifecycle.h"
#include "net/guest.h"
#include "net/ipt/core.h"
#include "net/ipt/portfwd.h"
#include "net/ipt/rules.h"
#include "net/macvlan.h"
#include "net/naming.h"
#include "net/nat/veth.h"
#include "net/netns.h"
#include "net/nl/addr.h"
#include "net/nl/core.h"
#include "net/nl/link.h"
#include "net/nl/linkdump.h"
#include "net/nl/probe.h"
#include "net/nl/route_query.h"
#include "net/nl/rule.h"
#include "net/route/monitor.h"
#include "net/static_ip.h"
#include "rootfs/binds.h"
#include "rootfs/image.h"
#include "rootfs/mount.h"
#include "rootfs/volatile.h"
#include "security/caps.h"
#include "security/jail.h"
#include "security/peer_auth.h"
#include "security/seccomp.h"
#include "security/seccomp_shims.h"
#include "security/selinux.h"
#include "status/collect.h"
#include "status/show.h"
#include "tty/console.h"
#include "tty/proxy.h"
#include "tty/pty.h"
#include "virt/affinity.h"
#include "virt/vproc.h"

/* monitor.c, container.c and main.c */

void ds_monitor_run(struct ds_config *cfg, int sync_pipe_write);
int ds_container_lock(const char *name, int wait);
void ds_container_unlock(int fd);
int ds_container_lock_orphan(const char *name);
void ds_container_claim_supervision(const char *name);
void ds_container_release_supervision(void);
void ds_container_prune_supervision(const char *name);
int ds_container_lock_helpers(const char *name);
void ds_container_wait_helpers(const char *name, int timeout_ms);
void ds_android_helpers_sync(struct ds_config *cfg);
int wait_for_socket_or_death(pid_t pid, const char *path, int timeout_ms,
                             int interval_us);
void cleanup_container_resources(struct ds_config *cfg, pid_t pid,
                                 int skip_unmount, int force_cleanup);
int ds_multi_stop(const char *raw_names);

/* android.c */

int is_android(void);
void android_native_personality(void);
void android_optimizations(int enable);
void android_remount_data_suid(void);
int android_setup_storage(const char *rootfs_path);

/* SELinux + Termux privilege helpers */
const char *ds_extract_mls(const char *ctx);
void ds_selinux_dyntransition(const char *mls);
int ds_drop_privileges(int uid);
int ds_resolve_termux_uid(void);

/* x11.c */

int ds_x11_daemon_start(struct ds_config *cfg);
void ds_x11_daemon_stop(struct ds_config *cfg);
int ds_setup_x11_socket(struct ds_config *cfg);

/* anland/anland.c */

int ds_anland_daemon_start(struct ds_config *cfg);
void ds_anland_daemon_stop(struct ds_config *cfg);
int ds_setup_anland_socket(struct ds_config *cfg);
void ds_anland_load_sock(struct ds_config *cfg);

/* virgl-android.c */

int ds_virgl_daemon_start(struct ds_config *cfg);
void ds_virgl_daemon_stop(struct ds_config *cfg);
int ds_setup_virgl_socket(struct ds_config *cfg);

/* pulseaudio-android.c */

int ds_pulse_daemon_start(struct ds_config *cfg);
void ds_pulse_daemon_stop(struct ds_config *cfg);
int ds_setup_pulse_socket(struct ds_config *cfg);

/* boot.c */

int internal_boot(struct ds_config *cfg);

/* container.c */

void write_plain_env_file(const char *src, const char *dst);
int is_valid_container_pid(pid_t pid);
int start_rootfs(struct ds_config *cfg);
int stop_rootfs(struct ds_config *cfg, int skip_unmount);
int stop_rootfs_with_timeout(struct ds_config *cfg, int skip_unmount,
                             int timeout_seconds);
int enter_namespace(pid_t pid, struct ds_config *cfg);
int enter_rootfs(struct ds_config *cfg, const char *user);
int run_in_rootfs(struct ds_config *cfg, char **argv, const char *as_user);
int show_info(struct ds_config *cfg, int trust_cfg_pid);
/* argc/argv: the process's original arguments, so restart can re-apply CLI
 * overrides after its post-stop config reload. */
int restart_rootfs(struct ds_config *cfg, int argc, char **argv);
/* The CLI override pass, lives in main.c. Idempotent over the same argv. */
int ds_apply_cli_overrides(int argc, char **argv, struct ds_config *cfg,
                           int strict);

/* documentation.c */

void print_documentation(const char *argv0);

/* check.c */

int check_requirements_hw(int hw_access);
int check_requirements_detailed(int json_output);

/* daemon.c - daemon, client, and probe entry points */

int ds_daemon_run(int foreground, char **argv);
/* cmd is the sub-command main() discovered, so a payload word like "run"
 * inside the command does not decide whether the session is interactive.
 * want_stdin is --stdin: relay our stdin to a pipe-mode run instead of
 * handing it /dev/null. */
int ds_client_run(int argc, char **argv, const char *cmd, int want_stdin);
const char *ds_command_word(int argc, char **argv);
int ds_daemon_probe(void);

#endif /* DROIDSPACES_H */
