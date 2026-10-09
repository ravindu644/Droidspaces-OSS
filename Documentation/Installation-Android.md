<!--
title: Android installation
section: Basics
order: 1
desc: Install Droidspaces on a rooted Android device: install the APK, let the app set up the backend, and create a Linux container without typing a terminal command.
keywords: install, droidspaces, android, rooted, container, apk, atomic, backend, sparse, image, linux
-->

# Android installation guide

On Android, Droidspaces needs no terminal. Everything from the first install to running a full Linux distribution is done in the app.

## Prerequisites

1. **A rooted device**, using one of the [supported rooting solutions](../README.md#rooting-requirements).
2. **A compatible kernel** with Droidspaces support enabled. See the [Kernel Configuration Guide](Kernel-Configuration.md).

## Step 1: Install the app

1. Download the **Droidspaces APK** from the [latest release](https://github.com/ravindu644/Droidspaces-OSS/releases/latest).
2. Install the APK on your device.
3. **Grant root access** and open the app.

## Step 2: Automatic backend setup

On first launch, Droidspaces does an **atomic installation** of the backend:

- It detects the device architecture (`aarch64`, `armhf`, etc.).
- It extracts the `droidspaces` and `busybox` binaries to `/data/local/Droidspaces/bin`.
- It moves them into place atomically, so the install succeeds even while an older version is running.
- It verifies checksums, so a corrupted binary is not installed.

## Step 3: Setting up your first container

You don't extract rootfs files by hand. The app does it.

### Option A: Install from the Rootfs Repository (recommended)

This is the shortest path. The app can browse, download and install distros itself, with no manual download.

1. **Open the Containers tab**: tap the middle icon in the bottom navigation bar.
2. **Open the repository**: tap the **cloud icon** (above the "+" button). This opens the Rootfs Repository sheet, which fetches every distro built for Android from our [official repository](https://github.com/Droidspaces/Droidspaces-rootfs-builder).
3. **Pick a distro**: browse or search the list. Each card shows the distro name, size, architecture and build date.
4. **Download**: tap **Download** on the distro you want. A progress bar appears on the card. The file is saved to your Downloads folder.
5. **Install**: when the download finishes, the button changes to **Install**. Tap it to start the container setup wizard.
6. **Configuration wizard**:
   - **Name**: a name for the container.
   - **Features**: toggle Hardware Access, IPv6, Network Isolation, Android storage integration and the rest as you need them.
   - **Container type**: we recommend **Sparse Image**. It performs better and is more stable on Android's f2fs storage, and it avoids odd SELinux and keyring issues.
7. **Done**: the app extracts the tarball and applies the **post-extraction fixes** (DNS, masking useless or dangerous services, and Safe Udev).

> [!TIP]
>
> The official repository contains distros preconfigured for Android. For a wider selection, add the LXC images mirror as a custom repository. See the [Rootfs Repository](Usage-Android-App.md#rootfs-repository) section of the usage guide.

### Option B: Install from a local tarball

If you already have a `.tar.xz` or `.tar.gz` rootfs file on the device:

1. **Open the Containers tab** and tap the **"+"** button.
2. **Select your tarball** from storage.
3. Follow the same **configuration wizard** steps as above.

An archive can include a regular `container.config` file at its root, alongside `bin/`,
`etc/` and `usr/`. The app reads its usual `key=value` settings and prefills the wizard.
The file may contain the complete config from an installed container or only the settings
you want to recommend. Missing settings use the wizard defaults. You can
change the loaded values before installing; hardware access and privileged mode keep
their confirmation dialogs. Recommendations use the existing config parser without
additional field filtering. Rootfs validation still runs before extraction.

Exporting a container includes its current host-side `container.config` as the first
archive member, replacing any old copy in the rootfs. This lets the app read exported
recommendations without decompressing the rest of the archive. If the first member is
not `container.config` or `./container.config`, the app skips recommendations and opens
the default configuration wizard. It does not search later entries.
The new container name and storage location come from the wizard. Environment files
are not imported. Bind mounts use the existing
`bind_mounts=source:destination[:ro],...` format and can be reviewed in the wizard.
A requested user-namespace setting stays checked but disabled when the kernel does
not support it. The rootfs itself is not modified during export.

> [!NOTE]
>
> Both methods end in the same wizard. The only difference is where the tarball comes from.

## Verification & settings

You can check the system status at any time:

1. Go to **Settings** (gear icon) -> **Requirements**.
2. Tap **Check Requirements**. This runs the full `droidspaces check` suite internally.
3. **Kernel config**: for kernel developers, the same screen has a copyable `droidspaces.config` defconfig fragment, like the one on [this page](./Kernel-Configuration.md#step-1-mandatory-configuration), to check that your kernel is compatible with Droidspaces.

## Next steps

- [Android App Usage Guide](Usage-Android-App.md) for managing containers.
- [Display, Audio & Desktop Guide](Graphics-and-Audio.md) to enable GPU acceleration, sound, and desktop environment auto-boot.
- [Linux CLI Guide](Linux-CLI.md) for command-line access.
