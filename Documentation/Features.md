<!--
title: Feature Deep Dives
section: Guides
order: 1
desc: Deep dive into every Droidspaces feature: namespace isolation, init system support, OverlayFS volatile mode, GPU acceleration, cgroup isolation, seccomp shields, and Android-specific tuning.
keywords: droidspaces, features, namespace, isolation, cgroup, overlayfs, volatile, mode, init, system, support, gpu, acceleration
-->

# Feature Deep Dives

Detailed explanations of each major Droidspaces feature and how it works under the hood.

---

## Namespace Isolation

### What Are Namespaces?

Linux namespaces are a kernel feature that partitions system resources so that each group of processes sees its own isolated set of resources. Droidspaces uses five namespaces to create isolated containers:

| Namespace | Flag | What It Isolates |
|-----------|------|-----------------|
| **PID** | `CLONE_NEWPID` | Process IDs. The container gets its own PID tree where init is PID 1. |
| **MNT** | `CLONE_NEWNS` | Mount points. The container has its own filesystem view via `pivot_root`. |
| **UTS** | `CLONE_NEWUTS` | Hostname and domain name. Each container can have its own hostname. |
| **IPC** | `CLONE_NEWIPC` | System V IPC and POSIX message queues. Prevents cross-container IPC leaks. |
| **Cgroup** | `CLONE_NEWCGROUP` | Cgroup root directory. Each container sees its own cgroup hierarchy. |
| **Network**| `CLONE_NEWNET` | Network stack. Isolated interfaces, routing, and firewall (NAT/None modes). |

### Network Namespace Isolation (`--net`)

Droidspaces supports three networking modes that determine whether a network namespace (`CLONE_NEWNET`) is used:

1. **Host Mode (`--net=host`) - Default**: Droidspaces deliberately does **not** unshare the network namespace. The container shares the host's network stack. This greatly simplifies setup: containers get internet access immediately without virtual bridges, NAT, or firewall rules. On Android, where networking is already complex (cellular, Wi-Fi, VPN), this avoids a whole category of connectivity issues.

2. **NAT Mode (`--net=nat`)**: The container is placed in a private network namespace. It is connected to the host via a virtual bridge or veth pair, providing **Pure Network Isolation** while maintaining internet access through the host's active internet uplink, which is detected automatically. Compatible with the vast majority of Android devices.

3. **None Mode (`--net=none`)**: The container is placed in a private, air-gapped network namespace with only the loopback interface enabled for maximum security.

### How It Compares to Chroot

A `chroot` only changes the apparent root directory for a process. It provides no process isolation, no mount isolation, no hostname isolation, and no IPC isolation. Any process inside a chroot shares the host's PID space, can see and signal other processes, and cannot run an init system like systemd.

Droidspaces uses `pivot_root` instead of `chroot`, which is a stronger isolation mechanism. Combined with private mount propagation (`MS_PRIVATE`), the container's mount events are completely invisible to the host.

---

## Init System Support

### Why Init Systems Matter

Without an init system, you're running individual processes in a chroot. You can't manage services, you can't use `systemctl`, you don't have journald for logging, and you don't have proper session management. It's a glorified shell.

Droidspaces boots a real init system. When systemd starts as PID 1 inside the container:

- Services are managed via `systemctl start/stop/enable`
- Logs are available via `journalctl`
- User sessions work properly with `login`, `su`, and `sudo`
- Targets and dependencies are resolved correctly
- Timer units, socket activation, and all other systemd features work

### How Droidspaces Enables It

Three things are required for systemd to function inside a container:

1. **PID 1:** The init process must be PID 1. Droidspaces achieves this with a PID namespace (`CLONE_NEWPID`) followed by a fork, making the container's init the first process in its namespace.

2. **Container detection:** Systemd needs to know it's running inside a container. Droidspaces writes `droidspaces` to `/run/systemd/container` and sets the `container=droidspaces` environment variable.

