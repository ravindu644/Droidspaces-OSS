<!--
title: 内核配置指南
section: 指南
order: 3
desc: 编译支持 Droidspaces 的自定义 Android 内核完整指南，包括非 GKI 和 GKI 补丁及兼容性修复。
keywords: 内核, 配置, droidspaces, android, 编译, gki, 非gki, 补丁, 兼容性
-->

# 内核配置指南

本指南介绍如何为 Android 设备编译支持 Droidspaces 的 Linux 内核。

> [!TIP]
>
> **内核编译新手？** 查看综合教程：
> https://github.com/ravindu644/Android-Kernel-Tutorials

---

### 快速导航

- [概述](#overview)
- [配置非 GKI 内核](#non-gki)
- [配置 GKI 内核](#gki)
- [测试您的内核](#testing)
- [推荐的内核版本](#versions)
- [嵌套容器](#nested)
- [附加资源](#resources)

---

<a id="overview"></a>
## 概述

Droidspaces 需要特定的内核选项来运行隔离的容器。这些选项将会启用 Linux 命名空间、cgroups、seccomp 过滤、网络以及设备文件系统支持。

---

<a id="non-gki"></a>
## 配置非 GKI 内核（旧版内核）

**适用对象：** 内核 3.18、4.4、4.9、4.14、4.19

非 GKI 内核是最容易配置的。请按以下步骤操作：

### 步骤 1：必要配置

将这些选项放入您的设备 defconfig 中，或将其用作配置片段。

```makefile
# 完全支持Droidspaces的内核配置
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC 机制
CONFIG_SYSCTL=y
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# 核心命名空间支持
CONFIG_NAMESPACES=y
CONFIG_PID_NS=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y

# Seccomp 支持
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# 控制组支持
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_PIDS=y
CONFIG_MEMCG=y
CONFIG_CGROUP_SCHED=y
CONFIG_FAIR_GROUP_SCHED=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_NET_PRIO=y

# 设备文件系统支持
CONFIG_DEVTMPFS=y

# Overlay 文件系统支持（易失模式必需）
CONFIG_OVERLAY_FS=y

# 启用 tmpfs 上的 xattr、posix acl 支持
# 用于 NixOS 支持
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y

# 固件加载支持
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_USER_HELPER=y
CONFIG_FW_LOADER_COMPRESS=y

# Droidspaces 网络隔离支持 - NAT/None 模式
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

# 旧版兼容
CONFIG_NF_CONNTRACK_IPV4=y
CONFIG_NF_NAT_IPV4=y
CONFIG_IP_NF_NAT=y

# 在旧内核上禁用此选项以使互联网正常工作
CONFIG_ANDROID_PARANOID_NETWORK=n
```

### 步骤 2：防火墙支持（UFW/Fail2ban） - 可选

> [!TIP]
>
> 仅当您想在容器内运行 UFW 或 Fail2ban 时才需要这些选项。
> 
> 运行这些服务时请**使用 NAT 模式**以避免宿主网络冲突。

```makefile
# UFW & FAIL2BAN 核心
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

### 步骤 3：应用补丁

使用以下命令应用 [Documentation/resources/kernel-patches/non-GKI](./resources/kernel-patches/non-GKI/) 目录中的所有补丁：

```bash
patch -p1 < /path/to/extracted/patchfile.patch
```

### 步骤 4：编译、刷入和测试

1. 将上述配置块保存为 `.config` 片段或合并到您的 defconfig 中。
2. 编译内核并将其刷入您的设备。
3. 通过 Droidspaces 应用 **设置 -> 需求检查 -> 检查需求** 来验证。

---

<a id="gki"></a>
## 配置 GKI 内核

**适用对象：** 内核 5.4、5.10、5.15、6.1、6.6、6.12+

Google 的通用内核镜像 (GKI) 强制执行严格的 **kABI（内核应用二进制接口）** 兼容性。启用像 `CONFIG_SYSVIPC` 或 `CONFIG_IPC_NS` 这样的标准 Droidspaces 功能通常会移动核心 `task_struct` 中的内存偏移量，导致预编译的供应商模块（GPU、摄像头等）崩溃或设备无限重启。

为解决此问题，Droidspaces 提供了专用的 **kABI 友好补丁**，使得在不改变内存偏移量的情况下启用这些功能。

### 步骤 1：应用必要的 kABI 补丁

> [!IMPORTANT]
>
> **这些补丁不是可选的。** 您**必须**为您的内核版本应用正确的 kABI 修复补丁。跳过这些补丁将在启用 `CONFIG_SYSVIPC`、`CONFIG_IPC_NS` 或 `CONFIG_POSIX_MQUEUE` 后立即导致设备无限重启。

**对于所有 6.12 以下的内核（5.4、5.10、5.15、6.1、6.6）：**

- 从 [Documentation/resources/kernel-patches/GKI/below-kernel-6.12/](./resources/kernel-patches/GKI/below-kernel-6.12/) 应用 `SYSVIPC` kABI 修复：

> [!TIP]
>
> 推荐使用 [`001.GKI-below-6.12-fix_sysvipc_kABI_6_7_8.patch`](./resources/kernel-patches/GKI/below-kernel-6.12/001.GKI-below-6.12-fix_sysvipc_kabi_6_7_8.patch)。
>
> 如果此补丁导致无限重启，请尝试同一文件夹中的其他补丁（例如 `1_2_3` 或 `3_4_5`）。

**仅适用于 5.10 及以下内核：**

- 您**还必须**应用 `POSIX_MQUEUE` kABI 修复：
  [Documentation/resources/kernel-patches/GKI/below-kernel-6.12/002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch](./resources/kernel-patches/GKI/below-kernel-6.12/002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch)

**对于 6.12 及以上内核：**

- 从 [Documentation/resources/kernel-patches/GKI/kernel-6.12/001.GKI-6.12-or-above-fix_sysvipc_kabi.patch](./resources/kernel-patches/GKI/kernel-6.12/001.GKI-6.12-or-above-fix_sysvipc_kabi.patch) 应用补丁。

**如何应用补丁：**

```bash
# 为您的内核版本应用每个必需的补丁
patch -p1 < /path/to/extracted/patchfile.patch
```

### 步骤 2：编辑 `gki_defconfig`

不要使用单独的片段文件，而是直接编辑 `arch/arm64/configs/gki_defconfig`，使用此 **GKI 专用配置**。

这些选项已经过测试，并被证明可以在所有 GKI 内核上正常工作，而不会破坏 ABI。

> [!WARNING]
>
> **不要**启用下面推荐的 GKI 配置以外的任何内容。当与步骤 1 中的补丁结合使用时，这些特定选项是 kABI 安全的。

```makefile
# 完全支持 DroidSpaces 的 GKI 内核配置
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# 命名空间
CONFIG_IPC_NS=y
CONFIG_PID_NS=y

# 硬件访问支持
CONFIG_DEVTMPFS=y

# 网络（增强 NAT 支持）
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y

# --- 以下配置为可选但建议开启 ---

# UFW 支持
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y

# Fail2ban 支持
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y

# 启用 tmpfs 上的 xattr、posix acl 支持
# 用于 NixOS 支持
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y
```

**工作流程规则：**
- **不要**将此配置作为代码块追加到文件末尾。
- 逐个搜索每个选项。
- 如果某个选项显示为 `# CONFIG_NAME is not set`，将其改为 `CONFIG_NAME=y`。
- 如果某个选项已设置为 `CONFIG_NAME=y`，请保持不变。
- 如果某个选项不存在，将其添加在末尾。

### 步骤 3：编译

使用您偏好的编译方法：Bazel、官方 AOSP `build.sh`/`prepare_vendor.sh` 脚本，或使用 `make` 的传统 `Kbuild`。

### 步骤 4：刷入和测试

使用 Odin、fastboot、Heimdall、Anykernel3 或您设备偏好的方法刷入编译好的 `boot.img` 或 `Image`。由于我们使用了 kABI 安全的补丁，您的原始供应商模块将继续完美工作。

启动后，打开 Droidspaces 应用，前往 **设置**（齿轮图标）-> **要求** -> **检查要求** 来验证您的配置。

---

<a id="testing"></a>
## 测试您的内核

### 1. 运行需求检查

- **在应用内**：前往 **设置**（齿轮图标）-> **要求** -> **检查要求**。
- **在终端中**：运行：

```bash
su -c droidspaces check
```

这会检查以下内容：

- Root 访问权限
- 内核版本（最低 3.18）
- PID、MNT、UTS、IPC 命名空间
- 网络命名空间（可选，NAT/None 模式需要）
- Cgroup 命名空间（可选，用于现代 cgroup 隔离）
- devtmpfs 支持
- OverlayFS 支持（可选，用于易失模式）
- VETH 和 Bridge 支持（可选，用于 NAT 模式）
- PTY/devpts 支持
- Loop 设备支持
- ext4 支持

### 2. 理解结果

| 结果 | 含义 |
|--------|---------|
| 绿色对勾 | 功能可用 |
| 黄色警告 | 功能为可选项且不可用（例如 OverlayFS） |
| 红色叉号 | 缺少必需功能；容器可能无法正常工作 |

### 3. 如果缺少某些功能该怎么办

| 缺少的功能 | 所需配置 | 缺少时的影响 |
|----------------|----------------|-------------------|
| PID 命名空间 | `CONFIG_PID_NS=y` | **致命。** 容器无法启动。 |
| MNT 命名空间 | `CONFIG_NAMESPACES=y` | **致命。** 容器无法启动。 |
| UTS 命名空间 | `CONFIG_UTS_NS=y` | **致命。** 容器无法启动。 |
| IPC 命名空间 | `CONFIG_IPC_NS=y` | **致命。** 容器无法启动。 |
| Cgroup device | `CONFIG_CGROUP_DEVICE=y` | **致命。** 容器无法启动。 |
| devtmpfs | `CONFIG_DEVTMPFS=y` | **致命。** Droidspaces 无法设置 `/dev`。 |
| OverlayFS | `CONFIG_OVERLAY_FS` | 易失模式不可用。 |
| 网络命名空间 | `CONFIG_NET_NS=y` | NAT 和 None 模式不可用。 |
| VETH / Bridge | `CONFIG_VETH` / `CONFIG_BRIDGE` | NAT 模式不可用。 |
| Seccomp | `CONFIG_SECCOMP=y` | Seccomp 防护盾已禁用。存在安全风险。 |

---

<a id="versions"></a>
## 推荐的内核版本

| 版本 | 支持级别 | 备注 |
|---------|---------|-------|
| 3.18 | 旧版 | 最低支持版本。仅提供基础命名空间支持。现代发行版不稳定或可能根本无法启动。 |
| 4.4 - 4.19 | 稳定 | 全面支持。嵌套容器（Docker/Podman）可原生运行。如果在像 4.14.113 这样的内核上遇到由于 VFS 死锁缺陷导致的 systemd 挂起，请尝试在应用中启用"死锁防护盾"，或在 CLI 中传入 `--block-nested-namespaces`，然后硬重启再试。 |
| 5.4 - 5.10 | 推荐 | 完整功能支持，包括嵌套容器和现代 cgroup v2。 |
| 5.15+ | 理想 | 所有功能可用，最佳性能和最广泛的兼容性。 |

---

<a id="nested"></a>
## 嵌套容器（Docker、Podman、LXC）

Droidspaces 在所有支持的内核版本上开箱即用地支持在容器内运行 Docker、Podman 或 LXC。

### 旧版内核注意事项（4.19 及以下）

旧版内核可能对现代嵌套容器工具带来一些挑战：

- **死锁防护盾的权衡**：如果您的设备受到 4.14.113 `grab_super()` VFS 死锁的影响，并且需要死锁防护盾来启动 systemd，启用防护盾也将阻止 Docker、LXC 和 Podman 所需的命名空间系统调用。当防护盾处于激活状态时，您无法使用嵌套容器。

- **网络不兼容性**：现代 Docker、LXC 和 Podman 依赖 `nftables`。旧版内核通常缺少完整的 `nftables` 支持。要解决此问题，请在 NAT 模式下使用 Droidspaces，并将容器的 iptables 替代方案切换为 `iptables-legacy` 和 `ip6tables-legacy`。

- **BPF 冲突**：现代 Docker 和 runc 使用 `BPF_CGROUP_DEVICE` 进行设备管理。旧版内核不支持所需的 BPF 附加类型，这会导致 `Invalid argument` 错误。要解决此问题，请配置 Docker 使用 `cgroupfs` 驱动和 `vfs` 存储驱动。

---

<a id="resources"></a>
## 附加资源

- [Android 内核教程](https://github.com/ravindu644/Android-Kernel-Tutorials) 作者 ravindu644
- [内核配置参考](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
- [Droidspaces Telegram 频道](https://t.me/Droidspaces) 获取内核相关支持
