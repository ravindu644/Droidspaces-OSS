<!--
title: Android 安装
section: Basics
order: 1
desc: 在已 root 的 Android 设备上安装 Droidspaces：安装 APK，让应用设置好后端，无需输入任何终端命令即可创建 Linux 容器。
keywords: install, droidspaces, android, rooted, container, apk, backend, sparse, image, linux, kernelsu, magisk, apatch
-->

# Android 安装

Android 上的一切都在应用内完成，从安装后端到在运行中的容器里打开 shell。本页带你从一台已 root 的手机走到 shell。

## 开始之前

你需要两样东西。

**Root：** 推荐使用 KernelSU 获取 root。Magisk 和 APatch 也可以。它们的 root shell 带有 seccomp 过滤器，会拦截容器运行时所需的操作，因此后端以守护进程的形式在开机时启动，而不是从 shell 启动。这是默认方式，安装后按应用提示重启一次即可。

**支持容器的内核：** 多数原厂内核缺少 Droidspaces 所需的选项，尤其是 PID 和 IPC 命名空间。[内核配置指南](./Kernel-Configuration.md)列出了这些选项，并说明如何构建带这些选项的内核。如果你不想自己编译，[社区支持的设备](./community-supported-devices.md)页面会链接到其他用户构建好的内核。应用会在首次启动时告诉你内核是否受支持。

> [!CAUTION]
>
> 不支持 GrapheneOS。它会拦截容器隔离所依赖的系统调用，无论是否 root。
>
> 不支持 SuSFS。如果你仍然使用它，请在其设置中关闭"为所有进程隐藏 sus 挂载"，否则容器无法启动；也不要上报在开启它时出现的问题。

## 第 1 步：安装应用

1. 从[最新版本](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)下载 APK 并安装。
2. 在 KernelSU 和 APatch 上，先在 **Root 管理器** 中为 Droidspaces 应用授予 root 权限。Magisk 会在应用请求时询问。
3. 打开应用，点 **开始使用**。
4. 点 **检查 Root 权限**，在 Magisk 上允许该请求。
5. 应用会自行安装后端：`droidspaces` 二进制和一个静态 BusyBox 会被放到 `/data/local/Droidspaces/bin`，一个小型启动模块会被安装到你的 Root 管理器，使守护进程和容器能在开机时启动。这需要几秒钟。
6. 阅读结果。"Droidspaces 后端安装成功"表示你的内核通过了探测。如果提示内核不支持 Droidspaces，请先去看[内核配置指南](./Kernel-Configuration.md)再继续。
7. 按应用提示重启。重启会应用后端所需的 SELinux 规则并启动守护进程。

重启后，在受支持的内核上，**主页** 标签页会显示"后端：已安装"和 "DAEMON" 标签，下方是你的内核版本、SELinux 状态和架构。

## 第 2 步：安装容器

打开 **容器** 标签页，点 **+**。你会看到两个选项。

### 从仓库安装

**仓库** 会打开一个为 Droidspaces 构建的发行版列表，并按你设备的架构过滤。里面有 Ubuntu、Debian、Fedora、Arch、Alpine、OpenWrt 等，部分还预配好了桌面环境。

1. 选择一个发行版，点 **下载**。文件会保存到你的下载文件夹，完成后会有通知。
2. 在同一张卡片上点 **安装**。

想要更多发行版，点同一面板中的 **管理仓库**，订阅 Linux Containers 镜像：

```
https://raw.githubusercontent.com/Droidspaces/linuxcontainers-mirror/refs/heads/main/rootfs.json
```

这些镜像是上游的原版构建。安装器会对它们应用同样的 Android 修复，但它们不包含官方构建的额外内容，例如预配置的桌面。

### 从文件安装

**从文件安装** 接收你手机上已有的 rootfs 归档。它必须是 `.tar.xz` 或 `.tar.gz`。用 **将容器导出为 tar 归档包** 从另一台手机导出的容器也可以在这里使用，其设置会自动填好。

### 设置向导

两种方式最终都会进入同一个向导。

1. **容器设置**：给容器起个名字。主机名默认与名字相同。
2. **容器配置**：首次创建容器时默认值就够用。NAT 网络无需配置即可上网。本页的每个开关都在[容器设置](./Features.md)中有说明，之后也可以从容器菜单中修改。
3. **存储**：保持 **使用稀疏镜像** 开启。这样 rootfs 会存放在一个 ext4 镜像文件中，在 Android 的存储上比目录更可靠。你选择的大小是上限；文件只会随容器占用空间而增长。**选择文件夹** 可以把镜像放到 SD 卡或 U 盘，而不是内部存储。
4. **安装概要**：检查无误后点安装。

应用会校验归档、解压它，并应用每个容器在 Android 上都需要的一组修复，例如 Android 要求的网络组（用于 socket 访问）以及不会让内核日志塞满手机的 journald 配置。安装需要一到几分钟，取决于发行版。

## 第 3 步：启动并进入 shell

1. 在 **容器** 标签页上，点新容器的 **启动**。会打开一个日志窗口，实时显示启动过程。启动完成后关闭它，卡片会显示"正在运行"和容器的 PID。
2. 打开 **面板** 标签页。运行中的容器会出现在这里，并显示**运行时间**、IP 地址、CPU 和内存。
3. 点卡片上的 **终端**，选择一个用户，你就进去了。

离开该界面后，终端会在后台继续运行，之后可以回到同一个会话。

如果你更习惯 Termux 或其他终端应用，在 **面板** 标签页点容器卡片打开详情，在 **可用用户** 下选择一个用户，点 **复制登录命令**，然后把命令粘贴到 root shell 中。

## 稍后检查要求

**设置** 里有一个 **要求** 页面。**检查要求** 会运行后端的全部探测，并告诉你缺少哪些内核选项；当容器设置里的某个功能置灰时，这是首先要看的地方。

---

下一页：[Linux 安装](./Installation-Linux.md)

在 Android 上可以跳过下一页，接着阅读这些：[Android 应用指南](./Usage-Android-App.md)、[容器设置](./Features.md)和[显示、音频与桌面](./Graphics-and-Audio.md)。
