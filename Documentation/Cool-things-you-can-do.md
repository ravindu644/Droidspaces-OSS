<!--
title: Recipes
section: Recipes
order: 1
desc: Things to build with Droidspaces on a phone: a home server reachable through Tailscale and locked down with UFW and Fail2Ban, Docker inside a container, and a watchdog that keeps Wi-Fi awake.
keywords: droidspaces, recipes, android, server, tailscale, ufw, fail2ban, docker, nested, container, wifi, power, save
-->

# Recipes

Three things people build with Droidspaces on a phone. Each one assumes a container that already starts and has internet, which the [installation guide](Installation-Android.md) gets you to. The commands are for Debian and Ubuntu; other distributions have the same packages under other names.

## A phone as a home server, reachable from anywhere

A container with SSH, reachable from your laptop wherever you are through Tailscale, with nothing open to the internet. Tailscale gives every device on your account a private address and an encrypted tunnel between them, so you never forward a port on your router.

**Before you start.** The container must be in NAT mode, the default, or gateway mode. In host mode a firewall inside the container fights the phone's own networking. UFW and Fail2Ban need a few extra netfilter options in the kernel; the [kernel guide](Kernel-Configuration.md#step-2-firewall-support-optional) lists them. Run everything below as root inside the container.

**1. A user of your own.** Distributions ship with a default user on UID 1000. Replace it with yours, give it `sudo`, and set a password:

```bash
DEFAULT_USER=$(getent passwd 1000 | cut -d: -f1)
[ -n "$DEFAULT_USER" ] && userdel -r "$DEFAULT_USER"
useradd -m -u 1000 -s /bin/bash alice
usermod -aG sudo alice
passwd alice
```

**2. SSH, with root login off:**

```bash
apt update && apt install -y openssh-server
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin no/' /etc/ssh/sshd_config
systemctl restart ssh
```

**3. Tailscale.** Install it and sign in; the command prints a link to open on any device:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
tailscale up
```

From then on `ssh alice@<container name>` works from any other device on your Tailscale account, by the name shown in the Tailscale admin console.

**4. UFW.** Refuse everything that does not arrive through Tailscale:

```bash
apt install -y ufw
ufw default deny incoming
ufw default allow outgoing
ufw allow in on tailscale0
ufw --force enable
```

If the container runs with IPv6 disabled, set `IPV6=no` in `/etc/default/ufw` first, or UFW fails to start.

**5. Fail2Ban**, which bans an address after repeated failed logins. Put this in `/etc/fail2ban/jail.local`, with your Tailscale address and LAN subnet filled in so you cannot lock yourself out:

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

Turn on **Run at Boot** in the container's settings, and the server comes up with the phone.

## Docker inside a container

Docker runs inside a Droidspaces container on every supported kernel, so Portainer, Home Assistant, Pi-hole or anything else packaged for Docker runs on the phone.

The Base and desktop images in the repository, the ones named Base, XFCE or bspwm, ship with Docker installed and configured. Install one, start it, and this works straight away:

```bash
docker run --rm hello-world
```

The Minimal images and OpenWrt do not include it. The rest of this recipe is for those, and for a rootfs you prepared yourself.

**Before you start.** The container needs its own network stack, so NAT mode, the default, or gateway mode; in host mode Docker cannot create its bridge. It needs an ext4 filesystem for Docker's `overlay2` storage, which a sparse image rootfs gives you. On Debian and Ubuntu the app's installer has already switched `iptables` to the legacy backend, which the Android kernel requires; on a rootfs you prepared yourself, do it by hand:

```bash
update-alternatives --set iptables /usr/sbin/iptables-legacy
update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
```

**1. Install Docker** with its own script, then let your user run it without `sudo`:

```bash
curl -fsSL https://get.docker.com | sh
usermod -aG docker alice
```

**2. Check it:**

```bash
docker run --rm hello-world
```

**3. Optional: user namespace remapping.** With **Allow Sandboxing** on in the container's settings, Docker can run its containers under a remapped user range so that root inside a Docker container is not root inside the Droidspaces container. Put this in `/etc/docker/daemon.json` and restart Docker:

```json
{ "userns-remap": "default" }
```

This is the setup on the maintainer's phone: Ubuntu 24.04 in a sparse image, gateway mode, Docker 29 with `overlay2` and `userns-remap`, on a 5.15 kernel.

If the Docker daemon will not start, run `dockerd` by hand in a terminal. Its output names the missing kernel option or the network step that failed.

**On kernels older than 5.x** two things can still bite. Docker's device management uses BPF attach types those kernels lack, which shows up as `Invalid argument`; put `"exec-opts": ["native.cgroupdriver=cgroupfs"]` and `"storage-driver": "vfs"` in `daemon.json`. And if NAT mode is impossible and the container must run in host mode, turn Docker's own networking off as a last resort. Docker containers then need `--network host` to reach the internet:

```json
{ "iptables": false, "ip6tables": false, "bridge": "none" }
```

## Keeping Wi-Fi awake

Android puts the Wi-Fi radio into power saving when the screen is off, and a server in a container becomes sluggish or drops connections. There is no setting for it, so a small container turns it off every minute.

The container must be in host mode: the phone's `wlan0` is not visible from any other mode. Use a minimal Alpine container for nothing but this.

**1. Install `iw`** inside it: `apk add iw` on Alpine, `apt install iw` on Debian and Ubuntu.

**2. The watchdog**, at `/usr/local/bin/wifi-watchdog.sh`, made executable with `chmod +x`:

```sh
#!/bin/sh
while true; do
    if iw dev wlan0 get power_save 2>/dev/null | grep -q on; then
        iw dev wlan0 set power_save off
    fi
    sleep 60
done
```

**3. A service to keep it running.** On Alpine, `/etc/init.d/wifi-watchdog`:

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

On a systemd distribution, `/etc/systemd/system/wifi-watchdog.service`:

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

Turn on **Run at Boot** for this container too.

---

Previous: [Kernel configuration](Kernel-Configuration.md) | Next: [Troubleshooting](Troubleshooting.md)
