# Uninstallation Guide

This guide explains how to safely and completely remove Droidspaces from your system.

---

## Android (App)

Follow these steps to ensure all container data and backend files are removed.

1.  **Stop Containers**: Ensure all running containers are stopped from the **Containers** tab.
2.  **Delete Containers**: 
    - Go to the **Containers** tab.
    - Long-press each container card and select **Uninstall Container**. This deletes the rootfs image and its configuration.
3.  **Remove Backend Data**: 
    - Using a root file manager or a terminal (like Termux with root access), delete the following directory:
    ```bash
    su -c "rm -rf /data/local/Droidspaces"
    ```
4.  **Uninstall APK**: Uninstall the Droidspaces app from your Android settings or launcher.
5. **Reboot**: Disable or remove the Magisk/KernelSU `Droidspaces: Run-at-boot` module from your root manager, then reboot to clear any remaining Droidspaces stuff.

---

## Linux (CLI)

1.  **Stop Containers**: Stop all running containers.
    ```bash
    sudo droidspaces --name=web,db stop
    ```
2.  **Remove Filesystem**: Delete the Droidspaces workspace directory. This will remove stale PID files, and logs.

    ```bash
    sudo rm -rf /var/lib/Droidspaces
    ```
3.  **Remove Binary**: Delete the `droidspaces` binary.
    ```bash
    sudo rm /usr/local/bin/droidspaces
    # Or wherever you installed it
    ```
