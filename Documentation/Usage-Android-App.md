# Android App Usage Guide

The Droidspaces Android app provides a premium GUI for managing Linux containers. It abstracts away the complexity of namespaces and mounts while giving you high-level control over your environments.

## Bottom Navigation

- **Home**: A dashboard displaying the number of installed and running containers, root availability status, and the backend version.
- **Containers**: A dedicated manager for all installed containers (Start, Stop, Edit, and Uninstall).
- **Panel**: A central hub for managing **Running Containers** and monitoring real-time **System Statistics** (CPU, RAM, Uptime, Temperature, etc.).

---

## Containers Tab (Management)

This tab lists all your installed environments. Each container has a control card:

- **Play Button**: Start the container and boot the init system.
- **Stop Button**: Sends a graceful shutdown signal to the container's init.
- **Cycle Button**: Fast-restart the container.
- **Terminal Icon (Logs)**: This button **does not open a shell**. It provides access to persistent session logs for the container's previous start, stop, and restart sequences.

---

## Panel Tab (Active Environments)

The **Panel** tab focuses strictly on your running containers. Tapping a running container card opens the **Details Screen**.

### Container Details Screen
This screen provides deep introspection into the running environment:
- **Distribution Info**: Shows the Pretty Name, Version, Hostname, and **IP Address (IPv4)**.
- **Available Users**: Lists detected users in the rootfs.
- **Copy Login**: Choose a user from the dropdown and tap this to copy a command like `su -c 'droidspaces enter [user]'`. 
- **Systemd Menu**: If the container uses systemd, a "Manage" button appears. Tapping it opens a list of all systemd services, allowing you to Start, Stop, or Restart individual services (e.g., SSH, Nginx, or a VNC server) directly from the app.

---

## Entering the Container's Shell

**Droidspaces does not have a built-in terminal emulator.** This ensures maximum performance and allows you to use your preferred terminal setup.

To enter a container shell:
1. Ensure the container is **RUNNING**.
2. Go to the **Panel** tab and open the container's details.
3. Choose your desired user (e.g., `root`) from the dropdown.
4. Tap **Copy Login**.
5. Open your favorite terminal emulator (like **Termux**) or use an **ADB shell**.
6. Paste and run the command.

---

## Settings & Requirements

Accessed via the gear icon in the top right:
- **Requirements**: Runs a 24-point diagnostic check on your kernel.
- **Kernel Config**: Provides a copyable `droidspaces.config` snippet specifically for your device.
- **Theme Engine**: Support for AMOLED Black, Material You, and Light/Dark modes.
