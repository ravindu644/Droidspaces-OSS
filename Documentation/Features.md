<!--
title: Container settings
section: Guides
order: 2
desc: Every container setting in Droidspaces, in the order the app shows them: what each toggle or flag does, when to turn it on, and what it needs from the kernel.
keywords: droidspaces, settings, features, network, mode, hardware, access, gpu, volatile, bind, mount, resource, limits, privileged, sandboxing, run, at, boot
-->

# Container settings

Each setting below is one toggle in the app's container configuration, and one flag on the command line. They are listed in the order the app shows them. The CLI flag and the key it writes to the container's config file are given with each one, so this page serves both the app and the CLI.

## How settings are stored

Every container has a `container.config` file of `key=value` lines. The app writes it from the configuration form. On the CLI, the flags you pass to the first `start` are saved into it, so later starts only need `--name`. Flags passed to a later `start` or `restart` update the file; `--reset` returns it to defaults while keeping the name and rootfs.

A change takes effect the next time the container starts. In the app, the container's menu has **Edit Container Configuration**.

## Networking

### Network Mode

`--net=MODE`, key `net_mode`. The default is `nat`.

| Mode | What the container gets | Use it when |
|------|-------------------------|-------------|
| **NAT** | Its own network stack and a private address, with internet through the host. Nothing to set up. The host's active connection is followed as it changes between Wi-Fi, mobile data and Ethernet. | You want a container that just has internet. This is the right choice for almost everything. |
| **Gateway** | Its own network stack, on a LAN owned by another running container, usually OpenWrt, which does DHCP, DNS, firewalling, routing and VPN for it. | You want a VPN kill switch, DNS filtering, or several containers on one isolated LAN behind one router. |
| **Macvlan** | Its own MAC address on a wired network the phone is plugged into, and an address from that network's router. No NAT. | The container should be a first-class device on your home LAN, reachable without port forwarding. Needs a USB Ethernet adapter on a phone. |
| **Host** | The host's own network stack, shared. | A program needs an interface only the host has. Services bind host ports directly, and a network manager inside the container can break the phone's connectivity. |
| **None** | A network stack with only loopback. | The container must be offline. |

[Networking from zero](Networking-From-Zero.md) explains all five in depth, including how to set up an OpenWrt gateway.

### Static IP Address

`--nat-ip=IP`, key `static_nat_ip`. NAT mode only. Fixes the container's address in the `172.28.0.0/16` range. Without it the address is derived from the container's PID and can change between starts, which matters when another device or a port forward on your router has it written down.

### Upstream Interface

`--upstream=IFACE[,IFACE...]`, key `upstream_interfaces`. NAT mode only. Leave it empty and the container follows whatever connection the host is using. Set it and the container's internet goes out through the listed interfaces only, first one that is up and has internet wins. Wildcards work, and `rmnet*` is the way to name mobile data, whose interface number changes between reconnects.

- `tun0`: the container can only reach the internet through a VPN app running on the phone. When the VPN is down, the container is offline.
- `rmnet*`: the container stays on mobile data while the phone uses Wi-Fi. Turn on "Mobile data always active" in Developer Options for this.
- `wlan0,rmnet*`: Wi-Fi when available, mobile data otherwise.

### Port Forwarding

`--port=[HOST:]CONTAINER[/tcp|udp]`, key `port_forwards`. NAT mode only. Makes a service inside the container reachable on a port of the host, from the host itself and from any network it is on. `--port=22` maps the same port on both sides, `--port=8080:80` maps host 8080 to container 80, and `--port=1000-2000:1000-2000` maps a range of the same size. Without a protocol, both TCP and UDP are forwarded. Repeat the flag or separate entries with commas.

### Disable IPv6

`--disable-ipv6`, key `disable_ipv6`. Turns IPv6 off inside the container. In NAT mode the container normally gets IPv6 as well as IPv4, provided the kernel has IPv6 NAT; without it the app holds this toggle on. In host mode the setting reaches the host's own stack, which can break VPN apps on the phone.

### DNS Servers

`--dns=SERVER[,SERVER...]`, key `dns_servers`. Without it, NAT, host and none mode containers use Cloudflare and Google's public resolvers. Gateway and macvlan containers use whatever DNS their LAN's DHCP server hands out, which is what you want when the gateway filters ads or resolves local names, so set this only to override that on purpose.

## Integration and hardware

### Android Storage

`--enable-android-storage`, key `enable_android_storage`. Android only. Mounts the phone's shared storage at `/storage/emulated/0` inside the container, so your photos, downloads and documents are reachable from Linux.

### Hardware Access

