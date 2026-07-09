<!--
title: Anland (Wayland) Usage Guide
section: Guides
order: 3
desc: Configure Anland Wayland display for Droidspaces Android containers, including backend installation, APK setup, and container options.
keywords: anland, wayland, droidspaces, android, kwin, weston, kde, plasma, gpu, adreno, snapdragon
-->

# Anland (Wayland) Usage Guide

Anland is an optional Wayland display path for Droidspaces. It uses a buffer-sharing protocol where a Linux compositor, such as KWin or Weston, renders desktop content into GPU buffers, and the Anland display surface on Android presents those buffers. A lightweight daemon bridges the Linux container and Android app over a Unix domain socket.

Unlike the Termux:X11 path, Anland is designed for Wayland compositor workflows. It is useful when you want to run desktop environments such as KDE Plasma Wayland inside a Droidspaces container.

### Quick Navigation

- [Prerequisites](#requirements)
- [Step 1: Install an Anland-supported Backend](#backend)
    - [Method 1: Build and Install Inside the Container](#manual-build)
    - [Method 2: Use the Droidspaces Rootfs Builder](#rootfs-builder)
- [Step 2: Install the Anland App](#app)
- [Step 3: Configure the Droidspaces Container](#container-config)

---

<a id="requirements"></a>

## Prerequisites

Before configuring Anland, make sure the following requirements are met:

1. **An Android device that supports Droidspaces**: Anland works together with Droidspaces and requires `Anland Display` to be enabled in the container configuration.
2. **An Anland-supported display backend**: The compositor backend must be patched for Anland. `KWin` is the main supported backend. See the [Anland producers directory](https://github.com/superturtlee/anland/tree/main/producers) for available backends.
3. **A Qualcomm Snapdragon / Adreno device is recommended**: Anland depends heavily on device-side GPU and buffer-sharing behavior, and Qualcomm platforms usually provide better compatibility.

> [!NOTE]
>
> Anland only handles the display path. Container management, rootfs handling, permissions, GPU access, and startup flow are still managed by Droidspaces.

---

<a id="backend"></a>

## Step 1: Install an Anland-supported Backend

The desktop environment inside the container must use an Anland-supported backend. Otherwise, the Android side cannot receive the Wayland output.

<a id="manual-build"></a>

### Method 1: Build and Install Inside the Container

Use this method if you want to choose a specific desktop environment, target distribution, or debug the backend manually.

1. **Enter the Droidspaces container.**

2. **Clone the Anland source code.**

   ```bash
   git clone https://github.com/superturtlee/anland.git
   ```

3. **Choose the matching desktop environment and distribution under the `producers` directory.**

   For example, the KDE backend layout looks like this:

   ```txt
   producers
   └── kde
       ├── anland_backend_debian13_v5
       ├── anland_backend_v5
       ├── Debian13_v5
       ├── Fedora43_v5
       └── ubuntu2604_v5
   └── other desktops
   ```

4. **Run the matching `build.sh` script as root.**

   The build script installs the required dependencies and installs the matching Anland backend into the container.

5. **Start the desktop with the project's `startup.sh` script.**

<a id="rootfs-builder"></a>

### Method 2: Use the Droidspaces Rootfs Builder

If you mainly use KDE Plasma, the automated rootfs builder is recommended. It is simpler and better suited for repeatable deployments.

Project: [Droidspaces-rootfs-KDE-builder](https://github.com/Goldzxcbug/Droidspaces-rootfs-KDE-builder)

> [!IMPORTANT]
>
> This method currently supports KDE Plasma only.

1. **Fork the project.**
2. Open the GitHub **Actions** page and find the `编译并发布Droidspaces Rootfs` workflow.
3. Run the workflow manually and enable the `Wayland 支持` option.
4. Wait for the build to finish. This usually takes about 15 minutes.
5. Open the project's **Releases** page, download the generated rootfs package, and import it into Droidspaces.
6. If KDE desktop auto-start is not enabled, enter the container as a normal user and run:

   ```bash
   startplasma-wayland
   ```

---

<a id="app"></a>

## Step 2: Install the Anland App

1. Download the **Anland APK** from the [latest Anland release](https://github.com/superturtlee/anland/releases/latest).
2. Install the APK on your Android device.
3. Grant **root permission** to Anland on first launch.

---

<a id="container-config"></a>

## Step 3: Configure the Droidspaces Container

Edit the target container configuration in Droidspaces and set the following options.

### Required Settings

1. Enable **GPU Access**.
2. Enable **Anland Display**.
3. Disable **Configure Termux:X11**.

> [!IMPORTANT]
>
> Anland and Termux:X11 are separate display paths. When using Anland, disable the Termux:X11 configuration to avoid conflicting environment variables and display services.

### Recommended Settings

1. **Disable Configure PulseAudio**.

   Anland provides its own audio handling path, so the Droidspaces PulseAudio configuration is usually unnecessary. After disabling it, also clear any existing `PULSE_SERVER` environment variable inside the container so applications do not continue connecting to an old PulseAudio service.

2. **Keep SELinux Permissive Mode disabled by default**.

   Try running Anland under SELinux enforcing mode first. If Anland fails to start or display correctly on a specific device, enable **SELinux Permissive Mode** temporarily as a compatibility troubleshooting step.

### KDE Plasma Notes

> [!IMPORTANT]
>
> When running KDE Plasma Wayland in a Debian or Ubuntu container, make sure the kernel has User Namespace (User-NS) support enabled, and enable `noseccomp` in Droidspaces privileged mode. Otherwise, KDE Plasma may become noticeably slow or unresponsive.
