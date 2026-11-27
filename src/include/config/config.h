/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CONFIG_CONFIG_H
#define DS_CONFIG_CONFIG_H

/* Networking modes */

/* NAT is the zero value so a zeroed config already holds the default */
enum ds_net_mode {
  DS_NET_NAT = 0, /* isolated netns + bridge + MASQUERADE      */
  DS_NET_HOST,    /* share host network namespace               */
  DS_NET_NONE,    /* isolated netns with loopback only          */
  DS_NET_GATEWAY, /* isolated netns attached to gateway LAN     */
  DS_NET_MACVLAN, /* isolated netns, macvlan on a host NIC      */
};

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
   * Both pairs start as {-1,-1} (ds_config_init). The monitor creates them
   * only when net_mode != DS_NET_HOST. */
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

/* config.c */
void sanitize_container_name(const char *name, char *out, size_t size);
int validate_container_name(const char *name);
int reject_container_name(const char *name);

void ds_config_init(struct ds_config *cfg);
void ds_config_free(struct ds_config *cfg);
void ds_config_reset_defaults(struct ds_config *cfg);
void apply_reset_config(struct ds_config *cfg);

/* load.c */
int ds_config_load(const char *config_path, struct ds_config *cfg);
int ds_config_load_file(FILE *f, struct ds_config *cfg);
/* <workspace>/Containers/<sanitized name>/container.config. -1 for a name
 * validate_container_name refuses. */
int ds_config_host_path(const char *name, char *buf, size_t size);
/* The host config of name, by path, never the booted one: for the reloads
 * that want the user's latest edits. */
int ds_config_load_host(const char *name, struct ds_config *cfg);
int ds_config_load_by_name(const char *name, struct ds_config *cfg);
int ds_config_peek(const char *name, struct ds_config *cfg);
int ds_config_peek_path(const char *config_path, struct ds_config *cfg);
void free_config_unknown_lines(struct ds_config *cfg);
char *ds_config_auto_path(const char *rootfs_path);

/* parse.c */
void sort_bind_mounts(struct ds_config *cfg);
void free_config_env_vars(struct ds_config *cfg);
void parse_env_file_to_config(struct ds_config *cfg);
int validate_bind_destination(const char *dest);

/* --net / net_mode= value to its mode, -1 if unknown. */
int ds_parse_net_mode(const char *s);
const char *ds_net_mode_name(enum ds_net_mode mode);

/* --macvlan-mode / macvlan_mode= value to MACVLAN_MODE_*, 0 if unknown. */
uint32_t ds_parse_macvlan_mode(const char *s);
const char *ds_macvlan_mode_name(uint32_t mode);

/* One SRC:DEST[:ro] or HOST[:CONTAINER][/proto] spec into cfg, cut up in
 * place. 0 stored, 1 skipped as a duplicate or overlap (port forwards only,
 * already warned about), -1 with the reason in err. */
int ds_parse_bind_spec(char *spec, struct ds_config *cfg, char *err,
                       size_t err_size);
int ds_parse_port_forward(char *spec, struct ds_config *cfg, char *err,
                          size_t err_size);
void ds_privileged_names(int mask, char *buf, size_t size, const char *sep);
int ds_config_add_bind(struct ds_config *cfg, const char *src, const char *dest,
                       int ro);
void free_config_binds(struct ds_config *cfg);
int parse_privileged(const char *value, struct ds_config *cfg);

/* save.c */
char *ds_config_serialize(struct ds_config *cfg, size_t *len, size_t *known);
int ds_config_save(const char *config_path, struct ds_config *cfg);
int ds_config_save_by_name(const char *name, struct ds_config *cfg);

#endif /* DS_CONFIG_CONFIG_H */