`--hw-access`, key `enable_hw_access`. Gives the container the host's real `/dev`: GPU, USB devices, sensors, input devices, and every block device, partitions and SD card included. The container behaves like a Linux machine with that hardware. Turn it on for USB peripherals, for a desktop with GPU acceleration on Linux, or for anything that needs a device node the GPU toggle does not cover.

> [!CAUTION]
>
> Raw block devices are in there. A wrong `dd` or `mkfs` inside the container destroys the phone's partitions. Use this only with a container you trust, and do not report bugs seen with it on. The app makes you type a confirmation for that reason.

The six host virtual terminals, `tty1` to `tty6`, stay hidden even with hardware access on, because a systemd container would otherwise put its login prompt on the host console. `--allow-vts` reveals them. The app has no toggle for it.

### GPU Access

`--gpu`, key `enable_gpu_mode`. The middle ground. The container keeps its private `/dev` and only the GPU render nodes are mirrored into it, so programs can use the GPU and nothing else on the host is exposed. Mirrored nodes are owned by root and a `droidspaces-gpu` group; add other users to that group with `usermod`. Hardware Access turns this on as well.

Known GPU families: Mesa DRI render nodes on Intel, AMD and most ARM SoCs, Qualcomm Adreno (`kgsl`), ARM Mali, NVIDIA, PowerVR and the DMA heaps these drivers need.

### Configure Termux:X11

`--x11`, key `enable_x11`. Android: starts the Termux:X11 server when the container starts, mounts its socket into the container, and sets `DISPLAY` for every login shell. Needs the Termux and Termux:X11 apps and the one-time setup in the [display guide](Graphics-and-Audio.md). Linux: mounts the host's X11 socket so GUI programs in the container open on your desktop.

### Anland Display

`--anland`, key `enable_anland`. Android only. Starts the Anland display daemon for the container, so a Wayland compositor patched for Anland can show its desktop in the Anland app. Needs GPU Access, a patched compositor in the container and the Anland app on the phone, and it replaces Termux:X11 rather than adding to it. The [Anland guide](Anland.md) walks through it.

### Configure VirGL 3D Acceleration

`--virgl`, key `enable_virgl`. Android only. Starts the VirGL server in Termux alongside Termux:X11 and sets `GALLIUM_DRIVER=virpipe` in the container, so OpenGL programs render on the phone's GPU instead of in software. The right choice on Mali and PowerVR GPUs. On Qualcomm, Turnip inside the container with GPU Access is faster; the [display guide](Graphics-and-Audio.md) compares them.

### Configure PulseAudio

`--pulse-audio`, key `enable_pulseaudio`. Android only. Starts a PulseAudio server in Termux, mounts its socket into the container and sets `PULSE_SERVER`, so anything that plays sound in the container comes out of the phone's speaker.

## Resource limits

`--memory=SIZE`, `--cpus=COUNT` and `--pids-limit=N`, keys `memory_limit`, `cpu_quota` with `cpu_period`, and `pids_limit`. Off by default; a container shares everything the device has.

- **Limit memory**: `--memory=512M` or `--memory=2G`. The container cannot grow past it into the device's swap, so at the limit its own OOM killer ends a process inside it instead of the phone slowing down as zram fills. Everything inside the container, `free` and fastfetch included, reports usage against this limit and no swap.
- **Allow swap**: `--allow-swap`, key `allow_swap`. Lets a memory-limited container use the device's swap past its limit, as it could before the cap, and shows it the device's swap. `droidspaces info` and `show --json` say whether a running container may swap.
- **Limit CPU**: `--cpus=1.5` gives the container one and a half cores' worth of time, spread over all cores.
- **Limit processes**: the most processes and threads the container may have at once, minimum 16. A systemd distribution needs a few hundred.

Each limit needs its cgroup controller in the kernel: `CONFIG_MEMCG`, `CONFIG_CFS_BANDWIDTH` and `CONFIG_CGROUP_PIDS`. The app greys out a limit the kernel cannot enforce and names the missing option; the CLI skips it at start with a warning. Stock Android kernels often lack the CPU and process ones. `droidspaces info` prints the limits in force and current usage.

One gap: a program that asks the kernel directly through `sysinfo()` rather than reading `/proc` still sees the host's memory and load. OpenWrt's LuCI does this. The limit is enforced all the same.

## Security and boot

### SELinux Permissive

`--selinux-permissive`, key `selinux_permissive`. Android and supported Linux distros only. Switches the host's SELinux to permissive while the container runs, and back to enforcing when the last container that asked for it stops.

### Allow Sandboxing

`--allow-sandboxing`, key `allow_sandboxing`. Off by default. Turn it on when something inside the container builds its own sandbox: rootless Docker or Podman, Flatpak, bwrap, Firefox, Chromium. They all create a user namespace and mount their own `/proc` inside it, and a stock container blocks both. With the toggle on, user namespaces are allowed and the container gets the pristine `/proc` and read-only `/sys` instances the kernel requires for that, under `/run/droidspaces/`.

