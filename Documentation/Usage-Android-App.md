<!--
title: Android App Usage
section: Guides
order: 4
desc: Complete guide to the Droidspaces Android app. Manage containers, configure networking, stats, terminal, and kernel settings.
keywords: Droidspaces Android app, container manager Android, NAT mode, systemd networkd, terminal usage, kernel settings
-->

# Android App Usage Guide

The Droidspaces Android app provides a premium GUI for managing Linux containers. It abstracts away the complexity of namespaces and mounts while giving you high-level control over your environments.

## Bottom Navigation

- **Home**: A dashboard displaying the number of installed and running containers, root availability status, and the backend version.
- **Containers**: A dedicated manager for all installed containers (Install, Start, Stop, Edit, and Uninstall).
- **Panel**: A central dashboard for managing **Running Containers** and monitoring real-time **System Statistics** (CPU, RAM, Temperature, etc.).

---

## Containers Tab

This tab allows you to install containers using the "+" icon, and lists all your installed environments. Each container has a control card:

- **Install Button (+)**: Install a new container.
- **Play Button**: Start the container and boot the init system.
- **Stop Button**: Sends a graceful shutdown signal to the container's init.
- **Cycle Button**: Fast-restart the container.
- **Terminal Icon (Logs)**: Provides access to persistent session logs for the container's previous start, stop, and restart sequences.

> [!TIP]
>
> You can **edit container configuration** or **uninstall** existing installed containers by **pressing and holding** the container’s card. Also gives you the ability to migrate to a `rootfs.img`-based container or resize your existing `rootfs.img`..!

---

## Rootfs Repository

The **cloud icon** above the "+" button opens the Rootfs Repository - a built-in distro browser that lets you download and install Linux rootfs images without leaving the app.

### How it works

1. Tap the **cloud icon** in the Containers tab.
2. The sheet opens and loads available distros from the [Droidspaces official repository](https://github.com/Droidspaces/Droidspaces-rootfs-builder). Only images matching your device’s architecture are shown.
3. **Search** the list by name, description, or author using the search bar.
4. Tap **Download** on a distro card. A progress bar tracks the download; the file is saved to your Downloads folder.
5. Once done, the button switches to **Install**. Tap it to go straight into the container setup wizard.

> [!NOTE]
>
> If a download fails, the card shows a **Retry** button. Previously downloaded files are detected automatically on relaunch - the Install button will already be available.

### Adding Custom Repositories

The repository supports third-party rootfs sources in the same JSON format.

1. Tap the **settings icon** (left of the refresh icon) in the sheet header.
2. Enter a **name** and a **URL** pointing to a valid `rootfs.json`.
3. Tap **Add**, then **Save**. The sheet refreshes and merges results from all sources.

> [!TIP]
>
> To get a much wider selection of distros, add the official LXC images mirror as a custom repository:
> - **Name**: anything you like (e.g. `LXC Mirror`)
> - **URL**: `https://raw.githubusercontent.com/Droidspaces/linuxcontainers-mirror/refs/heads/main/rootfs.json`

---

## Networking Configuration

When editing or creating a container, you can choose from three networking modes:

- **Host (Default)**: Shares host network directly.
- **NAT (Isolated)**: Private network namespace with deterministic IP and port forwarding support.
- **None**: No network access.

### Internet Uplink (NAT Mode)
If you select **NAT (Isolated)** mode, the internet uplink is detected fully automatically - there is nothing to configure. Droidspaces reads the kernel's own routing state to find the interface Android is currently using for internet (Wi-Fi, mobile data, ethernet) and a background Route Monitor keeps the container connected in real time as you switch networks.

> [!NOTE]
> NAT mode is IPv4 only. If your carrier only provides IPv6, see the [IPv4 NAT Workaround](Troubleshooting.md#ipv4-quirks).

### Port Forwarding
In NAT mode, use the **Port Forwarding** section to map host ports to container ports (e.g., `22:22`). You can also specify **port ranges** (e.g., `1000-2000:1000-2000`) for services that require multiple contiguous ports.

---

## Panel Tab (Active Environments)

The **Panel** tab focuses strictly on your running containers. Tapping a running container card opens the **Details Screen**.

### Container Details Screen
This screen provides deep introspection into the running environment:
- **Distribution Info**: Shows the Pretty Name, Version, Per-container-uptime, Hostname, and **IP Address (IPv4)**.
- **Available Users**: Lists detected users in the rootfs.
- **Copy Login**: Choose a user from the dropdown and tap this to copy a command like `su -c 'droidspaces enter [user]'`.
- **Terminal**: Open an interactive Terminal Emulator inside from the container, natively on the Droidspaces app !
- **Systemd Menu**: If the container uses systemd, a "Manage" button appears. Tapping it opens a list of all systemd services, allowing you to Start, Stop, or Restart individual services (e.g., SSH, Nginx, or a VNC server) directly from the app.

---

## Accessing the Container Shell

Droidspaces provides two primary ways to interact with your running Linux containers. Whether you want a quick check from within the app or a full-featured session in your favorite terminal, we've got you covered.

### Method 1: Built-in Terminal (v5.7.0+)

This is the most convenient way to quickly run commands without leaving the Droidspaces app.

1.  Ensure the container is **RUNNING**.
2.  Navigate to the **Panel** tab and tap the container to open its **Details**.
3.  Find the **Terminal** card and tap **Open**.
4.  Select the **User** you wish to log in as (e.g., `root` or your default user).
5.  An interactive terminal will launch directly within the app.

### Method 2: External Terminal (Copy Login)

For power users who prefer **Termux**, **ADB**, or other terminal emulators, Droidspaces allows you to "attach" external sessions to the container.

1.  Ensure the container is **RUNNING**.
2.  Open the container **Details** in the **Panel** tab.
3.  Select your desired user from the dropdown menu.
4.  Tap **Copy Login**. This copies a command like `su -c 'droidspaces --name=[name enter [user]'` to your clipboard.
5.  Open your preferred terminal (e.g., Termux) and **Paste** the command.
6.  **Run** the command (ensure your terminal has root permissions granted from your root manager).

---

## Settings & Requirements

Accessed via the gear icon in the top right:
- **Requirements**: Runs a 27-point diagnostic check on your kernel.
- **Kernel Config**: Provides a copyable `droidspaces.config` snippet specifically for your device.
- **Theme Engine**: Support for AMOLED Black, Material You, Changing accent colors, and Light/Dark modes.
