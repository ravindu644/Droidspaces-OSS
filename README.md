[![Latest release](https://img.shields.io/github/v/release/ravindu644/Droidspaces-OSS?label=Latest%20Release&style=for-the-badge)](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)
[![Telegram channel](https://img.shields.io/badge/Telegram-Channel-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/Droidspaces)
[![Android support](https://img.shields.io/badge/-Android-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#a-android-devices)
[![Linux desktop](https://img.shields.io/badge/-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#b-linux-desktop)

---

# Droidspaces

**Droidspaces** is a lightweight, portable Linux containerization tool that lets you run full Linux environments on top of Android or Linux, with complete init system support including **systemd**, **OpenRC**, and other init systems (runit, s6, etc.).

What makes Droidspaces unique is its **zero-dependency, native execution** on both Android and Linux. It's statically compiled against musl libc. If your device runs a Linux kernel, Droidspaces runs on it. No Termux, no middlemen, no setup overhead.

- **Tiny footprint:** under 260KB per platform
- **Truly native:** runs directly on Android and Linux from the same binary
- **Wide architecture support:** `aarch64`, `armhf`, `x86_64`, and `x86` as a single static binary
- **Beautiful Android app:** manage unlimited containers and do everything the CLI can, all from a clean, intuitive GUI

**Android** + **Linux Namespaces** = **Droidspaces**. Since Android is built on the Linux kernel, Droidspaces works seamlessly on Linux Desktop too. Both platforms are equally supported and maintained.

<details>
<summary><b>View Project's Screenshots (Linux & Android)</b></summary>

<table align="center">
  <tr valign="top">
    <td colspan="2" align="center">
      <b>Linux Showcase</b><br>
      <i>Linux Mint in volatile + --hw-access + foreground mode</i><br>
      <img src="Documentation/resources/linux/linux-showcase.png" width="95%"><br><br>
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="50%">
      <b>Android Home</b><br>
      <i>Beautiful home screen</i><br>
      <img src="Documentation/resources/android/home.jpg" width="90%">
    </td>
    <td align="center" width="50%">
      <b>Android Containers</b><br>
      <i>3 containers installed in the container menu</i><br>
      <img src="Documentation/resources/android/containers.jpg" width="90%">
    </td>
  </tr>
</table>

</details>

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
            - [Non-GKI (Legacy Kernels)](#non-GKI)
            - [GKI (Modern Kernels)](#GKI)
    - [Linux Desktop](#b-linux-desktop)
- [Tested Platforms](#tested-platforms)
- [Installation](#installation)
- [Usage](#usage)
- [Additional Documentation](#additional-documentation)
- [Credits](#credits)

---

<a id="what-is-droidspaces"></a>

## What is Droidspaces?

Droidspaces is a **container runtime** that uses Linux kernel namespaces to run full Linux distributions with a real init system (systemd, OpenRC, etc.) as PID 1.

Unlike traditional chroot, which simply changes the apparent root directory, Droidspaces creates proper process isolation. Each container gets its own PID tree, its own mount table, its own hostname, its own IPC resources, and its own cgroup hierarchy. The result is a full Linux environment that feels like a lightweight virtual machine, but with zero performance overhead because it shares the host kernel directly.

Droidspaces is designed to work natively on both **Android** and **Linux Desktop**. On Android, it handles all the kernel quirks, SELinux conflicts, complex networking scenarios, and encryption gotchas that break other container tools. On Linux Desktop, it works out of the box with no additional configuration needed.

The entire runtime is a **single static binary** under 260KB, compiled against musl libc with no external dependencies.

---

<a id="features"></a>

## Features

| Feature | Description |
|---------|-------------|
| **Init System Support** | Run systemd, OpenRC or any other init system as PID 1. Full service management, journald logging, and proper boot/shutdown sequences. |
| **Namespace Isolation** | Complete isolation via PID, MNT, UTS, IPC, and Cgroup namespaces. Each container has its own process tree, mount table, hostname, IPC resources, and cgroup hierarchy. |
| **Network Isolation** | **3 Networking Modes (Host, NAT, None)**. Pure network isolation via `CLONE_NEWNET` (NAT/None modes) or shared host networking (Host mode). Works on both Android and Linux. |
| **Port Forwarding** | Forward host ports to the container in NAT mode (e.g., `--port 22:22`). Supports TCP and UDP. |
| **Volatile Mode** | Ephemeral containers using OverlayFS. All changes are stored in RAM and discarded on exit. Perfect for testing and development. |
| **Custom Bind Mounts** | Map host directories into containers at arbitrary mount points. Supports both chained (`-B a:b -B c:d`) and comma-separated (`-B a:b,c:d`) syntax, up to 16 mounts. |
| **Config File Support** | Load configurations directly from `.config` files using `--conf`. Integrates seamlessly with the CLI overrides (`--reset` is supported) and automatically syncs to the workspace for persistence. |
| **Hardware Access Mode** | Expose host hardware (GPU, cameras, sensors, USB) to the container via devtmpfs. Enables GPU acceleration with Turnip + Zink / Panfrost on supported Android devices. PulseAudio and Virgl are also supported in Android |
| **Multiple Containers** | Run unlimited containers simultaneously, each with its own name, PID file, and configuration. Start, stop, enter, and manage them independently. |
| **In-container Reboot Support** | Handles in-container `reboot(2)` syscalls via a strict 3-level PID hierarchy to autonomously reinitialize the container sequence - TL;DR: you can restart the container remotely without touching Droidspaces! |
| **Android Storage** | Bind-mount `/storage/emulated/0` into the container for direct access to the device's shared storage. |
| **PTY/Console Support** | Full PTY isolation. Foreground mode provides an interactive console with proper terminal resize handling (binary only with the `-f` flag) |
| **Multi-DNS Support** | Configure custom DNS servers (comma-separated) that bypass the host's default DNS lookup. |
| **IPv6 Support** | Enable IPv6 networking in containers with a single flag. |
| **SELinux Permissive Mode** | Optionally set SELinux to permissive mode during container boot if needed. |
| **Rootfs Image Support** | Boot containers from ext4 `.img` files with automatic loop mounting, filesystem checks, and SELinux context hardening if needed. **The Android app also supports creating portable containers in rootfs.img mode** [ [How to create an ext4 rootfs.img manually ? ](./Documentation/Installation-Linux.md#option-b-create-an-ext4-image-recommended)] |
| **Auto-Recovery** | Automatic stale PID file cleanup, container scanning for orphaned processes, and robust config resurrection via in-memory metadata syncing from `/run/droidspaces`. |
| **Cgroup Isolation (v1/v2)** | Per-container cgroup hierarchies (`/sys/fs/cgroup/droidspaces/<name>`) with full systemd compatibility. Supports both legacy v1 and modern v2 hierarchies. |
| **Adaptive Seccomp Shield** | Kernel-aware BPF filter that resolves FBE keyring conflicts and prevents VFS deadlocks for **systemd** containers on legacy Android kernels (< 5.0). Automatically grants full namespace freedom to non-systemd containers (Alpine, OpenRC, runit, etc.), enabling features like **nested containers/Docker**. |

---

<a id="droidspaces-vs-chroot"></a>

## Droidspaces vs Chroot

| Feature | Chroot | Droidspaces |
|---------|--------|-------------|
| Init System | No. Cannot run systemd or OpenRC. | Yes. Full systemd/OpenRC, etc support as PID 1. |
| Process Isolation | None. Shares the host PID space. | Full. Private PID namespace with its own PID tree. |
| Filesystem Isolation | Partial. Only changes the apparent root. | Full. Uses `pivot_root` with a private mount namespace. |
| Mount Isolation | None. Mount events propagate to the host. | Full. `MS_PRIVATE` prevents mount propagation. |
| Cgroup Support | None. | Yes. Per-container cgroup hierarchies. |
| Resource Accounting | None. | Yes. Via cgroup isolation. |
| Service Management | Manual. Must start services individually. | Automatic. Init manages the full service lifecycle. |
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
| **Network Isolation** | **Broken on Android**. Even with all kernel configs enabled, network isolation with internet access never works. | **First-in-Class**. Perfectly handles network isolation with internet access on Android out of the box. |
| Binary Size | 10MB+ (plus dependencies) | Under 260KB per architecture. |
| Android Optimizations | None. Not designed for Android. | Yes. SELinux handling, FBE keyring management, storage integration, networking fixes |
| Termux Required | Often. Used as the execution environment. | Never. Runs directly as a native binary. |
| Nested Containers | Complex setup required. | Supported natively on 5.x+. On legacy kernels (< 5.0), supported *only* in non-systemd containers (Alpine) with kernel-level caveats. |
| Init System | LXC = yes, Docker = no. | Always. systemd/OpenRC as PID 1 by default. |

---

<a id="requirements"></a>

## Requirements

<a id="a-android-devices"></a>

### A. Android Devices

Droidspaces supports Android devices running Linux kernel **3.18 and above**:

| Kernel Version | Support Level | Notes |
|----------------|---------------|-------|
| 3.18 | Supported | **Legacy.** Minimum floor. Basic namespace support. systemd-based distros may be unstable; **Alpine** is recommended. |
| 4.4 - 4.19 | Stable | **Hardened.** Full support via Adaptive Seccomp Shield. [Supports any container with Systemd older than v258](./Documentation/Troubleshooting.md#modern-distros). |
| 5.4 - 5.10 | Recommended | **Mainline.** Full feature support including nested containers and Cgroup v2. |
| 5.15+ | Premium | **Full.** Best performance and maximum compatibility with all modern distributions. |

<a id="rooting-requirements"></a>

#### Rooting Requirements

Your device must be rooted. The following rooting methods have been tested:

| Root Method | Status | Notes |
|-------------|--------|-------|
| **KernelSU-Next** | Fully Supported | Tested and stable. Recommended. |
| **APatch** | Not Supported | Init system fails to start. |
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

For the list of devices and distributions verified by the community, see the [Tested Platforms Guide](Documentation/Platforms.md).

---

<a id="installation"></a>

## Installation

- [Android Installation Guide](Documentation/Installation-Android.md)
- [Linux Installation Guide](Documentation/Installation-Linux.md)

---

<a id="usage"></a>

## Usage

- [Android App Usage](Documentation/Usage-Android-App.md)
- [Linux CLI Usage](Documentation/Linux-CLI.md)

---

<a id="additional-documentation"></a>

## Additional Documentation

| Document | Description |
|----------|-------------|
| [Feature Deep Dives](Documentation/Features.md) | Detailed explanation of each major feature. |
| [Troubleshooting](Documentation/Troubleshooting.md) | Common issues and their solutions. |
| [Uninstallation Guide](Documentation/Uninstallation.md) | How to remove Droidspaces from your system. |

---

## License

Droidspaces is licensed under the [GNU General Public License v3.0](./LICENSE).

Copyright (C) 2026 [ravindu644](https://github.com/ravindu644) and contributors.

---

## Contributing

Contributions are welcome. Please open an issue or pull request on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS).

For questions or support, join the [Telegram channel](http://t.me/Droidspaces).

---

<a id="credits"></a>

## Credits & Acknowledgments

Droidspaces is built upon the incredible work of the open-source community. Special thanks to these projects for their inspiration and contributions:

*   **[LXC](https://github.com/lxc/lxc)** - For the core architectural vision and inspiration for modern Linux containerization.
*   **[Brutal-Busybox](https://github.com/feravolt/Brutal_busybox)** - For the statically-linked BusyBox binaries used in the Android userspace app to perform certain operations.
*   **[KernelSU-Next](https://github.com/KernelSU-Next/KernelSU-Next)**, **[MMRL](https://github.com/MMRLApp/MMRL)**, and **[LSPatch](https://github.com/LSPosed/LSPatch)** - For inspiring our modern UI design language and Android user experience.

---
