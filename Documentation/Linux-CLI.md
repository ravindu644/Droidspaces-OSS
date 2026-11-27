<!--
title: Linux CLI
section: Guides
order: 3
desc: Reference for the droidspaces command line: every command, every flag, and the config file keys.
keywords: droidspaces, cli, linux, container, command, line, reference, flags, config, file
-->

# Command line reference

The `droidspaces` binary is the same on Linux and Android, and so are its commands and flags. On Android the app installs it at `/data/local/Droidspaces/bin/droidspaces`, and the **Integrate Droidspaces to the system path** setting puts it on the `PATH` of root shells.

Flags and the command can come in any order, short and long forms are interchangeable, and `=` is optional: `droidspaces -n web start` and `droidspaces start --name=web` are the same. `droidspaces docs` shows the same reference in a pager, offline.

## Commands

| Command | What it does |
|---------|--------------|
| `start` | Boots a container. The first start needs `--name` and `--rootfs` or `--rootfs-img`; after that the saved config supplies them. |
| `stop` | Shuts the container down cleanly. `--name=a,b,c` stops several. |
| `restart` | Stops and starts again. Image mounts are kept, so it is fast. |
| `enter [user]` | Opens a shell inside a running container, as root by default. |
| `run [--] <cmd>` | Runs one command inside a running container and returns. `--user` picks the account, `--stdin` pipes your input in. Anything after `run` belongs to the command, and `--` lets the command itself start with a dash. |
| `show` | Lists running containers. With `--json`, JSON with distribution, hostname, IP, uptime, CPU and RAM for each. |
| `info` | Shows a container's settings and state. With `--json`, JSON. |
| `pid` | Prints the PID of the container's init, or `NONE`. |
| `scan` | Finds running containers whose host-side records were lost and registers them again. |
| `check` | Probes the kernel for everything Droidspaces uses. With `--json`, JSON with one `0` or `1` per probe and `requirements_met`. |
| `daemon` | Starts the daemon. `--foreground` keeps it attached to the terminal, for a service manager. |
| `mode` | Prints `daemon` if a daemon is answering, otherwise `direct`. |
| `docs`, `help`, `version` | The built-in documentation, the flag summary, and the version. |

Every command except `check`, `docs`, `help`, `version` and `mode` needs root. When exactly one container is running, `--name` can be left out of `stop`, `restart`, `enter`, `run`, `info` and `pid`.

If a daemon is running, the CLI hands the command to it, so members of the `droidspaces` group can use the CLI without `sudo`. `DS_NO_PROXY=1` in the environment makes the binary do the work itself.

## Flags

Each flag that configures the container is saved in its config file on `start`, so it only has to be given once. [Container settings](Features.md) explains what each one does and when to use it; this table is the syntax.

**Rootfs and identity**

| Flag | Value | Config key |
|------|-------|------------|
| `-r`, `--rootfs` | directory with the rootfs | `rootfs_path` |
| `-i`, `--rootfs-img` | ext4 image file or block device | `rootfs_path` |
| `-n`, `--name` | container name; a comma-separated list for `stop` | `name` |
| `-h`, `--hostname` | hostname inside the container, defaults to the name | `hostname` |
| `-C`, `--conf`, `--config` | config file to load | |
| `--reset` | reset the config to defaults, keeping name and rootfs | |
| `--init` | init binary, default `/sbin/init` | `custom_init` |
| `-E`, `--env` | file of `KEY=value` lines | `env_file` |
| `-B`, `--bind` | `SRC:DEST[:ro]`, repeatable or comma-separated | `bind_mounts` |

**Networking**

| Flag | Value | Config key |
|------|-------|------------|
| `--net` | `nat` (default), `gateway`, `macvlan`, `host`, `none` | `net_mode` |
| `--nat-ip` | fixed address in `172.28.0.0/16`, NAT only | `static_nat_ip` |
| `--upstream` | interfaces to send NAT traffic through, comma-separated, wildcards allowed | `upstream_interfaces` |
| `--port` | `[HOST:]CONTAINER[/tcp\|udp]`, ranges as `a-b:a-b`, repeatable, NAT only | `port_forwards` |
| `-d`, `--dns` | resolvers, comma-separated | `dns_servers` |
| `--disable-ipv6` | | `disable_ipv6` |
| `--gateway` | the running container that acts as router, required for gateway mode | `gateway_container` |
| `--gateway-net` | LAN segment name, default `lan` | `gateway_net` |
| `--gateway-iface` | the LAN interface's name inside the gateway, default `eth1` | `gateway_lan_ifname` |
| `--gateway-bridge` | host bridge name, default `ds-<gateway-net>` | `gateway_bridge` |
| `--macvlan-parent` | host NIC, required for macvlan mode | `macvlan_parent` |
| `--macvlan-mode` | `bridge` (default), `private`, `vepa`, `passthru` | `macvlan_mode` |

