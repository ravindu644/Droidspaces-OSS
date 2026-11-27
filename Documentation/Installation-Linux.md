<!--
title: Linux installation
section: Basics
order: 2
desc: Install Droidspaces on a Linux desktop or server: install the binary, get a rootfs, put it in an ext4 image, and boot the first container.
keywords: install, droidspaces, linux, container, runtime, rootfs, tarball, deb, ext4, image, namespaces
-->

# Linux installation

Droidspaces is one static binary on Linux. Any current distribution has everything it needs, and the only requirement is root.

## Step 1: Install the binary

Every release ships `.deb` packages for amd64, arm64, armhf, i386 and riscv64, and a tarball with the binary for each architecture.

On Debian, Ubuntu and their derivatives:

```bash
sudo dpkg -i droidspaces_*_amd64.deb
```

On anything else, extract the tarball and copy the binary for your CPU somewhere on your `PATH`. `uname -m` tells you which one: `x86_64`, `aarch64`, `armv7l` (use `armhf`), `i686` (use `x86`) or `riscv64`.

```bash
tar -xzf droidspaces-v*.tar.gz
sudo install -m 755 droidspaces-v*/x86_64/droidspaces /usr/local/bin/droidspaces
```

## Step 2: Check the system

```bash
sudo droidspaces check
```

Every line should pass. The optional ones at the bottom cover features such as bridges, macvlan, IPv6 NAT and resource limits, and a missing one only disables that feature.

## Step 3: Get a rootfs

A container needs a root filesystem of the distribution you want to run. The [Linux Containers image server](https://images.linuxcontainers.org/images/) has clean builds of most distributions. Browse to one, for example `ubuntu/noble/amd64/default/`, and download `rootfs.tar.xz`.

> [!IMPORTANT]
>
> Extract with `sudo`. Without it the files inside the container are not owned by root, setuid bits are lost, and `sudo` and several services inside the container stop working.

You can keep the rootfs in a directory or in an ext4 image file. The image is one file you can copy to another machine or phone, and it keeps the container's filesystem separate from the host's. Use whichever you prefer.

**A directory:**

```bash
mkdir ubuntu
sudo tar -xf rootfs.tar.xz -C ubuntu
```

**An ext4 image** (the file is sparse, so a 16 GB image takes only the space the container uses):

```bash
truncate -s 16G ubuntu.img
mkfs.ext4 -L ubuntu ubuntu.img
mkdir mnt
sudo mount ubuntu.img mnt
sudo tar -xf rootfs.tar.xz -C mnt
sudo umount mnt
rmdir mnt
```

## Step 4: Start, enter, stop

```bash
sudo droidspaces --name=ubuntu --rootfs=/path/to/ubuntu start
# or, for an image:
sudo droidspaces --name=ubuntu --rootfs-img=/path/to/ubuntu.img start

sudo droidspaces --name=ubuntu enter
sudo droidspaces --name=ubuntu stop
```

The flags you pass to the first `start` are saved in the container's config file, so later starts only need `--name`. `droidspaces show` lists what is running.

## Optional: run the daemon

The CLI works without a daemon on Linux. Running one lets members of a `droidspaces` group use the CLI without `sudo`, and restarts cleanly with the system. The repository has a systemd unit:

```bash
sudo groupadd droidspaces
sudo usermod -aG droidspaces "$USER"
sudo cp init/systemd-service/droidspaces.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now droidspaces
```

The unit expects the binary at `/usr/bin/droidspaces`, which is where the `.deb` puts it. Edit `ExecStart` if yours is elsewhere. Log out and in again for the group to apply.

---

Previous: [Android installation](Installation-Android.md) | Next: [Android app guide](Usage-Android-App.md)
