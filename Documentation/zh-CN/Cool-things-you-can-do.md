<!--
title: 实用配方
section: Recipes
order: 1
desc: 在手机上用 Droidspaces 搭建的东西：通过 Tailscale 让手机成为随处可访问的家庭服务器，并用 UFW 和 Fail2Ban 加固；在容器里运行 Docker；以及让 Wi-Fi 保持唤醒的看门狗。
keywords: droidspaces, recipes, android, server, tailscale, ufw, fail2ban, docker, nested, container, wifi, power, save
-->

# 实用配方

三样人们会在手机上用 Droidspaces 搭建的东西。每一项都假设容器已经能启动并能上网，[安装指南](./Installation-Android.md)可以带你走到这一步。命令针对 Debian 和 Ubuntu；其他发行版也有相同的软件包，只是名称不同。

## 把手机变成随处可访问的家庭服务器

一个带 SSH 的容器，无论你在哪里都能通过 Tailscale 从笔记本访问它，并且不向互联网开放任何端口。Tailscale 会为你的账号下每台设备分配一个私有地址，并在它们之间建立加密隧道，因此你永远不需要在路由器上做端口转发。

**开始之前：** 容器必须处于 NAT 模式（默认）或网关模式。在主机模式下，容器内的防火墙会和手机自身的网络机制冲突。UFW 和 Fail2Ban 需要内核中额外的 netfilter 选项；[内核指南](./Kernel-Configuration.md#step-2-firewall-support-optional)列出了它们。以下所有操作都在容器内以 root 身份执行。

**1. 创建你自己的用户：** 发行版通常自带一个 UID 为 1000 的默认用户。把它替换成你的用户，授予 `sudo`，并设置密码：

```bash
DEFAULT_USER=$(getent passwd 1000 | cut -d: -f1)
[ -n "$DEFAULT_USER" ] && userdel -r "$DEFAULT_USER"
useradd -m -u 1000 -s /bin/bash alice
usermod -aG sudo alice
passwd alice
```

**2. SSH，并关闭 root 登录：**

```bash
apt update && apt install -y openssh-server
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin no/' /etc/ssh/sshd_config
systemctl restart ssh
```

**3. Tailscale：** 安装并登录；命令会打印一个链接，可在任意设备上打开：

```bash
curl -fsSL https://tailscale.com/install.sh | sh
tailscale up
```

此后，在你 Tailscale 账号下的任何其他设备上，都可以用 Tailscale 管理控制台中显示的名称执行 `ssh alice@<container name>`。

**4. UFW：** 拒绝一切不是通过 Tailscale 到达的流量：

```bash
apt install -y ufw
ufw default deny incoming
ufw default allow outgoing
ufw allow in on tailscale0
ufw --force enable
```

如果容器在禁用 IPv6 的情况下运行，请先在 `/etc/default/ufw` 中设置 `IPV6=no`，否则 UFW 无法启动。

**5. Fail2Ban**，在多次登录失败后封禁对应地址。把下面的内容放进 `/etc/fail2ban/jail.local`，并填好你的 Tailscale 地址和 LAN 子网，以免把自己锁在外面：

```ini
[DEFAULT]
bantime  = 1h
findtime = 10m
maxretry = 5
banaction = ufw
ignoreip = 100.x.y.z 172.28.0.0/16 192.168.1.0/24

[sshd]
enabled = true
port    = ssh
backend = systemd
```

```bash
apt install -y fail2ban
systemctl restart fail2ban
fail2ban-client status sshd
```

在容器设置中打开 **开机自启动**，服务器就会随手机一起启动。

## 在容器里运行 Docker

在每个受支持的内核上，Docker 都能运行在 Droidspaces 容器里，因此 Portainer、Home Assistant、Pi-hole 或任何为 Docker 打包的软件都能跑在手机上。

仓库中的 Base 和桌面镜像（名为 Base、XFCE 或 bspwm 的那些）已经安装并配置好 Docker。安装一个、启动它，下面这条命令立刻就能用：

```bash
docker run --rm hello-world
```

Minimal 镜像和 OpenWrt 不包含它。本配方剩下的部分适用于这些镜像，以及你自己准备的 rootfs。

**开始之前：** 容器需要自己的网络栈，因此要用 NAT 模式（默认）或网关模式；在主机模式下 Docker 无法创建自己的网桥。它需要 ext4 文件系统来存放 Docker 的 `overlay2` 存储，稀疏镜像 rootfs 正好提供这一点。在 Debian 和 Ubuntu 上，App 的安装器已经把 `iptables` 切换到 legacy 后端，这是 Android 内核所要求的；对于你自己准备的 rootfs，请手动执行：

```bash
update-alternatives --set iptables /usr/sbin/iptables-legacy
update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
```

**1. 安装 Docker**，使用它自己的脚本，然后让你的用户无需 `sudo` 即可运行它：

```bash
curl -fsSL https://get.docker.com | sh
usermod -aG docker alice
```

**2. 检查：**

```bash
docker run --rm hello-world
```

**3. 可选：用户命名空间重映射：** 在容器设置中打开 **允许沙箱** 后，Docker 可以在重映射的用户范围内运行它的容器，这样 Docker 容器内的 root 就不再是 Droidspaces 容器内的 root。把下面的内容放进 `/etc/docker/daemon.json` 并重启 Docker：

```json
{ "userns-remap": "default" }
```

这是维护者手机上的配置：Ubuntu 24.04、稀疏镜像、网关模式、Docker 29（使用 `overlay2` 和 `userns-remap`），运行在 5.15 内核上。

如果 Docker 守护进程无法启动，请在终端中手动运行 `dockerd`。它的输出会指出缺少的内核选项或失败的网络步骤。

**在早于 5.x 的内核上**，还有两个问题可能出现。Docker 的设备管理会使用这些内核没有的 BPF attach 类型，表现为 `Invalid argument`；请在 `daemon.json` 中放入 `"exec-opts": ["native.cgroupdriver=cgroupfs"]` 和 `"storage-driver": "vfs"`。另外，如果无法使用 NAT 模式而容器必须以主机模式运行，作为最后手段，可以关闭 Docker 自己的网络功能。此时 Docker 容器需要 `--network host` 才能访问互联网：

```json
{ "iptables": false, "ip6tables": false, "bridge": "none" }
```

## 让 Wi-Fi 保持唤醒

息屏后 Android 会让 Wi-Fi 无线电进入省电模式，容器里的服务器会变得迟钝或掉线。系统没有对应设置，所以用一个很小的容器每分钟把它关掉。

容器必须处于主机模式：其他任何模式都看不到手机的 `wlan0`。可以专门用一个最小的 Alpine 容器来做这件事。

**1. 安装 `iw`**：Alpine 上用 `apk add iw`，Debian 和 Ubuntu 上用 `apt install iw`。

**2. 看门狗脚本**，放在 `/usr/local/bin/wifi-watchdog.sh`，并用 `chmod +x` 赋予可执行权限：

```sh
#!/bin/sh
while true; do
    if iw dev wlan0 get power_save 2>/dev/null | grep -q on; then
        iw dev wlan0 set power_save off
    fi
    sleep 60
done
```

**3. 用服务让它持续运行：** 在 Alpine 上，创建 `/etc/init.d/wifi-watchdog`：

```sh
#!/sbin/openrc-run
name="Wi-Fi power save watchdog"
command="/usr/local/bin/wifi-watchdog.sh"
command_background=true
pidfile="/run/${RC_SVCNAME}.pid"
depend() {
    need networking
}
```

```bash
chmod +x /etc/init.d/wifi-watchdog
rc-update add wifi-watchdog default
rc-service wifi-watchdog start
```

在 systemd 发行版上，创建 `/etc/systemd/system/wifi-watchdog.service`：

```ini
[Unit]
Description=Wi-Fi power save watchdog
After=network.target

[Service]
ExecStart=/usr/local/bin/wifi-watchdog.sh
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

```bash
systemctl enable --now wifi-watchdog
```

这个容器也要打开 **开机自启动**。

---

上一页：[内核配置](./Kernel-Configuration.md) | 下一页：[故障排查](./Troubleshooting.md)
