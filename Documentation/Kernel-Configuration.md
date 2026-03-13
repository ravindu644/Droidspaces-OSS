# Kernel Configuration Guide

This guide explains how to compile a Linux kernel with Droidspaces support for Android devices.

> [!TIP]
>
> **New to kernel compilation?** Check out the comprehensive tutorial at:
> https://github.com/ravindu644/Android-Kernel-Tutorials

---

### Quick Navigation

- [Overview](#overview)
- [Required Kernel Configuration](#kernel-config)
- [Recommended Kernel Patches](#kernel-patches)
- [Configuring Non-GKI Kernels](#non-gki)
- [Configuring GKI Kernels](#gki)
- [Testing Your Kernel](#testing)
- [Recommended Kernel Versions](#versions)
- [Nested Containers](#nested)
- [Additional Resources](#resources)

---

<a id="overview"></a>
## Overview

Droidspaces requires specific kernel configuration options to create isolated containers. These options enable Linux namespaces, cgroups, seccomp filtering, networking and device filesystem support.

The configuration requirements are the same for all kernel versions. The difference between non-GKI and GKI devices is in how the kernel is compiled and deployed.

---

<a id="kernel-config"></a>
## Required Kernel Configuration

Save this block as `droidspaces.config` and place it under your kernel's architecture configs folder (e.g., `arch/arm64/configs/`):

```makefile
# Kernel configurations for full DroidSpaces support
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC mechanisms (required for tools that rely on shared memory and IPC namespaces)
CONFIG_SYSCTL=y
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Core namespace support (essential for isolation and running init systems)
CONFIG_NAMESPACES=y
CONFIG_PID_NS=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y
CONFIG_USER_NS=y

# Seccomp support (enables syscall filtering and security hardening)
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# Control groups support (required for systemd and resource accounting)
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_PIDS=y
CONFIG_MEMCG=y

# Device filesystem support (enables hardware access when --hw-access is enabled)
CONFIG_DEVTMPFS=y

# Overlay filesystem support (required for volatile mode)
CONFIG_OVERLAY_FS=y

# Firmware loading support (optional, used when --hw-access is enabled)
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_USER_HELPER=y
CONFIG_FW_LOADER_COMPRESS=y

# Droidspaces Network Isolation Support - NAT/none modes
# Network namespace isolation
CONFIG_NET_NS=y

# Virtual ethernet pairs
CONFIG_VETH=y

# Bridge device
CONFIG_BRIDGE=y

# Netfilter core
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y

# Connection tracking
CONFIG_NF_CONNTRACK=y
# kernels ≤ 4.18 (Android 4.4 / 4.9)
CONFIG_NF_CONNTRACK_IPV4=y

# iptables infrastructure
CONFIG_IP_NF_IPTABLES=y

# filter table
CONFIG_IP_NF_FILTER=y

# NAT table
CONFIG_NF_NAT=y
# kernels ≤ 5.0 (Kernel 4.4 / 4.9)
CONFIG_NF_NAT_IPV4=y
CONFIG_IP_NF_NAT=y

# MASQUERADE target (renamed in 5.2)
CONFIG_IP_NF_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y

# MSS clamping
CONFIG_NETFILTER_XT_TARGET_TCPMSS=y

# Policy routing
CONFIG_IP_ADVANCED_ROUTER=y
CONFIG_IP_MULTIPLE_TABLES=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n
```

### What Each Option Does

| Config | Purpose |
|--------|---------|
| `CONFIG_SYSVIPC` | System V IPC. Required for shared memory and semaphores. |
| `CONFIG_POSIX_MQUEUE` | POSIX message queues. Required by some IPC-dependent tools. |
| `CONFIG_NAMESPACES` | Master switch for namespace support. Specifically enables Mount namespaces. |
| `CONFIG_PID_NS` | PID namespace. Gives each container its own process tree. |
| `CONFIG_UTS_NS` | UTS namespace. Allows each container to have its own hostname. |
| `CONFIG_IPC_NS` | IPC namespace. Depends on `SYSVIPC` and `POSIX_MQUEUE` (IPC NS won't appear in `menuconfig` unless these are enabled). |
| `CONFIG_USER_NS` | User namespace. Required by some distributions even when not directly used. |
| `CONFIG_SECCOMP` | Seccomp support. Enables the adaptive seccomp shield on legacy kernels. |
| `CONFIG_SECCOMP_FILTER` | BPF-based seccomp filtering. Required for the seccomp shield. |
| `CONFIG_CGROUPS` | Master switch for Control Groups. Required for systemd, resource management, and Cgroup namespaces. |
| `CONFIG_CGROUP_DEVICE` | Device access control via cgroups. |
| `CONFIG_CGROUP_PIDS` | PID limiting via cgroups. Used by systemd for process tracking. |
| `CONFIG_MEMCG` | Memory controller cgroup. Used by systemd for memory accounting. |
| `CONFIG_DEVTMPFS` | Device filesystem. Required for `/dev` setup and hardware access mode. |
| `CONFIG_OVERLAY_FS` | Overlay filesystem support. Required for volatile mode. |
| `CONFIG_NET_NS` | Network namespace. Required for NAT and None networking modes. |
| `CONFIG_VETH` | Virtual Ethernet pairs. Required for NAT mode to connect host and container. |
| `CONFIG_BRIDGE` | Bridge device support. Required for NAT mode networking. |
| `CONFIG_IP_NF_IPTABLES` | IPTables infrastructure. Required for NAT and packet filtering. |
| `CONFIG_NF_NAT` | Network Address Translation support. Required for NAT mode internet access. |
| `CONFIG_NETFILTER_XT_TARGET_MASQUERADE` | Masquerade target. Explicitly required for NAT mode on Android. |
| `CONFIG_NETFILTER_XT_TARGET_TCPMSS` | MSS Clamping. Prevents MTU issues in NAT mode over mobile data/WiFi. |
| `CONFIG_IP_ADVANCED_ROUTER` | Advanced routing. Required for isolated network namespace routing. |
| `CONFIG_ANDROID_PARANOID_NETWORK=n` | Disables Android's paranoid network restrictions which block container networking. |

---

<a id="kernel-patches"></a>
## Recommended Kernel Patches

In addition to the configuration options above, it is highly recommended for both GKI and non-GKI users to apply the patches located in the [Documentation/resources/kernel-patches](./resources/kernel-patches/) folder. These patches address critical stability issues and compatibility gaps when running containerized workloads on Android.

Applying these patches helps avoid "weird issues" and kernel panics that can occur under specific networking or resource management conditions.

> [!IMPORTANT]
> **Note to GKI users:** You can safely skip the `xt_qtaguid` patch (`01.fix_kernel_panic_in_xt_qtaguid.patch`) as this module is not available in GKI kernels.

---

<a id="non-gki"></a>

## Configuring Non-GKI Kernels (Legacy Kernels)

**Applies to:** Kernel 3.18, 4.4, 4.9, 4.14, 4.19

These kernels are the simplest to configure. The process is straightforward:

### Step 1: Prepare the Fragment

Ensure you have saved the configuration block from the [Required Configuration](#kernel-config) section as `droidspaces.config` in your architecture's config directory.

```bash
# Example for ARM64
# Place it alongside your device's defconfig
# $KERNEL_ROOT/arch/arm64/configs/droidspaces.config
```

### Step 2: Apply Recommended Patches (Optional but Recommended)

Before generating the configuration, apply the [recommended kernel patches](#kernel-patches) from the [Documentation/resources/kernel-patches](./resources/kernel-patches/) directory to your kernel source:

```bash
# General syntax
patch -p1 < /path/to/filename.patch
```

### Step 3: Merge Configuration

When generating your initial configuration, provide both your device's `defconfig` and the `droidspaces.config` fragment. The kernel's build system will merge them automatically:

```bash
# General syntax
make [BUILD_OPTIONS] <your_device>_defconfig droidspaces.config
```

> [!NOTE]
> Compiling an Android kernel requires setting various environment variables (like `ARCH`, `CC`, `CROSS_COMPILE`, `CLANG_TRIPLE`, etc.) depending on your toolchain. Ensure these are set correctly before running the `make` command.

### Step 4: Flash and Test

Flash the compiled kernel image to your device using your preferred method (Odin, fastboot, Heimdall, etc.).

After booting, verify the configuration from the App's built-in requirements checker.

All checks should pass with green checkmarks.

---

<a id="gki"></a>
## Configuring GKI Kernels (Modern Kernels)

**Applies to:** Kernel 5.4, 5.10, 5.15, 6.1+

GKI (Generic Kernel Image) devices use the [same kernel configuration](#kernel-config) as non-GKI devices. However, enabling these options on a GKI kernel introduces additional complexity:

### The ABI Problem

GKI kernels enforce a strict ABI (Application Binary Interface) between the kernel and vendor modules. Adding kernel configuration options like `CONFIG_SYSVIPC=y` or `CONFIG_CGROUP_DEVICE=y` can change the kernel's ABI, breaking compatibility with pre-built vendor modules.

### Required Additional Steps

1. **Disable module simversioning** to prevent module loading failures
2. **Handle ABI breakage** by rebuilding affected vendor modules or bypassing ABI checks

> [!WARNING]
>
> Detailed GKI configuration documentation is a work in progress. The steps for handling ABI breakage vary by device and kernel version. This section will be expanded in a future update.

---

<a id="testing"></a>
## Testing Your Kernel

After flashing a new kernel, verify Droidspaces compatibility:

### 1. Run the Requirements Check

- **On Android**: Use the built-in checker for the best experience. Go to **Settings** (gear icon) -> **Requirements** and tap **Check Requirements**.
- **On Linux / Terminal**: Run the manual check:

```bash
su -c droidspaces check
```

This checks for:
- Root access
- Kernel version (minimum 3.18)
- PID, MNT, UTS, IPC namespaces
- Network namespace (optional, for NAT/None modes)
- Cgroup namespace (optional, for modern cgroup isolation)
- devtmpfs support
- OverlayFS support (optional, for volatile mode)
- VETH and Bridge support (optional, for NAT mode)
- PTY/devpts support
- Loop device support
- ext4 support

### 2. Interpreting Results

| Result | Meaning |
|--------|---------|
| Green checkmark | Feature is available |
| Yellow warning | Feature is optional and not available (e.g., OverlayFS) |
| Red cross | Required feature is missing; containers may not work |

### 3. What to Do If Something Is Missing

| Missing Feature | Required Config | Impact if Missing |
|----------------|----------------|-------------------|
| PID namespace | `CONFIG_PID_NS=y` | **FATAL**. Containers cannot start. |
| MNT namespace | `CONFIG_NAMESPACES=y` | **FATAL**. Containers cannot start. |
| UTS namespace | `CONFIG_UTS_NS=y` | **FATAL**. Containers cannot start. |
| IPC namespace | `CONFIG_IPC_NS=y` | **FATAL**. Containers cannot start. |
| Cgroup namespace | Kernel 4.6+ and `CONFIG_CGROUPS` | Falls back to legacy cgroup bind-mounting. |
| devtmpfs | `CONFIG_DEVTMPFS=y` | **FATAL**. Static `/dev` doesn't exist; Droidspaces cannot function. |
| OverlayFS | `CONFIG_OVERLAY_FS` | Volatile mode unavailable. |
| Network namespace | `CONFIG_NET_NS=y` | NAT and None modes unavailable. |
| VETH / Bridge | `CONFIG_VETH` / `CONFIG_BRIDGE` | NAT mode isolation unavailable. |
| Seccomp | `CONFIG_SECCOMP=y` | Seccomp shield disabled; will cause boot crashes on legacy kernels. |

---

<a id="versions"></a>
## Recommended Kernel Versions

| Version | Support | Notes |
|---------|---------|-------|
| 3.18 | Legacy | **Minimum floor.** Basic namespace support. Modern distros are unstable or won't even boot. |
| 4.4 - 4.19 | Stable | **Hardened.** Full support. Nested containers (Docker/Podman) are natively supported. If you encounter systemd hangs on specific kernels (like 4.14.113) due to the VFS deadlock bug, try enabling the "Deadlock Shield" on App/`--block-nested-namespaces` in CLI, hard reboot your device, and try again. |
| 5.4 - 5.10 | Recommended | **Mainline.** Full feature support, including nested containers and modern Cgroup v2. |
| 5.15+ | Ideal | **Premium.** All features, best performance, and widest compatibility. |

---

<a id="nested"></a>
## Nested Containers (Docker, Podman, LXC)

Droidspaces natively supports nested containerization (running Docker, Podman, or LXC inside a Droidspaces container) out-of-the-box on all kernel versions. Since namespace creation restrictions are no longer hard-coded for legacy kernels, you have full freedom to deploy complex container stacks.

### Legacy Kernel Considerations (4.19 and below)

While namespace blocking is removed, legacy host kernels may still present challenges for modern nested tools:

- **Deadlock Shield Trade-off**: If your specific device suffers from the 4.14.113 `grab_super()` VFS deadlock and requires the **Deadlock Shield** to boot systemd, enabling the shield will block the namespace syscalls required by Docker,LXC and Podman. You cannot use nested containers if the shield is active.
- **Networking Incompatibilities**: Modern Docker/LXC/Podman rely on `nftables`. Legacy kernels often lack full `nftables` support. **Workaround:** Ensure you are using Droidspaces' **NAT mode**, and switch your container's alternatives configuration to use `iptables-legacy` and `ip6tables-legacy`.
- **BPF Conflicts**: Modern Docker/runc versions use `BPF_CGROUP_DEVICE` for device management. Legacy kernels lack the required BPF attach types, leading to `Invalid argument` errors.
  - **Workaround:** Configure Docker to use the older `cgroupfs` driver and `vfs` storage driver.

---

<a id="resources"></a>
## Additional Resources

- [Android Kernel Tutorials](https://github.com/ravindu644/Android-Kernel-Tutorials) by ravindu644
- [Kernel Configuration Reference](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
- [Droidspaces Telegram Channel](https://t.me/Droidspaces) for kernel-specific support
