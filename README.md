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

- **Tiny footprint:** under 300KB per platform
- **Truly native:** runs directly on Android and Linux from the same binary
- **Wide architecture support:** `aarch64`, `armhf`, `x86_64`, and `x86` as a single static binary
- **Beautiful Android app:** manage unlimited containers and do everything the CLI can, all from a clean, intuitive GUI

**Android** + **Linux Namespaces** = **Droidspaces**. Since Android is built on the Linux kernel, Droidspaces works seamlessly on Linux Desktop too. Both platforms are equally supported and maintained.

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
- [Droidspaces vs Chroot](#droidspaces-vs-chroot)
- [Droidspaces vs LXC/Docker on Android](#droidspaces-vs-lxcdocker-on-android)
- [Requirements](#requirements)
    - [Android](#a-android-devices)
        - [Rooting Requirements](#rooting-requirements)
        - [Known Quirks](#known-quirks)
        - [Android Kernel Requirements](#android-kernel-requirements)
            - [Non-GKI (Legacy Kernels)](#non-GKI)
            - [GKI (Modern Kernels)](#GKI)
    - [Linux Desktop](#b-linux-desktop)
- [Installation](#installation)
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

The entire runtime is a **single static binary** under 300KB, compiled against musl libc with no external dependencies.

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
| **Deep Android Integration** | None. Runs as a foreign process. | **Superior**. Native `init.rc` daemon or `post-fs-data.sh` modes. |
| **Persistence** | Poor. Easily killed by system. | **Unkillable**. Init-level auto-spawn support. |
| Dependencies | Many (liblxc, runc, containerd, etc.) | Zero. Single static binary. Runs on anything under the sun, including Android recovery if the kernel supports the features required by Droidspaces ! |
| Setup Complexity | High. Requires Termux, cross-compiled libraries, manual config files. | Low. Download and install the APK, then run it on Android; download, extract, and run it on Linux. |
| Older kernels Support | Spotty. Many features break on older kernels. | Full. Adaptive seccomp shield handles kernel quirks. |
| **Network Isolation** | **Broken on Android**. Even with all kernel configs enabled, network isolation with internet access never works. | **First-in-Class**. Perfectly handles network isolation with internet access on Android out of the box. |
| Binary Size | 10MB+ (plus dependencies) | Under 300KB per architecture. |
| Android Optimizations | None. Not designed for Android. | Yes. SELinux handling, FBE keyring management, storage integration, networking fixes |
| Termux Required | Often. Used as the execution environment. | Never. Runs directly as a native binary. Android app does have a built-in Terminal! |
| Nested Containers | Complex setup required. | Supported natively on all kernels out of the box. |
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
Covers kernels: **3.18, 4.4, 4.9, 4.14, 4.19**. These kernels work plug-and-play after adding the required config fragments.
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
*   **[KernelSU-Next](https://github.com/KernelSU-Next/KernelSU-Next)**, **[MMRL](https://github.com/MMRLApp/MMRL)**, and **[LSPatch](https://github.com/LSPosed/LSPatch)** - For inspiring our modern UI design language and Android user experience.
*   **[ReTerminal](https://github.com/RohitKushvaha01/ReTerminal)**, **[Termux](https://github.com/termux/termux-app)** , **[LXC-Manager](https://github.com/Container-On-Android/LXC-Manager)** - Terminal Backend for the built-in Terminal emulator.

---

## License

Droidspaces is licensed under the [GNU General Public License v3.0](./LICENSE).

Copyright (C) 2026 [ravindu644](https://github.com/ravindu644) and contributors.

---