3. **Cgroup access:** Systemd requires write access to its cgroup hierarchy to create scopes and slices. Droidspaces provides this through per-container cgroup trees (see [Cgroup Isolation](#cgroup-isolation)).

### Supported Init Systems

Droidspaces is theoretically compatible with **any init system** that can run as PID 1, including:

- **systemd** (most Linux distributions)
- **OpenRC** (Alpine Linux, Gentoo)
- **runit** (Void Linux, Devuan)
- **s6-init** (Alpine, various containers)
- **SysVinit** (Debian, Devuan)

The init binary is strictly expected at `/sbin/init`. If this binary is missing or not executable, Droidspaces will fail to boot the container to ensure that services and session management function as expected.

---

## Volatile Mode

### What Is Volatile Mode?

Volatile mode (`--volatile` or `-V`) creates an ephemeral container where all modifications are stored in RAM and discarded when the container stops. The original rootfs is never modified.

### How It Works

Droidspaces uses **OverlayFS**, a union filesystem built into the Linux kernel:

- **Lower layer:** The original rootfs (mounted read-only if using the rootfs.img mode)
- **Upper layer:** A tmpfs-backed directory that captures all writes
- **Merged view:** The container sees a unified filesystem where reads come from the lower layer and writes go to the upper layer

When the container stops, the upper layer (in RAM) is discarded. The original rootfs remains untouched.

### Use Cases

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

### Known Limitation: f2fs on Android

Most Android devices use f2fs for the `/data` partition. OverlayFS on many Android kernels does not support f2fs as a lower directory. This means **volatile mode with a directory rootfs on f2fs will fail**.

**Workaround:** Use a rootfs image (`--rootfs-img`) instead. The ext4 loop mount provides a compatible lower directory for OverlayFS.

Droidspaces detects this incompatibility at runtime and provides a clear diagnostic message.

---

## Hardware Access Mode

> [!CAUTION]
> Enabling Hardware Access Mode (`--hw-access`) exposes all host devices, including raw block devices, directly to the container. If a malicious process or accidental command targets these devices, it could permanently destroy your partition table, wipe your SD card, or brick your device. The developer(s) of Droidspaces is not responsible for any data loss or hardware damage that occurs as a result of using this feature. **Use at your own risk.**

### What It Does

The `--hw-access` flag exposes the host's hardware devices to the container by mounting `devtmpfs` instead of a private `tmpfs` at `/dev`.

This gives the container access to:
- **GPU** (for hardware-accelerated graphics via Turnip + Zink, Panfrost/Native GPU Acceleration in desktop for Intel and AMD)
- **Cameras**
- **Sensors**
- **USB devices**
- **Block Devices** (Partitions and physical disks)

### Security Implications

Hardware access mode grants the container visibility to **all** host devices. The container can interact with the GPU, USB controllers, and other hardware directly. Only use this mode when you trust the container's contents and need hardware access.

### The systemd 258+ Fix

Starting with systemd 258, the container detection logic was hardened. systemd now checks whether `/sys` is mounted read-only to determine if it's running in a container versus a physical machine. If `/sys` is read-write, systemd assumes it has full hardware authority and attempts to attach services (like `getty`) to physical TTYs (`tty1`-`tty6`). Since these do not exist in the isolated container environment, the services fail to start, leaving the console without a login prompt.

> [!NOTE]
> This information is based on current developer understanding of systemd's behavior in Droidspaces and may require further verification.

Droidspaces handles this with a "dynamic hole-punching" technique:

1. **Pinning Subsystems**: All `/sys` subdirectories are self-bind-mounted to preserve read-write access to individual hardware subsystems.
2. **Read-Only Remount**: The top-level `/sys` is remounted read-only.
3. **Container Identification**: systemd detects the read-only `/sys`, correctly identifies the container environment, and falls back to container-native console management.
4. **Hardware Access**: Individual hardware subsystems remain fully accessible via the pinned sub-mounts created in step 1.

### Usage

```bash
droidspaces --name=gpu-test --rootfs=/path/to/rootfs --hw-access start
```

### Automatic GPU Group Setup

When `--hw-access` is enabled, Droidspaces automatically:

1. **Scans host GPU devices** - Before `pivot_root`, it probes ~40 known GPU device paths (`/dev/dri/*`, `/dev/mali*`, `/dev/kgsl-3d0`, `/dev/nvidia*`, etc.) and collects their group IDs via `stat()`. **Dangerous nodes like `/dev/dri/card*` are explicitly skipped** to prevent host kernel panics, as these nodes are restricted to the host's display manager.
2. **Creates matching groups** - After `pivot_root`, it appends entries like `gpu_<GID>:x:<GID>:root` to the container's `/etc/group`. The container's root user is automatically added to each group.
3. **Idempotent restarts** - On container restart, existing groups are detected and skipped (no duplicate entries).

This eliminates the need for manual `groupadd`/`usermod` commands inside the container, while ensuring the host's kernel stability by avoiding restricted hardware paths.

### X11 Socket Mounting

For GUI application support, Droidspaces automatically bind-mounts the X11 socket directory:

- **Android (Termux X11):** Detects and mounts `/data/data/com.termux/files/usr/tmp/.X11-unix`
- **Desktop Linux:** Mounts `/tmp/.X11-unix` via `/proc/1/root/tmp/.X11-unix`

> [!TIP]
> X11 support can be enabled independently using the `--termux-x11` (`-X`) flag. This is the recommended way to use GUI applications on Android if you do not need full GPU/hardware access, as it preserves a higher level of isolation.


Droidspaces automatically injects `DISPLAY=:5` and (if VirGL is enabled) `GALLIUM_DRIVER=virpipe` into the container environment via `/run/droidspaces.env`, symlinked from `/etc/profile.d/droidspaces_env.sh`. Shells like `bash` and `sh` source this automatically. If you use `zsh`, `fish`, or another non-login shell, source it manually: `source /run/droidspaces.env`.

### Supported GPU Families

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

## Custom Bind Mounts

### What Are Bind Mounts?

Bind mounts allow you to map a directory from the host filesystem into the container at a specified location. The host directory becomes visible and writable inside the container.

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

### Automatic Directory Creation

If the destination directory doesn't exist inside the rootfs, Droidspaces creates it automatically using `mkdir -p`.

### Soft-Fail Model

If a host source path doesn't exist or a mount fails, Droidspaces issues a warning and skips the entry rather than failing the entire boot. This allows containers to start even if optional bind sources are temporarily unavailable.

### Security

Droidspaces validates bind mount targets with two protections:
1. **Pre-mount:** Uses `lstat()` to ensure the target inside the rootfs is not a symlink
2. **Post-mount:** Uses `realpath()` via the `is_subpath()` helper to verify the mounted path cannot escape the container root

---

## Network Isolation (3 Modes)

Droidspaces provides three distinct networking modes to balance ease-of-use with advanced isolation.

### 1. Host Mode (`--net=host`) - Default
The container shares the host's network namespace.
- **Pros**: Zero configuration, instant internet access, works with all Android VPNs/hotspots.
- **Cons**: No port isolation; services inside the container bind to host ports directly.

### 2. NAT Mode (`--net=nat`)
The container is placed in a private network namespace (`CLONE_NEWNET`) and connected to the host via a virtual bridge (`ds-br0`) or a direct veth pair.
- **Deterministic IP**: Each container is assigned a unique IP in the `172.28.0.0/16` range, derived from its PID.
- **Embedded DHCP**: Droidspaces includes a minimal, built-in DHCP server to automatically configure the container's `eth0`.
- **Pure Isolation**: The container cannot see or interact with the host's network interfaces directly.
- **Automatic Uplink Detection**: No configuration needed. Droidspaces reads the kernel's own ground truth to find the interface that provides internet access - on Android, the policy-routing rule netd installs for the active default network; on standard Linux, the main routing table's default route. CLAT (464xlat) interfaces on IPv6-only mobile networks are handled automatically.

> [!IMPORTANT]
> NAT mode is **IPv4 only**. If the host's uplink lacks an IPv4 address (IPv6-only network), internet access will not work. See [IPv4 NAT Quirks](Troubleshooting.md#ipv4-quirks) for a workaround.

### 3. None Mode (`--net=none`)
The container gets a private network namespace with only the loopback (`lo`) interface enabled.
- **Use Case**: Maximum security for offline tasks.

### Port Forwarding (NAT Mode)

In NAT mode, you can expose container services to the host or local network using the `--port` flag. Supported formats:

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

Forwarded ports are reachable from any network the host belongs to - including clients connected to the phone's own hotspot or USB tethering on Android.


### Real-Time Uplink Monitoring
On Android, the connection often hops between Wi-Fi and Mobile Data. Droidspaces includes a **Route Monitor** that subscribes to kernel routing events (FIB rules, routes, links, addresses). The moment Android switches its default network (e.g., you walk out of Wi-Fi range), the monitor updates the kernel's policy routing to keep the container connected - no configuration, no restart.

---

## Rootfs Image Support

### Why Use Images?

Directory-based rootfs setups are simple but have limitations:
- File permissions may not be preserved correctly on some filesystems (especially f2fs on Android)
- OverlayFS may not be compatible with the underlying filesystem
- **Built-in Integrity Checking**: Images can be verified with `e2fsck` at runtime.
- **Portability**: Your entire container is encapsulated in a single `.img` file. This makes it incredibly easy to back up, share, or travel with across the world. Just copy the file to any device with Droidspaces, and it's ready to boot.

Ext4 images solve these problems. The image file contains a complete ext4 filesystem that's loop-mounted at runtime, providing consistent behavior regardless of the host filesystem.

### How It Works

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

## Cgroup Isolation

### What It Does

Droidspaces creates per-container cgroup trees at `/sys/fs/cgroup/droidspaces/<name>` on the host. Combined with the cgroup namespace, each container sees its own clean cgroup hierarchy.

**Note:** Cgroup isolation is not available in `--force-cgroupv1` mode.

### Why It Matters

systemd relies heavily on cgroups for:
- Creating service scopes and slices
- Resource accounting (CPU, memory per service)
- Process tracking (knowing which processes belong to which service)
- Clean shutdown (killing all processes in a service's cgroup)

Without proper cgroup isolation, systemd cannot function. Multiple containers would collide in the cgroup hierarchy, and service management would fail.

### The "Jail" Trick

Before creating the cgroup namespace, Droidspaces moves the monitor process into the container-specific cgroup. This ensures that when `unshare(CLONE_NEWCGROUP)` is called, the new namespace's root maps to the container's subtree.

### Cgroup v1 and v2 Support

Droidspaces supports both cgroup versions:

- **Cgroup v2 (unified):** Used by modern distributions. Mounted as a single hierarchy.
- **Cgroup v1 (legacy):** Used by older distributions. Droidspaces handles comounted controllers (e.g., `cpu,cpuacct`) and creates symlinks for secondary names in older kernels or `--force-cgroupv1` mode.

### Forcing Legacy Cgroup V1 (`--force-cgroupv1`)

On legacy Android kernels (3.18, 4.4, or 4.9), the host system may either lack Cgroup v2 support entirely or provide a partial implementation without the essential controllers (CPU, memory, etc.) required by modern `systemd`. This inconsistency often causes `systemd` to misidentify the environment, leading to critical boot failures.

The `--force-cgroupv1` flag acts as an **expert escape hatch**. It instructs Droidspaces to strictly utilize the legacy v1 hierarchy even if v2 appears available on the host. This ensures maximum stability and compatibility for distributions using modern `systemd` versions on older kernel infrastructure.

### The `su` Fix

When entering a container with `enter` or `run`, the process must be in the container's host-side cgroup before joining namespaces. Otherwise, `systemd-logind` and `sd-pam` inside the container cannot map the process to a valid session, causing `su` and `sudo` to hang. Droidspaces handles this automatically by attaching to the container's cgroup before any `setns()` call.

---

## Adaptive Security & Deadlock Shield

Droidspaces includes sophisticated BPF-based seccomp filters to resolve critical Android kernel conflicts:

### 1. FBE Keyring Conflict (Automatic)
Android's File-Based Encryption stores filesystem keys in the kernel's session keyring. When systemd attempts to create new session keyrings, the process loses access to the host's encryption keys, causing `ENOKEY` errors.

**Solution:** On legacy kernels (< 5.0), Droidspaces *automatically* intercepts keyring syscalls (`keyctl`, `add_key`, `request_key`) returning `ENOSYS`, forcing systemd to use the existing keyring.

<a id="vfs-deadlock"></a>

### 2. VFS Namespace Deadlock (Manual Opt-in)
On certain devices with legacy kernels (notably 4.14.113, common on 2019-2020 Android devices), systemd's service sandboxing triggers a race condition in the kernel's VFS layer (`grab_super()` bug). This causes systemd to hang, `systemctl` to freeze, and potential device lockups. 4.9 and 4.19 kernels are largely unaffected.

**The Fix:** You can manually enable the **Deadlock Shield** (in the Android App config or via `--block-nested-namespaces` CLI). This intercepts `unshare` and `clone` namespace requests with `EPERM`, preventing systemd from triggering the deadlock.

### Nested Containers (Docker, Podman, LXC)

Because the Deadlock Shield is now strictly an **opt-in toggle** rather than a hard-coded blanket ban:
- **Native Support:** Users on all kernels can now run Docker, Podman, and LXC natively out-of-the-box.
- **The Trade-off:** If your device requires the Deadlock Shield to boot systemd, enabling it will intentionally block the namespace creations required by Docker/Podman.

> [!TIP]
>
> **Legacy Kernel Networking:** When running Docker/Podman inside Droidspaces on legacy kernels, modern `nftables` may fail to route traffic. We recommend using Droidspaces' NAT mode and switching your container's networking stack to `iptables-legacy` and `ip6tables-legacy`.


---

## Android-Specific Tuning

To handle the "opinionated" nature of the Android Linux kernel and ensure container stability, network connectivity, and hardware access, several adjustments must be applied to the container's rootfs.

> [!NOTE]
>
> The Droidspaces backend itself does not alter the rootfs. These changes are applied automatically when the user installs a new rootfs tarball using the Android App's built-in installer, or are pre-baked when using our official rootfs tarball from the [Droidspaces rootfs-builder](https://github.com/Droidspaces/Droidspaces-rootfs-builder).

### 1. Android Network & Hardware Groups

Older Android kernels restrict network socket creation and direct hardware access to specific, hardcoded Group IDs (GIDs). Droidspaces maps and configures these groups inside the container's rootfs:

- **GID Mapping**: Appends Android-specific groups to `/etc/group`:
  - `aid_inet` (3003): Allows internet access.
  - `aid_net_raw` (3004): Allows raw socket creation (e.g., for `ping`).
  - `aid_net_admin` (3005): Allows network administration.
- **Permissions Assignment**: Adds the container's `root` user to the `aid_inet`, `aid_net_raw`, `input`, `video`, and `tty` groups.
- **Package Manager Fix**: Configures the Debian/Ubuntu `_apt` user to use `aid_inet` as its primary group, allowing packages to be installed and updated without permission errors.
- **User Automation**: Modifies `/etc/adduser.conf` so any newly created user automatically inherits these groups.

### 2. Udev Trigger & Service Overrides

Standard Linux distributions run `udevadm trigger` to coldplug hardware devices during boot. Triggering all subsystems simultaneously on an Android device can cause the kernel to panic.

- **Hardware Access Guards**: Since udev services are only useful when hardware access is explicitly enabled, Droidspaces injects a drop-in `ExecCondition` override that prevents `systemd-udevd.service`, `systemd-udev-trigger.service`, and `systemd-udev-settle.service` from starting unless the container is configured with hardware access (`enable_hw_access=1`):
  ```ini
  [Service]
  ExecCondition=
  ExecCondition=/bin/sh -c "grep -q 'enable_hw_access=1' /run/droidspaces/container.config"
  ```
- **Safe Udev Trigger**: Instead of scanning everything, Droidspaces overrides the default `systemd-udev-trigger.service` using a drop-in configuration. If hardware access is enabled, this limits the trigger to a strictly defined, safe subset of subsystems:
  ```ini
  [Service]
  ExecStart=
  ExecStart=-/usr/bin/udevadm trigger --subsystem-match=usb --subsystem-match=block --subsystem-match=input --subsystem-match=tty --subsystem-match=net
  ```
  This allows the container to dynamically detect new USB drives, keyboards, and network interfaces without risking a host crash.
- **Read-Only Path Fix**: Overrides `ConditionPathIsReadWrite` for all udev units to prevent failures in environments where key system directories are mounted read-only.

### 3. Optimizing systemd & Logging

The Android kernel is notoriously verbose. Without tuning, standard `journald` setups would read host kernel messages and generate gigabytes of logs, quickly filling up the device's internal storage:

- **Journald Adjustments**: Disables reading kernel messages and system auditing (`ReadKMsg=no`, `Audit=no`) in `journald.conf` to prevent the container from hoarding system-wide kernel logs.
- **Volatile Storage**: Configures systemd journal logs to store in-memory only (`Storage=volatile`) and enforces strict maximum size constraints (200MB) to prevent constant writes from wearing out and filling the device's physical internal flash storage.
- **Service Masking**: Masks `systemd-networkd-wait-online.service` to prevent boot delays, and `systemd-journald-audit.socket` to prevent systemd deadlocks in old kernels like 4.9.
- **Power Key Handling**: Instructs `systemd-logind` to ignore host power and suspend key events so the container does not attempt to handle host power state transitions.

### 4. NAT Mode Network Guards

Under Host networking mode, running network managers like `NetworkManager` or `systemd-networkd` inside the container can conflict with the Android host's routing tables and break cellular/Wi-Fi connectivity.

Droidspaces injects a drop-in `ExecCondition` override for standard network services (such as `NetworkManager.service`, `systemd-networkd.service`, `dhcpcd.service`, and `systemd-resolved.service`). This ensures these services only execute if the container is explicitly configured in NAT mode:
```ini
[Service]
ExecCondition=
ExecCondition=/bin/sh -c "grep -q 'net_mode=nat' /run/droidspaces/container.config"
```

### 5. Storage and DHCP Configuration

- **systemd-networkd Config**: Automatically configures `10-eth-dhcp.network` to enable DHCP and IPv6 route acceptance for any `eth*` interfaces.
- **Logrotate Limit**: Enforces a `maxsize 50M` limit in `/etc/logrotate.conf` to prevent logs from consuming excessive disk space over time.
