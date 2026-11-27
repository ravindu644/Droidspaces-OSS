<!--
title: Anland (Wayland)
section: Guides
order: 5
desc: Set up Anland Wayland display in a Droidspaces Android container, from installing a patched compositor to configuring the container.
keywords: anland, wayland, droidspaces, android, kwin, weston, kde, plasma, gpu, adreno, snapdragon
-->

# Anland (Wayland)

Anland is an optional Wayland display path for Droidspaces. A Linux compositor patched for Anland, such as KWin or Weston, renders the desktop into GPU buffers. The Anland app displays those buffers on Android, while a small Droidspaces broker connects the container and the app over a Unix domain socket.

Unlike Termux:X11, Anland is designed for Wayland compositors such as KDE Plasma Wayland. Anland handles the display path; Droidspaces still manages the container, rootfs, permissions, GPU access and startup.

Qualcomm Snapdragon devices with Adreno GPUs are recommended. Anland relies on device-side GPU buffer sharing, and Qualcomm devices tend to be more compatible.

## More Anland guides

- [Anland user guide](https://github.com/SuperTurtleDev/anland/blob/legacy/doc/UserManual/anland_guide.md): Install Anland and connect to an Anland-capable Wayland desktop through chroot or Droidspaces.
- [Anland settings guide](https://github.com/SuperTurtleDev/anland/blob/legacy/doc/UserManual/anland_settings_guide.md): Configure input, display, connection, audio and window behavior in the Android app.

## Quick navigation

- [Requirements](#requirements)
- [Step 1: Install an Anland backend](#backend)
  - [Build it in the container](#manual-build)
  - [Build a rootfs automatically](#rootfs-builder)
- [Step 2: Install the Anland app](#app)
- [Step 3: Configure the container](#container-config)

<a id="requirements"></a>

## Requirements

Before setting up Anland, make sure you have:

1. **An Android device supported by Droidspaces.** Anland works with Droidspaces when **Anland Display** is enabled in the container configuration.
2. **A compositor backend patched for Anland.** KWin is the main supported backend. See the [Anland producers directory](https://github.com/SuperTurtleDev/anland/tree/legacy/producers) for available builds.
3. **A compatible GPU.** Qualcomm Snapdragon devices with Adreno GPUs are recommended for better compatibility.

<a id="backend"></a>

## Step 1: Install an Anland backend

The desktop in the container must use a compositor backend patched for Anland. An unpatched Wayland compositor cannot send its display to the Anland app.

<a id="manual-build"></a>

### Build it in the container

Build the backend yourself if you want to choose a distribution or desktop environment, or need to debug the setup.

1. Open a shell in the Droidspaces container.
2. Clone the Anland source:

   ```bash
   git clone -b legacy https://github.com/SuperTurtleDev/anland.git
   ```

3. Under `producers`, choose the directory for your desktop and distribution. KDE backend directories include:

   ```text
   producers/kde/
   ├── anland_backend/
   ├── Debian13_v5/
   ├── Fedora43_v5/
   └── ubuntu2604_v5/
   ```

4. As root, run `build.sh` in the matching directory. The script installs the required dependencies and Anland backend.
5. Start the desktop with the project's `startup.sh` script.

<a id="rootfs-builder"></a>

### Build a rootfs automatically

For a ready-to-import rootfs, use [Droidspaces-rootfs-Desktop-builder](https://github.com/Goldzxcbug/Droidspaces-rootfs-Desktop-builder).

> [!IMPORTANT]
> The builder currently supports KDE Plasma, GNOME and niri desktops.

1. Fork the project.
2. Open its GitHub **Actions** page and find the `编译并发布Droidspaces Rootfs` workflow.
3. Run the workflow manually and select `anland-wayland` for the display backend.
4. Wait for the build to finish. It usually takes about 15 minutes.
5. Open the project's **Releases** page, download the generated rootfs package and import it into Droidspaces.
6. If the KDE desktop is not configured to start automatically, enter the container as a regular user and run the command for your desktop:

   ```bash
   startplasma-wayland       # KDE Plasma
   startplasmamobile         # KDE Plasma Mobile
   gnome-session --session=gnome
   niri-anland
   ```

<a id="app"></a>

## Step 2: Install the Anland app

1. Download `anland-v5.apk` from the newest 5.x release on the [Anland releases page](https://github.com/superturtlee/anland/releases). Droidspaces supports only Anland 5.x. The 6.x releases, marked "Latest" there, are a different app and do not work with it.
2. Install the APK on your Android device.
3. Grant it root access. With KernelSU or APatch, allow it in the manager app. With Magisk, open Anland once and accept the root prompt.

<a id="container-config"></a>

## Step 3: Configure the container

Edit the target container's configuration in Droidspaces.

### Required settings

1. Turn on **GPU Access**. **Hardware Access** also satisfies this requirement, but GPU Access is sufficient.
2. Turn on **Anland Display**.
3. Turn off **Configure Termux:X11**.

Anland and Termux:X11 use separate display paths with conflicting environment variables and services. Enabling **Anland Display** turns off **Configure Termux:X11** for that container.
On the CLI, enable Anland with `--anland` or the `enable_anland` config key.

> [!IMPORTANT]
> Use one display path per container. Keep the Termux:X11 configuration off while using Anland.

### Recommended settings

- **Configure PulseAudio:** Leave this off because Anland has its own audio path. If PulseAudio was previously enabled, remove any existing `PULSE_SERVER` environment variable from the container so applications do not connect to the old server.
- **SELinux Permissive:** Leave this off and try Anland with SELinux enforcing first. If Anland cannot start or display anything on your device, you can temporarily enable this option to check for a compatibility issue.
- **Allow Sandboxing:** Enable this for KDE Plasma and similar desktop environments. It allows user namespaces, which can improve Plasma Wayland responsiveness. This weakens container isolation, so enable it only for containers you trust. Anland does not require privileged mode, `noseccomp`, `nomask` or other privileged options.

Start the container, then tap its card on the **Panel** tab to open the container details. The **Anland** card below **Terminal** has a **Launch** button that opens the desktop in the Anland app. The button becomes active when the container starts and Droidspaces records the broker socket. The desktop appears after the compositor inside the container starts.

---

Previous: [Display, audio and desktop](Graphics-and-Audio.md) | Next: [Kernel configuration](Kernel-Configuration.md)
