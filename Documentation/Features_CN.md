<!--
title: 功能详解
section: 指南
order: 1
desc: 深入了解每个 Droidspaces 功能：命名空间隔离、初始化系统支持、OverlayFS 易失模式、GPU 加速、cgroup 隔离、seccomp 防护盾以及 Android 特定调优。
keywords: droidspaces, 功能, 命名空间, 隔离, cgroup, overlayfs, 易失, 模式, 初始化, 系统, 支持, gpu, 加速
-->

# 功能详解

每个主要 Droidspaces 功能及其底层工作原理的详细说明。

---

## 命名空间隔离

### 什么是命名空间？

Linux 命名空间是一种内核功能，用于划分系统资源，使每组进程只能看到自己隔离的资源集合。Droidspaces 使用五种命名空间来创建隔离的容器：

| 命名空间 | 标志 | 隔离内容 |
|-----------|------|-----------------|
| **PID** | `CLONE_NEWPID` | 进程 ID。容器获得自己的 PID 树，其中 init 为 PID 1。 |
| **MNT** | `CLONE_NEWNS` | 挂载点。容器通过 `pivot_root` 拥有自己的文件系统视图。 |
| **UTS** | `CLONE_NEWUTS` | 主机名和域名。每个容器可以有自己的主机名。 |
| **IPC** | `CLONE_NEWIPC` | System V IPC 和 POSIX 消息队列。防止跨容器 IPC 泄漏。 |
| **Cgroup** | `CLONE_NEWCGROUP` | Cgroup 根目录。每个容器看到自己的 cgroup 层次结构。 |
| **Network**| `CLONE_NEWNET` | 网络栈。隔离的接口、路由和防火墙（NAT/None 模式）。 |

### 网络命名空间隔离 (`--net`)

Droidspaces 支持三种网络模式，决定是否使用网络命名空间 (`CLONE_NEWNET`)：

1. **Host 模式 (`--net=host`) - 默认**：Droidspaces 有意**不**隔离网络命名空间。容器共享宿主的网络栈。这大大简化了配置：容器无需虚拟网桥、NAT 或防火墙规则即可立即获取互联网访问。在 Android 上，网络本来就复杂（蜂窝网络、Wi-Fi、VPN），这避免了一整类连接问题。

2. **NAT 模式 (`--net=nat`)**：容器被放置在私有网络命名空间中。通过虚拟网桥或 veth 对连接到宿主，提供**纯网络隔离**，同时通过宿主的上行接口保持互联网访问。兼容绝大多数 Android 设备。

3. **None 模式 (`--net=none`)**：容器被放置在一个私有的、网络隔离的命名空间中，仅启用 loopback 接口，以实现最大安全性。

### 与 Chroot 的对比

`chroot` 仅改变进程的可见根目录。它不提供进程隔离、不提供挂载隔离、不提供主机名隔离，也不提供 IPC 隔离。chroot 内的任何进程共享宿主的 PID 空间，可以看到并向其他进程发送信号，且无法运行像 systemd 这样的初始化系统。

Droidspaces 使用 `pivot_root` 而不是 `chroot`，这是一种更强的隔离机制。结合私有挂载传播 (`MS_PRIVATE`)，容器的挂载事件对宿主完全不可见。

---

## 初始化系统支持

### 为什么初始化系统很重要

如果没有初始化系统，您只是在 chroot 中运行单个进程。您无法管理服务、无法使用 `systemctl`、没有 journald 进行日志记录，也没有适当的会话管理。这只不过是一个加强版的 shell。

Droidspaces 启动一个真正的初始化系统。当 systemd 作为容器内的 PID 1 启动时：

- 服务通过 `systemctl start/stop/enable` 进行管理
- 日志通过 `journalctl` 可用
- 用户会话通过 `login`、`su` 和 `sudo` 正常工作
- 目标 (target) 和依赖关系被正确解析
- 定时器单元、套接字激活以及所有其他 systemd 功能均可正常使用

