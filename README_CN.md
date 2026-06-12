[English](./README.md) | 简体中文

---

[![Latest release](https://img.shields.io/github/v/release/ravindu644/Droidspaces-OSS?label=Latest%20Release&style=for-the-badge)](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)
[![Telegram channel](https://img.shields.io/badge/Telegram-Channel-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/Droidspaces)
[![Android support](https://img.shields.io/badge/-Android-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#a-android-devices)
[![Linux desktop](https://img.shields.io/badge/-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#b-linux-desktop)
[![Translation status](https://img.shields.io/weblate/progress/droidspaces?server=https://hosted.weblate.org&style=for-the-badge&label=Translated&logo=weblate)](https://hosted.weblate.org/engage/droidspaces/)

---

# Droidspaces

**Droidspaces** 是一款轻量级、容器可移植的 Linux 容器化工具，能让你在 Android、Linux 甚至**Android Recovery/Ramdisk 等最小化环境**中运行完整的 Linux 系统，支持完整的初始化系统，包括 **systemd**、**OpenRC** 以及其他初始化系统（runit、s6 等）。

Droidspaces 的独特之处在于其对 Android 和 Linux 本身**零依赖**、具备**原生执行**能力。它基于 musl libc 进行静态编译。只要你的设备运行 Linux 内核，Droidspaces 就能在上面运行。无需 Termux，无需中介程序，没有繁琐的安装配置。

- **极致小巧：** 每平台不到 400KB
- **真正原生：** 同一份二进制文件直接在 Android 和 Linux 上运行
- **广泛架构支持：** `aarch64`、`armhf`、`x86_64`、`x86`、`riscv64`，单一静态二进制
- **精美安卓应用：** 全部通过简洁直观的 GUI 操作来安装并管理无限数量的容器，完成 CLI 能做的一切

**Android** + **Linux Namespaces** = **Droidspaces**。由于 Android 基于 Linux 内核构建，Droidspaces 同样能在 Linux 桌面端无缝运行。两个平台都有同等的支持与维护。

> [!TIP]
> 查看[社区支持的 Android 设备](./Documentation/zh-CN/community-supported-devices.md)，了解已知可运行 Droidspaces 的手机列表。

<details>
<summary><b>项目截图展示 (Linux & Android)</b></summary>

<table align="center">
  <tr valign="top">
    <td colspan="3" align="center">
      <b>Linux 展示</b><br>
      <i>Ubuntu + 前台模式</i><br>
      <img src="Documentation/resources/linux/linux-showcase.png" width="95%"><br><br>
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>Android 应用主页</b><br>
      <i>精美的主界面</i><br>
      <img src="Documentation/resources/gallery/1-home_page.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Android 容器</b><br>
      <i>容器菜单中已安装的容器</i><br>
      <img src="Documentation/resources/gallery/2-containers_tab.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>配置菜单</b><br>
      <i>主机名和网络模式</i><br>
      <img src="Documentation/resources/gallery/3_container_configuration.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>配置菜单</b><br>
      <i>第一部分，集成与硬件设置</i><br>
      <img src="Documentation/resources/gallery/4_container_configuration.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>配置菜单</b><br>
      <i>安全与启动项设置，以及高级选项</i><br>
      <img src="Documentation/resources/gallery/5_container_configuration.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>日志记录</b><br>
      <i>容器启动日志</i><br>
      <img src="Documentation/resources/gallery/6_startup_logs.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>Android 面板</b><br>
      <i>仪表盘和门户访问</i><br>
      <img src="Documentation/resources/gallery/7_panel.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>容器信息</b><br>
      <i>一站式容器管理</i><br>
      <img src="Documentation/resources/gallery/8_container_information.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>Systemd 服务</b><br>
      <i>完整的 systemd 图形管理界面</i><br>
      <img src="Documentation/resources/gallery/9_systemd_menu.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>用户选择器</b><br>
      <i>启动一个终端</i><br>
      <img src="Documentation/resources/gallery/10_terminal_user_picker.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>终端界面</b><br>
      <i>喜欢 fastfetch？有的兄弟，有的</i><br>
      <img src="Documentation/resources/gallery/11_terminal_fastfetch.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>隔离检查</b><br>
      <i>挂载隔离演示</i><br>
      <img src="Documentation/resources/gallery/12_mnt_net_isolation.png" width="95%">
    </td>
  </tr>
  <tr valign="top">
    <td align="center" width="33%">
      <b>设置页面</b><br>
      <i>检查运行要求，也可做自定义设置</i><br>
      <img src="Documentation/resources/gallery/13_settings_screen.png" width="95%">
    </td>
    <td align="center" width="33%">
      <b>要求检查器</b><br>
      <i>实时系统检查</i><br>
      <img src="Documentation/resources/gallery/14_built_in_requirements_checker.png" width="95%">
    </td>
    <td align="center" width="33%">
      <!-- 空单元格用于平衡三列布局 -->
    </td>
  </tr>
</table>

</details>

---

### 快速导航

- [什么是 Droidspaces？](#what-is-droidspaces)
- [功能特性](#features)
- [Droidspaces 与其他方案对比](#droidspaces-vs-the-alternatives)
- [系统需求](#requirements)
    - [Android](#a-android-devices)
        - [Root 环境要求](#rooting-requirements)
        - [已知问题](#known-quirks)
        - [Android 内核要求](#android-kernel-requirements)
            - [Non-GKI（旧内核）](#non-GKI)
            - [GKI（现代内核）](#GKI)
    - [Linux 桌面端](#b-linux-desktop)
- [安装指南](#installation)
- [使用指南](#usage)
- [显示、音频与桌面](#display-audio-desktop)
- [安全与隔离的理念](#security-model)
- [更多文档](#additional-documentation)
- [疑难解答](./Documentation/zh-CN/Troubleshooting.md)
- [社区支持的 Android 设备](./Documentation/zh-CN/community-supported-devices.md)
- [参与贡献](#contribution)
- [致谢](#credits)

---

<a id="what-is-droidspaces"></a>

## 什么是 Droidspaces？

Droidspaces 是一个利用 Linux 内核命名空间运行完整的 Linux 发行版的**容器运行环境**，并以真正的初始化系统（systemd、OpenRC 等）作为 PID 1。

与传统的 chroot 仅仅改变表面上的根目录不同，Droidspaces 创建了真正的进程隔离。每个容器拥有自己的 PID 树、自己的挂载表、自己的主机名、自己的 IPC 资源以及自己的 cgroup 层级。最终呈现的是一个完整的 Linux 环境，如同轻量级虚拟机一般，但由于直接共享宿主机内核，性能零损耗。

Droidspaces 的设计目标是原生运行在任何搭载 Linux 内核的设备上，包括 **Android**、**Linux 桌面端**以及 **Android Recovery/Ramdisk 等最小化环境**。在 Android 上，它处理了所有会导致其他容器工具崩溃的内核问题、SELinux 冲突、复杂网络场景和加密问题。在 Linux 桌面端，开箱即用，无需任何额外配置。在 Ramdisk 上，它同样能处理 `pivot_root` 等各种问题！

整个环境运行时是一个不到 400KB 的**单一静态二进制文件**，基于 musl libc 编译，无任何外部依赖。

---

<a id="features"></a>

## 功能特性

| 功能 | 描述 |
|---------|-------------|
| **初始化系统支持** | 以 PID 1 运行 systemd、OpenRC 或任何其他初始化系统。完整的服务程序管理和规范的启动/关机/重启流程。 |
| **深度 Android 集成** | 支持两种守护进程模式：**原生 init.rc**（最底层的集成方式，自动生成/不可被终止的持久性）和**用户空间守护进程**（应用内可切换，通过 `post-fs-data.sh` 启动，无需修改镜像）。**两种模式均绕过 root 域 seccomp 限制，确保容器生命周期稳定** [[init.rc 开发者指南](./init/README_CN.md)]。 |
| **命名空间隔离** | 通过 PID、MNT、UTS、IPC 以及 Cgroup 命名空间实现完全隔离。每个容器拥有自己的进程树、挂载表、主机名、IPC 资源和 cgroup 层级。 |
| **网络隔离** | **3 种网络模式（Host、NAT、None）**。通过 `CLONE_NEWNET` 实现纯网络隔离（NAT/None 模式）或共享宿主机网络（Host 模式）。在 Android 和 Linux 版本上均可使用。 |
| **Android 显示与 GPU** | 三种加速模式：**llvmpipe**（软件渲染，适用所有设备）、**VirGL**（Mali/PowerVR）和 **Turnip**（原生高通/Adreno）。自 v6.3.0 起，X 服务器和 VirGL 服务器在容器启动时自动拉起，无需任何 Termux 命令。环境变量（`DISPLAY=:5`、`GALLIUM_DRIVER=virpipe`）自动注入。[[更多信息](./Documentation/zh-CN/Graphics-and-Audio.md)] |
| **Android 音效** | PulseAudio 守护进程以 Termux 用户身份在宿主机上运行，使 Android 音频 HAL 授予其设备访问权限。套接字以绑定挂载方式挂载到容器的 `/tmp/.pulse-socket`，`PULSE_SERVER` 自动注入，音效开箱即用。[[更多信息](./Documentation/zh-CN/Graphics-and-Audio.md#pulseaudio)] |
| **Linux GPU 加速** | 在 Linux 桌面端上无需额外配置 AMD 和 Intel GPU 硬件加速。[[更多信息](./Documentation/zh-CN/Graphics-and-Audio.md)] |
| **端口转发** | 在 NAT 模式下将宿主机端口转发到容器（例如 `--port 22:22`）。支持 TCP 和 UDP，以及端口范围如 `1-500:1-500`。 |
| **易失模式** | 使用 OverlayFS 的临时容器。所有更改存储在内存中，退出后丢弃。非常适合用于测试和开发。 |
| **自定义绑定挂载** | 将宿主机目录映射到容器中的任意挂载点。支持链式语法（`-B a:b -B c:d`）和逗号分隔语法（`-B a:b,c:d`）。 |
| **配置文件支持** | 使用 `--conf` 直接从 `.config` 文件加载配置。与 CLI 覆盖无缝集成（支持 `--reset`），并自动同步到工作区以持久化。 |
| **硬件直通模式** | 通过单个配置开关，将宿主机硬件（GPU、摄像头、传感器、USB、块设备）直通给你的容器。 |
| **多容器管理** | 可同时运行无限数量的容器，每个容器拥有独立的名称、PID 文件和配置。可独立启动、停止、进入和管理。 |
| **容器内重启支持** | 无需打开 Droidspaces 软件即可远程重启容器！ |
| **Android 存储** | 将 `/storage/emulated/0` 绑定挂载到容器中，直接访问设备的共享存储空间。 |
| **PTY/终端支持** | 完整的 PTY 隔离。前台模式提供交互式终端，支持正确的终端尺寸调整处理（仅限使用 `-f` 标志的二进制模式）。 |
| **多 DNS 支持** | 配置自定义 DNS 服务器（逗号分隔），绕过宿主机的默认 DNS 查询。若不指定任何 DNS 服务器，则回退到 ISP 的默认 DNS。 |
| **SELinux 宽容模式** | 可在容器启动时根据需要选择将 SELinux 设为宽容模式。 |
| **Rootfs 镜像 / 直接块设备支持** | 支持从 ext4 `.img` 文件启动容器，支持自动 loop 挂载、文件系统检查以及 SELinux 上下文加固（如需要）。CLI 模式下也支持挂载分区、SD 卡等块设备！**Android 应用同样支持以 rootfs.img 模式创建可移植容器** [ [如何手动创建 ext4 rootfs.img？](./Documentation/zh-CN/Installation-Linux.md#option-b-create-an-ext4-image-recommended)] |
| **自动恢复** | 自动清理残留的 PID 文件，扫描容器中的孤立进程，以及通过 `/run/droidspaces` 的内存元数据同步实现强大的配置恢复。 |
| **Cgroup 隔离 (v1/v2)** | 每个容器都有独立的 cgroup 层级（`/sys/fs/cgroup/droidspaces/<name>`），完全兼容 systemd。同时支持旧版 v1 和现代 v2 层级。 |
| **自适应安全与死锁护盾** | 内核感知的 BPF 过滤器可在旧内核上自动解决 FBE 密钥环冲突。提供手动**死锁护盾**切换开关，用于修复受影响旧内核设备（例如内核 4.14.113）上特定的 VFS `grab_super()` 死锁问题。护盾禁用时（默认），Droidspaces 授予完整的命名空间自由，可在所有内核上原生启用**嵌套容器/Docker** 等功能。 |
| **特权模式** | 使用 `--privileged` 标志获取完全访问权限！请谨慎使用：启用此标志会放宽多项安全保护以支持 Flatpak/Bwrap/K3S 等功能，启用此模式时请勿提交 bug 报告。 |

---

<a id="droidspaces-vs-the-alternatives"></a>

## Droidspaces 与其他方案对比

| 类别 | **Droidspaces** | LXC + Termux | Docker + Termux | Chroot | PRoot |
|----------|-----------------|--------------|-----------------|--------|-------|
| **核心技术** | **命名空间** | 命名空间 | 命名空间 | 仅路径重定向 | 系统调用劫持 (PTRACE) |
| **性能** | **原生** | 原生 | 原生 | 原生 | 中等（每次系统调用 PTRACE 均有开销） |
| **启动耗时 (systemd)** | **150ms - 750ms** | 750ms - 2000ms | N/A（不支持 systemd） | N/A | N/A |
| **初始化系统 (PID 1)** | **支持（完整 systemd/OpenRC/runit/s6/SysVinit）** | 支持（完整 systemd/OpenRC/runit/s6/SysVinit） | 不支持 | 无 | 无 |
| **进程隔离** | **完全** | 完全 | 完全 | 无（共享宿主机 PID 命名空间） | 无（共享宿主机 PID 命名空间） |
| **文件系统隔离** | **完全** | 完全 | 完全 | 无（`chroot /proc/1/root /system/bin/sh` 可瞬间逃逸） | 有限 |
| **挂载隔离** | **完全** | 完全 | 完全 | 无 | 无 |
| **IPC / UTS / Cgroup 隔离** | **完全** | 完全 | 完全 | 无 | 无 |
| **Android 端容器持久性** | **真正的不可终止。存活超过 15 天。完全免疫开发者选项中的"不保留活动"和"无后台进程"。** | 低（会被 Android LMK / 电池优化杀死） | 低（被 Android LMK / 电池优化杀死） | 低（会被 Android LMK / 电池优化杀死） | 低（被 Android LMK / 电池优化杀死） |
| **数据持久性（应用卸载）** | **零数据丢失。所有容器、配置和数据保存在 `/data/local/Droidspaces`，完全独立于应用。二进制和守护进程在自己的进程会话中运行（`setsid`），与应用的进程组分离。卸载应用不会停止任何东西，也不会删除任何数据。** | 卸载 Termux 后一切消亡。LXC 配置和 rootfs 存储在 Termux 内部（`/data/data/com.termux`），卸载 Termux 会清除一切。 | 卸载 Termux 后一切消亡。`/data/docker` 中的容器数据保留，但若不重新安装整套工具链则无法访问。 | 若 rootfs 在 `/data/local/` 中则安全。若存储在 Termux 主目录内则不安全。 | 卸载 Termux 后一切消亡。PRoot rootfs 通常位于 `/data/data/com.termux`，卸载 Termux 时一并删除。 |
| **开机自启** | **支持（原生 `init.rc` / `service.d`）。即使手机锁定、`/data` 加密，甚至在任何用户应用启动之前，即可自动启动容器。** | 不支持 | 不支持 | 不支持 | 不支持 |
| **Android 端网络隔离** | **同类首创。完整的 NAT/Veth + 互联网访问开箱即用。无需手动配置。** | 仅主机网络模式下互联网可用（`lxc.net.0.type = none`）。真正的网络隔离（veth + NAT）通常需要手动设置网桥、iptables 和 ip_forward，且在大多数设备上仍然不可用。 | 需要 `--network host` 才能上网；真正的带互联网访问的网络隔离在 Android 上通常无法可靠工作。 | 无（没有网络命名空间） | 无（没有网络命名空间） |
| **硬件与原生 GPU 直通** | **完全（一键开关）。Adreno Turnip、USB、传感器、网络接口、块设备。完整的 `systemd-udevd` 支持，如同真正的 Linux PC。** | 手动绑定挂载，无 udev | 手动绑定挂载，无 udev | 手动绑定挂载，无 udev | 无 |
| **Termux-X11 支持** | **完全（一键开关）。X 服务器和 VirGL 服务器在容器启动时自动拉起。环境变量（`DISPLAY=:5`、`GALLIUM_DRIVER=virpipe`）自动注入。** | 手动套接字透传 | 手动套接字透传 | 手动套接字透传 | 手动套接字透传 |
| **特权模式** | **完全 + 可定制（`--nomask`、`--nocaps`、`--noseccomp` 等）** | 手动配置 | 支持（`--privileged`） | 完全（无任何防护） | 不支持 |
| **嵌套容器（Docker-in-DS）** | **所有内核上原生支持** | 复杂的手动配置 | 复杂的手动配置 | 不支持 | 不支持 |
| **临时/易失容器** | **支持（OverlayFS，RAM 后端存储，退出后零持久化）** | 不支持 | 支持 | 不支持 | 不支持 |
| **便携式 rootfs.img 支持** | **支持（原生 loop 挂载、fsck、SELinux 加固）** | 不支持 | 不支持 | 不支持 | 不支持 |
| **更旧的内核支持 (3.10+)** | **完全** | 不稳定（cgroup 冲突，在许多旧 Android 内核上不可用） | 不稳定（cgroup 冲突，在许多旧 Android 内核上不可用） | N/A | N/A |
| **Android 专项优化** | **SELinux 实时修补、FBE 密钥环处理、存储集成、网络修复等。** | 无（并非为 Android 设计） | 无（并非为 Android 设计） | 无 | 无 |
| **是否需要 Root** | **是** | 是 | 是 | 是 | 否 |
| **是否需要 Termux** | **从不需要。零依赖。** | 是 | 是 | 否 | 是 |
| **设置复杂度** | **低。安装 APK 即可运行。** | 高（手动 cgroup 挂载、手动配置修改、手动启动守护进程） | 高（手动 cgroup 挂载、手动配置修改、手动启动守护进程） | 中等（每次启动都需要手动挂载脚本） | 中等 |
| **二进制大小** | **每种架构均约 400KB 大小** | 10MB+ | 50MB+ | N/A | 约 10MB |
| **依赖项** | **零。单一静态 musl 二进制文件。** | Termux + liblxc + 模板 | Termux + dockerd + containerd + runc | Termux 或任何 shell 环境 | Termux + proot 二进制文件 |

---

<a id="requirements"></a>

## 系统需求

<a id="a-android-devices"></a>

### A. Android 设备

Droidspaces 支持运行 Linux 内核 **3.10 及以上**版本的 Android 设备：

| 内核版本 | 支持级别 | 说明 |
|----------------|---------------|-------|
| 3.10 | 已支持 | **旧版。** 最低要求。基本的命名空间支持。基于 systemd 的发行版可能不稳定；推荐使用 **Alpine**。 |
| 4.4 - 4.19 | 稳定 | **加固。** [完整支持 systemd 版本低于 v258 的现代发行版](./Documentation/zh-CN/Troubleshooting.md#modern-distros)。原生支持嵌套容器（Docker/Podman）。如果在特定内核（如 4.14.113）上遇到因 VFS 死锁 bug 导致的 systemd 挂起，请手动启用**死锁护盾** [[更多信息](./Documentation/zh-CN/Features.md#vfs-deadlock)]。 |
| 5.4 - 5.10 | 推荐 | **主线。** 完整功能支持，包括嵌套容器和 Cgroup v2。 |
| 5.15+ | 旗舰 | **完全。** 最佳性能和与现代发行版的最大兼容性。 |

<a id="rooting-requirements"></a>

#### Root 需求

你的设备必须已获取 root 权限。以下 root 方案已经过测试：

| Root 方案 | 状态 | 说明 |
|-------------|--------|-------|
| **KernelSU** | 完全支持 | 已测试且稳定。**推荐**。由于 Droidspaces 本身就需要自定义内核，我们建议将 KernelSU 集成到你的内核中。 |
| **APatch** | 支持* | *需要在 Droidspaces 应用中启用**守护进程模式**并重启设备，以绕过用户空间启动运行时的 root 域 seccomp 限制。 |
| **Magisk** | 支持* | *需要在 Droidspaces 应用中启用**守护进程模式**并重启设备，以绕过用户空间启动运行时的 root 域 seccomp 限制。 |

> [!TIP]
>
> 守护进程模式将容器生命周期管理从应用的用户空间转移到持久化后台服务。

<a id="known-quirks"></a>

> [!CAUTION]
>
> **不支持 GrapheneOS** - 因为它阻止了命名空间隔离和容器化所需的关键系统调用，即使有 root 访问权限也无法运行像 Droidspaces 这样的用户空间运行时。
>
> **不支持 SuSFS** - 使用 SUSFS 时请勿报告任何 BUG。如果必须在 Droidspaces 上使用 SuSFS，请确保在 SuSFS4KSU 设置中禁用"HIDE SUS MOUNTS FOR ALL PROCESSES"，以避免容器启动失败。

<a id="android-kernel-requirements"></a>

#### Android 内核需求

Android 内核通常经过大量修改，关键的容器功能可能被禁用。你的内核必须启用特定的配置选项（命名空间、Cgroups、Seccomp 等）才能运行 Droidspaces。

<a id="non-GKI"></a>

##### Non-GKI（旧内核）
适用于内核：**3.10, 3.18, 4.4, 4.9, 4.14, 4.19**。添加所需的配置片段后，这些内核即可即插即用。
参见：[旧内核配置](./Documentation/zh-CN/Kernel-Configuration.md#non-gki)

<a id="GKI"></a>

##### GKI（现代内核）
适用于内核：**5.4, 5.10, 5.15, 6.1+**。这些内核需要额外步骤来处理因配置更改导致的 ABI 破坏。
参见：[现代 GKI 内核配置](./Documentation/zh-CN/Kernel-Configuration.md#gki)

**内核支持的后续步骤：**
- **自动检查**：使用 Android 应用内置的需求检查器（**设置** -> **需求**）。
- **完整技术指南**：[内核配置指南](./Documentation/zh-CN/Kernel-Configuration.md)

> [!TIP]
>
> **需要内核编译帮助？** 请查看以下指南：
>
> https://github.com/ravindu644/Android-Kernel-Tutorials

---

<a id="b-linux-desktop"></a>

### B. Linux 桌面端

大多数现代 Linux 桌面发行版默认已包含 Droidspaces 所需的一切条件。**无需额外配置。**

只需从 [GitHub Releases](https://github.com/ravindu644/Droidspaces-OSS/releases/latest) 下载 tarball，解压，然后使用对应你 CPU 架构的二进制文件即可。

你可以通过运行以下命令来验证系统是否满足所有需求：

```bash
sudo ./droidspaces check
```

---

<a id="installation"></a>

## 安装指南

- [Android 安装指南](./Documentation/zh-CN/Installation-Android.md)
- [Linux 安装指南](./Documentation/zh-CN/Installation-Linux.md)

---

<a id="usage"></a>

## 使用指南

- [Android App 使用指南](./Documentation/zh-CN/Usage-Android-App.md)
- [Linux CLI 使用指南](./Documentation/zh-CN/Linux-CLI.md)

---

<a id="display-audio-desktop"></a>

## 显示、音频与桌面

自 v6.3.0 起，Droidspaces 在容器启动时会自动拉起 Termux:X11 X 服务器、VirGL 服务器和 PulseAudio 守护进程（对应开关已启用时）。环境变量 `DISPLAY=:5`、`GALLIUM_DRIVER=virpipe` 和 `PULSE_SERVER` 均通过 `/run/droidspaces.env` 在 init 系统完成启动前注入容器，显示、GPU 加速和音效无需手动配置即可正常使用。

我们的官方 XFCE rootfs 压缩包已预置 **XFCE 自动启动**配置：启用 Termux:X11 后启动容器，XFCE 会自动启动并出现在 Termux:X11 应用中，无需任何终端命令。从 [Rootfs 仓库](./Documentation/zh-CN/Usage-Android-App.md#rootfs-仓库)（搜索"XFCE"）下载，或直接从 [Droidspaces Rootfs Builder Releases](https://github.com/Droidspaces/Droidspaces-rootfs-builder/releases/latest) 下载。

GPU 加速方式、音效配置、桌面环境自动启动原理以及 Linux 桌面配置，请参阅 **[显示、音频与桌面指南](./Documentation/zh-CN/Graphics-and-Audio.md)**。

---

<a id="security-model"></a>

## 安全与隔离理念

> [!IMPORTANT]
>
> Droidspaces 是一个**特权容器运行环境**，专为追求简洁、性能和原生集成的**高级用户**打造，而非复杂的生产级沙箱。
>
> 为了在 Android 上提供完整的 systemd 支持、原生硬件加速（GPU）以及复杂的挂载/网络功能，容器 root 需要真正的特权。尽管 Droidspaces 不使用严格受限的"非特权"（用户命名空间）模式，但它仍然应用了多层安全防护：
> - **权能裁剪**：默认情况下，Droidspaces 会剥离高风险权能（如 `CAP_SYS_MODULE`、`CAP_SYS_RAWIO`）。
> - **挂载加固**：关键的宿主机路径被屏蔽或重新挂载为只读。
> - **Seccomp 过滤**：默认阻止常见的攻击向量（如 CVE-2026-31431 和恶意内核模块加载）。

> [!WARNING]
>
> **容器不等于监狱**
> 如果进程在 Droidspaces 容器中以 **root** 身份运行，它将拥有相当大的权限。
>
> 恶意的 root 用户可能试图逃逸或操纵宿主机。**Droidspaces 不是为不受信任代码设计的沙箱。**
>
> 我们专注于将完整的 Linux 服务器体验带到你的口袋中，而不是构建一个生产级的堡垒。

> [!NOTE]
>
> **我们的安全建议：**
> 1. **日常使用不要使用 root 用户**：就像在常规 Linux PC 上一样，在容器内创建普通用户并使用 `sudo`。
> 2. **谨慎使用特殊模式**：`--privileged` 和 `--hw-access` 等标志会刻意放宽安全限制，仅在必要时使用。
> 3. **尊重宿主机**：如果你容器的 root 权限被攻破，你的设备也将被攻破。

---

<a id="additional-documentation"></a>

## 更多文档

| 文档 | 描述 |
|----------|-------------|
| [功能深度解析](./Documentation/zh-CN/Features.md) | 每个主要功能的详细说明。 |
| [显示、音频与桌面指南](./Documentation/zh-CN/Graphics-and-Audio.md) | Android 和 Linux 上的 GPU 加速、PulseAudio 音效与桌面环境自动启动。 |
| [你可以做的酷炫事情（Tailscale、Docker 等）](./Documentation/zh-CN/Cool-things-you-can-do.md) | 在容器内运行 Tailscale、Docker 等工具的实用配置方案。 |
| [卸载指南](./Documentation/zh-CN/Uninstallation.md) | 如何从系统中移除 Droidspaces。 |

---

<a id="contribution"></a>

## 参与贡献

欢迎贡献 - 请随时在 [GitHub 仓库](https://github.com/ravindu644/Droidspaces-OSS)上提交 issue 或 pull request。

如有问题或需要支持，请加入 [Telegram 频道](http://t.me/Droidspaces)。

想要参与 Android 应用的翻译贡献，请访问 Weblate 项目：

<a href="https://hosted.weblate.org/engage/droidspaces/">
<img src="https://hosted.weblate.org/widget/droidspaces/open-graph.png" alt="翻译状态" />
</a>

---

<a id="credits"></a>

## 致谢

Droidspaces 建立在开源社区的杰出工作之上。特别感谢以下项目的启发和贡献：

*   **[LXC](https://github.com/lxc/lxc)** - 提供了现代 Linux 容器化的核心架构愿景和灵感。
*   ~~**[Brutal-Busybox](https://github.com/feravolt/Brutal_busybox)** - 为 Android 用户空间应用中某些操作提供了静态链接的 BusyBox 二进制文件。~~
*   **[Busybox](https://busybox.net/)** - 用于 Android 用户空间应用中执行某些操作的静态链接 BusyBox 二进制文件（[我们自定义 BusyBox 的源码](https://github.com/Droidspaces/busybox-droidspaces)）
*   ~~**[Magisk](https://github.com/topjohnwu/Magisk)** - 提供了 `magiskpolicy` 工具，为实时 SELinux 修补提供了核心引擎。~~
*   ~~**[KernelSU-Next](https://github.com/KernelSU-Next/KernelSU-Next)**、**[MMRL](https://github.com/MMRLApp/MMRL)** 和 **[LSPatch](https://github.com/LSPosed/LSPatch)** - 为我们现代化的 UI 设计语言和 Android 用户体验提供了灵感。~~
*   **[ReTerminal](https://github.com/RohitKushvaha01/ReTerminal)**、**[Termux](https://github.com/termux/termux-app)**、**[LXC-Manager](https://github.com/Container-On-Android/LXC-Manager)** - 为内置终端模拟器提供了终端后端支持。
*   **[JetBrains Mono](https://www.jetbrains.com/legalforms/fonts/)** - 应用中终端和代码 UI 使用的等宽字体，基于 [SIL Open Font License 1.1](https://scripts.sil.org/OFL) 许可。

---

## 许可证

Droidspaces 基于 [GNU General Public License v3.0](./LICENSE) 许可。

Copyright (C) 2026 [ravindu644](https://github.com/ravindu644) 和贡献者。

---
