<!--
title: Feature deep dives
section: Guides
order: 1
desc: How each Droidspaces feature works: namespace isolation, init system support, OverlayFS volatile mode, GPU acceleration, cgroup isolation, seccomp shields, and Android-specific tuning.
keywords: droidspaces, features, namespace, isolation, cgroup, overlayfs, volatile, mode, init, system, support, gpu, acceleration
-->

# Feature deep dives

What each major Droidspaces feature does and how it works underneath.

---

## Namespace isolation

### What are namespaces?

Linux namespaces are a kernel feature that partitions system resources, so each group of processes sees its own isolated set of them. Droidspaces uses five namespaces for every container, and a sixth, the network namespace, depending on the networking mode:

| Namespace | Flag | What It Isolates |
|-----------|------|-----------------|
| **PID** | `CLONE_NEWPID` | Process IDs. The container gets its own PID tree where init is PID 1. |
| **MNT** | `CLONE_NEWNS` | Mount points. The container has its own filesystem view via `pivot_root`. |
| **UTS** | `CLONE_NEWUTS` | Hostname and domain name. Each container can have its own hostname. |
| **IPC** | `CLONE_NEWIPC` | System V IPC and POSIX message queues. Prevents cross-container IPC leaks. |
| **Cgroup** | `CLONE_NEWCGROUP` | Cgroup root directory. Each container sees its own cgroup hierarchy. |
| **Network**| `CLONE_NEWNET` | Network stack. Isolated interfaces, routing, and firewall (NAT/None modes). |

### Network namespace isolation (`--net`)

Droidspaces has four networking modes, and the mode decides whether a network namespace (`CLONE_NEWNET`) is used:

1. **Host mode (`--net=host`), the default**: Droidspaces deliberately does **not** unshare the network namespace. The container shares the host's network stack, so it has internet access immediately, with no virtual bridge, NAT or firewall rules to set up. On Android, where networking is already complicated (cellular, Wi-Fi, VPN), this avoids a whole category of connectivity problems.

2. **NAT mode (`--net=nat`)**: The container gets a private network namespace, connected to the host through a virtual bridge or veth pair. It is fully isolated from the host's network, and still reaches the internet through the host's active uplink, which is detected automatically (or pinned manually with `--upstream`, see below). Works on the vast majority of Android devices.

3. **None mode (`--net=none`)**: The container gets a private, air-gapped network namespace with only the loopback interface up.

4. **Gateway mode (`--net=gateway`)**: The container's LAN is handed to *another* running container (typically OpenWRT). Droidspaces does only the L2 plumbing (bridge and veth pairs), and the gateway container owns all policy: DHCP, DNS, firewall, routing, VPN. Use it for VPN killswitches, segmented LANs and traffic analysis. The [Networking From Zero](Networking-From-Zero.md) guide covers it in full.

### How it compares to chroot

A `chroot` only changes the apparent root directory of a process. It gives no process, mount, hostname or IPC isolation. A process inside a chroot shares the host's PID space, can see and signal other processes, and cannot run an init system like systemd.

Droidspaces uses `pivot_root` instead of `chroot`, which isolates more. With private mount propagation (`MS_PRIVATE`) on top, the container's mount events are invisible to the host.

---

## Init system support

### Why init systems matter

Without an init system you are running individual processes in a chroot. There is no service management, no `systemctl`, no journald for logging and no proper session management. It is a shell with extra steps.

Droidspaces boots a real init system. When systemd starts as PID 1 inside the container:

- Services are managed via `systemctl start/stop/enable`
- Logs are available via `journalctl`
- User sessions work properly with `login`, `su`, and `sudo`
- Targets and dependencies are resolved correctly
- Timer units, socket activation, and all other systemd features work

### How Droidspaces enables it

systemd needs three things to work inside a container:

1. **PID 1:** The init process must be PID 1. Droidspaces creates a PID namespace (`CLONE_NEWPID`) and then forks, which makes the container's init the first process in its namespace.

2. **Container detection:** systemd needs to know it is running in a container. Droidspaces writes `droidspaces` to `/run/systemd/container` and sets the `container=droidspaces` environment variable.

