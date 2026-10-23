<!--
title: 内核配置
section: Guides
order: 6
desc: 如何构建支持 Droidspaces 的 Android 内核：非 GKI 和 GKI 内核所需的配置选项与补丁，以及如何检查结果。
keywords: kernel, configuration, droidspaces, android, compile, gki, nongki, patches, compatibility, kabi
-->

# 内核配置

Droidspaces 需要一些多数 Android 原厂内核没有启用的内核特性：PID 和 IPC 命名空间、一些网络组件以及 devtmpfs。本页列出了构建具备这些特性的内核所需的选项和补丁。支持的最旧内核是 3.10。

如果你从未编译过 Android 内核，请先从 [Android 内核教程](https://github.com/ravindu644/Android-Kernel-Tutorials)开始，并先查看[社区支持的设备](./community-supported-devices.md)页面：可能已经有人为你的手机构建好了内核。

这些步骤在非 GKI 内核（4.19 及更早）和 GKI 内核（5.4 及更新）之间有所不同。

## 非 GKI 内核（3.18 到 4.19）

这些内核可以直接接受这些选项，没有需要保护的 ABI。共四步。

### 第 1 步：必需选项

把这些加入你设备的 defconfig，或保留为 fragment 并在构建时合并：

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

<a id="step-2-firewall-support-optional"></a>

### 第 2 步：防火墙支持（可选）

只有在容器内运行 UFW 或 Fail2Ban 时才需要，例如[家庭服务器配方](./Cool-things-you-can-do.md#把手机变成随处可访问的家庭服务器)：

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

### 第 3 步：补丁

应用 [Documentation/resources/kernel-patches/non-GKI](../resources/kernel-patches/non-GKI/) 下的所有补丁：

```bash
patch -p1 < /path/to/patchfile.patch
```

### 第 4 步：构建、刷入、检查

按你设备的常规方式构建并刷入，然后打开 Droidspaces 应用，依次进入 **设置**、**要求**、**检查要求**。"必须"和"推荐"下的所有项都应该通过。

## GKI 内核（5.4 及更新）

Google 的 Generic Kernel Image 保持固定的内核 ABI，以便针对原厂内核构建的厂商模块继续加载。Droidspaces 需要的若干选项 `CONFIG_SYSVIPC`、`CONFIG_IPC_NS` 和 `CONFIG_POSIX_MQUEUE` 会向核心结构体添加字段并移动 ABI。直接开启它们，厂商模块会拒绝加载，手机就会无限重启。下面的补丁把这些字段移入 ABI 已经预留的填充区，因此可以在不改变任何偏移的情况下开启这些选项。

### 第 1 步：kABI 补丁

> [!IMPORTANT]
>
> 这些补丁不是可选的。没有对应你内核版本的补丁，开启 IPC 选项会让设备无限重启。

- **5.4 到 6.6：** 应用 [kernel-patches/GKI/below-kernel-6.12](../resources/kernel-patches/GKI/below-kernel-6.12/) 中的 SYSVIPC 补丁。先试 `001.GKI-below-6.12-fix_sysvipc_kabi_6_7_8.patch`；如果设备无限重启，再试同一目录下使用不同填充槽的替代补丁。
- **5.10 及更旧：** 还要应用同一目录下的 `002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch`。
- **6.12 及更新：** 应用 [kernel-patches/GKI/kernel-6.12](../resources/kernel-patches/GKI/kernel-6.12/) 中的补丁。

```bash
patch -p1 < /path/to/patchfile.patch
```

### 第 2 步：修改 GKI defconfig

直接编辑 `arch/arm64/configs/gki_defconfig`，而不是添加 fragment。对下面每个选项，在文件中找到它：把 `# CONFIG_X is not set` 改成 `CONFIG_X=y`，如果已经是开启状态就保持不动，如果不存在就添加到末尾。

> [!WARNING]
>
> 配合第 1 步的补丁，这些选项是 ABI 安全的。不要开启这之外的任何选项。末尾的两个资源限制选项是有意注释掉的；在改动它们之前，请先阅读 [GKI 上的 CPU 和进程限制](#cpu-and-process-limits-on-gki)。

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
CONFIG_NETFILTER_XT_TARGET_REJECT=y
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

### 第 3 步：构建、刷入、检查

用 Bazel、AOSP 构建脚本或纯 `make` 构建（取决于你的源码树），然后刷入结果。由于 ABI 没有移动，原厂厂商模块仍能正常工作。然后打开 Droidspaces 应用，依次进入 **设置**、**要求**、**检查要求**。

<a id="cpu-and-process-limits-on-gki"></a>

### GKI 上的 CPU 和进程限制

`--cpus` 需要的 `CONFIG_CFS_BANDWIDTH` 和 `--pids-limit` 需要的 `CONFIG_CGROUP_PIDS` 会改变调度器和 cgroup 结构体的大小，而几乎所有导出的内核函数都会引用这些结构体。在 5.15 GKI 树上，它们改变了大约四千个导出符号的校验和，没有任何填充技巧能覆盖这一点。原厂厂商模块随后会拒绝加载，设备无限重启。

只有当你能够用同一份源码树和配置重新构建设备上的每一个内核模块，并把它们放进 `vendor_boot`、`vendor_dlkm` 和 `system_dlkm`，与新 `boot.img` 一起一次性刷入时，才开启它们。只要有任何模块是没有源码的预编译文件，你就做不到。不要通过禁用 `CONFIG_MODVERSIONS` 或强制加载模块来绕过检查；结构体确实变了。

没有这两个选项，其他一切仍然可用。`--cpus` 和 `--pids-limit` 会被跳过并给出警告，App 会把这两个开关置灰。内存限制只需要 `CONFIG_MEMCG`，GKI 已经具备。要让受限容器不进入 swap，还需要 `CONFIG_MEMCG_SWAP`，并在 5.8 之前的内核命令行上加上 `swapaccount=1`（除非设置了 `CONFIG_MEMCG_SWAP_ENABLED`）；否则容器仍可能越过限制溢入 swap，启动日志会说明这一点，App 也会把 **允许交换空间（swap）** 置灰。

## 检查会看什么

`droidspaces check` 和 App 的"要求"页面探测的是正在运行的内核，而不是它的配置。每项探测需要什么，以及缺少它时会失去什么：

| 探测项 | 选项 | 缺少时 |
|-------|--------|------------|
| PID、mount、UTS 和 IPC 命名空间 | `CONFIG_NAMESPACES`、`CONFIG_PID_NS`、`CONFIG_UTS_NS`、`CONFIG_IPC_NS` | 容器无法启动。 |
| pivot_root、procfs、sysfs | 始终存在 | 容器无法启动。 |
| Seccomp | `CONFIG_SECCOMP`、`CONFIG_SECCOMP_FILTER` | 容器无法启动。 |
| devtmpfs | `CONFIG_DEVTMPFS` | 硬件访问不可用。 |
| Cgroup 命名空间、cgroup v2 | `CONFIG_CGROUPS`、cgroup v2 | systemd 容器行为异常。 |
| Loop 设备、ext4 | `CONFIG_BLK_DEV_LOOP`、`CONFIG_EXT4_FS` | 稀疏镜像容器无法挂载。 |
| PTY、devpts、epoll、signalfd | Android 上始终存在 | 交互式会话失败。 |
| 网络命名空间、veth、网桥 | `CONFIG_NET_NS`、`CONFIG_VETH`、`CONFIG_BRIDGE` | 只有主机模式可用。仅缺少网桥时，NAT 会使用直连 veth。 |
| IPv6 NAT | `CONFIG_IP6_NF_NAT`、`CONFIG_IP6_NF_TARGET_MASQUERADE` | NAT 容器仅支持 IPv4。 |
| Macvlan | `CONFIG_MACVLAN` | Macvlan 模式不可用。 |
| OverlayFS | `CONFIG_OVERLAY_FS` | 易失模式不可用。 |
| FUSE、TUN | `CONFIG_FUSE_FS`、`CONFIG_TUN` | 容器内的 FUSE 文件系统和 VPN 无法工作。 |
| 用户命名空间 | `CONFIG_USER_NS` | 允许沙箱不可用。 |
| 内存、CPU 和进程限制 | `CONFIG_MEMCG`、`CONFIG_CFS_BANDWIDTH`、`CONFIG_CGROUP_PIDS`，且内核命令行上没有针对它们的 `cgroup_disable=` | 对应的限制会被跳过。 |
| Swap 限制 | 开启 swap 统计的 `CONFIG_MEMCG_SWAP`，5.8 之前需要 `swapaccount=1` | 受内存限制的容器仍可使用 swap。 |
| CPU 用量统计 | 4.15 之前的内核需要 `CONFIG_CGROUP_CPUACCT` | `info` 不显示 CPU 用量。 |

## 该选哪个内核版本

| 版本 | 预期情况 |
|---------|----------------|
| 3.10 到 3.18 | 最低门槛。命名空间可用，但 systemd 发行版可能不稳定。Alpine、Artix 或带 OpenRC 的 Gentoo 是稳妥选择。 |
| 4.4 到 4.19 | 稳定。可使用任何 systemd 早于 258 的发行版，以及容器内的 Docker。 |
| 5.4 到 5.10 | 一切可用，包括 cgroup v2 和当前发行版。 |
| 5.15 及更新 | 一切可用，性能最佳。 |

## 到哪里提问

内核问题可以在 [Telegram 频道](https://t.me/Droidspaces)得到解答。内核自身的选项参考是[内核文档](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)。

---

上一页：[Anland](./Anland.md) | 下一页：[实用配方](./Cool-things-you-can-do.md)
