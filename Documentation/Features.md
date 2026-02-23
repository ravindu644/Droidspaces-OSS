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

### Why Not Network Namespace?

Droidspaces deliberately does **not** use a network namespace (`CLONE_NEWNET`). The container shares the host's network stack. This is a design choice that greatly simplifies setup: containers get internet access immediately without virtual bridges, NAT, or firewall rules. On Android, where networking is already complex (cellular, Wi-Fi, VPN), avoiding network namespaces prevents a whole category of connectivity issues.

### Why Not User Namespace?

User namespaces (`CLONE_NEWUSER`) are also not used. While modern Android kernels often support user namespaces, Droidspaces requires real root privileges on the host to perform critical operations like mounting filesystems, managing loop devices, and configuring cgroups. Running inside a user namespace would map the process to a non-privileged UID on the host, stripping the "real" root authority needed for these low-level system tasks. Skipping user namespaces ensures the container has the full power required to boot a complete Linux distribution.

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

- **systemd** (most Linux distributions)
- **OpenRC** (Alpine Linux, Gentoo)

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
- **GPU** (for hardware-accelerated graphics via Turnip + Zink, Panfrost)
- **Cameras**
- **Sensors** (accelerometer, gyroscope, etc.)
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

- Maximum of **16 bind mounts** per container
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

### Fast Restart with Images

When restarting an image-based container, Droidspaces preserves the loop mount. The restart command creates a coordination marker that tells the monitor process to skip cleanup. The next start detects this marker and reuses the existing mount, bypassing `e2fsck` and mount setup entirely.

---

## Cgroup Isolation

### What It Does

Droidspaces creates per-container cgroup trees at `/sys/fs/cgroup/droidspaces/<name>` on the host. Combined with the cgroup namespace, each container sees its own clean cgroup hierarchy.

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
- **Cgroup v1 (legacy):** Used by older distributions. Droidspaces handles comounted controllers (e.g., `cpu,cpuacct`) and creates symlinks for secondary names.
- **Hybrid mode:** Some systems use both v1 and v2. Droidspaces detects and handles this correctly.

### The `su` Fix

When entering a container with `enter` or `run`, the process must be in the container's host-side cgroup before joining namespaces. Otherwise, `systemd-logind` and `sd-pam` inside the container cannot map the process to a valid session, causing `su` and `sudo` to hang. Droidspaces handles this automatically by attaching to the container's cgroup before any `setns()` call.

---

## Adaptive Seccomp Shield

### What It Does

On Android devices with legacy kernels (< 5.0), Droidspaces installs a BPF-based seccomp filter that resolves two critical conflicts:

### Problem 1: FBE Keyring Conflict

Android's File-Based Encryption (FBE) stores filesystem keys in the kernel's session keyring. When systemd's services attempt to create new session keyrings, the process loses access to the host's encryption keys. This causes "Required key not available" (ENOKEY) errors.

**Solution:** The filter intercepts `keyctl`, `add_key`, and `request_key` syscalls and returns `ENOSYS`, causing systemd to fall back to the existing keyring.

### Problem 2: VFS Namespace Deadlock

On kernels 4.19 and below, systemd's service sandboxing (`PrivateTmp=yes`, `ProtectSystem=yes`) triggers race conditions in the kernel's VFS layer. This can cause system hangs or kernel panics.

**Solution:** The filter intercepts `unshare` and `clone` calls requesting namespace flags and returns `EPERM`. systemd gracefully runs the service without sandboxing.

### Adaptive Behavior

- **Kernel < 5.0:** Full seccomp shield is active (both keyring and namespace protections)
- **Kernel >= 5.0:** Shield is completely skipped. Modern kernels handle these cases natively, and disabling the filter allows full-featured operation including Docker and nested containers.

---

## Android-Specific Tuning

Droidspaces includes several sophisticated subsystems designed specifically to handle the "opinionated" nature of the Android Linux kernel.

### Safe Udev Trigger

Standard Linux distributions use `udevadm trigger` to "coldplug" hardware devices during boot. On many Android devices, triggering all devices simultaneously causes the kernel to deadlock or panic because Android's own hardware drivers (which are already running) do not expect another manager to re-trigger them.

**The Solution**: Droidspaces masks the standard udev trigger services and installs a **Safe Udev Trigger**. This service only triggers a strictly defined subset of subsystems (`usb`, `block`, `input`, `tty`) that are safe to re-scan. This enables the container to see new USB drives or keyboards without risking a system crash.

### Network Metric Auto-Tuning

Android doesn't use standard Linux networking; it uses "Connectivity Manager" to switch between WiFi, Cellular data, and VPNs on the fly. This often breaks containers because their internal routing table becomes stale.

**The Solution**: The Droidspaces installer injects a specialized `systemd-networkd` configuration that applies weighted metrics to different interface patterns:
- **WiFi (`wlan*`)**: Highest priority (Metric 100).
- **Mobile Data (`rmnet*`, `pdp*`)**: Secondary priority (Metric 200).
- **Others (`eth*`, `usb*`)**: Tertiary priority (Metric 300).

Combined with a hardcoded DNS fallback inside the container, this ensures that the Linux environment remains connected even as your phone hops between networks.

### Atomic Backend Installation

To prevent the "Text file busy" error when updating the `droidspaces` binary while a container is running, the Android app uses an **Atomic Move Strategy**:
1. The new binary is copied to a temporary file (`.tmp`).
2. Permissions are set on the temporary file.
3. The app uses `mv -f` (an atomic inode rename in the kernel) to replace the production binary.

This ensures that updates are 100% safe and never leave the system in a corrupted state if an update is interrupted.
