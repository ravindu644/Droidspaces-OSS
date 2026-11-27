<!--
title: Android 应用指南
section: Guides
order: 1
desc: Droidspaces Android 应用的每个界面：主页、容器和面板标签页，容器菜单，rootfs 仓库，终端，服务管理和设置。
keywords: droidspaces, android, app, guide, containers, panel, terminal, systemd, services, repository, settings, daemon, mode
-->

# Android 应用指南

应用底部有三个标签页：**主页**、**容器** 和 **面板**，右上角有一个齿轮图标用于打开 **设置**。本页按这个顺序介绍它们。如果你还没有安装容器，请先看[安装指南](./Installation-Android.md)。

## 主页

<p align="center">
  <img src="../resources/gallery/01-home.png" width="30%">
</p>

一张状态卡片会显示后端是否已安装且为最新、版本号，以及守护进程是否在运行。下方是已安装和运行中的容器数量，以及设备的内核版本、SELinux 状态、Android 版本和架构。底部的卡片会打开安装指南。

应用更新后会出现"后端：可升级"。点它即可安装新的后端；运行中的容器不受影响。

## 容器

<p align="center">
  <img src="../resources/gallery/02-containers.png" width="30%">
  <img src="../resources/gallery/03-container-menu.png" width="30%">
  <img src="../resources/gallery/15-start-logs.png" width="30%">
</p>

每个已安装的容器都有一张卡片，显示名称、状态、主机名、网络模式、镜像大小和资源限制，以及已开启的选项。**启动** 会启动容器。会打开一个日志窗口，实时显示后端的输出；操作结束后可以关闭它。**停止** 和 **重启** 同理。容器运行后，卡片会显示其 PID，按钮会变成 **停止** 和 **重启**。

长按卡片可打开菜单：

- **编辑容器配置**：与安装时相同的表单。上面的每项设置都在[容器设置](./Features.md)中有说明。改动在下次启动时生效。
- **迁移至稀疏镜像**：把目录形式的容器转换为 ext4 镜像文件。
- **调整稀疏镜像容量**：修改镜像容器的上限。
- **将容器导出为 tar 归档包**：把 rootfs 及其设置打包成可在另一台手机上安装的 `.tar.gz`。除非你选择包含环境变量，否则它们会被排除，因为其中常常含有敏感信息。
- **卸载容器**：如果容器正在运行则先停止，然后删除其 rootfs 和设置。你需要输入容器名确认。残留内容见[卸载](./Uninstallation.md)。

**+** 按钮用于添加容器，来源可以是 **仓库** 或 **从文件安装**。

### 仓库

<p align="center">
  <img src="../resources/gallery/04-add-container.png" width="30%">
  <img src="../resources/gallery/05-repository.png" width="30%">
</p>

这里列出了为 Droidspaces 构建的发行版，并按你设备的架构过滤。可以按名称搜索。**下载** 会把 tarball 下载到你的下载文件夹，并在卡片上显示进度；完成后会有通知，按钮会变成 **安装**。下载失败会出现 **重试** 按钮；之前下载过的 tarball 会在下次打开该面板时被识别。

面板顶部的 **管理仓库** 让你订阅其他发布 `rootfs.json` 的源。值得添加的是 Linux Containers 镜像，它提供官方仓库没有构建的发行版和版本：

```
https://raw.githubusercontent.com/Droidspaces/linuxcontainers-mirror/refs/heads/main/rootfs.json
```

### 从文件安装

从手机存储中选择一个 `.tar.xz` 或 `.tar.gz` 的 rootfs 归档。从 Droidspaces 导出的容器会带上其设置，并自动填入向导，之前会有一个警告页供你检查。其他任何 rootfs tarball 也可以，只要它带有 init 系统。

### 设置向导

<p align="center">
  <img src="../resources/gallery/16-wizard-name.png" width="30%">
  <img src="../resources/gallery/17-wizard-config.png" width="30%">
  <img src="../resources/gallery/18-wizard-network.png" width="30%">
</p>
<p align="center">
  <img src="../resources/gallery/19-wizard-limits.png" width="30%">
  <img src="../resources/gallery/20-wizard-storage.png" width="30%">
  <img src="../resources/gallery/21-wizard-summary.png" width="30%">
</p>