The cost is that root in the container can reach the host's live sysctl tree through that pristine `/proc`, so this is a statement of trust in the container's root user. Needs `CONFIG_USER_NS`, reported by `droidspaces check` as user namespace support. For Docker inside the container, put `{"userns-remap": "default"}` in `/etc/docker/daemon.json` and restart it.

### Volatile Mode

`--volatile`, key `volatile_mode`. Every write goes to RAM and is thrown away when the container stops; the rootfs is never changed. Good for trying a package, for a clean environment on every boot, or for breaking things on purpose. Needs OverlayFS in the kernel. On Android it needs a sparse image rootfs, because OverlayFS on most Android kernels refuses an f2fs directory as its base.

### Force Cgroup V1

`--force-cgroupv1`, key `force_cgroupv1`. Makes the container use the legacy cgroup v1 hierarchy even when the host offers v2. For old Android kernels, 4.9 and earlier, whose cgroup v2 is missing the controllers systemd expects, which makes systemd misread the environment and fail to boot. Leave it off unless a systemd container will not boot on such a kernel.

### Privileged Mode

`--privileged=TAG[,TAG...]`, key `privileged`. Removes security barriers one at a time:

- `nomask`: `/proc` and `/sys` are not masked. `/sys` stays read-only unless Hardware Access is on.
- `nocaps`: no capabilities are dropped; root in the container is full root.
- `noseccomp`: no system call filtering.
- `shared`: mounts made in the container propagate to the host.
- `full`: all of the above.

Each one is a hole in the container's isolation. Nothing in ordinary use needs them; Allow Sandboxing covers Flatpak, bwrap and rootless Docker on its own. Bugs seen with privileged mode on are not accepted, and the app asks you to confirm that.

### Run at Boot

Key `run_at_boot`, with `run_at_boot_priority`. App only. Starts the container when the phone boots, before the lock screen, through the boot module the app installed. Containers start in the order set under **Settings**, **Auto Boot Priority**; put a gateway container before the containers that route through it.

## Advanced options

### Environment Variables

`--env=PATH`, key `env_file`. A file of `KEY=value` lines, one per line, `#` for comments. The variables are set for the container's init and for every `enter` and `run` session. The app edits the file for you.

### Bind Mounts

`--bind=SRC:DEST[:ro]`, key `bind_mounts`. Makes a host directory visible inside the container at `DEST`, read-only with `:ro`. Repeat the flag or separate entries with commas. The destination must be an absolute path inside the container and cannot be `/`. A source that is missing at start is skipped with a warning rather than failing the boot.

### Custom Init Binary

`--init=PATH`, key `custom_init`. The program to run as PID 1 instead of `/sbin/init`. For a distribution that keeps its init elsewhere, or to start a shell as PID 1 when debugging a rootfs that will not boot.

### Termux:X11 Extra Flags and VirGL Extra Flags

`--tx11-flags="..."`, key `tx11_extra_flags`, and `--virgl-flags="..."`, key `virgl_extra_flags`. Extra arguments for the Termux:X11 and VirGL servers Droidspaces starts. Shown in the app only when the matching toggle is on.

## Storage

Chosen once, when the container is installed.

**Sparse image**: the rootfs lives in one ext4 image file that Droidspaces loop-mounts at start. The CLI equivalent is `--rootfs-img=PATH`, which also accepts a block device. The image behaves the same whatever filesystem the host uses, which matters on Android's f2fs, and it is one file to copy when you move the container. Images are checked with `e2fsck` when their superblock says they need it. The size you pick is a ceiling; the file grows as the container uses space. The app can **Resize the sparse image** later, and **Migrate to a sparse image** converts a directory container.

**Directory**: the rootfs is a plain directory, `--rootfs=PATH` on the CLI. Simpler to look into from the host. On Android, use it only if the image path is not possible.

**Storage location**: internal storage by default, or any folder you pick, including an SD card or USB drive. A location that cannot hold a directory rootfs forces the sparse image.

## Things with no toggle

**Init systems.** Any init that can run as PID 1 works: systemd, OpenRC, runit, s6, SysVinit, OpenWrt's procd. The container's `/sbin/init` must exist, or you name another with Custom Init Binary. The app's container details recognise systemd, OpenRC, procd and runit and let you manage their services.

**Hostname.** `--hostname=NAME`, key `hostname`. Defaults to the container name.

**Foreground mode.** `--foreground` on the CLI attaches your terminal to the container's console for the whole boot, which is the best way to see why a container does not come up.

---

Previous: [Android app guide](Usage-Android-App.md) | Next: [Linux CLI](Linux-CLI.md)
