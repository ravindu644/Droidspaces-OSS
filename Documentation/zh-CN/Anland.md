<!--
title: Anland (Wayland) 使用指南
section: 指南
order: 3
desc: 在 Droidspaces Android 容器中配置 Anland Wayland 显示，包括后端安装、APK 安装与容器选项。
keywords: anland, wayland, droidspaces, android, kwin, weston, kde, plasma, gpu, adreno, snapdragon
-->

# Anland (Wayland) 使用指南

Anland 是 Droidspaces 的可选 Wayland 显示方案。它通过一套缓冲区共享协议，让 Linux 合成器（如 KWin / Weston）将桌面内容渲染到 GPU 缓冲区，再由 Android 端的 Anland 显示表面进行呈现。Linux 容器与 Android 应用之间通过轻量级守护进程和 Unix 域套接字完成桥接。

与 Termux:X11 路径不同，Anland 面向 Wayland 合成器工作流，适合希望在 Droidspaces 容器中运行 KDE Plasma Wayland 等桌面环境的场景。

### 快速导航

- [前提条件](#requirements)
- [第 1 步：安装支持 Anland 的后端](#backend)
    - [方式一：在容器内手动编译安装](#manual-build)
    - [方式二：使用 Droidspaces Rootfs 自动构建项目](#rootfs-builder)
- [第 2 步：安装 Anland 应用](#app)
- [第 3 步：配置 Droidspaces 容器](#container-config)

---

<a id="requirements"></a>

## 前提条件

在配置 Anland 之前，请先确认满足以下条件：

1. **支持 Droidspaces 的 Android 设备**：Anland 与 Droidspaces 配合使用，需要在容器配置中启用 `Anland Display`。
2. **支持 Anland 的显示后端**：需要使用打过 Anland 补丁的合成器后端。目前主要支持 `KWin`，相关后端可参考 [Anland producers 目录](https://github.com/superturtlee/anland/tree/main/producers)。
3. **推荐高通 Snapdragon / Adreno 设备**：Anland 更依赖设备侧 GPU 与缓冲区共享能力，高通平台通常兼容性更好。

> [!NOTE]
>
> Anland 只负责显示链路。容器、rootfs、权限、GPU 访问和启动流程仍由 Droidspaces 管理。

---

<a id="backend"></a>

## 第 1 步：安装支持 Anland 的后端

容器内的桌面环境必须使用支持 Anland 的后端，否则 Android 端无法接收到 Wayland 画面。

<a id="manual-build"></a>

### 方式一：在容器内手动编译安装

这种方式适合希望自行选择桌面环境、发行版或调试后端的用户。

1. **进入 Droidspaces 容器。**

2. **拉取 Anland 源码。**

   ```bash
   git clone https://github.com/superturtlee/anland.git
   ```

3. **在 `producers` 目录中选择对应的桌面环境和发行版。**

   以 KDE 后端为例：

   ```txt
   producers
   └── kde
       ├── anland_backend_debian13_v5
       ├── anland_backend_v5
       ├── Debian13_v5
       ├── Fedora43_v5
       └── ubuntu2604_v5
   └── other desktops
   ```

4. **以 root 身份执行对应目录中的 `build.sh`。**

   构建脚本会处理依赖安装，并将对应的 Anland 后端安装到容器中。

5. **使用项目提供的 `startup.sh` 启动桌面。**

<a id="rootfs-builder"></a>

### 方式二：使用 Droidspaces Rootfs 自动构建项目

如果你主要使用 KDE Plasma，推荐使用自动构建的 rootfs，流程更简单，也更适合重复部署。

项目地址：[Droidspaces-rootfs-KDE-builder](https://github.com/Goldzxcbug/Droidspaces-rootfs-KDE-builder)

> [!IMPORTANT]
>
> 该方式目前仅支持 KDE Plasma 桌面。

1. **Fork 该项目。**
2. 进入 GitHub **Actions** 页面，找到 `编译并发布Droidspaces Rootfs` 工作流。
3. 手动运行工作流，并启用 `Wayland 支持` 选项。
4. 等待构建完成，通常约 15 分钟。
5. 进入项目的 **Releases** 页面，下载生成的 rootfs 包，并导入 Droidspaces。
6. 如果没有启用 KDE 桌面自启动，请进入容器中的普通用户环境，运行：

   ```bash
   startplasma-wayland
   ```

---

<a id="app"></a>

## 第 2 步：安装 Anland 应用

1. 从 [Anland 最新发布版本](https://github.com/superturtlee/anland/releases/latest) 下载 **Anland APK**。
2. 在 Android 设备上安装 APK。
3. 首次启动时授予 Anland **Root 权限**。

---

<a id="container-config"></a>

## 第 3 步：配置 Droidspaces 容器

在 Droidspaces 中编辑目标容器配置，并设置以下选项。

### 必需设置

1. 启用 **GPU 访问**。
2. 启用 **Anland Display**。
3. 关闭 **配置 Termux:X11**。

> [!IMPORTANT]
>
> Anland 与 Termux:X11 是两条不同的显示路径。使用 Anland 时应关闭 Termux:X11 相关配置，避免环境变量和显示服务互相干扰。

### 推荐设置

1. **关闭配置 PulseAudio**。

   Anland 自带音频处理链路，通常不需要 Droidspaces 的 PulseAudio 配置。关闭该选项后，还需要清理容器内已有的 `PULSE_SERVER` 环境变量，避免应用继续连接旧的 PulseAudio 服务。

2. **默认关闭 SELinux 宽容模式。**

   建议优先在 SELinux enforcing 模式下运行。若某些设备在强制模式下无法正常启动或显示 Anland，再临时启用 **SELinux 宽容模式** 作为兼容性排查手段。

### KDE Plasma 注意事项

> [!IMPORTANT]
>
> 在 Debian 或 Ubuntu 容器中运行 KDE Plasma Wayland 时，请确保内核启用了 User Namespace（User-NS）支持，并在 Droidspaces 特权模式中启用 `noseccomp`。否则 KDE Plasma 可能出现明显卡顿或响应迟缓。
