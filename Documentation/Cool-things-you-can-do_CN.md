<!--
title: 使用 Droidspaces 能做的酷事
section: 实战教程
order: 1
desc: Droidspaces 酷项目：使用 Tailscale + UFW + Fail2Ban 搭建安全移动服务器，以及在 Droidspaces 中嵌套运行 Docker 容器。
keywords: droidspaces, projects, android, server, tailscale, ufw, failban, container, nested, docker
-->

# 通过 Droidspaces 能做的酷事

> [!IMPORTANT]
> 本指南特别针对 **Android 设备**。虽然 Droidspaces 也可以在 Linux 桌面上运行，但这些说明专门针对 Android 环境独特的网络、存储和内核需求。

### 快速导航

- [1. 搭建安全的"移动服务器"（Tailscale + UFW + Fail2Ban）](#1-setting-up-a-secure-mobile-server-tailscale--ufw--fail2ban)  
    - [前提条件](#prerequisites)  
    - [步骤 1：安装网络工具和兼容层](#step-1-install-networking-tools--compatibility-layer)  
    - [步骤 2：个人用户设置与 SSH 加固](#step-2-personal-user-setup--ssh-hardening)  
    - [步骤 3：设置 Tailscale](#step-3-set-up-tailscale)  
    - [步骤 4：使用 UFW（防火墙）保护容器](#step-4-secure-the-container-with-ufw-firewall)  
    - [步骤 5：使用 Fail2Ban 添加暴力破解防护](#step-5-add-brute-force-protection-with-fail2ban)  
- [2. 运行 Docker 容器（嵌套容器化）](#2-running-docker-containers-nested-containerization)  
    - [前提条件](#prerequisites-1)  
    - [步骤 1：确保使用 NAT 网络](#step-1-ensure-nat-networking)  
    - [步骤 2：兼容层（iptables-legacy）](#step-2-compatibility-layer-iptables-legacy)  
    - [步骤 3：安装 Docker](#step-3-install-docker)  
    - [步骤 4：非 root 用户设置](#step-4-non-root-user-setup)  
    - [步骤 5：验证安装](#step-5-verify-installation)  
    - [Host 模式或旧内核的"最后手段"（仅限老内核）](#last-resort-for-host-mode-or-legacy-kernels-old-kernels-only)  

---

<a id="1-setting-up-a-secure-mobile-server-tailscale--ufw--fail2ban"></a>
## 1. 搭建安全的"移动服务器"（Tailscale + UFW + Fail2Ban）

通过将 Droidspaces 与 Tailscale 及标准 Linux 安全工具结合使用，你可以将 Android 设备变成一台安全、可从任何地方访问的 Linux 服务器。

<a id="prerequisites"></a>
### 前提条件

- **内核支持**：此设置需要多个 Netfilter 和 IPSet 模块。请参阅 [UFW/Fail2ban 的附加内核配置](./Kernel-Configuration_CN.md#additional-kernel-configuration-for-ufwfail2ban) 查看所需选项的完整列表。
- **LTS 发行版**：强烈建议使用长期支持（LTS）发行版，如 **Ubuntu 24.04 LTS** 或 **Debian 12**，以获得最佳的稳定性和软件包支持。
- **Root 用户**：本指南中的所有步骤都必须在容器内以 **root** 用户身份运行。
- **包管理器**：以下命令使用 `apt`，仅在 Debian 和 Ubuntu 系列的发行版中可用。
- **NAT 模式**：**必填。** 你必须在 NAT 模式（`--net=nat`）下运行容器。在使用 UFW 这类防火墙时使用主机网络模式可能会干扰 Android 宿主端的网络连接，甚至根本无法正常工作。

---

<a id="step-1-install-networking-tools--compatibility-layer"></a>
### 步骤 1：安装网络工具和兼容层

为了处理防火墙规则和网络调试，你首先需要安装必要的网络工具，并确保与 Android 内核的兼容性。

1. **安装工具**：
   ```bash
   apt update && apt install -y net-tools iptables
   ```

2. **切换到旧版 iptables**：
   现代 Ubuntu/Debian 版本默认使用 `nftables` 后端，这在 Android 内核上的 Droidspaces 容器中经常会运行失败。你**必须**切换到旧版 `iptables` 后端以确保防火墙能正常工作：
   ```bash
   update-alternatives --set iptables /usr/sbin/iptables-legacy
   update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
   ```

---

<a id="step-2-personal-user-setup--ssh-hardening"></a>
### 步骤 2：个人用户设置与 SSH 加固

为了维护服务器的安全，最佳方案是创建一个具有 `sudo` 权限的专用用户，并禁用 SSH 的直接 root 访问。

1. **回收 UID 1000**：Linux 发行版通常将 UID `1000` 分配给第一个非 root 用户（如 `ubuntu`）。要为你自己的个人用户使用此 ID，你应首先检测并完全删除任何现有的 UID 1000：
   ```bash
   # 识别并删除与 UID 1000 关联的默认用户
   DEFAULT_USER=$(getent passwd 1000 | cut -d: -f 1)
   userdel -r "$DEFAULT_USER"
   groupdel "$DEFAULT_USER" 2>/dev/null
   ```

2. **将你的个人用户创建为 UID 1000**（将 `YOUR_USER` 替换为你期望的用户名）：
   ```bash
   useradd -m -u 1000 -s /bin/bash YOUR_USER
   usermod -aG sudo YOUR_USER
   passwd YOUR_USER
   ```

3. **安装 OpenSSH 服务器**：
   ```bash
   apt install -y openssh-server
   ```

4. **禁用 Root 登录**：
   编辑 `/etc/ssh/sshd_config` 以防止直接 root 访问：
   ```bash
   sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin no/' /etc/ssh/sshd_config
   sed -i 's/PermitRootLogin yes/PermitRootLogin no/' /etc/ssh/sshd_config
   systemctl restart ssh
   ```

---

<a id="step-3-set-up-tailscale"></a>
### 步骤 3：设置 Tailscale

Tailscale 为你的容器提供安全的 P2P 隧道，让你可以从 Tailnet 中的任何设备访问它，无需在公共路由器上开放端口。

1. **安装 Tailscale**：
   ```bash
   curl -fsSL https://tailscale.com/install.sh | sh
   ```

2. **认证**：
   ```bash
   tailscale up
   ```

---

<a id="step-4-secure-the-container-with-ufw-firewall"></a>
### 步骤 4：使用 UFW（防火墙）保护容器

由于 Droidspaces 的 NAT 模式目前仅支持 IPv4，我们应在 UFW 中禁用 IPv6 以避免初始化错误。

1. **在 UFW 中禁用 IPv6**：
   ```bash
   sed -i 's/IPV6=yes/IPV6=no/' /etc/default/ufw
   ```

2. **设置默认策略**：
   ```bash
   ufw default deny incoming
   ufw default allow outgoing
   ```

3. **将 Tailscale 接口加入白名单**：
   与其将特定 IP 地址列入白名单，不如告诉 UFW 信任所有通过你的私有 Tailscale 隧道传入的流量：
   ```bash
   ufw allow in on tailscale0
   ```

4. **启用防火墙**：
   ```bash
   ufw --force enable
   ```

---

<a id="step-5-add-brute-force-protection-with-fail2ban"></a>
### 步骤 5：使用 Fail2Ban 添加暴力破解防护

Fail2Ban 监控你的系统日志，并自动封禁表现出恶意行为的 IP 地址。

1. **安装 Fail2Ban**：
   ```bash
   apt install -y fail2ban
   ```

2. **创建本地配置**：
   在 `/etc/fail2ban/jail.local` 创建一个持久配置文件来保护 SSH 并与 UFW 集成：

   ```ini
   [DEFAULT]
   # 在 10 分钟内失败 5 次后封禁 1 小时
   bantime  = 1h
   findtime = 10m
   maxretry = 5

   # 使用 UFW 作为封禁动作
   banaction = ufw

   # 使用白名单以防止意外锁定：
   # 1. YOUR_TAILSCALE_IP：你的私有隧道地址（例如 100.74.132.81）
   # 2. 172.28.0.0/16：Droidspaces 内部 NAT 网桥（涵盖所有容器）
   # 3. YOUR_LAN_SUBNET：如果你使用端口转发，你的本地 Wi-Fi 子网（例如，若你的局域网 IP 是 192.168.1.15，则使用 192.168.1.0/24）
   ignoreip = YOUR_TAILSCALE_IP 172.28.0.0/16 YOUR_LAN_SUBNET

   [sshd]
   enabled = true
   port    = ssh
   backend = systemd
   ```

3. **启动并验证**：
   ```bash
   systemctl restart fail2ban
   fail2ban-client status sshd
   ```
你的"移动服务器"现在已是一座坚固的堡垒！任何试图从公网访问它的人都将被封禁，而你则可以通过你的私有 Tailscale 网络保持完全的访问。

---

<a id="2-running-docker-containers-nested-containerization"></a>
## 2. 运行 Docker 容器（嵌套容器化）

Droidspaces 支持在所有受支持的内核版本上于容器内原生运行 Docker。这使你可以直接在 Android 设备上运行嵌套的容器化服务（如 Portainer、Home Assistant 等）。

<a id="prerequisites-1"></a>
### 前提条件

- **LTS 发行版**：如果你的内核版本低于 **5.x.x**，强烈建议使用 LTS 发行版，如 **Ubuntu 24.04 LTS**，以获得最佳兼容性。
- **内核配置**：确保你的内核已启用所需的 Droidspaces 选项。参见[所需的内核配置](./Kernel-Configuration_CN.md#required-kernel-configuration)。
- **存储模式**：你**必须**使用 **ext4 /data** 或 **rootfs.img 模式**（推荐）。
    - *为什么？* Android 默认的 `f2fs` 文件系统不支持 Docker 的 `overlay2` 存储驱动所需的 overlay 特性。使用 `rootfs.img` 可以确保你运行在原生的 ext4 文件系统上。
- **NAT 模式**：**必须** Docker 需要 NAT 网络来创建其内部 `docker0` 网桥并为嵌套容器提供互联网访问。

---

<a id="step-1-ensure-nat-networking"></a>
### 步骤 1：确保使用 NAT 网络

在主机网络模式下运行 Droidspaces 会导致 Docker 在尝试创建 `docker0` 接口时失败。请为你的容器选择 NAT 模式。

你可以通过 Android App 编辑容器配置来轻松切换到 NAT 模式。

<a id="step-2-compatibility-layer-iptables-legacy"></a>
### 步骤 2：兼容层（iptables-legacy）

Docker 的网络栈严重依赖 `iptables`。现代发行版通常默认使用 `nftables` 后端，这会在容器中引发"chain not found"错误。在安装 Docker 之前请切换到旧版后端：

```bash
update-alternatives --set iptables /usr/sbin/iptables-legacy
update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
```

<a id="step-3-install-docker"></a>
### 步骤 3：安装 Docker

使用官方的 Docker 安装脚本或发行版的包管理器：

```bash
# 使用官方便捷脚本
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh
```

<a id="step-4-non-root-user-setup"></a>
### 步骤 4：非 root 用户设置

如果想在运行 Docker 命令时不加 `sudo` 前缀，请将你的用户添加到 `docker` 组：

```bash
# 将 YOUR_USER 替换为你的用户名
usermod -aG docker YOUR_USER

# 无需注销即可应用组更改
newgrp docker
```

<a id="step-5-verify-installation"></a>
### 步骤 5：验证安装

测试 Docker 是否可以拉取并运行一个嵌套容器：

```bash
docker run --rm hello-world
```

如果你看到 "Hello from Docker!" 消息，说明你已成功在 Android 上运行嵌套容器！

> [!TIP]
>
> **Docker 故障排除**：如果 Docker 守护进程未能自动启动或 `docker run` 命令失败，请在你的终端中手动运行 `sudo dockerd`。这将输出实时日志，帮助你识别是否存在缺失的内核模块、文件系统冲突或网桥问题。

<a id="last-resort-for-host-mode-or-legacy-kernels-old-kernels-only"></a>
### Host 模式或旧版内核的"最后手段"（仅限老内核）

如果你无论如何都想在**Host模式**下运行 Docker，或者你的内核太旧而不支持 `iptables-legacy` 和 NAT 网络，你可以禁用 Docker 的内部网络管理，作为最后手段。

运行以下命令来配置守护进程：

```bash
mkdir -p /etc/docker
cat <<EOF > /etc/docker/daemon.json
{
  "iptables": false,
  "ip6tables": false,
  "bridge": "none"
}
EOF
systemctl restart docker
```
> [!WARNING]
>
> 上述 `daemon.json` 配置禁用了 Docker 的内部网桥（`docker0`）和所有自动端口转发。你只能使用 `--network host` 参数来运行具有互联网访问的 Docker 容器。
>
> 例如：`docker run -it --network host ubuntu`

---
