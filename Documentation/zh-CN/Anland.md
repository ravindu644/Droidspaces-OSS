<!--
title: Anland (Wayland) 使用指南
section: Guides
order: 5
desc: 在 Droidspaces Android 容器中配置 Anland Wayland 显示，从安装打过补丁的合成器到配置容器。
keywords: anland, wayland, droidspaces, android, kwin, weston, kde, plasma, gpu, adreno, snapdragon
-->

# Anland (Wayland) 使用指南

Anland 是 Droidspaces 的可选 Wayland 显示方案。为 Anland 打过补丁的 Linux 合成器（如 KWin 或 Weston）把桌面渲染到 GPU 缓冲区中，Anland 应用在 Android 上显示这些缓冲区，而一个小巧的 Droidspaces 中转程序（broker）通过 Unix 域套接字连接容器和应用。

与 Termux:X11 不同，Anland 是为 KDE Plasma Wayland 这类 Wayland 合成器设计的。Anland 负责显示链路；容器、rootfs、权限、GPU 访问和启动流程仍由 Droidspaces 管理。

推荐使用搭载 Adreno GPU 的高通 Snapdragon 设备。Anland 依赖设备侧的 GPU 缓冲区共享，高通设备通常兼容性更好。

## 更多 Anland 指南

- [Anland 使用指南](https://github.com/SuperTurtleDev/anland/blob/legacy/doc/UserManual/anland_guide_zh.md)：安装 Anland，并通过 chroot 或 Droidspaces 连接支持 Anland 的 Wayland 桌面。
- [Anland 设置使用说明](https://github.com/SuperTurtleDev/anland/blob/legacy/doc/UserManual/anland_settings_guide_zh.md)：配置 Android 应用中的输入、显示、连接、音频与窗口行为。

## 快速导航

- [前提条件](#requirements)
- [第 1 步：安装 Anland 后端](#backend)
  - [在容器内编译](#manual-build)
  - [自动构建 rootfs](#rootfs-builder)
- [第 2 步：安装 Anland 应用](#app)
- [第 3 步：配置容器](#container-config)

<a id="requirements"></a>

## 前提条件

在配置 Anland 之前，请先确认满足以下条件：

1. **支持 Droidspaces 的 Android 设备：** 在容器配置中启用 **Anland 显示** 后，Anland 即可与 Droidspaces 配合使用。
2. **打过 Anland 补丁的合成器后端：** 主要支持的后端是 KWin。可用的构建见 [Anland producers 目录](https://github.com/SuperTurtleDev/anland/tree/legacy/producers)。
3. **兼容的 GPU：** 推荐使用搭载 Adreno GPU 的高通 Snapdragon 设备，兼容性更好。

<a id="backend"></a>

## 第 1 步：安装 Anland 后端

容器内的桌面必须使用打过 Anland 补丁的合成器后端。未打补丁的 Wayland 合成器无法把画面发送到 Anland 应用。

<a id="manual-build"></a>

### 在容器内编译

如果你想自己选择发行版或桌面环境，或者需要调试配置，就自己编译后端。

1. 在 Droidspaces 容器中打开一个 shell。
2. 克隆 Anland 源码：

   ```bash
   git clone -b legacy https://github.com/SuperTurtleDev/anland.git
   ```

3. 在 `producers` 下选择与你的桌面和发行版对应的目录。KDE 后端目录包括：

   ```text
   producers/kde/
   ├── anland_backend/
   ├── Debian13_v5/
   ├── Fedora43_v5/
   └── ubuntu2604_v5/
   ```

4. 以 root 身份在对应目录中运行 `build.sh`。该脚本会安装所需的依赖和 Anland 后端。
5. 用项目提供的 `startup.sh` 脚本启动桌面。

<a id="rootfs-builder"></a>

### 自动构建 rootfs

想要一个可以直接导入的 rootfs，请使用 [Droidspaces-rootfs-Desktop-builder](https://github.com/Goldzxcbug/Droidspaces-rootfs-Desktop-builder)。

> [!IMPORTANT]
> 该构建器目前支持 KDE Plasma、GNOME 和 niri 桌面。

1. Fork 该项目。
2. 打开它的 GitHub **Actions** 页面，找到 `编译并发布Droidspaces Rootfs` 工作流。
3. 手动运行该工作流，显示后端选择 `anland-wayland`。
4. 等待构建完成，通常约 15 分钟。
5. 打开项目的 **Releases** 页面，下载生成的 rootfs 包，并导入 Droidspaces。
6. 如果 KDE 桌面没有配置为自动启动，请以普通用户进入容器，运行与你的桌面对应的命令：

   ```bash
   startplasma-wayland       # KDE Plasma
   startplasmamobile         # KDE Plasma Mobile
   gnome-session --session=gnome
   niri-anland
   ```

<a id="app"></a>

## 第 2 步：安装 Anland 应用

1. 从 [Anland 发布页面](https://github.com/superturtlee/anland/releases)上最新的 5.x 版本中下载 `anland-v5.apk`。Droidspaces 只支持 Anland 5.x。那里标记为"Latest"的 6.x 版本是另一个应用，无法与它配合使用。
2. 在 Android 设备上安装 APK。
3. 授予它 Root 权限。使用 KernelSU 或 APatch 时，在管理器应用中允许它。使用 Magisk 时，打开一次 Anland 并接受 root 授权提示。

<a id="container-config"></a>

## 第 3 步：配置容器

在 Droidspaces 中编辑目标容器的配置。

### 必需设置

1. 打开 **GPU 访问**。**硬件访问** 也能满足这一要求，但 GPU 访问就足够了。
2. 打开 **Anland 显示**。
3. 关闭 **配置 Termux:X11**。

Anland 和 Termux:X11 使用各自的显示路径，它们的环境变量和服务互相冲突。打开 **Anland 显示** 会为该容器关闭 **配置 Termux:X11**。
在 CLI 上，用 `--anland` 或配置键 `enable_anland` 启用 Anland。

> [!IMPORTANT]
> 每个容器只使用一条显示路径。使用 Anland 时，请保持 Termux:X11 配置关闭。

### 推荐设置

- **配置 PulseAudio：** 保持关闭，因为 Anland 有自己的音频链路。如果之前启用过 PulseAudio，请从容器中删除已有的 `PULSE_SERVER` 环境变量，避免应用继续连接旧的服务。
- **SELinux 宽容模式：** 保持关闭，先在 SELinux 强制模式下试用 Anland。如果 Anland 在你的设备上无法启动或显示不出任何内容，可以临时启用此选项，排查是否存在兼容性问题。
- **允许沙箱：** 运行 KDE Plasma 和类似桌面环境时启用。它允许用户命名空间，可以改善 Plasma Wayland 的响应速度。这会削弱容器隔离，因此只对你信任的容器启用。Anland 不需要特权模式、`noseccomp`、`nomask` 或其他特权选项。

启动容器，然后在 **面板** 标签页点它的卡片，打开容器详情。**终端** 下方的 **Anland** 卡片有一个 **启动** 按钮，可以在 Anland 应用中打开桌面。容器启动且 Droidspaces 记录下 broker 套接字后，该按钮变为可用。容器内的合成器启动后，桌面就会出现。

---

上一页：[显示、音频与桌面](./Graphics-and-Audio.md) | 下一页：[内核配置](./Kernel-Configuration.md)