先填名称和主机名，然后是配置表单，接着是存储，最后是概要。[安装指南](./Installation-Android.md#设置向导)逐步介绍了整个过程，[容器设置](./Features.md)解释了每个开关。

## 面板

<p align="center">
  <img src="../resources/gallery/09-panel.png" width="30%">
  <img src="../resources/gallery/10-container-details.png" width="30%">
</p>

这里只显示运行中的容器，每个都带有运行时间、IP 地址、CPU 和内存。设置了内存或 CPU 限制时，用量会按限制显示。**终端** 可以直接从卡片打开 shell；开启了 [Anland 显示](./Anland.md)的容器，在桌面就绪后旁边会出现一个 **启动** 按钮。点卡片上的其他位置会打开容器详情。

### 容器详情

- **容器信息**：发行版、主机名、运行时间、IP 地址、CPU 和内存。
- **终端**：点 **打开** 获取 shell。已有会话在运行时，卡片会提示，点它即可回到这些会话。
- **可用用户**：rootfs 中的用户账号。选择一个并点 **复制登录命令**，即可把该用户的 `droidspaces enter` 命令复制到剪贴板，供 Termux 或其他 root shell 使用。
- **磁盘占用**：已用空间和总空间。
- **Init 系统**：点 **管理** 控制服务。具体内容取决于 init 系统。

### 管理服务

<p align="center">
  <img src="../resources/gallery/11-systemd-services.png" width="30%">
</p>

**Systemd：** 列表显示每个单元及其状态，并可按全部、已启用、已禁用、已屏蔽、静态或异常状态的单元筛选。每个单元都可以启动、停止、重启、启用、禁用或屏蔽，**查看日志** 会显示其 journal。**检查单元** 会打开单元属性、依赖关系和 `systemctl status` 输出，**编辑覆盖配置** 可以为它写入 drop-in，这样无需在容器里使用文本编辑器就能修改服务设置。

**OpenRC**、**OpenWrt (procd)** 和 **runit** 会得到相同的列表，支持启动、停止、重启、启用和禁用，OpenWrt 上还支持重载，runit 上还有一个开机运行开关，用于添加或删除该服务的 `down` 文件。

## 终端

<p align="center">
  <img src="../resources/gallery/12-terminal.png" width="30%">
  <img src="../resources/gallery/22-terminal-settings.png" width="30%">
</p>

内置终端会以你选择的用户身份在容器中打开 shell。标签页以 `user@hostname` 命名；**+** 标签页会打开另一个会话，每次都会询问用户。会话运行在前台服务中，因此离开界面、切换应用和息屏都不会中断；通知里有退出按钮和唤醒锁开关。

双指缩放可以改变文字大小。点终端会打开键盘。**设置** 下的 **终端** 页面可以设置字体（支持导入自定义等宽字体）、初始字号、仅对终端页面生效的深色模式、关闭标签页前的确认，以及 **轻触即可打开键盘**。在编辑器里工作时可以关掉后者，这样轻触只会移动光标；终端中会出现一个键盘按钮，用于打开和关闭键盘。

## 设置

<p align="center">
  <img src="../resources/gallery/13-settings.png" width="30%">
  <img src="../resources/gallery/14-requirements-check.png" width="30%">
</p>

**后端**

- **重新安装后端**：重新写入二进制和启动模块。如果主页报告后端已损坏，就用它。
- **守护进程模式**：默认开启。后端作为系统守护进程在开机时启动，位于 root shell 之外，从而绕过 Magisk 和 APatch 加在 root shell 上的 seccomp 过滤器。保持开启；修改后需要重启。
- **将 Droidspaces 集成到系统路径中**：通过启动模块把 `droidspaces` 放到每个 root shell 的 `PATH` 中，这样你可以直接输入 `droidspaces`，而不必输入 `/data/local/Droidspaces/bin/droidspaces`。请阅读开关上的警告：在基于 OverlayFS 的模块系统上会导致无限重启。
- **自启动优先级**：设置了开机自启动的容器的启动顺序。拖动即可调整。网关容器要排在使用它做路由的容器之上。
- **要求**：列出 Droidspaces 所需的内核选项，并提供一个供内核构建者使用的复制按钮；**检查要求** 会针对当前运行的内核执行全部探测。

**外观**：语言、浅色和深色主题、AMOLED 黑色、Android 12 及以上的系统调色板、强调色，以及上面的终端设置。

**更新**：检查应用的新版本。

**调试**：**生成错误报告** 会把日志和设备信息收集到一个文件中，方便附在 issue 里。

**实验性**：仅当 ROM 是 32 位而内核是 64 位时，才会显示 **使用 64 位后端**，它会把后端切换到 64 位版本。

**关于**：版本、开发者、源码链接、Telegram 频道和贡献者。

## 数据在哪里

后端创建的所有内容都在 `/data/local/Droidspaces` 下：二进制在 `bin/`，每个容器的 rootfs 或镜像及其 `container.config` 在 `Containers/<name>/`，日志在 `Logs/`。应用只读写这里；卸载应用不会影响它，守护进程会继续运行。

---

上一页：[Linux 安装](./Installation-Linux.md) | 下一页：[容器设置](./Features.md)
