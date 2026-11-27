<!--
title: Uninstallation
section: Reference
order: 4
desc: Remove Droidspaces from Android or Linux: containers, the backend and its data, the boot module, and the app.
keywords: uninstall, droidspaces, remove, android, delete, rootfs, cleanup, container, runtime, backend, data
-->

# Uninstallation

## Android

Uninstalling the app removes only the app. The backend, the boot module and every container stay under `/data/local/Droidspaces`, and containers keep running. That is by design, so an app update or reinstall never touches your containers. To remove everything:

1. In the **Containers** tab, stop each container, then press and hold its card and choose **Uninstall Container**. This deletes the container's rootfs and settings. Repeat for every container.
2. In a root shell, Termux with `su` or any other, delete the backend and its data:

   ```sh
   rm -rf /data/local/Droidspaces
   ```

3. Remove the "Droidspaces: Daemon & Init" module in your root manager, KernelSU, Magisk or APatch. It is the boot module that starts the daemon and containers at boot.
4. Uninstall the app.
5. Reboot.

If you added Droidspaces to your vendor image for native init integration, undo that as well, following [init/README.md](../init/README.md).

Rootfs tarballs the app downloaded are still in your Downloads folder.

## Linux

```bash
sudo droidspaces stop --name=a,b,c       # every running container
sudo systemctl disable --now droidspaces  # if you installed the daemon unit
sudo rm -f /etc/systemd/system/droidspaces.service
sudo systemctl daemon-reload
sudo rm -rf /var/lib/Droidspaces
sudo apt remove droidspaces               # if installed from the .deb
sudo rm -f /usr/local/bin/droidspaces     # if installed from the tarball
```

Rootfs directories and images you created yourself are wherever you put them.

---

Previous: [NixOS](Nix-NixOS.md) | Back to the start: [Android installation](Installation-Android.md)