**Hardware and integration**

| Flag | Value | Config key |
|------|-------|------------|
| `-H`, `--hw-access` | | `enable_hw_access` |
| `--allow-vts` | with `--hw-access`, leave the host's `tty1` to `tty6` visible | `allow_vts` |
| `--gpu` | | `enable_gpu_mode` |
| `-X`, `--x11` | | `enable_x11` |
| `--tx11-flags` | extra arguments for Termux:X11 | `tx11_extra_flags` |
| `--virgl` | Android only | `enable_virgl` |
| `--virgl-flags` | extra arguments for the VirGL server | `virgl_extra_flags` |
| `--pulse-audio` | Android only | `enable_pulseaudio` |
| `--anland` | Android only, Wayland desktop through the Anland app | `enable_anland` |
| `--enable-android-storage` | Android only | `enable_android_storage` |

**Resource limits, security and boot**

| Flag | Value | Config key |
|------|-------|------------|
| `--memory` | size with `K`, `M`, `G` or `T`, minimum `4M` | `memory_limit`, in bytes |
| `--allow-swap` | | `allow_swap` |
| `--cpus` | cores, fractions allowed | `cpu_quota` and `cpu_period`, in microseconds |
| `--pids-limit` | process count, minimum 16 | `pids_limit` |
| `-P`, `--selinux-permissive` | | `selinux_permissive` |
| `--allow-sandboxing` | | `allow_sandboxing` |
| `-V`, `--volatile` | | `volatile_mode` |
| `--force-cgroupv1` | | `force_cgroupv1` |
| `--privileged` | `nomask`, `nocaps`, `noseccomp`, `shared`, `full`, comma-separated | `privileged` |

**Session flags**, not saved

| Flag | What it does |
|------|--------------|
| `-f`, `--foreground` | Keep the container's console on your terminal through the boot. |
| `-u`, `--user` | The account `run` uses. |
| `--stdin` | Forward your input to the `run` command. Without it the command gets nothing. |
| `--json` | JSON output from `show`, `info` and `check`. |

## The config file

A plain `key=value` file, one key per line, with the keys from the tables above. Droidspaces looks for it in this order: the file named by `--config`, then `container.config` next to the rootfs, then `Containers/<name>/container.config` in the workspace. The workspace is `/var/lib/Droidspaces` on Linux and `/data/local/Droidspaces` on Android.

```ini
name=web
hostname=web
rootfs_path=/var/lib/Droidspaces/Containers/web/rootfs.img
net_mode=nat
port_forwards=80:80,443:443
bind_mounts=/srv/www:/var/www:ro
memory_limit=1073741824
cpu_quota=200000
cpu_period=100000
allow_sandboxing=1
```

Boolean keys take `0` or `1`. Keys the backend does not know, such as the app's `run_at_boot`, are kept in place. While a container runs, the config it booted with is visible inside it at `/run/droidspaces/container.config`. That copy is read-only and there for scripts inside the container: changing it has no effect, since the host never reads it.

## Examples

```bash
# A first container from an image, with a port forwarded
sudo droidspaces --name=web --rootfs-img=/srv/web.img --port=8080:80 start

# Later starts need only the name
sudo droidspaces --name=web start

# Watch the boot on the console
sudo droidspaces --name=web --foreground start

# A shell as a user, and a one-off command with a pipe
sudo droidspaces --name=web enter alice
sudo droidspaces --name=web run sh -c 'ps aux | grep nginx'

# A script piped into the container
sudo droidspaces --name=web --stdin run sh -s < setup.sh

# An OpenWrt router and a client that lives behind it
sudo droidspaces --name=openwrt --rootfs=/srv/openwrt --net=nat start
sudo droidspaces --name=kali --rootfs=/srv/kali --net=gateway --gateway=openwrt start

# A container on the wired LAN with its own address
sudo droidspaces --name=ubuntu --rootfs=/srv/ubuntu --net=macvlan --macvlan-parent=eth0 start

# Try something without keeping it
sudo droidspaces --name=scratch --rootfs=/srv/ubuntu --volatile start

# What is running, as JSON
sudo droidspaces --json show
```

## Where things are

| Path | Contents |
|------|----------|
| `<workspace>/Containers/<name>/` | the container's config and, for app-installed containers, its rootfs |
| `<workspace>/Logs/<name>/` | logs of each start, stop and restart |
| `<workspace>/Logs/droidspacesd.log` | the daemon's log |
| `/mnt/Droidspaces/<name>` | where an image rootfs is mounted while the container runs |

---

Previous: [Container settings](Features.md) | Next: [Display, audio and desktop](Graphics-and-Audio.md)
