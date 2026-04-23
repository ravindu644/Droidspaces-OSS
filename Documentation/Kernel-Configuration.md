# Kernel Configuration Guide

This guide explains how to compile a Linux kernel with Droidspaces support for Android devices.

> [!TIP]
>
> **New to kernel compilation?** Check out the comprehensive tutorial at:
> https://github.com/ravindu644/Android-Kernel-Tutorials

---

### Quick Navigation

- [Overview](#overview)
- [Configuring Non-GKI Kernels](#non-gki)
- [Configuring GKI Kernels](#gki)
- [Testing Your Kernel](#testing)
- [Recommended Kernel Versions](#versions)
- [Nested Containers](#nested)
- [Additional Resources](#resources)

---

<a id="overview"></a>
## Overview

Droidspaces needs specific kernel options to run isolated containers. These options enable Linux namespaces, cgroups, seccomp filtering, networking, and device filesystem support.

---

<a id="non-gki"></a>
## Configuring Non-GKI Kernels (Legacy Kernels)

**Applies to:** Kernel 3.18, 4.4, 4.9, 4.14, 4.19

Non-GKI kernels are the easiest to configure. Follow these steps:

### Step 1: Mandatory Configuration

Place these options in your device defconfig or use them as a configuration fragment.

```makefile
# Kernel configurations for full DroidSpaces support
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC mechanisms
CONFIG_SYSCTL=y
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Core namespace support
CONFIG_NAMESPACES=y
CONFIG_PID_NS=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y

# Seccomp support
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# Control groups support
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_PIDS=y
CONFIG_MEMCG=y
CONFIG_CGROUP_SCHED=y
CONFIG_FAIR_GROUP_SCHED=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_NET_PRIO=y

# Device filesystem support
CONFIG_DEVTMPFS=y

# Overlay filesystem support (required for volatile mode)
CONFIG_OVERLAY_FS=y

# Firmware loading support
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_USER_HELPER=y
CONFIG_FW_LOADER_COMPRESS=y

# Droidspaces Network Isolation Support - NAT/none modes
CONFIG_NET_NS=y
CONFIG_VETH=y
CONFIG_BRIDGE=y
CONFIG_NETFILTER=y
CONFIG_BRIDGE_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NF_CONNTRACK=y
CONFIG_IP_NF_IPTABLES=y
CONFIG_IP_NF_FILTER=y
CONFIG_NF_NAT=y
CONFIG_NF_TABLES=y
CONFIG_IP_NF_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_TCPMSS=y
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y
CONFIG_NF_CONNTRACK_NETLINK=y
CONFIG_NF_NAT_REDIRECT=y
CONFIG_IP_ADVANCED_ROUTER=y
CONFIG_IP_MULTIPLE_TABLES=y

# legacy compat
CONFIG_NF_CONNTRACK_IPV4=y
CONFIG_NF_NAT_IPV4=y
CONFIG_IP_NF_NAT=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n
```

### Step 2: Firewall Support (UFW/Fail2ban) - Optional

> [!TIP]
>
> These options are only required if you want to run UFW or Fail2ban inside the container.
> 
> **Use NAT mode** when running these to avoid host networking conflicts.

```makefile
# UFW & FAIL2BAN CORE
CONFIG_NETFILTER_XT_MATCH_COMMENT=y
CONFIG_NETFILTER_XT_MATCH_STATE=y
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=y
CONFIG_NETFILTER_XT_MATCH_MULTIPORT=y
CONFIG_NETFILTER_XT_MATCH_HL=y
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_IP_NF_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_IP_NF_TARGET_ULOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y
CONFIG_NETFILTER_XT_MATCH_LIMIT=y
CONFIG_NETFILTER_XT_MATCH_HASHLIMIT=y
CONFIG_NETFILTER_XT_MATCH_OWNER=y
CONFIG_NETFILTER_XT_MATCH_PKTTYPE=y
CONFIG_NETFILTER_XT_MATCH_MARK=y
CONFIG_NETFILTER_XT_TARGET_MARK=y
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y
CONFIG_NETFILTER_NETLINK_QUEUE=y
CONFIG_NETFILTER_NETLINK_LOG=y
CONFIG_NETFILTER_XT_TARGET_NFLOG=y
```

### Step 3: Apply Patches

Apply all patches from the [Documentation/resources/kernel-patches/non-GKI](./resources/kernel-patches/non-GKI/) directory using this command:

```bash
patch -p1 < /path/to/extracted/patchfile.patch
```

### Step 4: Build, Flash and Test

1. Save the configuration blocks above as `.config` fragments or merge them into your defconfig.
2. Compile and flash the kernel to your device.
3. Verify using the Droidspaces app **Settings -> Requirements -> Check Requirements**.

---

<a id="gki"></a>
## Configuring GKI Kernels

**Applies to:** Kernel 5.4, 5.10, 5.15, 6.1, 6.6, 6.12+

Google's Generic Kernel Image (GKI) enforces strict **kABI (Kernel Application Binary Interface)** compliance. Enabling standard Droidspaces features like `CONFIG_SYSVIPC`, `CONFIG_IPC_NS` or `CONFIG_POSIX_MQUEUE` would normally shift memory offsets in the core `task_struct`, causing pre-compiled vendor modules (GPU, Camera, etc.) to crash or bootloop the device.

To solve this, Droidspaces provides specialized **kABI-friendly patches** that allow these features to be enabled without shifting offsets.

### Step 1: Apply the Mandatory kABI Patches

> [!IMPORTANT]
>
> **These patches are NOT OPTIONAL.** You **MUST** apply the correct kABI fix patches for your kernel version. Skipping these patches will cause an immediate bootloop upon enabling `CONFIG_SYSVIPC`, `CONFIG_IPC_NS` or `CONFIG_POSIX_MQUEUE`.

**For ALL Kernels BELOW 6.12 (5.4, 5.10, 5.15, 6.1, 6.6):**

- Apply the `SYSVIPC` kABI fix from [Documentation/resources/kernel-patches/GKI/below-kernel-6.12/](./resources/kernel-patches/GKI/below-kernel-6.12/):

> [!TIP]
>
> The [`001.GKI-below-6.12-fix_sysvipc_kABI_6_7_8.patch`](./resources/kernel-patches/GKI/below-kernel-6.12/001.GKI-below-6.12-fix_sysvipc_kabi_6_7_8.patch) is recommended.
>
> If this patch causes a bootloop, try the alternative patches from the same folder (e.g., `1_2_3` or `3_4_5`).

**For Kernels 5.10 and BELOW ONLY:**

- You **MUST ALSO** apply the `POSIX_MQUEUE` kABI fix:
  [Documentation/resources/kernel-patches/GKI/below-kernel-6.12/002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch](./resources/kernel-patches/GKI/below-kernel-6.12/002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch)

**For Kernels 6.12 and ABOVE:**

- Apply the patch from [Documentation/resources/kernel-patches/GKI/kernel-6.12/001.GKI-6.12-or-above-fix_sysvipc_kabi.patch](./resources/kernel-patches/GKI/kernel-6.12/001.GKI-6.12-or-above-fix_sysvipc_kabi.patch).

**How to apply the patches:**

```bash
# Apply each required patch for your kernel version
patch -p1 < /path/to/extracted/patchfile.patch
```

### Step 2: Edit `gki_defconfig`

Rather than using separate fragment files, directly edit `arch/arm64/configs/gki_defconfig`, use this **GKI-Exclusive Configuration**.

These options have been tested and proven to work across all GKI kernels without breaking ABI.

> [!WARNING]
>
> **DO NOT** enable anything other than the recommended GKI configuration below. These specific items are kABI-safe when combined with the Step 1 patch.

```makefile
# Kernel configurations for full DroidSpaces support for GKI
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Namespaces
CONFIG_IPC_NS=y
CONFIG_PID_NS=y

# HW Access Support
CONFIG_DEVTMPFS=y

# --- Below configs are optional but recommended ---

# Networking (Docker/NAT support)
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y

# UFW support
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y

# Fail2ban support
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y
```

**Workflow Rules:**
- **Do not** append this as a block at the end of the file.
- Search for each option individually.
- If an option appears as `# CONFIG_NAME is not set`, change it to `CONFIG_NAME=y`.
- If an option is already set to `CONFIG_NAME=y`, leave it alone.
- If an option does not exist, add it at the end.

### Step 3: Compile

Use your preferred build method: Bazel, the official AOSP `build.sh`/`prepare_vendor.sh` scripts, or traditional `Kbuild` with `make`.

### Step 4: Flash and Test

Flash the compiled `boot.img` or `Image` using Odin, fastboot, Heimdall, Anykernel3 or your device's preferred method. Since we used kABI-safe patches, your stock vendor modules will continue to work perfectly.

After booting, open the Droidspaces app and go to **Settings** (gear icon) -> **Requirements** -> **Check Requirements** to verify your setup.

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
