<!--
title: Android 安装指南
section: Basics
order: 1
desc: Droidspaces 的 Android 分步安装指南。Root 你的设备、安装 APK、设置后端，并通过零终端命令运行 Linux 容器。
keywords: install, droidspaces, android, rooted, container, apk, atomic, backend, sparse, image, linux
-->

# Android 安装指南

Droidspaces 在 Android 上旨在提供"零终端"体验。从首次安装到运行完整的 Linux 发行版，一切都通过直观的 Android 应用完成。

## 前提条件

1. **已 Root 的设备**，使用[此处](../../README_CN.md#rooting-requirements)列出的受支持的 Root 方案
2. **兼容的内核**，已启用 Droidspaces 支持（请参阅[内核配置指南](./Kernel-Configuration.md)）

## 第 1 步：安装应用

1. 从[最新发布版本](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)下载 **Droidspaces APK**。
2. 在你的设备上安装 APK。
3. **授予 Root 权限**并打开应用。

## 第 2 步：自动后端设置

首次启动时，Droidspaces 会执行后端系统的**原子安装**：
- 它会检测你的设备架构（`aarch64`、`armhf` 等）。
- 它将 `droidspaces` 和 `busybox` 二进制文件解压到 `/data/local/Droidspaces/bin`。
- 它执行原子移动操作，确保即使旧版本当前正在运行，二进制文件也能正确安装。
- 它验证校验和以确保零损坏。

## 第 3 步：设置你的第一个容器

你无需手动解压 rootfs 文件。应用会全程处理。

### 方式 A：从 Rootfs 仓库安装（推荐）

最简单的上手方式。应用可以直接浏览、下载并安装发行版，无需手动下载任何文件。

1. **打开容器选项卡**：点击底部导航栏中的中间图标。
2. **打开仓库**：点击 **云图标**（位于"+"按钮上方）。这将打开 Rootfs 仓库面板，自动从我们的[官方仓库](https://github.com/Droidspaces/Droidspaces-rootfs-builder)获取所有适配 Android 的可用发行版。
3. **选择发行版**：浏览或搜索列表。每张卡片显示发行版名称、大小、架构和构建日期。
4. **下载**：点击你想要的发行版卡片上的 **下载** 按钮。卡片上会出现进度条，文件将保存到你的下载文件夹。
5. **安装**：下载完成后，按钮会变为 **安装**。点击它以启动容器设置向导。
6. **配置向导**：
   - **名称**：为你的容器起一个友好的名称。
   - **功能**：根据需要切换硬件加速、IPv6、网络隔离、Android 存储集成等。
   - **容器类型**：我们推荐使用**稀疏镜像**，以便在 Android 的 f2fs 存储上获得更好的性能和稳定性，同时避免奇怪的 SELinux/Keyring 问题。
7. **完成**：应用将解压 tarball 并自动应用**解压后修复**（DNS、屏蔽无用/危险的服务以及安全 Udev）。

> [!TIP]
> 官方仓库包含专为 Android 预配置的发行版。如需更多选择，你可以将 LXC 镜像作为自定义仓库添加——详见使用指南中的 [Rootfs 仓库](./Usage-Android-App.md#rootfs-repository) 章节。

### 方式 B：从本地 Tarball 安装

如果你的设备上已有 `.tar.xz` 或 `.tar.gz` 格式的 rootfs 文件：

1. **打开容器选项卡**，点击 **"+"** 按钮。
2. 从存储中**选择你的 tarball 文件**。
3. 按照上述相同的**配置向导**步骤操作即可。

> [!NOTE]
> 两种方式最终都会进入同一个向导——唯一的区别是 tarball 的来源不同。

## 验证与设置

你可以随时验证系统状态：
1. 前往**设置**（齿轮图标）-> **需求**。
2. 点击**检查需求**。这会在内部运行完整的 `droidspaces check` 套件。
3. **内核配置**：如果你是一位内核开发者，你可以找到一���可复制的 `droidspaces.config` defconfig 片段，类似于[此页面](./Kernel-Configuration.md#required-kernel-configuration)，以确保你的内核与 Droidspaces 完全兼容。

## 下一步

- [Android 应用使用指南](./Usage-Android-App.md)：了解管理详情。
- [显示、音频与桌面指南](./Graphics-and-Audio.md)：配置 GPU 加速、音效与桌面环境自动启动。
- [Linux CLI 指南](./Linux-CLI.md)：提供专家级命令行访问。
