<!--
title: Kernel configuration
section: Guides
order: 6
desc: How to build an Android kernel with Droidspaces support: the config options and patches for non-GKI and GKI kernels, and how to check the result.
keywords: kernel, configuration, droidspaces, android, compile, gki, nongki, patches, compatibility, kabi
-->

# Kernel configuration

Droidspaces needs kernel features that most stock Android kernels leave out: PID and IPC namespaces, a few networking pieces and devtmpfs. This page is the list of options and patches for building a kernel that has them. The oldest supported kernel is 3.10.

If you have never built an Android kernel, start with the [Android kernel tutorials](https://github.com/ravindu644/Android-Kernel-Tutorials), and check the [community-supported devices](community-supported-devices.md) page first: someone may have built a kernel for your phone already.

The steps differ between non-GKI kernels, 4.19 and older, and GKI kernels, 5.4 and newer.

## Non-GKI kernels (3.18 to 4.19)

These kernels take the options as they are, with no ABI to protect. Four steps.

### Step 1: The required options

Add these to your device's defconfig, or keep them as a fragment and merge it at build time:

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
CONFIG_CGROUP_SCHED=y
CONFIG_FAIR_GROUP_SCHED=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_NET_PRIO=y

# Resource limits: --memory, --cpus, --pids-limit, in that order, then the
# accounting that reports CPU usage on kernels before 4.15.
# Optional: a limit whose option is missing is skipped with a warning
CONFIG_MEMCG=y
# Keeps a memory-limited container out of swap. Before 6.1 only; before 5.8
# also boot with swapaccount=1 unless CONFIG_MEMCG_SWAP_ENABLED=y
CONFIG_MEMCG_SWAP=y
CONFIG_CFS_BANDWIDTH=y
CONFIG_CGROUP_PIDS=y
CONFIG_CGROUP_CPUACCT=y

# Device filesystem support
CONFIG_DEVTMPFS=y

# Overlay filesystem support (required for volatile mode)
CONFIG_OVERLAY_FS=y

# Enable xattr, posix acl support on tmpfs
# For NixOS support
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y

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

# IPv6 in NAT mode (NAT66). Optional: without these, NAT containers are IPv4 only
CONFIG_IPV6=y
CONFIG_IPV6_MULTIPLE_TABLES=y
CONFIG_IP6_NF_IPTABLES=y
CONFIG_IP6_NF_FILTER=y
CONFIG_IP6_NF_MANGLE=y
CONFIG_IP6_NF_NAT=y
CONFIG_IP6_NF_TARGET_MASQUERADE=y

# legacy compat
CONFIG_NF_CONNTRACK_IPV6=y
CONFIG_NF_NAT_IPV6=y

# Macvlan mode (--net=macvlan): a container straight on a wired LAN.
# Optional: without it, macvlan mode is unavailable
CONFIG_MACVLAN=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n

# Fix for docker unsafe procfs error
CONFIG_USER_NS=y
```

### Step 2: Firewall support, optional

Only needed to run UFW or Fail2Ban inside a container, as in the [home server recipe](Cool-things-you-can-do.md#a-phone-as-a-home-server-reachable-from-anywhere):

```makefile
# UFW & FAIL2BAN CORE
CONFIG_NETFILTER_XT_MATCH_COMMENT=y
CONFIG_NETFILTER_XT_MATCH_STATE=y
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=y
CONFIG_NETFILTER_XT_MATCH_MULTIPORT=y
CONFIG_NETFILTER_XT_MATCH_HL=y
CONFIG_IP_NF_TARGET_REJECT=y
CONFIG_IP6_NF_TARGET_REJECT=y
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

### Step 3: Patches

Apply every patch under [Documentation/resources/kernel-patches/non-GKI](./resources/kernel-patches/non-GKI/):

```bash
patch -p1 < /path/to/patchfile.patch
```

### Step 4: Build, flash, check

Build and flash as usual for your device, then open the Droidspaces app and run **Settings**, **Requirements**, **Check Requirements**. Everything under "must have" and "recommended" should pass.

## GKI kernels (5.4 and newer)

Google's Generic Kernel Image keeps a fixed kernel ABI so that vendor modules built against the stock kernel keep loading. Several options Droidspaces needs, `CONFIG_SYSVIPC`, `CONFIG_IPC_NS` and `CONFIG_POSIX_MQUEUE`, add fields to core structures and shift the ABI. Turned on as they are, the phone bootloops when the vendor's modules refuse to load. The patches below move those fields into padding the ABI already reserves, so the options can go on without changing any offset.

### Step 1: The kABI patches

> [!IMPORTANT]
>
> These are not optional. Without the patch for your kernel version, enabling the IPC options bootloops the device.

- **5.4 to 6.6:** apply the SYSVIPC patch from [kernel-patches/GKI/below-kernel-6.12](./resources/kernel-patches/GKI/below-kernel-6.12/). Start with `001.GKI-below-6.12-fix_sysvipc_kabi_6_7_8.patch`; if the device bootloops, try the alternatives in the same folder, which use different padding slots.
- **5.10 and older:** also apply `002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch` from the same folder.
- **6.12 and newer:** apply the patch in [kernel-patches/GKI/kernel-6.12](./resources/kernel-patches/GKI/kernel-6.12/).

```bash
patch -p1 < /path/to/patchfile.patch
```

### Step 2: Edit the GKI defconfig

Edit `arch/arm64/configs/gki_defconfig` in place rather than adding a fragment. For each option below, find it in the file: change `# CONFIG_X is not set` to `CONFIG_X=y`, leave it alone if it is already on, and add it at the end if it is absent.

> [!WARNING]
>
> These options are ABI-safe together with the Step 1 patches. Do not turn on anything beyond them. The two resource limit options at the end are commented out on purpose; see [CPU and process limits on GKI](#cpu-and-process-limits-on-gki) before touching them.

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

# Networking (Enhanced NAT support)
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y

# --- Below configs are optional but recommended ---

# Fix for docker unsafe procfs error
CONFIG_USER_NS=y

# IPv6 in NAT mode (NAT66)
CONFIG_IP6_NF_NAT=y
CONFIG_IP6_NF_TARGET_MASQUERADE=y

# Macvlan mode (--net=macvlan). kABI-safe: on a 5.15 GKI tree it changed none
# of the 8423 exported symbols, and the driver is self-contained
CONFIG_MACVLAN=y

# UFW support
CONFIG_IP_NF_TARGET_REJECT=y
CONFIG_IP6_NF_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y

# Fail2ban support
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y

# Enable xattr, posix acl support on tmpfs
# For NixOS support
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y

# Resource limits: --cpus and --pids-limit. CONFIG_MEMCG is already on in GKI.
# These two BREAK the kABI and no patch covers them: they resize scheduler and
# cgroup structures, which changes the CRC of thousands of exported symbols.
# Stock vendor modules then refuse to load and the device bootloops.
# Leave them commented out unless you rebuild EVERY kernel module from the
# same source and flash vendor_boot, vendor_dlkm and system_dlkm together
# with the new boot.img, all at once.
# CONFIG_CFS_BANDWIDTH=y
# CONFIG_CGROUP_PIDS=y
```

### Step 3: Build, flash, check

Build with Bazel, the AOSP build scripts or plain `make`, whichever your tree uses, and flash the result. Your stock vendor modules keep working because the ABI did not move. Then open the Droidspaces app and run **Settings**, **Requirements**, **Check Requirements**.

### CPU and process limits on GKI

`CONFIG_CFS_BANDWIDTH`, which `--cpus` needs, and `CONFIG_CGROUP_PIDS`, which `--pids-limit` needs, change the size of scheduler and cgroup structures that almost every exported kernel function refers to. On a 5.15 GKI tree they changed the checksum of about four thousand exported symbols, and no padding trick covers that. Stock vendor modules then refuse to load and the device bootloops.

Turn them on only if you can rebuild every kernel module on the device from the same source tree and configuration, and flash them in `vendor_boot`, `vendor_dlkm` and `system_dlkm` together with the new `boot.img`, all at once. If any module is a prebuilt with no source, you cannot. Do not disable `CONFIG_MODVERSIONS` or force-load modules to get past the check; the structures really did change.

Without these two options everything else works. `--cpus` and `--pids-limit` are skipped with a warning and the app greys out the two toggles. Memory limits need only `CONFIG_MEMCG`, which GKI already has. Keeping a limited container out of swap also needs `CONFIG_MEMCG_SWAP`, plus `swapaccount=1` on the kernel command line before 5.8 unless `CONFIG_MEMCG_SWAP_ENABLED` is set; without it the container can still spill into swap past its limit, the start log says so, and the app greys out **Allow swap**.

## What the check looks for

`droidspaces check`, and the app's Requirements screen, probe the running kernel rather than its config. What each probe needs, and what you lose without it:

| Probe | Option | Without it |
|-------|--------|------------|
| PID, mount, UTS and IPC namespaces | `CONFIG_NAMESPACES`, `CONFIG_PID_NS`, `CONFIG_UTS_NS`, `CONFIG_IPC_NS` | Containers cannot start. |
| pivot_root, procfs, sysfs | always present | Containers cannot start. |
| Seccomp | `CONFIG_SECCOMP`, `CONFIG_SECCOMP_FILTER` | Containers cannot start. |
| devtmpfs | `CONFIG_DEVTMPFS` | Hardware access is unavailable. |
| Cgroup namespace, cgroup v2 | `CONFIG_CGROUPS`, cgroup v2 | systemd containers misbehave. |
| New mount API | kernel 5.2, `fsopen()` and friends | systemd 259 and newer keep service credentials in a plain directory and `ProtectProc=` does nothing. On such a container Droidspaces answers EPERM for these calls so systemd takes those fallbacks instead of failing, and gives every unit and login shell `LIBMOUNT_FORCE_MOUNT2=always` so `mount` keeps using the classic call. A `mount` run behind an environment reset, `sudo` for one, needs that variable passed along. Older systemd is left alone. |
| Loop device, ext4 | `CONFIG_BLK_DEV_LOOP`, `CONFIG_EXT4_FS` | Sparse image containers cannot mount. |
| PTY, devpts, epoll, signalfd | always present on Android | Interactive sessions fail. |
| Network namespace, veth, bridge | `CONFIG_NET_NS`, `CONFIG_VETH`, `CONFIG_BRIDGE` | Only host mode works. Without the bridge alone, NAT uses a direct veth. |
| IPv6 NAT | `CONFIG_IP6_NF_NAT`, `CONFIG_IP6_NF_TARGET_MASQUERADE` | NAT containers are IPv4 only. |
| Macvlan | `CONFIG_MACVLAN` | Macvlan mode is unavailable. |
| OverlayFS | `CONFIG_OVERLAY_FS` | Volatile mode is unavailable. |
| FUSE, TUN | `CONFIG_FUSE_FS`, `CONFIG_TUN` | FUSE filesystems and VPNs inside the container do not work. |
| User namespaces | `CONFIG_USER_NS` | Allow Sandboxing is unavailable. |
| Memory, CPU and process limits | `CONFIG_MEMCG`, `CONFIG_CFS_BANDWIDTH`, `CONFIG_CGROUP_PIDS`, and no `cgroup_disable=` for them on the kernel command line | That limit is skipped. |
| Swap limit | `CONFIG_MEMCG_SWAP` with swap accounting on, `swapaccount=1` before 5.8 | A memory-limited container can still use swap. |
| CPU usage accounting | `CONFIG_CGROUP_CPUACCT` on kernels before 4.15 | `info` shows no CPU usage. |

## Which kernel version to aim for

| Version | What to expect |
|---------|----------------|
| 3.10 to 3.18 | The floor. Namespaces work, but systemd distributions might be unstable. Alpine, Artix or Gentoo with OpenRC is the safe choice. |
| 4.4 to 4.19 | Solid. Any distribution whose systemd is older than 258, and Docker inside containers. |
| 5.4 to 5.10 | Everything, including cgroup v2 and current distributions. |
| 5.15 and newer | Everything, with the best performance. |

## Where to ask

Kernel questions get answered in the [Telegram channel](https://t.me/Droidspaces). The kernel's own option reference is the [kernel documentation](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html).

---

Previous: [Anland](Anland.md) | Next: [Recipes](Cool-things-you-can-do.md)
