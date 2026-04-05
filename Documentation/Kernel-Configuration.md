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
- [Additional Kernel Configuration for UFW/Fail2ban](#additional-kernel-config)
- [Configuring Non-GKI Kernels](#non-gki)
- [Configuring GKI Kernels](#gki)
    - [Method 1](#method-1-legacy-patching-boot-image-only) - Unprofessional way
    - [Method 2](#method-2-the-hybrid-lkm-workflow-recommended) - Recommended way
- [Testing Your Kernel](#testing)
- [Recommended Kernel Versions](#versions)
- [Nested Containers](#nested)
- [Additional Resources](#resources)

---

<a id="overview"></a>
## Overview

Droidspaces needs specific kernel options to run isolated containers. These options enable Linux namespaces, cgroups, seccomp filtering, networking, and device filesystem support.

The required configuration is the same for all kernel versions. The only difference between non-GKI and GKI devices is how the kernel is compiled and deployed.

---

<a id="kernel-config"></a>
## Required Kernel Configuration

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

# Seccomp support (enables syscall filtering and security hardening)
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# Control groups support (required for systemd and resource accounting)
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_PIDS=y
CONFIG_MEMCG=y
CONFIG_CGROUP_SCHED=y
CONFIG_FAIR_GROUP_SCHED=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_NET_PRIO=y

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
CONFIG_BRIDGE_NETFILTER=y
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

# NF Tables
CONFIG_NF_TABLES=y

# kernels ≤ 5.0 (Kernel 4.4 / 4.9)
CONFIG_NF_NAT_IPV4=y
CONFIG_IP_NF_NAT=y

# MASQUERADE target (renamed in 5.2)
CONFIG_IP_NF_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y

# MSS clamping
CONFIG_NETFILTER_XT_TARGET_TCPMSS=y

# addrtype match (required for --dst-type LOCAL DNAT port forwarding)
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y

# Conntrack netlink + NAT redirect (required for stateful NAT)
CONFIG_NF_CONNTRACK_NETLINK=y
CONFIG_NF_NAT_REDIRECT=y

# Policy routing
CONFIG_IP_ADVANCED_ROUTER=y
CONFIG_IP_MULTIPLE_TABLES=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n
```
---

<a id="additional-kernel-config"></a>
## Additional Kernel Configuration for UFW/Fail2ban

> [!TIP]
> These options are not required for basic Droidspaces usage. Only add them if you want to run a firewall (UFW or Fail2ban) inside a Droidspaces container.

**Use NAT mode when running UFW or Fail2ban.** Running them in host mode will conflict with the host's networking stack.

```makefile
# UFW CORE
CONFIG_NETFILTER_XT_MATCH_COMMENT=y
CONFIG_NETFILTER_XT_MATCH_STATE=y
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=y
CONFIG_NETFILTER_XT_MATCH_MULTIPORT=y
CONFIG_NETFILTER_XT_MATCH_HL=y
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_IP_NF_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_IP_NF_TARGET_ULOG=y

# FAIL2BAN CORE
CONFIG_NETFILTER_XT_MATCH_RECENT=y
CONFIG_NETFILTER_XT_MATCH_LIMIT=y
CONFIG_NETFILTER_XT_MATCH_HASHLIMIT=y
CONFIG_NETFILTER_XT_MATCH_OWNER=y
CONFIG_NETFILTER_XT_MATCH_PKTTYPE=y
CONFIG_NETFILTER_XT_MATCH_MARK=y
CONFIG_NETFILTER_XT_TARGET_MARK=y

# IPSET (efficient fail2ban banlists)
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y

# NFNETLINK / logging
CONFIG_NETFILTER_NETLINK_QUEUE=y
CONFIG_NETFILTER_NETLINK_LOG=y
CONFIG_NETFILTER_XT_TARGET_NFLOG=y
```

---

<a id="non-gki"></a>
## Configuring Non-GKI Kernels (Legacy Kernels)

**Applies to:** Kernel 3.18, 4.4, 4.9, 4.14, 4.19

Non-GKI kernels are the easiest to configure. Follow these steps:

### Step 1: Apply the Non-GKI Patches

Apply all patches from the [Documentation/resources/kernel-patches/non-GKI](./resources/kernel-patches/non-GKI/) directory to your kernel source before doing anything else:

```bash
patch -p1 < /path/to/filename.patch
```

### Step 2: Place the Config Fragments

Save the [required kernel configuration](#kernel-config) block as `droidspaces.config` and place it in your kernel's architecture config folder (e.g., `arch/arm64/configs/`). If you want to use UFW or Fail2ban, also save the [additional kernel configuration](#additional-kernel-config) block as `droidspaces-additional.config` and place it in the same folder.

```bash
# For ARM64 devices, place them alongside your device defconfig:
# $KERNEL_ROOT/arch/arm64/configs/droidspaces.config
# $KERNEL_ROOT/arch/arm64/configs/droidspaces-additional.config  (optional)
```

### Step 3: Merge the Configuration

Pass your device defconfig and the Droidspaces fragment(s) to `make`. The kernel build system will merge them automatically:

```bash
make [BUILD_OPTIONS] <your_device>_defconfig droidspaces.config droidspaces-additional.config
```

> [!NOTE]
> You need to set environment variables like `ARCH`, `CC`, `CROSS_COMPILE`, and `CLANG_TRIPLE` before running `make`, depending on your toolchain. Make sure these are configured correctly for your device.

### Step 4: Flash and Test

Flash the compiled kernel to your device using Odin, fastboot, Heimdall, or whatever method your device supports.

After booting, open the Droidspaces app and go to **Settings** (gear icon) -> **Requirements** -> **Check Requirements**. All checks should pass with green checkmarks.

<a id="gki"></a>
## Configuring GKI Kernels

**Applies to:** Kernel 5.4, 5.10, 5.15, 6.1+

---

### The GKI "Built-in" Trap

Core Droidspaces features - specifically **IPC Namespaces (`CONFIG_IPC_NS`)** and **Devices Control Groups (`CONFIG_CGROUP_DEVICE`)** - **CANNOT BE COMPILED AS MODULES (`=m`)**. They must be built directly into the kernel image (`=y`).

Because these features are built-in, enabling them fundamentally changes the core kernel's internal data structures. This **IMMEDIATELY BREAKS THE GKI ABI**.

In GKI, an ABI break is an "all-or-nothing" event. Once the internal structures change, **EVERY SINGLE KERNEL MODULE (`.ko`)** in the system - including critical vendor drivers for your GPU, Camera, Audio, and Sensors - must be recompiled and re-linked against the new kernel image.

---

### Why "Hacking" CRC is Dangerous: A Technical Deep Dive

Many developers try to bypass the kernel's **CRC** to force the kernel to load vendor modules even after the ABI has broken. This is a recipe for disaster.

#### The Memory Offset Problem

Imagine a kernel structure used by a vendor GPU driver:

```c
struct b {
    int varX;  // Offset: 0x0 (32-bit)
    long varY; // Offset: 0x20 (64-bit)
    long varZ; // Offset: 0x60 (64-bit)
};
```

The pre-compiled vendor module expects `varY` to be exactly at `&b + 0x20`.

Now, if you enable a Droidspaces config that adds a new variable (`varC`) to this struct to support namespaces:

```c
struct b {
    int varX;  // Offset: 0x0
    int varC;  // Offset: 0x20 (NEW VARIABLE)
    long varY; // Offset: 0x40 (SHIFTED!)
    long varZ; // Offset: 0x80 (SHIFTED!)
};
```

If you force the kernel to load the old vendor module by bypassing CRC checks, the module will try to read `varY` from the old offset (`0x20`). It will actually be reading `varC`.

**The Result**: The module writes data to the wrong memory addresses. This causes:
- **Silent Memory Corruption**: Your OS will behave "weirdly," apps will crash randomly, and you may lose data.
- **Hardware Panics**: The phone will shut off instantly without a single line of logs because the kernel has corrupted its own state.

For real-world examples of these failures, see [Issue #23](https://github.com/ravindu644/Droidspaces-OSS/issues/23) and [Issue #26](https://github.com/ravindu644/Droidspaces-OSS/issues/26).

---

### Method 1: Legacy Patching (Boot Image Only)

> [!CAUTION]
> 
> **KERNEL 5.4 / 5.10 / 5.15 ONLY. THIS METHOD IS COMPLETELY BROKEN ON KERNEL 6.1 AND HIGHER.**
>
> This method uses ABI/CRC bypass patches and only replaces `boot.img`. It may work on older GKI kernels in some device configurations, but it is inherently unstable and relies on the memory corruption behaviour described above.
>
> **If you choose to proceed, you accept full responsibility for the outcome.** Issues caused by this method - bootloops, random power-offs, data corruption, camera or sensor failures, or anything else - **will be closed immediately without investigation.** You have been warned.

#### Step 1: Apply the GKI Patches

Apply **all** patches from the [Documentation/resources/kernel-patches/GKI](./resources/kernel-patches/GKI/) directory to your kernel source:

```bash
patch -p1 < /path/to/filename.patch
```

Every patch in this directory is required. Skipping even one will cause a bootloop, as ABI compatibility with pre-compiled vendor modules cannot be maintained without them.

#### Step 2: Edit `gki_defconfig`

Rather than using separate fragment files in the GKI build system, directly edit `arch/arm64/configs/gki_defconfig`.

Follow these rules:

- **Do not** append the contents of [required kernel configuration](#kernel-config) or [additional kernel configuration](#additional-kernel-config) to the end of `gki_defconfig` as a block.
- Search for each option individually.
- If an option appears as `# CONFIG_NAME is not set`, change it to `CONFIG_NAME=y`.
- If an option is already set to `CONFIG_NAME=y`, leave it alone.
- If an option does not exist anywhere in the file, add it at the end.

#### Step 3: Compile

Use your preferred build method: Bazel, the official AOSP `build.sh`/`prepare_vendor.sh` scripts, or traditional `Kbuild` with `make`.

#### Step 4: Flash and Test

Flash **only** the compiled `boot.img` using Odin, fastboot, Heimdall, or your device's preferred method.

After booting, open the Droidspaces app and go to **Settings** (gear icon) -> **Requirements** -> **Check Requirements**. If something does not pass, refer to the [Testing](#testing) section and debug it yourself.

---

### Method 2: The Hybrid LKM Workflow (Recommended)

Replacing only `boot.img` is not enough for a stable system. You must update the entire module ecosystem of your device. This is the correct way to handle a GKI ABI break.

#### For Intermediate Users

Recommended for those who can build kernels but are new to complex LKM management.

- **Build**: Compile your kernel with all Droidspaces features AND all required LKMs.

- **Identify**: Run [LKM_Tools](https://github.com/ravindu644/LKM_Tools) to build `vendor_boot.img`, `vendor_dlkm.img`, `system_dlkm.img`. If it reports **missing modules** (vendor-specific modules that were not built from source):
    - **Grab**: Extract those missing modules from your stock ROM.
    - **Stage**: Place the stock `.ko` files in the staging directory.
    - **Patch**: **ONLY NOW** apply the [CRC patch](./resources/kernel-patches/GKI/01.disable_crc_checks_for_lkms.patch) to the kernel's module loader to allow these few mismatched modules to be accepted.
    - **Re-trigger**: Run LKM_Tools again to "perfectly wire up" the stock modules into your partitions.

- **Flash**: Flash **all** generated images (`boot`, `vendor_boot`, `system_dlkm`, `vendor_dlkm`) in a single "one-shot" operation.

> [!TIP]
>
> This method is significantly more stable than a global CRC bypass. Since 90% of your modules are compiled natively against your new kernel, you are only bypassing CRC for ~30 stock modules instead of hundreds. It's "better than nothing" if you lack full source code for every vendor driver.

#### For Advanced Users

The only production-grade way to build a GKI kernel. This method ensures 100% ABI compatibility and system stability with zero compromises.

- **Strict ABI Compliance**: **DO NOT APPLY ANY CRC OR ABI RELATED PATCHES.** Your entire tree must be compiled with strict symbol versioning enabled.

- **Source-level Wiring**: Identify which LKMs are missing using [LKM_Tools](https://github.com/ravindu644/LKM_Tools). If an LKM is not building, manually wire it into your kernel's `Drivers/{Makefile, Kconfig}`.

- **Kanging Missing Sources**: If the entire source code for a specific vendor LKM is missing from your tree, you must "kang" (backport/pull) the source from another GKI kernel tree from GitHub, wire it up, and compile it locally.

- **One-Shot Packaging**: Use LKM Tools to "perfectly wire up" the resulting `.ko` files across the DLKM partitions.

- **Flash**: Deploy the full set of images (boot + all DLKMs) in a single session.

---

### Why LKM Tools?

A common question among developers is why they should use [LKM_Tools](https://github.com/ravindu644/LKM_Tools) instead of relying on the standard Bazel/AOSP build scripts (`build.sh`, `prepare_vendor.sh`).

The reason is simple: **Module loading order is critical for system stability.**

1. **OEM Specialization**: Your device manufacturer uses a specific `modules.load` configuration that defines exactly which modules are loaded and in what precise sequence.
2. **Incomplete/Useless Images**: Standard AOSP/Bazel build scripts generate a "generic" `vendor_boot.img` that is often incomplete and unusable for production. For example, if a stock OEM `vendor_boot` contains 200 LKMs, the generic AOSP script may build a `vendor_boot` with as few as ~100 modules. These missing modules are critical for hardware initialization.
3. **The Solution**: [LKM_Tools](https://github.com/ravindu644/LKM_Tools) is designed to handle this complexity. It replicates the stock OEM module layout, ensuring your newly compiled modules are wired and loaded in the exact same sequence as the stock ROM.

---

### Reference & Tooling

Do not attempt Method 2 without following a proven reference implementation.

- **Gold Standard Reference**: [ravindu644/android_kernel_a166p](https://github.com/ravindu644/android_kernel_a166p) (Used to wire ~600 modules with zero hacks).
- **Essential Tool**: [ravindu644/LKM_Tools](https://github.com/ravindu644/LKM_Tools) (Automates the complex re-packaging of DLKM partitions).

---

<a id="testing"></a>
## Testing Your Kernel

### 1. Run the Requirements Check

- **In the app**: Go to **Settings** (gear icon) -> **Requirements** -> **Check Requirements**.
- **In a terminal**: Run:

```bash
su -c droidspaces check
```

This checks for:

- Root access
- Kernel version (minimum 3.18)
- PID, MNT, UTS, IPC namespaces
- Network namespace (optional, required for NAT/None modes)
- Cgroup namespace (optional, for modern cgroup isolation)
- devtmpfs support
- OverlayFS support (optional, for volatile mode)
- VETH and Bridge support (optional, for NAT mode)
- PTY/devpts support
- Loop device support
- ext4 support

### 2. Understanding the Results

| Result | Meaning |
|--------|---------|
| Green checkmark | Feature is available |
| Yellow warning | Feature is optional and not available (e.g., OverlayFS) |
| Red cross | Required feature is missing; containers may not work |

### 3. What to Do If Something Is Missing

| Missing Feature | Required Config | Impact if Missing |
|----------------|----------------|-------------------|
| PID namespace | `CONFIG_PID_NS=y` | **Fatal.** Containers cannot start. |
| MNT namespace | `CONFIG_NAMESPACES=y` | **Fatal.** Containers cannot start. |
| UTS namespace | `CONFIG_UTS_NS=y` | **Fatal.** Containers cannot start. |
| IPC namespace | `CONFIG_IPC_NS=y` | **Fatal.** Containers cannot start. |
| Cgroup device | `CONFIG_CGROUP_DEVICE=y` | **Fatal.**  Containers cannot start. |
| devtmpfs | `CONFIG_DEVTMPFS=y` | **Fatal.** Droidspaces cannot set up `/dev`. |
| OverlayFS | `CONFIG_OVERLAY_FS` | Volatile mode unavailable. |
| Network namespace | `CONFIG_NET_NS=y` | NAT and None modes unavailable. |
| VETH / Bridge | `CONFIG_VETH` / `CONFIG_BRIDGE` | NAT mode unavailable. |
| Seccomp | `CONFIG_SECCOMP=y` | Seccomp shield disabled. Security risk. |

---

<a id="versions"></a>
## Recommended Kernel Versions

| Version | Support | Notes |
|---------|---------|-------|
| 3.18 | Legacy | Minimum supported version. Basic namespace support only. Modern distros are unstable or may not boot at all. |
| 4.4 - 4.19 | Stable | Full support. Nested containers (Docker/Podman) work natively. If you hit systemd hangs on kernels like 4.14.113 due to the VFS deadlock bug, try enabling the "Deadlock Shield" in the app or passing `--block-nested-namespaces` in the CLI, then hard reboot and try again. |
| 5.4 - 5.10 | Recommended | Full feature support including nested containers and modern cgroup v2. |
| 5.15+ | Ideal | All features, best performance, and the widest compatibility. |

---

<a id="nested"></a>
## Nested Containers (Docker, Podman, LXC)

Droidspaces supports running Docker, Podman, or LXC inside a container out of the box on all supported kernel versions.

### Legacy Kernel Considerations (4.19 and below)

Legacy kernels may present some challenges for modern nested container tools:

- **Deadlock Shield trade-off**: If your device is affected by the 4.14.113 `grab_super()` VFS deadlock and requires the Deadlock Shield to boot systemd, enabling the shield will also block the namespace syscalls that Docker, LXC, and Podman need. You cannot use nested containers while the shield is active.

- **Networking incompatibilities**: Modern Docker, LXC, and Podman rely on `nftables`. Legacy kernels often lack full `nftables` support. To work around this, use Droidspaces in NAT mode and switch your container's iptables alternative to `iptables-legacy` and `ip6tables-legacy`.

- **BPF conflicts**: Modern Docker and runc use `BPF_CGROUP_DEVICE` for device management. Legacy kernels do not support the required BPF attach types, which causes `Invalid argument` errors. To work around this, configure Docker to use the `cgroupfs` driver and the `vfs` storage driver.

---

<a id="resources"></a>
## Additional Resources

- [Android Kernel Tutorials](https://github.com/ravindu644/Android-Kernel-Tutorials) by ravindu644
- [Kernel Configuration Reference](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
- [Droidspaces Telegram Channel](https://t.me/Droidspaces) for kernel-specific support
