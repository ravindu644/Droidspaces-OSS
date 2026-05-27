English | [简体中文](./README_CN.md)

---

[![Latest release](https://img.shields.io/github/v/release/ravindu644/Droidspaces-OSS?label=Latest%20Release&style=for-the-badge)](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)
[![Telegram channel](https://img.shields.io/badge/Telegram-Channel-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/Droidspaces)
[![Android support](https://img.shields.io/badge/-Android-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#a-android-devices)
[![Linux desktop](https://img.shields.io/badge/-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#b-linux-desktop)
[![Translation status](https://img.shields.io/weblate/progress/droidspaces?server=https://hosted.weblate.org&style=for-the-badge&label=Translated&logo=weblate)](https://hosted.weblate.org/engage/droidspaces/)

---

# Droidspaces

**Droidspaces** is a lightweight, portable Linux containerization tool that lets you run full Linux environments on top of Android, Linux, or even in **minimal environments like Android recovery/Ramdisks**, with complete init system support including **systemd**, **OpenRC**, and other init systems (runit, s6, etc.).

What makes Droidspaces unique is its **zero-dependency, native execution** on both Android and Linux. It's statically compiled against musl libc. If your device runs a Linux kernel, Droidspaces runs on it. No Termux, no middlemen, no setup overhead.

- **Tiny footprint:** under 400KB per platform
- **Truly native:** runs directly on Android and Linux from the same binary
- **Wide architecture support:** `aarch64`, `armhf`, `x86_64`, `x86`, and `riscv64` as a single static binary
- **Beautiful Android app:** manage unlimited containers and do everything the CLI can, all from a clean, intuitive GUI

**Android** + **Linux Namespaces** = **Droidspaces**. Since Android is built on the Linux kernel, Droidspaces works seamlessly on Linux Desktop too. Both platforms are equally supported and maintained.

> [!TIP]
> Check out [Community-supported Android devices](./Documentation/community-supported-devices.md) for a growing list of phones known to run Droidspaces.

<details>
<summary><b>View Project's Screenshots (Linux & Android)</b></summary>

<table align="center">
  <tr valign="top">
    <td colspan="3" align="center">
      <b>Linux Showcase</b><br>
      <i>Ubuntu + foreground mode</i><br>
      <img src="Documentation/resources/linux/linux-showcase.png" width="95%"><br><br>
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>Android Home</b><br>
      <i>Beautiful home screen</i><br>
      <img src="Documentation/resources/gallery/1-home_page.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Android Containers</b><br>
      <i>Installed in the container menu</i><br>
      <img src="Documentation/resources/gallery/2-containers_tab.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Configuration menu</b><br>
      <i>Hostname and Networking modes</i><br>
      <img src="Documentation/resources/gallery/3_container_configuration.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>Configuration menu</b><br>
      <i>Integration & Hardware, 1st part</i><br>
      <img src="Documentation/resources/gallery/4_container_configuration.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Configuration menu</b><br>
      <i>Security & boot, Advanced</i><br>
      <img src="Documentation/resources/gallery/5_container_configuration.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Logging</b><br>
      <i>Container boot-up logs</i><br>
      <img src="Documentation/resources/gallery/6_startup_logs.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>Android Panel</b><br>
      <i>Dashboard and portal access</i><br>
      <img src="Documentation/resources/gallery/7_panel.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Container information</b><br>
      <i>Manage the container in 1 place</i><br>
      <img src="Documentation/resources/gallery/8_container_information.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Systemd services</b><br>
      <i>Full systemd management</i><br>
      <img src="Documentation/resources/gallery/9_systemd_menu.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>User Picker</b><br>
      <i>Summon up a terminal</i><br>
      <img src="Documentation/resources/gallery/10_terminal_user_picker.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Terminal UI</b><br>
      <i>Love fastfetch ? here it is !</i><br>
      <img src="Documentation/resources/gallery/11_terminal_fastfetch.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Isolation checks</b><br>
      <i>Demonstration of isolated mounts</i><br>
      <img src="Documentation/resources/gallery/12_mnt_net_isolation.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>Settings page</b><br>
      <i>Check requirements, customization</i><br>
      <img src="Documentation/resources/gallery/13_settings_screen.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Requirements checker</b><br>
      <i>Real-time system checks</i><br>
      <img src="Documentation/resources/gallery/14_built_in_requirements_checker.png" width="95%">
    </td>
    <td align="center" width="33%">
      <!-- Empty to balance the 3-column row -->
    </td>
  </tr>
</table>

</details>

---

### Quick Navigation

- [What is Droidspaces?](#what-is-droidspaces)
- [Features](#features)
- [Security & Isolation Philosophy](#security-model)
- [Droidspaces vs The Alternatives](#droidspaces-vs-the-alternatives)
- [Requirements](#requirements)
    - [Android](#a-android-devices)
        - [Rooting Requirements](#rooting-requirements)
        - [Known Quirks](#known-quirks)
        - [Android Kernel Requirements](#android-kernel-requirements)
            - [Non-GKI (Legacy Kernels)](#non-GKI)
            - [GKI (Modern Kernels)](#GKI)
    - [Linux Desktop](#b-linux-desktop)
- [Installation](#installation)
- [Community-supported Android devices](./Documentation/community-supported-devices.md)
- [Usage](#usage)
- [Troubleshooting](./Documentation/Troubleshooting.md)
- [Additional Documentation](#additional-documentation)
- [Contributing](#contribution)
- [Credits](#credits)

---

<a id="what-is-droidspaces"></a>

## What is Droidspaces?

Droidspaces is a **container runtime** that uses Linux kernel namespaces to run full Linux distributions with a real init system (systemd, OpenRC, etc.) as PID 1.

Unlike traditional chroot, which simply changes the apparent root directory, Droidspaces creates proper process isolation. Each container gets its own PID tree, its own mount table, its own hostname, its own IPC resources, and its own cgroup hierarchy. The result is a full Linux environment that feels like a lightweight virtual machine, but with zero performance overhead because it shares the host kernel directly.

Droidspaces is designed to work natively on anything that runs a Linux kernel, including **Android**, **Linux Desktop**, and **minimal environments like Android recovery/ramdisks**. On Android, it handles all kernel quirks, SELinux conflicts, complex networking scenarios, and encryption issues that break other container tools. On Linux Desktop, it works out of the box with no additional configuration needed. On ramdisks, it handles all the quirks like `pivot_root` as well!

The entire runtime is a **single static binary** under 400KB, compiled against musl libc with no external dependencies.

---

<a id="features"></a>

## Features

| Feature | Description |
|---------|-------------|
| **Init System Support** | Run systemd, OpenRC or any other init system as PID 1. Full service management and proper boot/shutdown/reboot sequences. |
| **Deep Android Integration** | Supports two daemon modes: **Native init.rc** (lowest-level integration with auto-spawn/unkillable persistence) and **Userspace Daemon** (app-togglable, starts via `post-fs-data.sh`, no image modification required). **Both modes bypass root-domain seccomp blocks to ensure stable container lifecycles** [[init.rc Developer Guide](./init/README.md)]. |
| **Namespace Isolation** | Complete isolation via PID, MNT, UTS, IPC, and Cgroup namespaces. Each container has its own process tree, mount table, hostname, IPC resources, and cgroup hierarchy. |
| **Network Isolation** | **3 Networking Modes (Host, NAT, None)**. Pure network isolation via `CLONE_NEWNET` (NAT/None modes) or shared host networking (Host mode). Works on both Android and Linux. |
| **Android GPU Acceleration** | Native hardware acceleration for Qualcomm Adreno GPUs via the Turnip driver. Use our [pre-built rootfs templates](https://github.com/ravindu644/Droidspaces-rootfs-builder/releases/latest) for an out-of-the-box experience. [[More info](./Documentation/GPU-Acceleration.md)] |
| **Linux GPU Acceleration** | Zero-configuration GPU acceleration for AMD and Intel GPUs on Linux desktop hosts. [[More info](./Documentation/GPU-Acceleration.md)] |
| **Port Forwarding** | Forward host ports to the container in NAT mode (e.g., `--port 22:22`). Supports TCP and UDP, as well as ranges like `1-500:1-500`. |
| **Volatile Mode** | Ephemeral containers using OverlayFS. All changes are stored in RAM and discarded on exit. Perfect for testing and development. |
| **Custom Bind Mounts** | Map host directories into containers at arbitrary mount points. Supports both chained (`-B a:b -B c:d`) and comma-separated (`-B a:b,c:d`) syntax. |
| **Config File Support** | Load configurations directly from `.config` files using `--conf`. Integrates seamlessly with the CLI overrides (`--reset` is supported) and automatically syncs to the workspace for persistence. |
| **Hardware Access Mode** | Expose host hardware (GPU, cameras, sensors, USB, block devices) directly to your containers with a single configuration toggle. |
| **Multiple Containers** | Run unlimited containers simultaneously, each with its own name, PID file, and configuration. Start, stop, enter, and manage them independently. |
| **In-container Reboot Support** | You can restart the container remotely without even touching Droidspaces! |
| **Android Storage** | Bind-mount `/storage/emulated/0` into the container for direct access to the device's shared storage. |
| **PTY/Console Support** | Full PTY isolation. Foreground mode provides an interactive console with proper terminal resize handling (binary only with the `-f` flag) |
| **Multi-DNS Support** | Configure custom DNS servers (comma-separated) to bypass the host's default DNS lookup. If you don't specify any DNS servers, it falls back to your ISP's default DNS. |
| **SELinux Permissive Mode** | Optionally set SELinux to permissive mode during container boot if needed. |
| **Rootfs Image / Direct block device Support** | Boot containers from ext4 `.img` files with automatic loop mounting, filesystem checks, and SELinux context hardening if needed. Mounting block devices like partitions, sdcards are supported too in CLI ! **The Android app also supports creating portable containers in rootfs.img mode** [ [How to create an ext4 rootfs.img manually ? ](./Documentation/Installation-Linux.md#option-b-create-an-ext4-image-recommended)] |
| **Auto-Recovery** | Automatic stale PID file cleanup, container scanning for orphaned processes, and robust config resurrection via in-memory metadata syncing from `/run/droidspaces`. |
| **Cgroup Isolation (v1/v2)** | Per-container cgroup hierarchies (`/sys/fs/cgroup/droidspaces/<name>`) with full systemd compatibility. Supports both legacy v1 and modern v2 hierarchies. |
| **Adaptive Security & Deadlock Shield** | Kernel-aware BPF filters resolve FBE keyring conflicts automatically on legacy kernels. A manual **Deadlock Shield** toggle is available to fix the specific VFS `grab_super()` deadlock on affected legacy devices (e.g., kernel 4.14.113). When the shield is disabled (default), Droidspaces grants full namespace freedom enabling features like **nested containers/Docker** natively on all kernels. |
| **Privileged Mode** | Gain full access with the `--privileged` flag! Use with caution: do not report bugs when using this flag as it relaxes several security barriers for features like Flatpak/Bwrap/K3S. |

---

<a id="security-model"></a>

## Security & Isolation Philosophy

> [!IMPORTANT]
>
> Droidspaces is a **privileged container runtime** built for **power users** who prioritize simplicity, performance, and native integration over complex, production-grade jailing.
>
> To provide full systemd support, native hardware acceleration (GPU), and complex mounts/networking on Android, the container root needs real privileges. Even though Droidspaces does not use the heavily restricted "unprivileged" (User Namespace) mode, it applies several security layers:
> - **Capability Dropping**: By default, Droidspaces drops high-risk capabilities (e.g., `CAP_SYS_MODULE`, `CAP_SYS_RAWIO`).
> - **Mount Hardening**: Critical host paths are masked or remounted as read-only.
> - **Seccomp Filters**: Common exploit vectors (like CVE-2026-31431 and malicious kernel module loading) are blocked by default.

> [!WARNING]
>
> **A Container is not a Jail**
> If a process runs as **root** inside a Droidspaces container, it has significant power.
> 
> A malicious root user can attempt to escape or manipulate the host. **Droidspaces is not a sandbox for untrusted code.**
>
> We focus on bringing a full Linux server experience to your pocket, not on building a production-grade fortress.

> [!NOTE]
>
> **Our Security Advice:**
> 1. **Don't daily-drive root**: Just as you would on a standard Linux PC, create a normal user inside your container and use `sudo`.
> 2. **Be Careful with Modes**: Flags like `--privileged` and `--hw-access` intentionally relax security barriers. Use them only when necessary.
> 3. **Respect the Host**: If you compromise your container's root, you compromise your device.

---

<a id="droidspaces-vs-the-alternatives"></a>

## Droidspaces vs The Alternatives

| Category | **Droidspaces** | LXC + Termux | Docker + Termux | Chroot | PRoot |
|----------|-----------------|--------------|-----------------|--------|-------|
| **Technology** | **Namespaces** | Namespaces | Namespaces | Path redirection only | Syscall hooking (PTRACE) |
| **Performance** | **Native** | Native | Native | Native | Moderate (PTRACE overhead on every syscall) |
| **Boot Time (systemd)** | **150ms - 750ms** | 750ms - 2000ms | N/A (systemd not supported) | N/A | N/A |
| **Init System (PID 1)** | **Yes (full systemd/OpenRC/runit/s6/SysVinit support)** | Yes (full systemd/OpenRC/runit/s6/SysVinit support) | No | None | None |
| **Process Isolation** | **Full** | Full | Full | None (shares host PID namespace) | None (shares host PID namespace) |
| **Filesystem Isolation** | **Full** | Full | Full | None (`chroot /proc/1/root /system/bin/sh` escapes instantly) | Limited |
| **Mount Isolation** | **Full** | Full | Full | None | None |
| **IPC / UTS / Cgroup Isolation** | **Full** | Full | Full | None | None |
| **Container Persistence on Android** | **Truly unkillable. Survives 15+ days. Immune to "Don't Keep Activities" and "No Background Processes" in Developer Options.** | Low (killed by Android LMK / battery optimization) | Low (killed by Android LMK / battery optimization) | Low (killed by Android LMK / battery optimization) | Low (killed by Android LMK / battery optimization) |
| **Data Persistence (app uninstall)** | **Zero data loss. All containers, configs, and data live in `/data/local/Droidspaces`, fully independent of the app. The binary and daemons run in their own process session (`setsid`), detached from the app's process group. Uninstalling the app stops nothing and deletes nothing.** | Everything dies on Termux uninstall. LXC configs and rootfs stored inside Termux (`/data/data/com.termux`); uninstalling Termux wipes everything. | Everything dies on Termux uninstall. Container data in `/data/docker` survives but is inaccessible without reinstalling the entire stack. | Safe if rootfs is in `/data/local/`. Unsafe if stored inside Termux home directory. | Everything dies on Termux uninstall. PRoot rootfs typically lives in `/data/data/com.termux`; uninstalling Termux deletes it. |
| **Run at Boot** | **Yes (native `init.rc` / `service.d`). Auto-starts containers even if the phone is locked, `/data` is encrypted, and before any user app has even started.** | No | No | No | No |
| **Network Isolation on Android** | **First-in-class. Full NAT/Veth + internet works out of the box. No manual configuration needed.** | Internet works only in host-network mode (`lxc.net.0.type = none`). True network isolation (veth + NAT) often requires manual bridge, iptables, and ip_forward setup - and still breaks on most devices. | Requires `--network host` to get internet; actual network isolation with internet access often does not work reliably on Android. | None (no network namespace) | None (no network namespace) |
| **Hardware & Native GPU Access** | **Full (single toggle). Adreno Turnip, USB, sensors, network interfaces, block devices. Full `systemd-udevd` support - behaves like a real Linux PC.** | Manual bind mounts, no udev | Manual bind mounts, no udev | Manual bind mounts, no udev | None |
| **Termux-X11 Support** | **Full (single toggle)** | Manual socket passthrough | Manual socket passthrough | Manual socket passthrough | Manual socket passthrough |
| **Privileged Mode** | **Full + customizable (`--nomask`, `--nocaps`, `--noseccomp`, etc.)** | Manual config | Yes (`--privileged`) | Full (no guardrails) | No |
| **Nested Containers (Docker-in-DS)** | **Natively supported on all kernels** | Complex manual setup | Complex manual setup | No | No |
| **Ephemeral / Volatile Containers** | **Yes (OverlayFS, RAM-backed, zero persistence on exit)** | No | Yes | No | No |
| **Portable rootfs.img Support** | **Yes (native loop mount, fsck, SELinux hardening)** | No | No | No | No |
| **Older Kernel Support (3.10+)** | **Full** | Spotty (cgroup conflicts, broken on many old Android kernels) | Spotty (cgroup conflicts, broken on many old Android kernels) | N/A | N/A |
| **Android-Specific Optimizations** | **SELinux live patching, FBE keyring handling, storage integration, networking fixes, etc.** | None (not designed for Android) | None (not designed for Android) | None | None |
| **Root Required** | **Yes** | Yes | Yes | Yes | No |
| **Termux Required** | **Never. Zero dependencies.** | Yes | Yes | No | Yes |
| **Setup Complexity** | **Low. Install APK and run.** | High (manual cgroup mounts, manual config changes, manual daemon start) | High (manual cgroup mounts, manual config changes, manual daemon start) | Medium (manual mount script required every boot) | Medium |
| **Binary Size** | **~400KB per architecture** | 10MB+ | 50MB+ | N/A | ~10MB |
| **Dependencies** | **Zero. Single static musl binary.** | Termux + liblxc + templates | Termux + dockerd + containerd + runc | Termux or any shell environment | Termux + proot binary |

---

<a id="requirements"></a>

## Requirements

<a id="a-android-devices"></a>

### A. Android Devices

Droidspaces supports Android devices running Linux kernel **3.10 and above**:

| Kernel Version | Support Level | Notes |
|----------------|---------------|-------|
| 3.10 | Supported | **Legacy.** Minimum floor. Basic namespace support. systemd-based distros may be unstable; **Alpine** is recommended. |
| 4.4 - 4.19 | Stable | **Hardened.** [Full support upto modern distros with systemd older than v258](./Documentation/Troubleshooting.md#modern-distros). Nested containers (Docker/Podman) are natively supported. If you encounter systemd hangs on specific kernels (like 4.14.113) due to the VFS deadlock bug, manually enable the **Deadlock Shield** [[more info](./Documentation/Features.md#vfs-deadlock)]. |
| 5.4 - 5.10 | Recommended | **Mainline.** Full feature support including nested containers and Cgroup v2. |
| 5.15+ | Premium | **Full.** Best performance and maximum compatibility with all modern distributions. |

<a id="rooting-requirements"></a>

#### Rooting Requirements

Your device must be rooted. The following rooting methods have been tested:

| Root Method | Status | Notes |
|-------------|--------|-------|
| **KernelSU** | Fully Supported | Tested and stable. **Recommended**. Since Droidspaces requires a custom kernel anyway, we recommend adding KernelSU to your kernel. |
| **APatch** | Supported* | *Requires enabling **Daemon Mode** in the Droidspaces app and a device reboot to bypass root-domain seccomp restrictions on userspace-initiated runtimes. |
| **Magisk** | Supported* | *Requires enabling **Daemon Mode** in the Droidspaces app and a device reboot to bypass root-domain seccomp restrictions on userspace-initiated runtimes. |

> [!TIP]
>
> Daemon Mode moves the container lifecycle management from the app's userspace to a persistent background service.

<a id="known-quirks"></a>

> [!CAUTION]
>
> **GrapheneOS is not supported** - because it blocks critical syscalls used for namespace isolation and containerization, making it impossible to run a userspace runtime like Droidspaces even with root access.
>
> **SuSFS is not supported** - DO NOT REPORT ANY BUGS WHEN USING SUSFS. If you must use SuSFS with Droidspaces, ensure that "HIDE SUS MOUNTS FOR ALL PROCESSES" is disabled in your SuSFS4KSU settings to avoid container boot failures.

<a id="android-kernel-requirements"></a>

#### Android Kernel Requirements

Android kernels are often heavily modified and may have critical container features disabled. Your kernel must have specific configuration options enabled (Namespaces, Cgroups, Seccomp, etc.) to run Droidspaces.

<a id="non-GKI"></a>

##### Non-GKI (Legacy Kernels)
Covers kernels: **3.10, 3.18, 4.4, 4.9, 4.14, 4.19**. These kernels work plug-and-play after adding the required config fragments.
See: [Legacy Kernel Configuration](Documentation/Kernel-Configuration.md#configuring-non-gki-kernels-legacy-kernels)

<a id="GKI"></a>

##### GKI (Modern Kernels)
Covers kernels: **5.4, 5.10, 5.15, 6.1+**. These kernels require additional steps to handle ABI breakage caused by configuration changes.
See: [Modern GKI Kernel Configuration](Documentation/Kernel-Configuration.md#configuring-gki-kernels)

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
| [Cool Things You Can Do (Tailscale, Docker, etc.)](Documentation/Cool-things-you-can-do.md) |
| [Uninstallation Guide](Documentation/Uninstallation.md) | How to remove Droidspaces from your system. |

---

<a id="contribution"></a>

## Contributing

Contributions are welcome - feel free to open an issue or pull request on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS).

For questions or support, join the [Telegram channel](http://t.me/Droidspaces).

To contribute translations for the Android app, visit the Weblate project:

<a href="https://hosted.weblate.org/engage/droidspaces/">
<img src="https://hosted.weblate.org/widget/droidspaces/open-graph.png" alt="Translation status" />
</a>

---

<a id="credits"></a>

## Credits & Acknowledgments

Droidspaces is built upon the incredible work of the open-source community. Special thanks to these projects for their inspiration and contributions:

*   **[LXC](https://github.com/lxc/lxc)** - For the core architectural vision and inspiration for modern Linux containerization.
*   **[Brutal-Busybox](https://github.com/feravolt/Brutal_busybox)** - For the statically-linked BusyBox binaries used in the Android userspace app to perform certain operations.
*   **[Magisk](https://github.com/topjohnwu/Magisk)** - For the `magiskpolicy` utility, providing the core engine for live SELinux patching.
*   ~~**[KernelSU-Next](https://github.com/KernelSU-Next/KernelSU-Next)**, **[MMRL](https://github.com/MMRLApp/MMRL)**, and **[LSPatch](https://github.com/LSPosed/LSPatch)** - For inspiring our modern UI design language and Android user experience.~~
*   **[ReTerminal](https://github.com/RohitKushvaha01/ReTerminal)**, **[Termux](https://github.com/termux/termux-app)** , **[LXC-Manager](https://github.com/Container-On-Android/LXC-Manager)** - Terminal Backend for the built-in Terminal emulator.
*   **[JetBrains Mono](https://www.jetbrains.com/legalforms/fonts/)** - The monospace typeface used throughout the app's terminal and code UI, licensed under the [SIL Open Font License 1.1](https://scripts.sil.org/OFL).

---

## License

Droidspaces is licensed under the [GNU General Public License v3.0](./LICENSE).

Copyright (C) 2026 [ravindu644](https://github.com/ravindu644) and contributors.

---