3. **Cgroup access:** systemd needs write access to its cgroup hierarchy to create scopes and slices. Droidspaces gives each container its own cgroup tree (see [Cgroup Isolation](#cgroup-isolation)).

### Supported init systems

In theory Droidspaces works with **any init system** that can run as PID 1, including:

- **systemd** (most Linux distributions)
- **OpenRC** (Alpine Linux, Gentoo)
- **runit** (Void Linux, Devuan)
- **s6-init** (Alpine, various containers)
- **SysVinit** (Debian, Devuan)

The init binary must be at `/sbin/init`. If it is missing or not executable, Droidspaces refuses to boot the container, so that services and session management always have a working init.

---

## Volatile mode

### What is volatile mode?

Volatile mode (`--volatile` or `-V`) runs an ephemeral container: every change is kept in RAM and thrown away when the container stops. The original rootfs is never modified.

### How it works

Droidspaces uses **OverlayFS**, the union filesystem built into the Linux kernel:

- **Lower layer:** The original rootfs (mounted read-only if using the rootfs.img mode)
- **Upper layer:** A tmpfs-backed directory that captures all writes
- **Merged view:** The container sees a unified filesystem where reads come from the lower layer and writes go to the upper layer

When the container stops, the upper layer in RAM is discarded and the original rootfs is left as it was.

### Use cases

- **Testing:** Install packages, modify configurations, and verify changes without committing anything
- **Development:** Spin up a clean environment for each build
- **Security:** Guaranteed clean state on every boot
- **Experimentation:** Break things without consequences

### Usage

```bash
# Volatile container from a directory
droidspaces --name=test --rootfs=/path/to/rootfs --volatile start

# Volatile container from an image
droidspaces --name=test --rootfs-img=/path/to/rootfs.img --volatile start
```

### Known limitation: f2fs on Android

Most Android devices use f2fs for the `/data` partition, and OverlayFS on many Android kernels does not accept f2fs as a lower directory. So **volatile mode with a directory rootfs on f2fs will fail**.

**Workaround:** Use a rootfs image (`--rootfs-img`) instead. The ext4 loop mount gives OverlayFS a lower directory it accepts.

Droidspaces detects this incompatibility at runtime and prints a diagnostic message saying so.

---

## Hardware access mode

> [!CAUTION]
> Enabling Hardware Access Mode (`--hw-access`) exposes all host devices, including raw block devices, directly to the container. If a malicious process or accidental command targets these devices, it could permanently destroy your partition table, wipe your SD card, or brick your device. The developer(s) of Droidspaces is not responsible for any data loss or hardware damage that occurs as a result of using this feature. **Use at your own risk.**

### What it does

The `--hw-access` flag exposes the host's hardware devices to the container by mounting `devtmpfs` at `/dev` instead of a private `tmpfs`.

The container then has access to:

- **GPU** (for hardware-accelerated graphics via Turnip + Zink, Panfrost/Native GPU Acceleration in desktop for Intel and AMD)
- **Cameras**
- **Sensors**
- **USB devices**
- **Block Devices** (Partitions and physical disks)

### Security implications

Hardware access mode lets the container see **all** host devices, and talk to the GPU, USB controllers and other hardware directly. Use it only when you trust what is in the container and need the hardware.

The container's `/dev` is the kernel's devtmpfs, the same instance the host uses on Linux. Droidspaces never writes into it: the nodes it needs (`null`, `console`, `ptmx`, GPU nodes and so on) are created in a private tmpfs and bind-mounted over the devtmpfs paths inside the container's mount namespace. The one exception is `/dev/tty1` to `tty6`, which are masked with `/dev/null` by default because a systemd container starts `getty` on them and would put its login prompt on the host console. Pass `--allow-vts` to leave the host's virtual terminals visible.

### The systemd 258+ fix

systemd 258 hardened its container detection. It now checks whether `/sys` is mounted read-only to decide between a container and a physical machine. If `/sys` is read-write, systemd assumes it owns the hardware and tries to attach services such as `getty` to the physical TTYs (`tty1`-`tty6`). Those do not exist in the isolated container, so the services fail and the console has no login prompt.

> [!NOTE]
> This information is based on current developer understanding of systemd's behavior in Droidspaces and may require further verification.

Droidspaces handles this by "dynamic hole-punching":

1. **Pinning subsystems**: Every `/sys` subdirectory is bind-mounted onto itself, which keeps each hardware subsystem read-write.
2. **Read-only remount**: The top-level `/sys` is remounted read-only.
3. **Container identification**: systemd sees the read-only `/sys`, identifies the container environment correctly, and falls back to container-native console management.
4. **Hardware access**: Each hardware subsystem stays fully accessible through the pinned sub-mounts from step 1.

### Usage

```bash
droidspaces --name=gpu-test --rootfs=/path/to/rootfs --hw-access start
```

### Automatic GPU group setup

With `--hw-access` enabled, Droidspaces automatically:

1. **Scans host GPU devices**: Before `pivot_root`, it probes ~40 known GPU device paths (`/dev/dri/*`, `/dev/mali*`, `/dev/kgsl-3d0`, `/dev/nvidia*`, etc.) and collects their group IDs via `stat()`.
2. **Creates matching groups**: After `pivot_root`, it appends entries like `gpu_<GID>:x:<GID>:root` to the container's `/etc/group`. The container's root user is added to each group.
3. **Handles restarts idempotently**: On a container restart, existing groups are detected and skipped, so there are no duplicate entries.

No manual `groupadd`/`usermod` inside the container is needed.

### X11 socket mounting

For GUI applications, Droidspaces bind-mounts the X11 socket directory automatically:

- **Android (Termux X11):** Detects and mounts `/data/data/com.termux/files/usr/tmp/.X11-unix`
- **Desktop Linux:** Mounts `/tmp/.X11-unix` via `/proc/1/root/tmp/.X11-unix`

> [!TIP]
> X11 support can be enabled on its own with the `--termux-x11` (`-X`) flag. This is the recommended way to run GUI applications on Android if you do not need full GPU/hardware access, because the container stays more isolated.

Droidspaces injects `DISPLAY=:5` and (if VirGL is enabled) `GALLIUM_DRIVER=virpipe` into the container environment through `/run/droidspaces.env`, which is symlinked from `/etc/profile.d/droidspaces_env.sh`. Shells like `bash` and `sh` source it automatically. If you use `zsh`, `fish`, or another non-login shell, source it yourself: `source /run/droidspaces.env`.

### Supported GPU families

| Family | Device Paths |
|--------|-------------|
| **DRI** (Intel, AMD, Mesa) | `/dev/dri/renderD128-130`, `/dev/dri/card0-2` |
| **NVIDIA** (Proprietary) | `/dev/nvidia*`, `/dev/nvidia-uvm*`, `/dev/nvidia-caps/*` |
| **ARM Mali** | `/dev/mali`, `/dev/mali0`, `/dev/mali1` |
| **Qualcomm Adreno** | `/dev/kgsl-3d0`, `/dev/kgsl`, `/dev/genlock` |
| **AMD Compute** | `/dev/kfd` |
| **PowerVR** | `/dev/pvr_sync` |
| **NVIDIA Tegra** | `/dev/nvhost-ctrl`, `/dev/nvhost-gpu`, `/dev/nvmap` |
| **DMA Heaps** | `/dev/dma_heap/system`, `/dev/dma_heap/linux,cma`, `/dev/dma_heap/reserved`, `/dev/dma_heap/qcom,system` |
| **Sync** | `/dev/sw_sync` |

---

## Custom bind mounts

### What are bind mounts?

A bind mount maps a directory from the host filesystem to a location inside the container. The host directory is then visible and writable inside the container.

### Syntax

```bash
# Single mount
--bind-mount=/host/path:/container/path
-B /host/path:/container/path

# Multiple mounts (comma-separated)
-B /src1:/dst1,/src2:/dst2,/src3:/dst3

# Multiple mounts (chained)
-B /src1:/dst1 -B /src2:/dst2

# Mix and match
-B /src1:/dst1,/src2:/dst2 -B /src3:/dst3
```

### Limits

- Destination must be an **absolute path**
- Path traversal (`..`) in destinations is **rejected** for security

### Automatic directory creation

If the destination directory does not exist inside the rootfs, Droidspaces creates it with `mkdir -p`.

### Soft-fail model

If a host source path does not exist or a mount fails, Droidspaces prints a warning and skips that entry instead of failing the whole boot. A container still starts when an optional bind source is temporarily missing.

### Security

Droidspaces checks bind mount targets twice:

1. **Pre-mount:** Uses `lstat()` to ensure the target inside the rootfs is not a symlink
2. **Post-mount:** Uses `realpath()` via the `is_subpath()` helper to verify the mounted path cannot escape the container root

---

## Network isolation (4 modes)

Droidspaces has four networking modes, from shared-with-the-host to fully isolated.

### 1. Host mode (`--net=host`) - default

The container shares the host's network namespace.

- **Pros**: No configuration, internet access immediately, works with all Android VPNs/hotspots.
- **Cons**: No port isolation; services inside the container bind to host ports directly.

### 2. NAT mode (`--net=nat`)

The container gets a private network namespace (`CLONE_NEWNET`) and is connected to the host through a virtual bridge (`ds-br0`) or a direct veth pair.

- **Deterministic IP**: Each container is assigned a unique IP in the `172.28.0.0/16` range, derived from its PID.
- **Embedded DHCP**: Droidspaces includes a minimal, built-in DHCP server to automatically configure the container's `eth0`.
- **Full isolation**: The container cannot see or interact with the host's network interfaces directly.
- **Automatic uplink detection**: No configuration needed. Droidspaces asks the kernel which interface provides internet access: on Android, the policy-routing rule netd installs for the active default network; on standard Linux, the main routing table's default route. CLAT (464xlat) interfaces on IPv6-only mobile networks are handled automatically.

> [!IMPORTANT]
> NAT mode is **IPv4 only**. If the host's uplink has no IPv4 address (IPv6-only network), internet access will not work.

### 3. None mode (`--net=none`)

The container gets a private network namespace with only the loopback (`lo`) interface up.

- **Use case**: Offline tasks where the container should have no network at all.

### 4. Gateway mode (`--net=gateway`)

The container sits on an isolated L2 bridge whose **policy is owned by another running container** (typically OpenWRT) instead of by Droidspaces. Droidspaces does only the plumbing: it creates the bridge and the veth pairs and moves them into place. The gateway container provides DHCP, DNS, firewall, routing and VPN.

- **Required flag**: `--gateway=NAME` names the running container that acts as the router.
- **Segments**: `--gateway-net=NAME` (default `lan`) selects which bridge/segment the client lands on. Clients sharing a `--gateway-net` share a LAN; different `--gateway-net` values are isolated segments through the same gateway.
- **Interface naming**: `--gateway-iface=IFACE` (default `eth1`) sets what the LAN interface is called *inside* the gateway container, so it matches the gateway's own config.
- **Self-healing**: Wiring is driven entirely from the host side, so clients are (re)wired automatically when the gateway container starts or reboots. No client restart is needed.
- **Use cases**: VPN killswitch for selected containers, VLAN-style segmented LANs, single-chokepoint traffic analysis, gateway-wide DNS filtering. See [Networking From Zero](Networking-From-Zero.md) for the complete walkthrough.

### Port forwarding (NAT mode)

In NAT mode, the `--port` flag exposes container services to the host or the local network. Supported formats:

```bash
# Forward host port 8080 to container port 80
--port 8080:80

# Symmetric shorthand (host 8080 -> container 8080)
--port 8080

# Forward host range to container range (must be same size)
--port 1000-2000:1000-2000

# Mix and match with explicit protocols
--port 2222:22/tcp --port 5000-5050:5000-5050/udp
```

Forwarded ports are reachable from any network the host is on, including clients on the phone's own hotspot or USB tethering on Android.

### Real-time uplink monitoring

On Android the connection often moves between Wi-Fi and mobile data. Droidspaces runs a **Route Monitor** that subscribes to kernel routing events (FIB rules, routes, links, addresses). As soon as Android switches its default network (you walk out of Wi-Fi range, for example), the monitor updates the kernel's policy routing to keep the container connected, with no configuration and no restart. The same monitor works on desktop Linux (a Wi-Fi to ethernet handoff, for example), where it follows the main routing table's default route.

### Manual uplink pinning (`--upstream`)

By default the uplink is chosen automatically. To make the container's WAN **ignore the host's active network** and go out through a specific interface, pin it with `--upstream`. This turns auto-detection off completely: the listed interface(s) become the *only* WAN candidates.

```bash
# Single interface
--upstream=wlan0

# Priority-ordered list with wildcards (comma-separated)
--upstream=wlan0,rmnet*
```

- **Authoritative, not a fallback**: Traffic never moves to whatever `netd` marks active, only to interfaces you listed.
- **Priority failover *within* the list**: The Route Monitor re-resolves on every link/route change and uses the first listed interface that is up and has internet. `wlan0,rmnet*` prefers Wi-Fi, falls back to mobile data, and returns to Wi-Fi when it comes back.
- **Literals and wildcards** (`*`, `?`): Use `rmnet*` for mobile data, whose interface number is not stable across reconnects.
- **Interfaces that disappear and reappear** mid-session are handled: there is no WAN until a pinned interface is up, then it is wired automatically.
- **Use cases**: Pin `tun0` to route the container only through a phone-side VPN (a free killswitch), or pin `rmnet*` (with "Mobile data always active") to keep the container on cellular while the phone stays on Wi-Fi.

> `--upstream` is only valid with `--net=nat`; it is ignored (with a warning) in other modes.

---

## Rootfs image support

### Why use images?

A directory rootfs is simple, but it has limitations:

- File permissions may not be preserved correctly on some filesystems (especially f2fs on Android)
- OverlayFS may not be compatible with the underlying filesystem

Ext4 images solve both. The image file holds a complete ext4 filesystem that is loop-mounted at runtime, so it behaves the same whatever the host filesystem is. Images also give you:

- **Built-in integrity checking**: Images can be verified with `e2fsck` at runtime.
- **Portability**: The whole container is one `.img` file, which makes it easy to back up, share, or carry to another device. Copy the file to any device with Droidspaces and it boots.

### How it works

When you use `--rootfs-img`:

1. **Filesystem check:** Droidspaces runs `e2fsck -f -y` on the image to ensure integrity
2. **SELinux context:** On Android, applies the `vold_data_file` SELinux context to prevent silent I/O denials
3. **Loop mount:** The image is mounted at `/mnt/Droidspaces/<name>`
4. **Retry logic:** On kernel 4.14, mounts may fail due to stale loop device state. Droidspaces retries up to 3 times with `sync()` and settle delays.

### Usage

```bash
# Image-based container (--name is mandatory)
droidspaces --name=ubuntu --rootfs-img=/path/to/rootfs.img start

# Volatile mode with image (image mounted read-only)
droidspaces --name=ubuntu --rootfs-img=/path/to/rootfs.img --volatile start
```

---

## Cgroup isolation

### What it does

Droidspaces creates a cgroup tree per container at `/sys/fs/cgroup/droidspaces/<name>` on the host. Together with the cgroup namespace, each container sees its own clean cgroup hierarchy.

**Note:** Cgroup isolation is not available in `--force-cgroupv1` mode.

### Why it matters

systemd relies on cgroups for:

- Creating service scopes and slices
- Resource accounting (CPU, memory per service)
- Process tracking (knowing which processes belong to which service)
- Clean shutdown (killing all processes in a service's cgroup)

Without cgroup isolation systemd cannot work: containers would collide in the cgroup hierarchy and service management would fail.

### The "jail" trick

Before creating the cgroup namespace, Droidspaces moves the monitor process into the container's own cgroup. When `unshare(CLONE_NEWCGROUP)` is then called, the root of the new namespace maps to the container's subtree.

### Cgroup v1 and v2 support

Droidspaces supports both cgroup versions:

- **Cgroup v2 (unified):** Used by modern distributions. Mounted as a single hierarchy.
- **Cgroup v1 (legacy):** Used by older distributions. Droidspaces handles comounted controllers (e.g., `cpu,cpuacct`) and creates symlinks for secondary names in older kernels or `--force-cgroupv1` mode.

### Forcing legacy cgroup v1 (`--force-cgroupv1`)

On legacy Android kernels (3.18, 4.4, or 4.9), the host may have no cgroup v2 support at all, or a partial one without the controllers (CPU, memory, etc.) that modern `systemd` needs. That often makes `systemd` misidentify the environment and fail to boot.

The `--force-cgroupv1` flag is an escape hatch for experts. It makes Droidspaces use only the legacy v1 hierarchy, even if v2 appears to be available on the host. Distributions with modern `systemd` versions then run reliably on older kernels.

### The `su` fix

When you enter a container with `enter` or `run`, the process has to be in the container's host-side cgroup before it joins the namespaces. Otherwise `systemd-logind` and `sd-pam` inside the container cannot map the process to a valid session, and `su` and `sudo` hang. Droidspaces attaches to the container's cgroup before any `setns()` call, so this is handled for you.

---

## Adaptive security

Droidspaces uses BPF seccomp filters to work around Android kernel conflicts:

### 1. FBE keyring conflict (automatic)

Android's File-Based Encryption keeps filesystem keys in the kernel's session keyring. When systemd creates new session keyrings, the process loses access to the host's encryption keys and gets `ENOKEY` errors.

**Solution:** On legacy kernels (< 5.0), Droidspaces *automatically* intercepts the keyring syscalls (`keyctl`, `add_key`, `request_key`) and returns `ENOSYS`, which makes systemd use the existing keyring.

> [!TIP]
>
> **Legacy Kernel Networking:** When running Docker/Podman inside Droidspaces on legacy kernels, modern `nftables` may fail to route traffic. We recommend using Droidspaces' NAT mode and switching your container's networking stack to `iptables-legacy` and `ip6tables-legacy`.


<a id="sandboxing"></a>

## Sandboxing (`--allow-sandboxing`)

Off by default. Turn it on when something inside the container needs to build its own sandbox: unprivileged Docker or Podman (`userns-remap`, rootless), Flatpak, bwrap, Firefox and Chromium. All of them create a user namespace and then mount their own `proc` and `sysfs` inside it, and a stock Droidspaces container blocks both steps.

### What it changes

1. **User namespaces are allowed.** The seccomp filter stops returning `EPERM` for `unshare(CLONE_NEWUSER)` and `clone(CLONE_NEWUSER)`, and `clone3` is no longer hidden.
2. **A pristine `proc` and a read-only `sysfs` are mounted under `/run/droidspaces/`.** The kernel lets a child user namespace mount `proc` or `sysfs` only if some instance of that filesystem in the mount namespace is "fully visible": the root of the filesystem, nothing bind-mounted over a real file inside it, and not read-only when the new mount is read-write. The container's own `/proc` and `/sys` never qualify, because the jail masks and the virtualized `uptime`, `loadavg`, `meminfo` and friends are exactly such bind mounts. Any instance anywhere satisfies the rule, so Droidspaces adds one out of the way. LXC does the same in `nesting.conf` with `/dev/.lxc/proc` and `/dev/.lxc/sys`. The masks and the virtualized files stay where they were.
3. **User namespace limits are writable at their standard path.** Bubblewrap opens `/proc/sys/user/max_user_namespaces` before entering its child user namespace when it disables further nesting. Droidspaces binds the corresponding subtree from the pristine `proc` over `/proc/sys/user` so Flatpak and other bubblewrap users can apply that limit.
4. **`CAP_SYS_PTRACE` stays in the bounding set.** runc opens `/proc/<pid>/ns/net` and `/proc/<pid>/ns/mnt` of a container init that has already switched to the remapped uid, and root only gets that read on another uid's process through this capability.

### What it costs

The pristine `proc` has to be writable and unmasked, or the kernel would not count it. So `/run/droidspaces/proc/sys/` is the live host sysctl tree and `/run/droidspaces/proc/sysrq-trigger` is real. Nothing writes there by accident. `/proc/sys` at its normal path remains read-only except for `/proc/sys/user`, which exposes the same live sysctls needed by bubblewrap. Root in the container can already reach them through the pristine `proc`. Treat the toggle as trusting the container's root user. The `sysfs` copy is read-only and exposes nothing new.

### Usage

```bash
droidspaces --name=mycontainer --rootfs=/path/to/rootfs --allow-sandboxing start
```

In the app it is the **Allow Sandboxing** toggle under Security. It is greyed out when the kernel was built without `CONFIG_USER_NS`, which `droidspaces check` reports as "Sandboxing (user namespaces)". `--allow-userns` and the `allow_userns=` config key are the old names and still work.

Quick check from inside a running container:

```bash
grep CapBnd /proc/1/status          # bit 19 set
bwrap --unshare-user --proc /proc --dev /dev --ro-bind / / true
```

For Docker, add `{"userns-remap": "default"}` to `/etc/docker/daemon.json`, restart the daemon, and `docker run --rm alpine cat /proc/self/uid_map` should print `0 100000 65536`.

---

## Android-specific tuning

The Android kernel is opinionated. For containers to be stable, reach the network and use hardware on it, the container's rootfs needs several adjustments.

> [!NOTE]
>
> The Droidspaces backend itself does not alter the rootfs. These changes are applied automatically when the user installs a new rootfs tarball using the Android App's built-in installer, or are pre-baked when using our official rootfs tarball from the [Droidspaces rootfs-builder](https://github.com/Droidspaces/Droidspaces-rootfs-builder).

### 1. Android network & hardware groups

Older Android kernels allow network socket creation and direct hardware access only to specific, hardcoded group IDs (GIDs). Droidspaces maps and configures these groups inside the container's rootfs:

- **GID mapping**: Appends Android-specific groups to `/etc/group`:
  - `aid_inet` (3003): Allows internet access.
  - `aid_net_raw` (3004): Allows raw socket creation (e.g., for `ping`).
  - `aid_net_admin` (3005): Allows network administration.
- **Permissions assignment**: Adds the container's `root` user to the `aid_inet`, `aid_net_raw`, `input`, `video`, and `tty` groups.
- **Package manager fix**: Sets `aid_inet` as the primary group of the Debian/Ubuntu `_apt` user, so packages install and update without permission errors.
- **New users**: Modifies `/etc/adduser.conf` so any newly created user gets these groups automatically.

### 2. Udev trigger & service overrides

Standard Linux distributions run `udevadm trigger` during boot to coldplug hardware devices. Triggering every subsystem at once on an Android device can panic the kernel.

- **Hardware access guards**: udev services are only useful when hardware access is enabled, so Droidspaces injects a drop-in `ExecCondition` override that stops `systemd-udevd.service`, `systemd-udev-trigger.service`, and `systemd-udev-settle.service` from starting unless the container is configured with hardware access (`enable_hw_access=1`):
  ```ini
  [Service]
  ExecCondition=
  ExecCondition=/bin/sh -c "grep -q 'enable_hw_access=1' /run/droidspaces/container.config"
  ```
- **Safe udev trigger**: Instead of scanning everything, Droidspaces overrides the default `systemd-udev-trigger.service` with a drop-in. With hardware access enabled, the trigger is limited to a fixed, safe subset of subsystems:
  ```ini
  [Service]
  ExecStart=
  ExecStart=-/usr/bin/udevadm trigger --subsystem-match=usb --subsystem-match=block --subsystem-match=input --subsystem-match=tty --subsystem-match=net
  ```
  The container still detects new USB drives, keyboards and network interfaces, without the risk of crashing the host.
- **Read-only path fix**: Overrides `ConditionPathIsReadWrite` for all udev units, so they do not fail where key system directories are mounted read-only.

### 3. Optimizing systemd & logging

The Android kernel logs a lot. Without tuning, a standard `journald` setup reads the host's kernel messages and writes gigabytes of logs, which fills the device's internal storage quickly:

- **Journald adjustments**: Turns off reading kernel messages and system auditing (`ReadKMsg=no`, `Audit=no`) in `journald.conf`, so the container does not collect system-wide kernel logs.
- **Volatile storage**: Keeps systemd journal logs in memory only (`Storage=volatile`) with a hard size limit (200MB), so constant writes do not wear out and fill the device's internal flash.
- **Service masking**: Masks `systemd-networkd-wait-online.service` to prevent boot delays, and `systemd-journald-audit.socket` to prevent systemd deadlocks in old kernels like 4.9.
- **Power key handling**: Tells `systemd-logind` to ignore host power and suspend key events, so the container does not try to handle host power state changes.

### 4. NAT mode network guards

In host networking mode, network managers like `NetworkManager` or `systemd-networkd` running inside the container can fight with the Android host's routing tables and break cellular/Wi-Fi connectivity.

Droidspaces injects a drop-in `ExecCondition` override for the standard network services (such as `NetworkManager.service`, `systemd-networkd.service`, `dhcpcd.service`, and `systemd-resolved.service`), so they run only when the container is configured in NAT mode:

```ini
[Service]
ExecCondition=
ExecCondition=/bin/sh -c "grep -q 'net_mode=nat' /run/droidspaces/container.config"
```

### 5. Storage and DHCP configuration

- **systemd-networkd config**: Configures `10-eth-dhcp.network` to enable DHCP and IPv6 route acceptance for any `eth*` interfaces.
- **Logrotate limit**: Sets a `maxsize 50M` limit in `/etc/logrotate.conf` so logs do not take up too much disk space over time.
