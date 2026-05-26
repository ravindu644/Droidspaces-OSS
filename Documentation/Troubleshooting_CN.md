<!--
title: 故障排除
section: 参考
order: 1
desc: 排查 Droidspaces 容器问题：systemd 挂起、paranoid networking、SELinux 损坏、OverlayFS f2fs、稀疏镜像空间回收、WiFi 省电模式。
keywords: droidspaces, 故障排除, systemd, 挂起, 修复, 网络, 问题, selinux, overlayfs, ffs, 容器, 错误
-->

# 故障排除

常见问题、原因及修复方法。

### 快速导航
- [现代发行版（Arch、Fedora 等）在旧内核上启动失败](#modern-distros-arch-fedora-etc-failure-on-legacy-kernels)
- ["Required key not available" (ENOKEY)](#required-key-not-available)
- [内核 4.14 上的挂载错误](#mount-errors-on-kernel-414)
- [OverlayFS 不受支持 (f2fs)](#overlayfs-not-supported-f2fs)
- [容器名称冲突](#container-name-conflicts)
- [Systemd 在旧内核上挂起](#systemd-hangs-on-older-kernels)
- [容器无法停止](#container-wont-stop)
- [Android 上 Rootfs 镜像 I/O 错误](#rootfs-image-io-errors-on-android)
- [网络完全不可用 - ping 失败并报 "socket: permission denied"](#paranoid-networking)
- [DNS / 域名解析问题](#dns--name-resolution-issues)
- [WiFi/移动数据断连](#wifimobile-data-disconnects)
- [SELinux 导致的 Rootfs 损坏](#selinux-induced-rootfs-corruption-directory-mode)
- [Systemd 服务沙箱冲突](#systemd-service-sandboxing-conflicts-legacy-kernels)
- [回收存储空间（稀疏镜像）](#reclaim-storage)
- [WiFi `Power save: on` 导致 Android 网络体验卡顿](#nuke-wifi-powersave)
- [获取帮助](#getting-help)

---

<a id="modern-distros"></a>
## 现代发行版（Arch、Fedora 等）在旧内核上启动失败

这不是 Droidspaces 的 bug，而是发行版 `systemd` 版本的限制。现代发行版如 Arch Linux、Fedora 或 openSUSE 使用了需要较新内核功能的 `systemd` 版本（v258 及以上）。在旧内核（3.18、4.4、4.9、4.14、4.19）上，这些发行版要么以"Unsupported Kernel"消息启动失败，要么在初始化过程中崩溃，要么在执行 `systemctl` 命令时表现出卡住状态。

Systemd 的开发理念越来越趋向于针对现代 Linux 环境。从 v258（2025 年 9 月发布）开始，代码库中移除了大量为 5.4 以下内核保留的旧版兼容方案和向后兼容层。

具体来说，开发者删除了旧的 capability 检查、已弃用的回退机制以及过时的 D-Bus 方法。

移除这些回退机制后，现代 `systemd` 默认假设现代内核 API 已经存在。当这些 API 不可用时（如在旧内核中），它会直接硬性失败，而不是优雅降级。

**原因：** 宿主机内核太旧，无法支持新版 `systemd` 所需的现代系统调用和功能。

旧内核缺少现代系统调用（如 `clone3`、`openat2` 或较新的 `bpf` 钩子），而 `systemd` 现在默认使用这些调用。当 `systemd` 调用一个 4.14 或 4.19 内核无法识别的系统调用时，内核会拒绝它，从而导致失败。

**解决方案：**
- 使用任何使用 **OpenRC**、**runit** 或 **s6** 作为初始化系统的容器。
- 使用 `systemd` 版本低于 v258 的发行版，如 **Ubuntu 22.04**、**Ubuntu 24.04**、**Ubuntu 25.04** 或 **Ubuntu 25.10（截至 2026 年 3 月使用 v257.9）**。
- 使用 **Debian 12 (Bookworm)** 或 **Debian 13 (Trixie)**。


---

## "Required key not available"

**症状：** 容器崩溃或文件系统操作失败，并报"Required key not available"错误。最常见于启用了文件级加密 (FBE) 的 Android 设备。

**原因：** 容器内的 systemd 服务尝试创建新的会话密钥环 (session keyring)，导致进程失去对 Android FBE 加密密钥的访问权限。

**受影响的内核：** 3.18、4.4、4.9、4.14、4.19（旧版 Android 内核）

**解决方案：** 在 5.0 以下内核上，Droidspaces 的自适应 Seccomp 防护盾会自动处理此问题。防护盾会拦截密钥环相关的系统调用并返回 `ENOSYS`，使 systemd 回退到使用现有的会话密钥环。

如果您仍然看到此错误：
- 确认您的 Droidspaces 二进制文件是最新版本（v4.2.4+）
- 运行 `droidspaces check` 验证 seccomp 支持
- 确保内核配置中有 `CONFIG_SECCOMP=y` 和 `CONFIG_SECCOMP_FILTER=y`
- 迁移到 **rootfs.img 模式**（在 Android 上推荐，可隔离文件系统密钥）
- **高级方法**：通过精确编辑 `boot`/`vendor`/`vendor_boot` 分区中的 `fstab` 文件来解密 `/data` 分区（需要高级 Android 修改知识）

---

## 内核 4.14 上的挂载错误

**症状：** 停止后的首次容器启动尝试因挂载错误而失败，但第二次尝试成功。

**原因：** 在内核 4.14 上，loop 设备清理是异步的。卸载 rootfs 镜像后，下一次挂载尝试时 loop 设备可能尚未完全释放。

**解决方案：** Droidspaces v4.2.3+ 包含一个 3 次重试循环，每次尝试之间带有 `sync()` 调用和 1 秒平息等待延迟。这可以自动处理竞态条件。

如果您仍然遇到问题：
- 更新到最新的 Droidspaces 版本
- 在停止和启动容器之间等待几秒钟
- 在重启前使用 `sync`：`sync && droidspaces --name=mycontainer restart`

---

## OverlayFS 不受支持 (f2fs)

**症状：** 使用 `--volatile` 启动容器时失败，提示 OverlayFS 不受支持或 f2fs 不兼容。

**原因：** 大多数 Android 设备对 `/data` 分区使用 f2fs 文件系统。许多 Android 内核（4.14、5.15）上的 OverlayFS 不支持将 f2fs 作为 lower 目录。

**解决方案：** 使用 rootfs 镜像代替目录：

```bash
# 这在 f2fs 上会失败：
droidspaces --rootfs=/data/rootfs --volatile start

# 这可以正常工作（ext4 镜像提供兼容的 lower 目录）：
droidspaces --name=test --rootfs-img=/data/rootfs.img --volatile start
```

---

## 容器名称冲突

**症状：** 启动容器失败，因为同名容器已在运行，或发生 PID 文件冲突。

**解决方案：**

1. 检查当前正在运行的内容：
   ```bash
   droidspaces show
   ```

2. 如果容器已列出但您认为它实际已停止，清理残留状态：
   ```bash
   droidspaces scan
   ```

3. 使用不同的名称：
   ```bash
   droidspaces --name=mycontainer-2 --rootfs=/path/to/rootfs start
   ```

---

## Systemd 在旧内核上挂起

**症状：** 在旧内核（3.18、4.4、4.9、4.14、4.19）上启动容器时，整个 systemd 挂起或无响应。

**原因：** systemd 的服务沙箱功能（`PrivateTmp=yes`、`ProtectSystem=yes`）在旧内核的 VFS `grab_super` 路径中触发竞态条件。

**解决方案：** 尝试在应用中启用"死锁防护盾"/在 CLI 中使用 `--block-nested-namespaces`，硬重启设备，然后重试。

---

## 容器无法停止

**症状：** `droidspaces stop` 需要超过 15 秒来停止容器，最终失败。

**原因：** 与[Systemd 在旧内核上挂起](#systemd-hangs-on-older-kernels)完全相同的原因。

**解决方案：** 尝试在应用中启用"死锁防护盾"/在 CLI 中使用 `--block-nested-namespaces`，硬重启设备，然后重试。

---

## Android 上 Rootfs 镜像 I/O 错误

**症状：** 对 rootfs 镜像进行 loop 挂载时静默失败。

**原因：** 在某些 Android 设备上，`.img` 文件的 SELinux 上下文阻止 loop 驱动执行 I/O。

**解决方案：** Droidspaces v4.3.0+ 会在挂载前自动将 `vold_data_file` SELinux 上下文应用到镜像文件。如果您使用的是旧版本，请更新到最新版本。

您也可以手动应用上下文：
```bash
chcon u:object_r:vold_data_file:s0 /path/to/rootfs.img
```

---

<a id="paranoid-networking"></a>

## 网络完全不可用 - ping 失败并报 "socket: permission denied"

**症状：** 容器内没有网络访问。即使是像 `ping` 这样的基本命令也会立即失败并报 `socket: permission denied` 错误，即使以 root 身份运行也是如此。

**原因：** 这是由 **Android Paranoid Networking** 引起的，这是许多 Android 内核（特别是 4.14 及更早版本）中的安全功能。与标准 Linux 不同，Android 内核将网络套接字创建限制为特定的补充组 ID (GID)。没有这些特定的 ID，内核的安全钩子会阻止进程：

* **AID_INET (3003)：** 创建任何 AF_INET/AF_INET6 套接字所必需。没有此 ID，`connect()` 和 `bind()` 调用会失败。
* **AID_NET_RAW (3004)：** `ping` 和其他原始网络任务所必需。
* **AID_NET_ADMIN (3005)：** 网络配置和路由任务所必需。

**解决方案：**

- **内核级修复：** 如果您正在构建自己的内核，最有效的解决方案是在内核配置中完全禁用此限制：
  ```bash
  CONFIG_ANDROID_PARANOID_NETWORK=n
  ```

- **用户空间修复：** 使用已经专门修补过、在组数据库中包含这些 Android 特有 GID 的 rootfs。我们的官方 rootfs tarball 已预先配置好以处理这些权限要求：

   [Droidspaces-rootfs-builder Releases](https://github.com/ravindu644/Droidspaces-rootfs-builder/releases/latest)

---

## DNS / 域名解析问题

**症状：** 互联网可以访问（可以 ping 通 IP 地址），但域名解析失败，即使 `/etc/resolv.conf` 中有正确的 DNS 名称服务器。此问题在使用移动数据时尤其常见，但在某些 ISP 的 WiFi 上也可能发生。

**原因：** 某些 ISP 似乎不喜欢自定义 DNS 设置。它们完全阻止常见的 DNS 服务器，如 `8.8.8.8` 和 `1.1.1.1`。

**解决方案：** 使用 ISP 自己的 DNS 服务器，而不是自定义的。

1. 在 Android root shell 中运行以下命令，获取 ISP 分配的默认 DNS 地址：

   ```shell
   dumpsys connectivity | sed 's/}}/\n/g' | grep 'InterfaceName: wlan0' | grep -o 'DnsAddresses: \[[^]]*\]' | grep -o '/[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*' | tr -d '/'
   ```

2. 在 Droidspaces 应用中编辑容器配置，将结果添加为 DNS 服务器。

---

## WiFi/移动数据断连

**症状：** 在容器启动或停止过程中，宿主设备的 WiFi 或移动数据永久停止工作。不重启设备可能无法重新开启。

**原因：** 容器的 `systemd-networkd` 服务可能与Android的网络管理冲突，或尝试覆盖宿主端的网络配置。

**解决方案：**

- 如果您使用宿主网络模式：在容器内屏蔽 `systemd-networkd` 服务以阻止其启动：

   1. **通过Android应用**：前往 **面板** -> **容器名称** -> **管理** (Systemd 菜单)，找到 `systemd-networkd`，然后点击 `systemd-networkd` 卡片旁边的三点图标并选择 **屏蔽**。
   2. **通过终端**：
      ```bash
      sudo systemctl mask systemd-networkd
      ```
- 使用 NAT 模式以获得最大的网络自由度，避免与宿主网络产生任何冲突。

---

## SELinux 导致的 Rootfs 损坏（目录模式）

**症状：** 符号链接大小意外变化（例如 `dpkg` 发出关于 `libstdc++.so.6` 的警告）、共享库加载失败（`LD_LIBRARY_PATH` 问题）或随机二进制崩溃。

**原因：** 在 Android 上，`/data/local/Droidspaces/Containers` 目录通常被分配一个通用的 SELinux 上下文。这导致在以**目录模式**（`--rootfs=/path/to/dir`）运行时，内核会阻止或静默干扰高级文件系统操作（如创建某些符号链接或特殊文件）。由于目录树中的每个文件和符号链接都直接暴露在宿主文件系统下，Android 的 SELinux 策略可能会重新标记或限制单个条目，从而损坏 Linux 内部文件系统的预期布局。

**推荐解决方案：** 迁移到 **rootfs.img 模式**（`--rootfs-img=/path/to/rootfs.img`）。

在此模式下，rootfs 作为独立的 ext4 镜像存储，并在运行时通过 loop 挂载。镜像内文件的 SELinux xattr 标签封装在镜像自身的文件系统元数据中，因此 Android 的策略引擎无法重新标记或与它们冲突。这避免了宿主机为目录树中的每个文件分配通用上下文的根本问题。

> [!Note]
>
> SELinux 强制策略仍然在进程级别适用——容器进程的域以及对 loop 设备或挂载点的访问仍受宿主策略的约束。`.img` 模式不会创建一个完全对 SELinux 透明的环境，但它确实消除了宿主端对内部文件系统结构和扩展属性的干扰。

> [!WARNING]
> 虽然切换到 `permissive` （宽容）模式似乎可以解决此问题，但**不建议**将其作为永久解决方案。如果 rootfs 已因 SELinux 拒绝操作而损坏，通常这种损坏是永久性的，无法仅通过更改模式来撤消。

---

## Systemd 服务沙箱冲突（旧内核）

**症状：** 像 `redis`、`mysql` 或 `apache` 这样的服务启动失败，并报 `exit-code` 或 `status=226/NAMESPACE` 错误，即使相同的配置在其他地方可以正常工作。

**原因：** 现代服务文件通常使用高级 systemd 沙箱指令（`PrivateTmp`、`ProtectSystem`、`RestrictNamespaces`）。在旧内核（3.18 - 4.19）上，Droidspaces 的**自适应 Seccomp 防护盾**会拦截这些与命名空间相关的系统调用并返回 `EPERM` 以防止内核死锁。然而，某些发行版的 systemd 版本将这些错误视为致命错误，并拒绝启动服务。

**解决方案：** 创建服务覆盖文件以禁用冲突的沙箱功能：

1.  **确定服务**：例如 `redis-server`
2.  **创建覆盖文件**：
    ```bash
    sudo systemctl edit <service-name>
    ```
3.  **添加以下行**（在编辑器提供的空白区域）：
    ```ini
    [Service]
    # 禁用有问题的安全沙箱功能
    PrivateTmp=no
    PrivateDevices=no
    ProtectSystem=no
    ProtectHome=no
    RestrictNamespaces=no
    MemoryDenyWriteExecute=no
    NoNewPrivileges=no
    CapabilityBoundingSet=
    ```
4.  **重新加载并重启**：
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl restart <service-name>
    ```

---

<a id="reclaim-storage"></a>
## 回收存储空间（稀疏镜像）

**症状：** 您在**稀疏镜像模式**（`rootfs.img`）下的容器内删除了大文件或卸载了大量软件包，但Android内部存储上的 `rootfs.img` 文件仍然占用相同大小的空间（不会"缩小"）。

**原因：** Ext4 稀疏镜像会在写入数据时扩展，但宿主文件系统无法自动检测到镜像内部块何时被释放。这在旧内核（例如 4.14）上尤为常见。

**解决方案：** 在容器内使用 `fstrim` 告诉内核"丢弃"未使用的块，从而在稀疏镜像中打孔，回收物理磁盘空间。

1. **正常启动容器**。
2. **在容器内以 root 身份运行 fstrim**：
   ```bash
   sudo fstrim -av
   ```
3. 该命令将报告已修剪的字节数。您会发现Android存储空间内 `rootfs.img` 的大小现已减少到与实际数据使用量相符。

---

<a id="nuke-wifi-powersave"></a>

## Wi-Fi `Power save: on` 导致Android网络体验卡顿

**症状：** 当设备屏幕关闭时，Android 会自动将 Wi-Fi 硬件置于省电模式。这可能导致容器内网络卡顿或连接中断。由于 Android 用户空间没有通用开关来禁用此行为，您必须使用后台服务显式强制将省电状态设为"关闭"。

**解决方案：** 您可以创建一个轻量级的专用容器，以宿主网络模式（`--net=host`）运行，在其中运行一个简单的"看门狗"脚本来保持 Wi-Fi 省电模式禁用。我们建议为此使用最小化的 Alpine Linux 容器。

以下是设置步骤：

### 1. 安装必要的工具
首先，确保容器内安装了 `iw` 工具。
- **Alpine：** `apk add iw`
- **Ubuntu/Debian：** `apt install iw`

### 2. 创建 Watchdog 脚本
在 `/usr/local/bin/wifi-watchdog.sh` 路径下创建包含以下内容的新文件：

```shell
#!/bin/sh
while true; do
    # 检查省电模式是否为 "on"
    if /usr/sbin/iw dev wlan0 get power_save 2>/dev/null | grep -q "on"; then
        /usr/sbin/iw dev wlan0 set power_save off
        echo "$(date): WiFi 省电模式已开启。已强制关闭。" >> /tmp/wifi-fix.log
    fi
    sleep 60
done
```

赋予脚本可执行权限：
```shell
chmod +x /usr/local/bin/wifi-watchdog.sh
```

### 3. 配置初始化服务

根据容器的初始化系统，将脚本配置为后台服务运行。

**对于 OpenRC (Alpine Linux)**

在 `/etc/init.d/wifi-watchdog` 路径下创建服务文件：

```shell
#!/sbin/openrc-run

name="WiFi PowerSave Watchdog"
description="确保 wlan0 省电模式保持关闭"

# 使用循环脚本的路径
command="/usr/local/bin/wifi-watchdog.sh"

# 这告诉 OpenRC 处理后台运行和 PID 创建
command_background=true
pidfile="/run/${RC_SVCNAME}.pid"

# 确保仅在网络可用后启动
depend() {
    need networking
}
```

赋予服务文件可执行权限并启动：

```shell
chmod +x /etc/init.d/wifi-watchdog
rc-update add wifi-watchdog default
rc-service wifi-watchdog start
```

**对于 systemd (Ubuntu/Debian)**

在 `/etc/systemd/system/wifi-watchdog.service` 路径下创建 systemd 单元文件：

```ini
[Unit]
Description=WiFi PowerSave Watchdog
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/wifi-watchdog.sh
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

启用并启动服务：

```shell
# 重新加载 systemd 以识别新服务
systemctl daemon-reload

# 启用开机自启
systemctl enable wifi-watchdog

# 现在启动它
systemctl start wifi-watchdog
```

> [!NOTE]
> 此变通方案**需要宿主网络模式**（`--net=host`）。脚本需要直接访问 Android 的 `wlan0` 接口，该接口在 `NAT` 或 `None` 模式下不可见。我们建议专门为此看门狗使用一个小型"一次性"容器。

---

## 获取帮助

如果您的问题未在此列出：

1. 运行 `droidspaces check` 并注意任何失败项
2. 检查容器日志：`droidspaces --name=mycontainer run journalctl -n 100`
3. 尝试以前台模式启动以获得更多可见性：`droidspaces --name=mycontainer --rootfs=/path/to/rootfs --foreground start`
4. 加入 [Telegram 频道](https://t.me/Droidspaces) 获取社区支持
5. 在 [GitHub 仓库](https://github.com/ravindu644/Droidspaces-OSS/issues) 提交 issue
