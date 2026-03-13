# Android App Usage Guide

The Droidspaces Android app provides a premium GUI for managing Linux containers. It abstracts away the complexity of namespaces and mounts while giving you high-level control over your environments.

## Bottom Navigation

- **Home**: A dashboard displaying the number of installed and running containers, root availability status, and the backend version.
- **Containers**: A dedicated manager for all installed containers (Install, Start, Stop, Edit, and Uninstall).
- **Panel**: A central hub for managing **Running Containers** and monitoring real-time **System Statistics** (CPU, RAM, Uptime, Temperature, etc.).

---

## Containers Tab

This tab allows you to install containers using the "+" icon, and lists all your installed environments. Each container has a control card:

- **Install Button (+)**: Install a new container.
- **Play Button**: Start the container and boot the init system.
- **Stop Button**: Sends a graceful shutdown signal to the container's init.
- **Cycle Button**: Fast-restart the container.
- **Terminal Icon (Logs)**: This button **does not open a shell**. It provides access to persistent session logs for the container's previous start, stop, and restart sequences.

> [!TIP]
>
> You can **edit container configuration** or **uninstall** existing installed containers by **pressing and holding** the container's card.

---

## Networking Configuration

When editing or creating a container, you can choose from three networking modes:

- **Host (Default)**: Shares host network directly.
- **NAT (Isolated)**: Private network namespace with deterministic IP and port forwarding support.
- **None**: No network access.

### Configuring Upstream Interfaces (NAT Mode)
If you select **NAT (Isolated)** mode, you **must** specify one or more upstream interfaces for the container to have internet access. The app provides a convenient auto-detection workflow:

1. **Detect Wi-Fi**: Connect to your Wi-Fi network and press the refresh button in the "Upstream Interfaces" menu. Select the interface (usually `wlan0`) that appears.
2. **Detect Mobile Data**: Disable Wi-Fi and connect to mobile data. Press the refresh button again and select the mobile data interface (e.g., `rmnet0`, `ccmni1`).
3. **Save**: Both interfaces will now be used by the Route Monitor to keep your container connected as you switch networks.

> [!TIP]
> You can manually enter wildcards in the Upstream Interfaces list (e.g., `rmnet*`) to ensure connectivity even if your carrier cycles through different interface names (like `rmnet_data0` and `rmnet_data1`).

> [!NOTE]
> NAT mode is IPv4 only. If your carrier only provides IPv6, see the [IPv4 NAT Workaround](Troubleshooting.md#ipv4-quirks).

### Port Forwarding
In NAT mode, use the **Port Forwarding** section to map host ports to container ports (e.g., `22:22`).

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
5. Open your favorite terminal emulator (like **Termux**) or use an **ADB shell** (Make sure to give them root access).
6. Paste and run the command.

---

## Settings & Requirements

Accessed via the gear icon in the top right:
- **Requirements**: Runs a 27-point diagnostic check on your kernel.
- **Kernel Config**: Provides a copyable `droidspaces.config` snippet specifically for your device.
- **Theme Engine**: Support for AMOLED Black, Material You, and Light/Dark modes.
