/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

void print_ds_banner(void) {
  printf(C_CYAN C_BOLD "Welcome to " C_WHITE DS_PROJECT_NAME
                       " v" DS_VERSION C_CYAN " !" C_RESET "\r\n\r\n");
  fflush(stdout);
}

void print_usage(void) {
  printf(C_BOLD
         "%s v%s - High-performance Container Runtime for Android/Linux" C_RESET
         "\n",
         DS_PROJECT_NAME, DS_VERSION);
  printf("by " C_CYAN "%s" C_RESET "\n", DS_AUTHOR);
  printf("\n" C_BLUE "%s" C_RESET "\n", DS_REPO);
  printf(C_DIM "Built on: %s %s" C_RESET "\n\n", __DATE__, __TIME__);
  printf("Usage: droidspaces [options] <command> [args]\n\n");

  printf(C_BOLD
         "Commands:" C_RESET "\n"
         "  start                     Start a new container\n"
         "  stop                      Stop one or more containers\n"
         "  restart                   Restart a container\n"
         "  enter [user]              Enter a running container\n"
         "  run [--] <cmd> [args]     Run a command in a running container\n"
         "  info                      Show detailed container info\n"
         "  pid                       Show the live PID of the container init\n"
         "  show                      List all running containers\n"
         "  scan                      Scan for untracked containers\n"
         "  check                     Check system requirements\n"
         "  docs                      Show interactive documentation\n"
         "  help                      Show this help message\n"
         "  version                   Show version information\n"
         "  daemon                    Run daemon mode (use --foreground for "
         "foreground execution)\n\n");

  printf(C_BOLD "Options (Container Setup):" C_RESET "\n"
                "  -r, --rootfs=PATH         Path to rootfs directory\n"
                "  -i, --rootfs-img=PATH     Path to rootfs image (.img)\n"
                "  -n, --name=NAME           Container name (mandatory)\n"
                "  -h, --hostname=NAME       Set container hostname\n"
                "  -C, --conf=PATH           Load configuration from file\n\n");

  printf(
      C_BOLD
      "Options (Networking):" C_RESET "\n"
      "      --net=MODE            Modes: nat (default), host, none, gateway, "
      "macvlan\n"
      "      --gateway=NAME        Gateway container for --net=gateway\n"
      "      --gateway-net=NAME    Gateway LAN name/bridge suffix (default: "
      "lan)\n"
      "      --gateway-iface=IFACE Interface name inside gateway (default: "
      "eth1)\n"
      "      --gateway-bridge=BR   Host bridge for gateway LAN (default: "
      "ds-NAME)\n"
      "      --macvlan-parent=IF   Host NIC for --net=macvlan, e.g. eth0\n"
      "      --macvlan-mode=MODE   bridge (default), private, vepa, passthru\n"
      "      --nat-ip=IP           Assign a fixed IP in 172.28.*.* range (nat "
      "mode)\n"
      "      --upstream IFACE      Pin NAT WAN to interface(s); disables "
      "auto-detect\n"
      "                            comma-separated, wildcards ok, e.g. "
      "--upstream wlan0 or wlan0,rmnet*\n"
      "      --port [H:]C[/P]      Forward ports (supports ranges and "
      "symmetric ports)\n"
      "                            e.g. --port 22, 80:80/tcp, "
      "1000-2000:1000-2000/udp\n"
      "  -d, --dns=SERVERS         Set custom DNS servers (comma separated)\n"
      "                            e.g. --dns 1.1.1.1,8.8.8.8\n"
      "  -I, --disable-ipv6        Disable IPv6 inside the container\n\n");

  printf(
      C_BOLD
      "Options (Integration & Hardware):" C_RESET "\n"
      "  -S, --enable-android-storage\n"
      "                            Mount Android internal storage (/sdcard)\n"
      "  -H, --hw-access           Enable direct hardware access (/dev nodes)\n"
      "      --gpu                 Enable GPU acceleration nodes\n"
      "  -X, --x11                 Bridge host X11 display into the container\n"
      "      --tx11-flags=\"FLAGS\"    Extra flags passed to termux-x11\n"
      "      --virgl               Configure VirGL 3D acceleration support\n"
      "      --virgl-flags=\"FLAGS\"   Extra flags passed to "
      "virgl_test_server_android\n"
      "      --pulse-audio         Configure PulseAudio sound server "
      "support\n"
      "      --anland              Embed the anland display daemon "
      "(Android)\n\n");

  printf(
      C_BOLD
      "Options (Security & Boot):" C_RESET "\n"
      "  -P, --selinux-permissive  Set host SELinux to permissive mode\n"
      "  -V, --volatile            Discard changes on exit (OverlayFS)\n"
      "      --force-cgroupv1      Force legacy cgroup v1 hierarchy\n"
      "      --memory=LIMIT        Memory limit (e.g. 512M, 2G)\n"
      "      --allow-swap          Let a memory-limited container use host "
      "swap\n"
      "      --cpus=COUNT          CPU limit (e.g. 1.5, 2)\n"
      "      --pids-limit=N        Max number of PIDs\n"
      "      --privileged=TAGS     Relax security: nomask, nocaps, noseccomp, "
      "shared, full\n"
      "      --allow-sandboxing    Allow user namespaces for unprivileged "
      "Docker, Podman, bwrap\n"
      "      --allow-vts           Leave host VTs (tty1-6) visible with "
      "--hw-access\n\n");

  printf(
      C_BOLD
      "Options (Advanced):" C_RESET "\n"
      "  -f, --foreground          Run in foreground (attach console)\n"
      "      --init=PATH           Custom init binary (default: /sbin/init)\n"
      "  -u, --user=USER           Run command as USER (for 'run' command "
      "only)\n"
      "      --stdin               Forward stdin to the 'run' command through "
      "the\n"
      "                            daemon (default is /dev/null)\n"
      "  -E, --env=PATH            Load environment variables from file\n"
      "  -B, --bind=SRC:DEST[:ro]  Bind mount host directory into container\n"
      "                            Supports multiple flags or "
      "comma-separation\n"
      "                            e.g. -B /data:/data -B /tmp:/tmp\n"
      "                            e.g. -B /data:/data,/tmp:/tmp\n"
      "      --reset               Reset config to defaults (keeps "
      "name/rootfs)\n"
      "      --json                JSON output (show, info, check)\n"
      "      --help                Show this help message\n\n");

  printf(C_BOLD
         "Examples:" C_RESET "\n"
         "  droidspaces --name=mycontainer --rootfs=/path/to/rootfs start\n"
         "  droidspaces --name=mycontainer enter\n"
         "  droidspaces --name=mycontainer stop\n\n");
}
