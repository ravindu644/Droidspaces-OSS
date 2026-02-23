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

You don't need to manually extract rootfs files. The app handles it:

1. **Download a rootfs tarball**: We recommend the official [Linux Containers images](https://images.linuxcontainers.org/images/).
2. **Open the Containers Tab**: Tap the middle icon in the bottom navigation bar.
3. **Add a Container**: Tap the **"+"** button at the bottom right.
4. **Choose your Tarball**: Select the downloaded `.tar.xz` or `.tar.gz` file.
5. **Configuration Wizard**:
   - **Name**: Give your container a friendly name.
   - **Features**: Toggle Hardware Access, IPv6, or Android Storage integration.
   - **Container Type**: We recommend **Sparse Image** for better performance and stability on Android’s f2fs storage.
6. **Installation**: The app will extract the tarball and apply **Post-Extraction Fixes** automatically (DNS, Network Metrics, and Safe Udev).

## Verification & Settings

You can verify your system status at any time:
1. Go to **Settings** (gear icon) -> **Requirements**.
2. Tap **Check Requirements**. This runs the full `droidspaces check` suite internally.
3. **Kernel Config**: If you are a kernel developer, you can find a copyable `defconfig` block on this page to ensure your kernel is perfectly compatible with Droidspaces.

## Next Steps

- [Android App Usage Guide](Usage-Android-App.md) for management details.
- [CLI Reference](CLI-Reference.md) for expert command-line access.
