<!--
title: Android installation
section: Basics
order: 1
desc: Install Droidspaces on a rooted Android device: install the APK, let the app set up the backend, and create a Linux container without typing a terminal command.
keywords: install, droidspaces, android, rooted, container, apk, backend, sparse, image, linux, kernelsu, magisk, apatch
-->

# Android installation

Everything on Android happens in the app, from installing the backend to opening a shell in a running container. This page takes you from a rooted phone to that shell.

## Before you start

You need two things.

**Root.** KernelSU is the recommended root method. Magisk and APatch work as well. Their root shells carry a seccomp filter that blocks what a container runtime needs, which is why the backend runs as a daemon started at boot rather than from the shell. That is the default, and the reboot the app asks for after installing is all it takes.

**A kernel with container support.** Most stock kernels are missing options that Droidspaces needs, especially PID and IPC namespaces. The [kernel configuration guide](Kernel-Configuration.md) lists them and explains how to build a kernel with them. If you would rather not build one, the [community-supported devices](community-supported-devices.md) page links to kernels other users have built. If you cannot flash a kernel at all, read [this section](Kernel-Configuration.md#if-you-cannot-flash-a-kernel) first. The app tells you on first launch whether your kernel is supported.

> [!CAUTION]
>
> GrapheneOS is not supported. It blocks system calls that container isolation depends on, root or not.
>
> SuSFS is not supported. If you use it anyway, turn off "Hide sus mounts for all processes" in its settings or containers will not boot, and do not report bugs seen with it enabled.

## Step 1: Install the app

1. Download the APK from the [latest release](https://github.com/ravindu644/Droidspaces-OSS/releases/latest) and install it.
2. On KernelSU and APatch, allow root for the Droidspaces app in your root manager first. Magisk asks when the app requests it.
3. Open the app and tap **Get Started**.
4. Tap **Check Root Access** and, on Magisk, allow the request.
5. The app installs the backend on its own: the `droidspaces` binary and a static BusyBox go to `/data/local/Droidspaces/bin`, and a small boot module goes to your root manager so the daemon and your containers can start at boot. This takes a few seconds.
6. Read the result. "Droidspaces backend installed successfully" means your kernel passed the probe. If it says the kernel has no Droidspaces support, go to the [kernel configuration guide](Kernel-Configuration.md) before continuing.
7. Reboot when the app asks. The reboot applies the SELinux rules the backend needs and starts the daemon.

After the reboot, on a supported kernel, the **Home** tab shows "Backend: Installed" with a "DAEMON" chip, and your kernel version, SELinux status and architecture underneath.

## Step 2: Install a container

Open the **Containers** tab and tap **+**. You get two choices.

### From the repository

**Repository** opens a list of distributions built for Droidspaces, filtered to your device's architecture. Ubuntu, Debian, Fedora, Arch, Alpine, OpenWrt and others are there, and some come with a desktop environment already wired up.

1. Pick a distribution and tap **Download**. The file goes to your Downloads folder, and a notification tells you when it is done.
2. Tap **Install** on the same card.

For more distributions, tap **Manage Repositories** in the same sheet and subscribe to the Linux Containers mirror:

```
https://raw.githubusercontent.com/Droidspaces/linuxcontainers-mirror/refs/heads/main/rootfs.json
```

Those images are stock upstream builds. The installer applies the same Android fixes to them, but they come without the extras of the official builds, such as a preconfigured desktop.

### From a file

**Install from file** takes a rootfs archive you already have on the phone. It must be a `.tar.xz` or `.tar.gz`. A container exported from another phone with **Export container as tarball** works here too, and its settings are filled in for you.

### The setup wizard

Both paths end in the same wizard.

1. **Container Setup**: give the container a name. The hostname defaults to the name.
2. **Container configuration**: the defaults are right for a first container. NAT networking gives it internet with no setup. Every toggle on this page is explained in [container settings](Features.md), and you can change any of them later from the container's menu.
3. **Storage**: leave **Use Sparse Image** on. The rootfs then lives in a single ext4 image file, which is more reliable on Android's storage than a directory. The size you pick is the maximum; the file only grows as the container uses space. **Choose a folder** puts the image on an SD card or USB drive instead of internal storage.
4. **Installation Summary**: check it and tap install.

The app validates the archive, extracts it and applies a set of fixes that every container needs on Android, such as the network groups Android requires for socket access and a journald configuration that does not fill the phone with kernel logs. Installing takes one to a few minutes depending on the distribution.

## Step 3: Start it and get a shell

1. On the **Containers** tab, tap **Start** on the new container. A log window opens and shows the boot as it happens. Close it once the boot is done, and the card shows "RUNNING" and the container's PID.
2. Open the **Panel** tab. Running containers appear here with their uptime, IP address, CPU and RAM.
3. Tap **Terminal** on the card, pick a user, and you are in.

The terminal keeps running in the background when you leave the screen, so you can come back to the same session later.

If you prefer Termux or another terminal app, tap the container's card on the **Panel** tab to open its details, pick a user under **Available Users** and tap **Copy login**. Paste the command into a root shell.

## Checking requirements later

**Settings** has a **Requirements** page. **Check Requirements** runs every probe the backend has and tells you which kernel options are missing, which is the first thing to look at when a feature is greyed out in a container's settings.

---

Next: [Linux installation](Installation-Linux.md)

On Android you can skip ahead. Read these next: [Android app guide](Usage-Android-App.md), [Container settings](Features.md) and [Display, audio and desktop](Graphics-and-Audio.md).