### Droidspaces 如何实现这一点

systemd 要在容器内运行需要三个条件：

1. **PID 1：** 初始化进程必须是 PID 1。Droidspaces 通过 PID 命名空间 (`CLONE_NEWPID`) 后接 fork 来实现这一点，使容器的 init 成为其命名空间中的第一个进程。

2. **容器检测：** Systemd 需要知道它正在容器内运行。Droidspaces 将 `droidspaces` 写入 `/run/systemd/container` 并设置 `container=droidspaces` 环境变量。

3. **Cgroup 访问：** Systemd 需要对其 cgroup 层次结构具有写权限，以创建 scope 和 slice。Droidspaces 通过每个容器的 cgroup 树来提供此功能（参见[Cgroup 隔离](#cgroup-isolation)）。

### 支持的初始化系统

Droidspaces 理论上兼容**任何可运行作为 PID 1 的初始化系统**，包括：

- **systemd**（大多数 Linux 发行版）
- **OpenRC**（Alpine Linux、Gentoo）
- **runit**（Void Linux、Devuan）
- **s6-init**（Alpine、各种容器）
- **SysVinit**（Debian、Devuan）

初始化二进制文件严格预期位于 `/sbin/init`。如果此二进制文件缺失或不可执行，Droidspaces 将无法启动容器，以确保服务管理和会话管理按预期运行。

---

## 易失模式 (Volatile Mode)

### 什么是易失模式？

易失模式（`--volatile` 或 `-V`）创建一个临时容器，所有修改都存储在 RAM 中，并在容器停止时丢弃。原始 rootfs 永远不会被修改。

### 工作原理

Droidspaces 使用 **OverlayFS**，这是一种内置于 Linux 内核的联合文件系统：

- **Lower 层：** 原始 rootfs（如果使用 rootfs.img 模式则只读挂载）
- **Upper 层：** tmpfs 支持的目录，捕获所有写入操作
- **合并视图：** 容器看到一个统一的文件系统，读取来自 lower 层，写入进入 upper 层

当容器停止时，upper 层（在 RAM 中）被丢弃。原始 rootfs 保持不变。

### 使用场景

- **测试：** 安装软件包、修改配置并验证更改，而不提交任何内容
- **开发：** 为每次构建启动一个干净的环境
- **安全：** 每次启动时保证清洁状态
- **实验：** 随意破坏而无后果

### 使用方法

```bash
# 从目录创建易失容器
droidspaces --name=test --rootfs=/path/to/rootfs --volatile start

# 从镜像创建易失容器
droidspaces --name=test --rootfs-img=/path/to/rootfs.img --volatile start
```

### 已知限制：Android 上的 f2fs

大多数 Android 设备对 `/data` 分区使用 f2fs 文件系统。许多 Android 内核上的 OverlayFS 不支持将 f2fs 作为 lower 目录。这意味着**在 f2fs 上使用目录 rootfs 的易失模式将失败**。

**变通方案：** 改用 rootfs 镜像（`--rootfs-img`）。ext4 loop 挂载为 OverlayFS 提供兼容的 lower 目录。

Droidspaces 在运行时会检测到这种不兼容性并提供清晰的诊断消息。

---

## 硬件访问模式

> [!CAUTION]
> 启用硬件访问模式 (`--hw-access`) 会将所有宿主设备（包括原始块设备）直接暴露给容器。如果恶意进程或意外命令命中了这些设备，可能会永久性地破坏您的分区表、擦除 SD 卡或使设备变砖。Droidspaces 的开发者不对因使用此功能而导致的任何数据丢失或硬件损坏负责。**使用风险自负。**

### 它有什么作用

`--hw-access` 标志通过挂载 `devtmpfs` 而不是私有 `tmpfs` 到 `/dev`，将宿主硬件设备暴露给容器。

这使容器可以访问：
- **GPU**（通过 Turnip + Zink 进行硬件加速图形渲染，以及桌面端 Intel 和 AMD 的 Panfrost/原生 GPU 加速）
- **摄像头**
- **传感器**
- **USB 设备**
- **块设备**（分区和物理磁盘）

### 安全性影响

硬件访问模式授予容器对**所有**宿主设备的可见性。容器可以直接与 GPU、USB 控制器和其他硬件交互。仅当您信任容器内容并需要硬件访问时才使用此模式。

### systemd 258+ 修复

从 systemd 258 开始，容器检测逻辑得到了强化。systemd 现在会检查 `/sys` 是否为只读挂载，以判断它是在容器内运行还是物理机上运行。如果 `/sys` 是可读写的，systemd 就会假定它具有完整的硬件管理权限，并尝试将服务（如 `getty`）附加到物理 TTY（`tty1`-`tty6`）。由于这些在隔离的容器环境中不存在，服务无法启动，导致控制台没有登录提示。

> [!NOTE]
> 此信息基于当前开发者对 Droidspaces 中 systemd 行为的理解，可能需要进一步验证。

Droidspaces 通过一种"动态打孔"技术处理此问题：

1. **固定子系统**：所有 `/sys` 子目录通过自我 bind mount 来保留对个别硬件子系统的读写访问。
2. **只读重新挂载**：顶层的 `/sys` 被重新挂载为只读。
3. **容器识别**：systemd 检测到只读 `/sys`，正确识别容器环境，并回退到容器原生的控制台管理。
4. **硬件访问**：通过步骤 1 中创建的固定子挂载，各个硬件子系统保持完全可访问。

### 使用方法

```bash
droidspaces --name=gpu-test --rootfs=/path/to/rootfs --hw-access start
```

### 自动 GPU 组设置

当启用 `--hw-access` 时，Droidspaces 会自动：

1. **扫描宿主 GPU 设备** - 在 `pivot_root` 之前，探查约 40 个已知的 GPU 设备路径（`/dev/dri/*`、`/dev/mali*`、`/dev/kgsl-3d0`、`/dev/nvidia*` 等）并通过 `stat()` 收集它们的组 ID。**显式跳过像 `/dev/dri/card*` 这样的危险节点**，以防止宿主内核恐慌，因为这些节点仅限于宿主的显示管理器。
2. **创建匹配的组** - 在 `pivot_root` 之后，将类似 `gpu_<GID>:x:<GID>:root` 的条目追加到容器的 `/etc/group` 中。容器的 root 用户自动添加到每个组中。
3. **幂等重启** - 容器重启时，检测到现有组并跳过（不产生重复条目）。

这消除了容器内手动执行 `groupadd`/`usermod` 命令的需要，同时通过避免受限的硬件路径来确保宿主内核的稳定性。

### X11 套接字挂载

为支持 GUI 应用程序，Droidspaces 自动 bind mount X11 套接字目录：

- **Android (Termux X11)：** 检测并挂载 `/data/data/com.termux/files/usr/tmp/.X11-unix`
- **桌面 Linux：** 通过 `/proc/1/root/tmp/.X11-unix` 挂载 `/tmp/.X11-unix`

> [!TIP]
> X11 支持可以使用 `--termux-x11` (`-X`) 标志独立启用。如果您不需要完整的 GPU/硬件访问权限，这是在 Android 上使用 GUI 应用程序的推荐方式，因为它保留了更高级别的隔离性。


启动容器后，在容器内设置 `DISPLAY=:0` 以使用 X11 显示。

### 支持的 GPU 系列

| 系列 | 设备路径 |
|--------|-------------|
| **DRI** (Intel, AMD, Mesa) | `/dev/dri/renderD128-130`、`/dev/dri/card0-2` |
| **NVIDIA** (专有驱动) | `/dev/nvidia*`、`/dev/nvidia-uvm*`、`/dev/nvidia-caps/*` |
| **ARM Mali** | `/dev/mali`、`/dev/mali0`、`/dev/mali1` |
| **Qualcomm Adreno** | `/dev/kgsl-3d0`、`/dev/kgsl`、`/dev/genlock` |
| **AMD Compute** | `/dev/kfd` |
| **PowerVR** | `/dev/pvr_sync` |
| **NVIDIA Tegra** | `/dev/nvhost-ctrl`、`/dev/nvhost-gpu`、`/dev/nvmap` |
| **DMA Heaps** | `/dev/dma_heap/system`、`/dev/dma_heap/linux,cma`、`/dev/dma_heap/reserved`、`/dev/dma_heap/qcom,system` |
| **Sync** | `/dev/sw_sync` |

---

## 自定义 Bind 挂载

### 什么是 Bind 挂载？

Bind 挂载允许您将宿主文件系统上的目录映射到容器内的指定位置。宿主目录在容器内变为可见且可写。

### 语法

```bash
# 单个挂载
--bind-mount=/host/path:/container/path
-B /host/path:/container/path

# 多个挂载（逗号分隔）
-B /src1:/dst1,/src2:/dst2,/src3:/dst3

# 多个挂载（链式）
-B /src1:/dst1 -B /src2:/dst2

# 混合使用
-B /src1:/dst1,/src2:/dst2 -B /src3:/dst3
```

### 限制

- 目标路径必须是**绝对路径**
- 目标路径中的路径遍历 (`..`) 出于安全原因被**拒绝**

### 自动目录创建

如果目标目录在 rootfs 内不存在，Droidspaces 会使用 `mkdir -p` 自动创建。

### 软失败模型

如果宿主源路径不存在或挂载失败，Droidspaces 会发出警告并跳过该条目，而不是导致整个启动失败。这允许容器在可选 bind 源暂时不可用时仍能启动。

### 安全性

Droidspaces 通过两种保护机制验证 bind 挂载目标：
1. **挂载前：** 使用 `lstat()` 确保 rootfs 内的目标不是符号链接
2. **挂载后：** 使用 `realpath()` 通过 `is_subpath()` 辅助函数验证已挂载的路径无法逃逸容器根目录

---

## 网络隔离（3 种模式）

Droidspaces 提供三种不同的网络模式，以在使用便捷性和高级隔离之间取得平衡。

### 1. Host 模式 (`--net=host`) - 默认
容器共享宿主的网络命名空间。
- **优点**：零配置、即时互联网访问、兼容所有 Android VPN/热点。
- **缺点**：无端口隔离；容器内的服务直接绑定到宿主端口。

### 2. NAT 模式 (`--net=nat`)
容器被放置在私有网络命名空间 (`CLONE_NEWNET`) 中，并通过虚拟网桥 (`ds-br0`) 或直接的 veth 对连接到宿主。
- **确定性 IP**：每个容器在 `172.28.0.0/16` 范围内分配唯一 IP，由其 PID 决定。
- **内置 DHCP**：Droidspaces 包含一个最小的内置 DHCP 服务器，用于自动配置容器的 `eth0`。
- **纯隔离**：容器无法直接看到或与宿主的网络接口交互。
- **必须指定上行接口**：您**必须**通过 `--upstream` 指定哪些宿主接口提供互联网访问（例如 `--upstream wlan0,rmnet0`）。也支持通配符（例如 `rmnet*`、`wlan0`、`v4-rmnet_data*`）。

> [!IMPORTANT]
> NAT 模式仅支持 **IPv4**。如果您的上行接口缺少 IPv4 地址（纯 IPv6 网络），互联网访问将无法正常工作。请参阅[IPv4 NAT 常见问题](Troubleshooting.md#ipv4-quirks)以获取变通方案。

### 3. None 模式 (`--net=none`)
容器获得一个私有网络命名空间，仅启用 loopback (`lo`) 接口。
- **使用场景**：离线任务的最大安全性。

### 端口转发（NAT 模式）

在 NAT 模式下，您可以使用 `--port` 标志将容器服务暴露给宿主或本地网络。支持的格式：

```bash
# 将宿主端口 8080 转发到容器端口 80
--port 8080:80

# 对称简写（宿主 8080 -> 容器 8080）
--port 8080

# 将宿主端口范围转发到容器端口范围（必须相同大小）
--port 1000-2000:1000-2000

# 混合使用并显式指定协议
--port 2222:22/tcp --port 5000-5050:5000-5050/udp
```


### 上行接口监控
在 Android 上，连接经常在 WiFi 和移动数据之间切换。Droidspaces 包含一个**路由监控器**，用于跟踪您声明的 `--upstream` 接口。如果您活跃的接口发生更改（例如您走出了 WiFi 覆盖范围），监控器会自动更新内核的策略路由以保持容器连接，无需重启。

---

## Rootfs 镜像支持

### 为什么使用镜像？

基于目录的 rootfs 配置虽然简单，但有局限性：
- 在某些文件系统上（特别是 Android 上的 f2fs）文件权限可能无法正确保留
- OverlayFS 可能与底层文件系统不兼容
- **内置完整性检查**：镜像可以在运行时通过 `e2fsck` 进行验证。
- **可移植性**：您的整个容器封装在一个单独的 `.img` 文件中。这使得备份、分享或随身携带变得极其容易。只需将文件复制到任何装有 Droidspaces 的设备上即可启动。

Ext4 镜像解决了这些问题。镜像文件包含一个完整的 ext4 文件系统，在运行时通过 loop 挂载，提供一致的行为，不受宿主文件系统的影响。

### 工作原理

当您使用 `--rootfs-img` 时：

1. **文件系统检查：** Droidspaces 对镜像运行 `e2fsck -f -y` 以确保完整性
2. **SELinux 上下文：** 在 Android 上，应用 `vold_data_file` SELinux 上下文以防止静默 I/O 拒绝
3. **Loop 挂载：** 镜像挂载到 `/mnt/Droidspaces/<name>`
4. **重试逻辑：** 在内核 4.14 上，由于残留的 loop 设备状态，挂载可能会失败。Droidspaces 最多重试 3 次，带有 `sync()` 和平息等待延迟。

### 使用方法

```bash
# 基于镜像的容器（--name 为必填项）
droidspaces --name=ubuntu --rootfs-img=/path/to/rootfs.img start

# 带镜像的易失模式（镜像只读挂载）
droidspaces --name=ubuntu --rootfs-img=/path/to/rootfs.img --volatile start
```

---

## Cgroup 隔离

### 它有什么作用

Droidspaces 在宿主的 `/sys/fs/cgroup/droidspaces/<name>` 路径下为每个容器创建 cgroup 树。结合 cgroup 命名空间，每个容器看到自己干净的 cgroup 层次结构。

**注意：** Cgroup 隔离在 `--force-cgroupv1` 模式下不可用。

### 为什么它很重要

systemd 严重依赖 cgroups 来执行以下操作：
- 创建服务 scope 和 slice
- 资源统计（每个服务的 CPU、内存）
- 进程追踪（知道哪些进程属于哪个服务）
- 干净关闭（终止服务 cgroup 中的所有进程）

没有适当的 cgroup 隔离，systemd 无法正常工作。多个容器会在 cgroup 层次结构中发生冲突，服务管理将失败。

### "软禁 (Jail)"技巧

在创建 cgroup 命名空间之前，Droidspaces 将监控器进程移动到容器特定的 cgroup 中。这确保了当调用 `unshare(CLONE_NEWCGROUP)` 时，新命名空间的根会映射到容器的子树。

### Cgroup v1 和 v2 支持

Droidspaces 支持两种 cgroup 版本：

- **Cgroup v2 (统一模式)：** 现代发行版使用。作为单一层次结构挂载。
- **Cgroup v1 (旧版)：** 旧发行版使用。Droidspaces 处理联合挂载的控制器（如 `cpu,cpuacct`），并在旧内核或 `--force-cgroupv1` 模式下为次要名称创建符号链接。

### 强制使用旧版 Cgroup V1 (`--force-cgroupv1`)

在旧版 Android 内核（3.18、4.4 或 4.9）上，宿主系统可能完全缺少 Cgroup v2 支持，或者提供了不完整的实现，缺少现代 `systemd` 所需的基本控制器（CPU、内存等）。这种不一致性常常导致 `systemd` 错误识别环境，从而导致关键的启动失败。

`--force-cgroupv1` 标志作为一个**专家级应急出口**。它指示 Droidspaces 严格使用旧版 v1 层次结构，即使 v2 在宿主上似乎可用。这确保了在旧内核基础设施上使用现代 `systemd` 版本的发行版实现最大的稳定性和兼容性。

### `su` 修复

当使用 `enter` 或 `run` 进入容器时，进程必须在加入命名空间之前位于容器的宿主端 cgroup 中。否则，容器内的 `systemd-logind` 和 `sd-pam` 无法将进程映射到有效会话，导致 `su` 和 `sudo` 挂起。Droidspaces 在任何 `setns()` 调用之前自动附加到容器的 cgroup 来处理此问题。

---

## 自适应安全与死锁防护盾

Droidspaces 包含基于 BPF 的复杂 seccomp 过滤器，以解决关键的 Android 内核冲突：

### 1. FBE 密钥环冲突（自动）
Android 的文件级加密将文件系统密钥存储在内核会话密钥环中。当 systemd 尝试创建新的会话密钥环时，进程失去对宿主加密密钥的访问权限，导致 `ENOKEY` 错误。

**解决方案：** 在旧内核（< 5.0）上，Droidspaces *自动*拦截密钥环系统调用（`keyctl`、`add_key`、`request_key`），返回 `ENOSYS`，强制 systemd 使用现有的密钥环。

<a id="vfs-deadlock"></a>

### 2. VFS 命名空间死锁（手动启用）
在某些使用旧内核（特别是 4.14.113，常见于 2019-2020 年的 Android 设备）的设备上，systemd 的服务沙箱功能会触发内核 VFS 层中的竞态条件（`grab_super()` 缺陷）。这会导致 systemd 挂起、`systemctl` 冻结以及潜在的设备死锁。4.9 和 4.19 内核基本不受影响。

**修复方法：** 您可以手动启用**死锁防护盾**（在 Android 应用配置中或通过 `--block-nested-namespaces` CLI 参数）。这会拦截 `unshare` 和 `clone` 命名空间请求并返回 `EPERM`，阻止 systemd 触发死锁。

### 嵌套容器（Docker、Podman、LXC）

由于死锁防护盾现在严格作为**可选开关**而不是硬编码的全面禁止：
- **原生支持：** 所有内核上的用户现在都可以开箱即用地原生运行 Docker、Podman 和 LXC。
- **权衡取舍：** 如果您的设备需要死锁防护盾来启动 systemd，启用它将有意阻止 Docker/Podman 所需的命名空间创建。

> [!TIP]
>
> **旧内核网络：** 在旧内核上的 Droidspaces 内运行 Docker/Podman 时，现代 `nftables` 可能无法正确路由流量。我们建议使用 Droidspaces 的 NAT 模式，并将容器的网络栈切换到 `iptables-legacy` 和 `ip6tables-legacy`。


---

## Android 特定调优

Droidspaces 包含几个专门设计用来应对 Android Linux 内核"特性化"本质的复杂子系统。

### 安全 Udev 触发器

标准 Linux 发行版在启动期间使用 `udevadm trigger` 来"冷插拔"硬件设备。在许多 Android 设备上，同时触发所有设备会导致内核死锁或恐慌，因为 Android 自身的硬件驱动（已经在运行）不希望另一个管理器重新触发它们。

**解决方案**：Droidspaces 屏蔽标准 udev 触发服务，并安装一个**安全 Udev 触发器**。此服务仅触发一组严格定义的可安全重新扫描的子系统（`usb`、`block`、`input`、`tty`）。这使容器能够识别新的 USB 驱动器或键盘，而不会冒着系统崩溃的风险。
