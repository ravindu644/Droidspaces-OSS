<!--
title: Android Installation
section: Basics
order: 1
desc: Step-by-step Android installation guide for Droidspaces. Root your device, install the APK, set up the backend, and run Linux containers with zero terminal commands.
keywords: install, droidspaces, android, rooted, container, apk, atomic, backend, sparse, image, linux
-->

# Android Installation Guide

Droidspaces on Android is designed to be a "Zero Terminal" experience. From the first install to running a full Linux distribution, everything is handled through the intuitive Android app.

## Prerequisites

1. **Rooted device** with supported rooting solutions from [here](../README.md#rooting-requirements)
2. **Compatible kernel** with Droidspaces support enabled (see [Kernel Configuration Guide](Kernel-Configuration.md))

## Step 1: Install the App

1. Download the **Droidspaces APK** from the [latest release](https://github.com/ravindu644/Droidspaces-OSS/releases/latest).
2. Install the APK on your device.
3. **Grant root access** and open the app.

## Step 2: Automatic Backend Setup

On the first launch, Droidspaces performs an **Atomic Installation** of the backend system:
- It detects your device architecture (`aarch64`, `armhf`, etc.).
- It extracts the `droidspaces` and `busybox` binaries to `/data/local/Droidspaces/bin`.
- It performs an atomic move to ensure the binaries are installed correctly even if an older version is currently running.
- It verifies checksums to ensure zero corruption.

## Step 3: Setting Up Your First Container

You don't need to manually extract rootfs files. The app handles it all.

### Option A: Install from the Rootfs Repository (Recommended)

The easiest way to get started. The app can browse, download, and install distros directly - no manual downloading required.

1. **Open the Containers Tab**: Tap the middle icon in the bottom navigation bar.
2. **Open the Repository**: Tap the **cloud icon** (above the "+" button). This opens the Rootfs Repository sheet, which automatically fetches all available distros optimized for Android from our [official repository](https://github.com/Droidspaces/Droidspaces-rootfs-builder).
3. **Pick a distro**: Browse or search the list. Each card shows the distro name, size, architecture, and build date.
4. **Download**: Tap **Download** on the distro you want. A progress bar appears on the card. The file saves to your Downloads folder.
5. **Install**: Once complete, the button changes to **Install**. Tap it to launch the container setup wizard.
6. **Configuration Wizard**:
   - **Name**: Give your container a friendly name.
   - **Features**: Toggle Hardware Access, IPv6, Network Isolation, Android storage integration, etc., according to your needs.
   - **Container Type**: We recommend **Sparse Image** for better performance and stability on Android’s f2fs storage, as well as to prevent weird SELinux/Keyring issues.
7. **Done**: The app extracts the tarball and automatically applies **Post-Extraction Fixes** (DNS, masking useless/dangerous services, and Safe Udev).

> [!TIP]
>
> The official repository includes distros pre-configured for Android. For a wider selection, you can add the LXC images mirror as a custom repository - see the [Rootfs Repository](Usage-Android-App.md#rootfs-repository) section of the Usage Guide.

### Option B: Install from a Local Tarball

If you already have a `.tar.xz` or `.tar.gz` rootfs file on your device:

1. **Open the Containers Tab** and tap the **"+"** button.
2. **Select your tarball** from storage.
3. Follow the same **Configuration Wizard** steps above.

> [!NOTE]
>
> Both methods lead to the same wizard - the only difference is where the tarball comes from.

## Verification & Settings

You can verify your system status at any time:
1. Go to **Settings** (gear icon) -> **Requirements**.
2. Tap **Check Requirements**. This runs the full `droidspaces check` suite internally.
3. **Kernel Config**: If you're a kernel developer, you can find a copyable `droidspaces.config` defconfig fragment, similar to [this page](./Kernel-Configuration.md#required-kernel-configuration), to make sure your kernel is perfectly compatible with Droidspaces.

## Next Steps

- [Android App Usage Guide](Usage-Android-App.md) for management details.
- [GPU Acceleration Guide](GPU-Acceleration.md) to enable hardware-accelerated desktop environments.
- [Linux CLI Guide](Linux-CLI.md) for expert command-line access.
