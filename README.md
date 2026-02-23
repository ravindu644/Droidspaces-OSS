[![Latest release](https://img.shields.io/github/v/release/ravindu644/Droidspaces-OSS?label=Latest%20Release&style=for-the-badge)](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)
[![Telegram channel](https://img.shields.io/badge/Telegram-Channel-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/Droidspaces)
[![Android support](https://img.shields.io/badge/-Android-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#a-android-devices)
[![Linux desktop](https://img.shields.io/badge/-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#b-linux-desktop)

---

# Droidspaces

**Droidspaces** is a lightweight, portable Linux containerization tool that lets you run full Linux environments on top of Android or Linux, with complete init system support including **systemd** and **OpenRC**.

What makes Droidspaces unique is its **zero-dependency, native execution** on both Android and Linux. It's statically compiled against musl libc. If your device runs a Linux kernel, Droidspaces runs on it. No Termux, no middlemen, no setup overhead.

- **Tiny footprint:** under 150KB per platform
- **Truly native:** runs directly on Android and Linux from the same binary
- **Wide architecture support:** `aarch64`, `armhf`, `x86_64`, and `x86` as a single static binary
- **Beautiful Android app:** manage unlimited containers and do everything the CLI can, all from a clean, intuitive GUI

**Android** + **Linux Namespaces** = **Droidspaces**. Since Android is built on the Linux kernel, Droidspaces works seamlessly on Linux Desktop too. Both platforms are equally supported and maintained.

---

### Quick Navigation

- [What is Droidspaces?](#what-is-droidspaces)
- [Features](#features)
- [Droidspaces vs Chroot](#droidspaces-vs-chroot)
- [Droidspaces vs LXC/Docker on Android](#droidspaces-vs-lxcdocker-on-android)
- [Requirements](#requirements)
    - [Android](#a-android-devices)
        - [Rooting Requirements](#rooting-requirements)
        - [Android Kernel Requirements](#android-kernel-requirements)
            - [Non-GKI (Legacy Kernels)](#i-non-gki-android-devices)
            - [GKI (Modern Kernels)](#ii-gki-android-devices)
    - [Linux Desktop](#b-linux-desktop)
- [Tested Platforms](#tested-platforms)
- [Installation](#installation)
- [Usage](#usage)
- [Additional Documentation](#additional-documentation)

---

<a id="what-is-droidspaces"></a>

## What is Droidspaces?

Droidspaces is a **container runtime** that uses Linux kernel namespaces to run full Linux distributions with a real init system (systemd or OpenRC) as PID 1.

Unlike traditional chroot, which simply changes the apparent root directory, Droidspaces creates proper process isolation. Each container gets its own PID tree, its own mount table, its own hostname, its own IPC resources, and its own cgroup hierarchy. The result is a full Linux environment that feels like a lightweight virtual machine, but with zero performance overhead because it shares the host kernel directly.

Droidspaces is designed to work natively on both **Android** and **Linux Desktop**. On Android, it handles all the kernel quirks, SELinux conflicts, and encryption gotchas that break other container tools. On Linux Desktop, it works out of the box with no additional configuration needed.

The entire runtime is a **single static binary** under 150KB, compiled against musl libc with no external dependencies.

---

<a id="features"></a>

## Features

| Feature | Description |
|---------|-------------|
| **Init System Support** | Run systemd or OpenRC as PID 1. Full service management, journald logging, and proper boot/shutdown sequences. |
| **Namespace Isolation** | Complete isolation via PID, MNT, UTS, IPC, and Cgroup namespaces. Each container has its own process tree, mount table, hostname, IPC resources, and cgroup hierarchy. |
| **Volatile Mode** | Ephemeral containers using OverlayFS. All changes are stored in RAM and discarded on exit. Perfect for testing and development. |
| **Custom Bind Mounts** | Map host directories into containers at arbitrary mount points. Supports both chained (`-B a:b -B c:d`) and comma-separated (`-B a:b,c:d`) syntax, up to 16 mounts. |
| **Hardware Access Mode** | Expose host hardware (GPU, cameras, sensors, USB) to the container via devtmpfs. Enables GPU acceleration with Turnip + Zink / Panfrost on supported devices. |
| **Multiple Containers** | Run unlimited containers simultaneously, each with its own name, PID file, and configuration. Start, stop, enter, and manage them independently. |
| **Fast Restart** | Near-instant container restarts (under 200ms) by preserving the loop mount and coordinating state between the CLI and the background monitor process. |
| **Android Storage** | Bind-mount `/storage/emulated/0` into the container for direct access to the device's shared storage. |
| **PTY/Console Support** | Full PTY isolation. Foreground mode provides an interactive console with proper terminal resize handling (binary only with the `-f` flag) |
| **Multi-DNS Support** | Configure custom DNS servers (comma-separated) that bypass the host's default DNS lookup. |
| **IPv6 Support** | Enable IPv6 networking in containers with a single flag. |
| **SELinux Permissive Mode** | Optionally set SELinux to permissive mode during container boot if needed. |
| **Rootfs Image Support** | Boot containers from ext4 `.img` files with automatic loop mounting, filesystem checks, and SELinux context hardening if needed. **The Android app also supports creating portable containers in rootfs.img mode.** |
| **Auto-Recovery** | Automatic stale PID file cleanup, container scanning for orphaned processes, and robust mount cleanup on exit. |
| **Cgroup Isolation** | Per-container cgroup hierarchies (`/sys/fs/cgroup/droidspaces/<name>`) with full systemd compatibility. Supports both cgroup v1 and v2. |
| **Adaptive Seccomp Shield** | Kernel-aware BPF filter that resolves FBE keyring conflicts and prevents VFS deadlocks on legacy Android kernels (< 5.0). Automatically disabled on modern kernels for full feature support. |

---

<a id="droidspaces-vs-chroot"></a>

## Droidspaces vs Chroot

| Feature | Chroot | Droidspaces |
|---------|--------|-------------|
| Init System | No. Cannot run systemd or OpenRC. | Yes. Full systemd/OpenRC support as PID 1. |
| Process Isolation | None. Shares the host PID space. | Full. Private PID namespace with its own PID tree. |
| Filesystem Isolation | Partial. Only changes the apparent root. | Full. Uses `pivot_root` with a private mount namespace. |
| Mount Isolation | None. Mount events propagate to the host. | Full. `MS_PRIVATE` prevents mount propagation. |
| Cgroup Support | None. | Yes. Per-container cgroup hierarchies. |
| Resource Accounting | None. | Yes. Via cgroup isolation. |
| Service Management | Manual. Must start services individually. | Automatic. systemd manages the full service lifecycle. |
| Hostname Isolation | None. Shares the host hostname. | Yes. UTS namespace provides independent hostname. |
| IPC Isolation | None. Shares System V IPC. | Yes. IPC namespace for semaphores and shared memory. |
| Ephemeral Containers | Not possible. | Yes. Volatile mode via OverlayFS. |

---

<a id="droidspaces-vs-lxcdocker-on-android"></a>

## Droidspaces vs LXC/Docker on Android

| Aspect | LXC/Docker | Droidspaces |
|--------|------------|-------------|
| Dependencies | Many (liblxc, runc, containerd, etc.) | Zero. Single static binary. |
| Setup Complexity | High. Requires Termux, cross-compiled libraries, manual config files. | Low. Download and install the APK, then run it on Android; download, extract, and run it on Linux. |
| Older kernels Support | Spotty. Many features break on older kernels. | Full. Adaptive seccomp shield handles kernel quirks. |
| Binary Size | 10MB+ (plus dependencies) | Under 150KB per architecture. |
| Android Optimizations | None. Not designed for Android. | Yes. SELinux handling, FBE keyring management, storage integration, networking fixes |
| Termux Required | Often. Used as the execution environment. | Never. Runs directly as a native binary. |
| Nested Containers | Complex setup required. | Works natively on kernel 5.x +. |
| Init System | LXC = yes, Docker = no. | Always. systemd/OpenRC as PID 1 by default. |

---

<a id="requirements"></a>

## Requirements

<a id="a-android-devices"></a>

### A. Android Devices

Droidspaces supports Android devices running Linux kernel **3.18 and above**:

| Kernel Version | Support Level | Notes |
|----------------|---------------|-------|
| 3.18 | Supported | Minimum floor. Basic namespace support. |
| 4.4 - 4.19 | Stable | Full support with adaptive seccomp shield. |
| 5.4 - 5.10 | Recommended | Full features including nested container support. |
| 5.15+ | Full | All features, modern cgroup v2 support. |

<a id="rooting-requirements"></a>

#### Rooting Requirements

Your device must be rooted. The following rooting methods have been tested:

| Root Method | Status | Notes |
|-------------|--------|-------|
| **KernelSU-Next v1.1.1** | Fully Supported | Tested and stable. Recommended. |
| **APatch** | Not Supported | Init system fails to start. |
| **RKSU** | Under Investigation | Hangs during container start/stop/restart.|
| **Magisk** | Planned | Testing has not yet been conducted. |

<a id="kernel-requirements"></a>

<a id="android-kernel-requirements"></a>

#### Android Kernel Requirements

Android kernels are often heavily modified and may have critical container features disabled. Your kernel must have specific configuration options enabled (Namespaces, Cgroups, Seccomp, etc.) to run Droidspaces.

<a id="non-GKI"></a>

##### Non-GKI (Legacy Kernels)
Covers kernels: **3.18, 4.4, 4.9, 4.14, 4.19**. These kernels work plug-and-play after adding the required config fragments.
See: [Legacy Kernel Configuration](Documentation/Kernel-Configuration.md#non-gki-devices-legacy-kernels)

<a id="GKI"></a>

##### GKI (Modern Kernels)
Covers kernels: **5.4, 5.10, 5.15, 6.1+**. These kernels require additional steps to handle ABI breakage caused by configuration changes.
See: [Modern GKI Kernel Configuration](Documentation/Kernel-Configuration.md#gki-devices-modern-kernels)

**Next Steps for Kernel Support:**
- **Check automatically**: Use the built-in requirements checker in the Android app (**Settings** -> **Requirements**).
- **Full Technical Guide**: [Kernel Configuration Guide](Documentation/Kernel-Configuration.md)

> [!TIP]
>
> **Need help compiling a kernel?** Check out this guide:
>
> https://github.com/ravindu644/Android-Kernel-Tutorials

---

<a id="b-linux-desktop"></a>

### B. Linux Desktop

Most modern Linux desktop distributions already include all the requirements needed by Droidspaces by default. **No additional configuration is needed.**

Just download the tarball from the [GitHub Releases](https://github.com/ravindu644/Droidspaces-OSS/releases/latest), extract it, and use the binary for your CPU architecture.

You can verify your system meets all requirements by running:

```bash
sudo ./droidspaces check
```

---

<a id="tested-platforms"></a>

## Tested Platforms

Droidspaces is brand-agnostic and distribution-agnostic. Regardless of your mobile device's brand or your desktop Linux distribution, Droidspaces will work as long as your kernel meets the [required configuration](Documentation/Kernel-Configuration.md).

### Android

| Device | Kernel | Status | Notes |
|--------|--------|--------|-------|
| Samsung Galaxy S10 (Exynos) | 4.14.113 | Stable | No nested container support (kernel too old). |
| Samsung Galaxy A16 5G | 5.15.167 | Stable | Full feature support including nested containers. |

All devices tested with **KernelSU-Next v1.1.1**.

### Linux Desktop

| Distribution | Kernel | Status |
|-------------|--------|--------|
| Fedora 42 | 6.17 - 6.19 | Stable |
| CachyOS | 6.17 - 6.19 | Stable |
| Arch Linux | 6.17 - 6.19 | Stable |

---

<a id="installation"></a>

## Installation

- [Android Installation Guide](Documentation/Installation-Android.md)
- [Linux Installation Guide](Documentation/Installation-Linux.md)

---

<a id="usage"></a>

## Usage

- [Android App Usage](Documentation/Usage-Android-App.md)
- [Linux CLI Usage](Documentation/Usage-Linux-CLI.md)

---

<a id="additional-documentation"></a>

## Additional Documentation

| Document | Description |
|----------|-------------|
| [CLI Reference](Documentation/CLI-Reference.md) | Complete reference for all commands and flags. |
| [Feature Deep Dives](Documentation/Features.md) | Detailed explanation of each major feature. |
| [Troubleshooting](Documentation/Troubleshooting.md) | Common issues and their solutions. |
| [Kernel Configuration Guide](Documentation/Kernel-Configuration.md) | How to compile a compatible kernel for Android. |

---

## License

Droidspaces is licensed under the [GNU General Public License v3.0](./LICENSE).

Copyright (C) 2026 ravindu644

---

## Contributing

Contributions are welcome. Please open an issue or pull request on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS).

For questions or support, join the [Telegram channel](https://t.me/Droidspaces).
