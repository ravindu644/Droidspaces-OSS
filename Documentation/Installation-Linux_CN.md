<!--
title: Linux 安装指南
section: Basics
order: 2
desc: 在 Linux 桌面或服务器上安装 Droidspaces。下载 tarball，解压二进制文件，创建 rootfs 镜像，并启动你的第一个容器。
keywords: install, droidspaces, linux, container, runtime, rootfs, tarball, ext, image, namespaces
-->

# Linux 安装指南

本指南介绍如何在 Linux 桌面或服务器上安装 Droidspaces。

## 前提条件

大多数现代 Linux 发行版已包含 Droidspaces 所需的一切。无需特殊的内核配置或额外的软件包。

**要求：**
- Root 权限（`sudo`）

## 第一步：下载发行版

前往[最新发布页面](https://github.com/ravindu644/Droidspaces-OSS/releases/latest)并下载 **Droidspaces Backend Tarball**。

或者，从命令行下载：

```bash
# 将 VERSION 替换为实际版本号（例如 v4.3.0）
wget https://github.com/ravindu644/Droidspaces-OSS/releases/download/VERSION/droidspaces-vVERSION-DATE.tar.gz
```

## 第二步：解压并安装

### 确认你的架构

```bash
uname -m
```

此命令将输出以下之一：`x86_64`、`aarch64`、`armv7l`（armhf）或 `i686`（x86）。

### 安装二进制文件

```bash
# 解压 tarball
tar xzf droidspaces-v*.tar.gz

# 进入解压后的目录
cd droidspaces-v*/

# 将适用于你架构的二进制文件复制到 PATH 中的目录
sudo cp x86_64/droidspaces /usr/local/bin/droidspaces

# 使其可执行
sudo chmod +x /usr/local/bin/droidspaces
```

## 第三步：验证安装

运行系统要求检查器：

```bash
sudo droidspaces check
```

所有检查项应显示绿色对勾通过。在现代化的 Linux 桌面上，所有功能应该都开箱即用。

## 第四步：获取 Rootfs

你需要一个 Linux 根文件系统来创建你的第一个容器。我们推荐使用Linux官方的 **Linux Containers 镜像仓库**，它为数十种发行版提供了干净、预构建的 rootfs tarball。

**下载地址**：[images.linuxcontainers.org/images/](https://images.linuxcontainers.org/images/)

导航到你想要的发行版（例如 `ubuntu/noble/amd64/default/`）并下载 `rootfs.tar.xz` 文件。

> [!IMPORTANT]
> 在以下两种方法中，解压 rootfs tarball 时你**必须**使用 `sudo`。这可以确保文件所有权（`UID 0`）和特殊的 setuid 权限得以保留。如果不这样做，容器将会损坏，导致诸如 `sudo` 之类的命令无法正常工作（因为环境中的二进制文件不再归 root 所有），且系统服务可能无法启动。

---

### 方案 A：使用目录形式的 Rootfs
只需将 tarball 解压到一个目录中：

```bash
mkdir my-container
sudo tar -xvf rootfs.tar.xz -C my-container
```

### 方案 B：创建 ext4 镜像（推荐）
将你的 rootfs 封装在一个 `.img` 文件中，可提供更好的可移植性并避免与宿主机文件系统冲突。

1. **创建一个稀疏镜像文件**（例如 16GB）：
   ```bash
   truncate -s 16G rootfs.img
   ```

2. **将其格式化为 ext4**：
   ```bash
   mkfs.ext4 -L Droidspaces rootfs.img
   ```

3. **挂载该镜像**：
   ```bash
   mkdir -p rootfs_mount
   sudo mount rootfs.img rootfs_mount
   ```

4. **将 rootfs tarball 解压到挂载点**：
   ```bash
   sudo tar -xvf /path/to/rootfs.tar.xz -C rootfs_mount
   ```

5. **卸载并清理**：
   ```bash
   sudo umount rootfs_mount
   rmdir rootfs_mount
   ```

现在你可以使用以下命令立即启动它：
`sudo droidspaces --name=my-container --rootfs-img=rootfs.img start`

## 下一步

- [Linux CLI 指南](Linux-CLI.md) 获取完整的命令和参数参考
- [功能深入解析](Features.md) 了解每个功能的详细说明
