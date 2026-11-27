<!--
title: Linux 安装
section: Basics
order: 2
desc: 在 Linux 桌面或服务器上安装 Droidspaces：安装二进制、获取 rootfs、放进 ext4 镜像，并启动第一个容器。
keywords: install, droidspaces, linux, container, runtime, rootfs, tarball, deb, ext4, image, namespaces
-->

# Linux 安装

在 Linux 上，Droidspaces 就是一个静态二进制。任何当前发行版都具备它所需的依赖，唯一的要求是 root。

## 第 1 步：安装二进制

每个版本都会为 amd64、arm64、armhf、i386 和 riscv64 提供 `.deb` 包，以及一个包含对应架构二进制的 tarball。

在 Debian、Ubuntu 及其衍生版上：

```bash
sudo dpkg -i droidspaces_*_amd64.deb
```

在其他系统上，解压 tarball，并把对应你 CPU 的二进制复制到 `PATH` 中的某个目录。`uname -m` 会告诉你该用哪一个：`x86_64`、`aarch64`、`armv7l`（用 `armhf`）、`i686`（用 `x86`）或 `riscv64`。

```bash
tar -xzf droidspaces-v*.tar.gz
sudo install -m 755 droidspaces-v*/x86_64/droidspaces /usr/local/bin/droidspaces
```

## 第 2 步：检查系统

```bash
sudo droidspaces check
```

每一行都应该通过。底部的可选项覆盖网桥、Macvlan、IPv6 NAT 和资源限制等功能；缺少某一项只会禁用对应功能。

## 第 3 步：获取 rootfs

容器需要一份你要运行的发行版的根文件系统（rootfs）。[Linux Containers 镜像服务器](https://images.linuxcontainers.org/images/)提供了大多数发行版的原版构建。浏览到其中一个，例如 `ubuntu/noble/amd64/default/`，下载 `rootfs.tar.xz`。

> [!IMPORTANT]
>
> 一定要用 `sudo` 解压。否则容器内的文件不属于 root，setuid 位会丢失，容器内的 `sudo` 和若干服务将无法工作。

rootfs 可以放在目录里，也可以放在 ext4 镜像文件中。镜像是一个文件，便于复制到另一台机器或手机，并且能让容器的文件系统与宿主机保持分离。用你喜欢的哪种都行。

**目录：**

```bash
mkdir ubuntu
sudo tar -xf rootfs.tar.xz -C ubuntu
```

**ext4 镜像**（文件是稀疏的，所以 16 GB 镜像只占用容器实际使用的空间）：

```bash
truncate -s 16G ubuntu.img
mkfs.ext4 -L ubuntu ubuntu.img
mkdir mnt
sudo mount ubuntu.img mnt
sudo tar -xf rootfs.tar.xz -C mnt
sudo umount mnt
rmdir mnt
```

## 第 4 步：启动、进入、停止

```bash
sudo droidspaces --name=ubuntu --rootfs=/path/to/ubuntu start
# 或者，使用镜像：
sudo droidspaces --name=ubuntu --rootfs-img=/path/to/ubuntu.img start

sudo droidspaces --name=ubuntu enter
sudo droidspaces --name=ubuntu stop
```

第一次 `start` 传入的参数会保存到容器的配置文件中，之后启动只需要 `--name`。`droidspaces show` 会列出正在运行的容器。

## 可选：运行守护进程

在 Linux 上，CLI 无需守护进程也能工作。运行守护进程后，`droidspaces` 用户组的成员无需 `sudo` 即可使用 CLI，并且它会随系统干净地重启。仓库中有一个 systemd 单元：

```bash
sudo groupadd droidspaces
sudo usermod -aG droidspaces "$USER"
sudo cp init/systemd-service/droidspaces.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now droidspaces
```

该单元期望二进制位于 `/usr/bin/droidspaces`，也就是 `.deb` 安装的位置。如果你的位置不同，请修改 `ExecStart`。注销并重新登录后用户组才会生效。

---

上一页：[Android 安装](./Installation-Android.md) | 下一页：[Android 应用指南](./Usage-Android-App.md)
