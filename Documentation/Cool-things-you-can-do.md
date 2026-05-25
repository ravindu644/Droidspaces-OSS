<!--
title: Cool Things You Can Do with Droidspaces
section: Recipes
order: 1
desc: Cool projects with Droidspaces: secure mobile server with Tailscale + UFW + Fail2Ban, and Docker containers nested inside Droidspaces.
keywords: droidspaces, projects, android, server, tailscale, ufw, failban, container, nested, docker
-->

# Cool Things You Can Do with Droidspaces

> [!IMPORTANT]
> This guide is specifically focused on **Android devices**. While Droidspaces also runs on Linux Desktop, these instructions address the unique networking, storage, and kernel requirements of the Android environment.

### Quick Navigation

- [1. Setting Up a Secure "Mobile Server" (Tailscale + UFW + Fail2Ban)](#1-setting-up-a-secure-mobile-server-tailscale--ufw--fail2ban)
    - [Prerequisites](#prerequisites)
    - [Step 1: Install Networking Tools & Compatibility Layer](#step-1-install-networking-tools--compatibility-layer)
    - [Step 2: Personal User Setup & SSH Hardening](#step-2-personal-user-setup--ssh-hardening)
    - [Step 3: Set Up Tailscale](#step-3-set-up-tailscale)
    - [Step 4: Secure the Container with UFW (Firewall)](#step-4-secure-the-container-with-ufw-firewall)
    - [Step 5: Add Brute-Force Protection with Fail2Ban](#step-5-add-brute-force-protection-with-fail2ban)
- [2. Running Docker Containers (Nested Containerization)](#2-running-docker-containers-nested-containerization)
    - [Prerequisites](#prerequisites-1)
    - [Step 1: Ensure NAT Networking](#step-1-ensure-nat-networking)
    - [Step 2: Compatibility Layer (iptables-legacy)](#step-2-compatibility-layer-iptables-legacy)
    - [Step 3: Install Docker](#step-3-install-docker)
    - [Step 4: Non-Root User Setup](#step-4-non-root-user-setup)
    - [Step 5: Verify Installation](#step-5-verify-installation)
    - ["Last Resort" for Host Mode or Legacy Kernels (Old Kernels Only)](#last-resort-for-host-mode-or-legacy-kernels-old-kernels-only)

---

## 1. Setting Up a Secure "Mobile Server" (Tailscale + UFW + Fail2Ban)

You can turn your Android device into a secure, accessible-from-anywhere Linux server by combining Droidspaces with Tailscale and standard Linux security tools.

### Prerequisites

- **Kernel Support**: This setup requires several Netfilter and IPSet modules. See [Additional Kernel Configuration for UFW/Fail2ban](./Kernel-Configuration.md#additional-kernel-configuration-for-ufwfail2ban) for the full list of required options.
- **LTS Distribution**: It is highly recommended to use a Long-Term Support (LTS) distribution like **Ubuntu 24.04 LTS** or **Debian 12** for the best stability and package support.
- **Root User**: All steps in this guide must be run as the **root** user inside the container.
- **Package Manager**: The commands below use `apt`, which is only available in Debian and Ubuntu-based distributions.
- **NAT Mode**: **Mandatory.** You must run your container in NAT mode (`--net=nat`). Using host networking while running a firewall like UFW can interfere with the Android host's connectivity, or it won't even work.

---

### Step 1: Install Networking Tools & Compatibility Layer

To handle firewall rules and network debugging, you first need to install the essential networking tools and ensure compatibility with the Android kernel.

1. **Install tools**:
   ```bash
   apt update && apt install -y net-tools iptables
   ```

2. **Switch to Legacy iptables**:
   Modern Ubuntu/Debian versions use the `nftables` backend by default, which often fails in Droidspaces containers on Android kernels. You **must** switch to the legacy `iptables` backend to ensure your firewall works:
   ```bash
   update-alternatives --set iptables /usr/sbin/iptables-legacy
   update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
   ```

---

### Step 2: Personal User Setup & SSH Hardening

To maintain a secure server, creating a dedicated user with `sudo` privileges and disabling direct root access over SSH is best practice.

1. **Reclaim UID 1000**: Linux distributions usually assign UID `1000` to the first non-root user (like `ubuntu`). To use this ID for your personal user, you should first detect and completely remove any existing UID 1000:
   ```bash
   # Identify and delete the default user associated with UID 1000
   DEFAULT_USER=$(getent passwd 1000 | cut -d: -f 1)
   userdel -r "$DEFAULT_USER"
   groupdel "$DEFAULT_USER" 2>/dev/null
   ```

2. **Create your personal user as UID 1000** (Replace `YOUR_USER` with your desired username):
   ```bash
   useradd -m -u 1000 -s /bin/bash YOUR_USER
   usermod -aG sudo YOUR_USER
   passwd YOUR_USER
   ```

3. **Install OpenSSH Server**:
   ```bash
   apt install -y openssh-server
   ```

4. **Disable Root Login**:
   Edit `/etc/ssh/sshd_config` to prevent direct root access:
   ```bash
   sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin no/' /etc/ssh/sshd_config
   sed -i 's/PermitRootLogin yes/PermitRootLogin no/' /etc/ssh/sshd_config
   systemctl restart ssh
   ```

---

### Step 3: Set Up Tailscale

Tailscale provides a secure P2P tunnel to your container, allowing you to access it from any device in your Tailnet without opening ports on your public router.

1. **Install Tailscale**:
   ```bash
   curl -fsSL https://tailscale.com/install.sh | sh
   ```

2. **Authenticate**:
   ```bash
   tailscale up
   ```

---

### Step 4: Secure the Container with UFW (Firewall)

Since Droidspaces NAT mode currently only supports IPv4, we should disable IPv6 in UFW to avoid initialization errors.

1. **Disable IPv6 in UFW**:
   ```bash
   sed -i 's/IPV6=yes/IPV6=no/' /etc/default/ufw
   ```

2. **Set Default Policies**:
   ```bash
   ufw default deny incoming
   ufw default allow outgoing
   ```

3. **Whitelist the Tailscale Interface**:
   Instead of whitelisting specific IP addresses, tell UFW to trust anything coming through your private Tailscale tunnel:
   ```bash
   ufw allow in on tailscale0
   ```

4. **Enable the Firewall**:
   ```bash
   ufw --force enable
   ```

---

### Step 5: Add Brute-Force Protection with Fail2Ban

Fail2Ban monitors your system logs and automatically blocks IP addresses that show malicious behavior.

1. **Install Fail2Ban**:
   ```bash
   apt install -y fail2ban
   ```

2. **Create a Local Configuration**:
   Create a persistent configuration file at `/etc/fail2ban/jail.local` to protect SSH and integrate it with UFW:

   ```ini
   [DEFAULT]
   # Ban for 1 hour after 5 failed attempts within 10 minutes
   bantime  = 1h
   findtime = 10m
   maxretry = 5

   # Use UFW as the banning action
   banaction = ufw

   # Whitelist to prevent accidental lockouts:
   # 1. YOUR_TAILSCALE_IP: Your private tunnel address (e.g. 100.74.132.81)
   # 2. 172.28.0.0/16: The internal Droidspaces NAT bridge (covers all containers)
   # 3. YOUR_LAN_SUBNET: Your local Wi-Fi range if using port forwarding (e.g. if your LAN IP is 192.168.1.15, use 192.168.1.0/24)
   ignoreip = YOUR_TAILSCALE_IP 172.28.0.0/16 YOUR_LAN_SUBNET

   [sshd]
   enabled = true
   port    = ssh
   backend = systemd
   ```

3. **Start and Verify**:
   ```bash
   systemctl restart fail2ban
   fail2ban-client status sshd
   ```
Your "Mobile Server" is now officially a hardened fortress! Anyone attempting to access it from the open internet will be blocked, while you maintain full access through your private Tailscale network.

---

## 2. Running Docker Containers (Nested Containerization)

Droidspaces supports running Docker natively inside your containers on all supported kernel versions. This allows you to run nested containerized services (like Portainer, Home Assistant, etc.) directly on your Android device.

### Prerequisites

- **LTS Distribution**: If your kernel version is less than **5.x.x**, it is highly recommended to use an LTS distribution like **Ubuntu 24.04 LTS** for the best compatibility.
- **Kernel Configuration**: Ensure your kernel has the required Droidspaces options enabled. See [Required Kernel Configuration](./Kernel-Configuration.md#required-kernel-configuration).
- **Storage Mode**: You **must** use either **ext4 /data** or **rootfs.img mode** (recommended).
    - *Why?* Android's default `f2fs` filesystem does not support the overlay features required by Docker's `overlay2` storage driver. Using a `rootfs.img` ensures you are running on a native ext4 filesystem.
- **NAT Mode**: **Mandatory.** Docker requires NAT networking to create its internal `docker0` bridge and provide internet access to nested containers.

---

### Step 1: Ensure NAT Networking

Running Droidspaces in host networking mode will cause Docker to fail when attempting to create the `docker0` interface. Always choose NAT mode for your container.

You can easily change to the NAT mode by editing the container configuration using the Android app.

### Step 2: Compatibility Layer (iptables-legacy)

Docker relies heavily on `iptables` for its networking stack. Modern distributions often default to the `nftables` backend, which can cause "chain not found" errors in containers. Switch to the legacy backend before installing Docker:

```bash
update-alternatives --set iptables /usr/sbin/iptables-legacy
update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
```

### Step 3: Install Docker

Use the official Docker installation script or the distribution's package manager:

```bash
# Using the official convenience script
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh
```

### Step 4: Non-Root User Setup

To run Docker commands without prefixing them with `sudo`, add your user to the `docker` group:

```bash
# Replace YOUR_USER with your username
usermod -aG docker YOUR_USER

# Apply the group change without logging out
newgrp docker
```

### Step 5: Verify Installation

Test that Docker can pull and run a nested container:

```bash
docker run --rm hello-world
```

If you see the "Hello from Docker!" message, you are successfully running nested containers on Android! 🐳

> [!TIP]
>
> **Troubleshooting Docker**: If the Docker daemon fails to start automatically or the `docker run` command fails, run `sudo dockerd` manually in your terminal. This will output real-time logs and help you identify if there are any missing kernel modules, filesystem conflicts, or network bridge issues.

### "Last Resort" for Host Mode or Legacy Kernels (Old Kernels Only)

If you absolutely must run Docker in **host networking mode**, or if your kernel is too old to support `iptables-legacy` and NAT networking, you can disable Docker's internal networking management as a last resort.

Run these commands to configure the daemon:

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
> Above `daemon.json` configuration disables Docker's internal bridge (`docker0`) and all automatic port forwarding. You will only be able to run docker containers with internet with the `--network host` flag.
>
> eg: `docker run -it --network host ubuntu`

---
