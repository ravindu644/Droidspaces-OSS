# Troubleshooting

Common issues, their causes, and how to fix them.

### Quick Navigation
- [Modern Distros (Arch, Fedora, etc.) Failure on Legacy Kernels](#modern-distros-arch-fedora-etc-failure-on-legacy-kernels)
- ["Required key not available" (ENOKEY)](#required-key-not-available)
- [Mount Errors on Kernel 4.14](#mount-errors-on-kernel-414)
- [OverlayFS Not Supported (f2fs)](#overlayfs-not-supported-f2fs)
- [Container Name Conflicts](#container-name-conflicts)
- [Systemd Hangs on Older Kernels](#systemd-hangs-on-older-kernels)
- [Container Won't Stop](#container-wont-stop)
- [Rootfs Image I/O Errors on Android](#rootfs-image-io-errors-on-android)
- [Networking is completely dead - ping fails with "socket: permission denied"](#paranoid-networking)
- [DNS / Name Resolution Issues](#dns--name-resolution-issues)
- [WiFi/Mobile Data Disconnects](#wifimobile-data-disconnects)
- [SELinux-Induced Rootfs Corruption](#selinux-induced-rootfs-corruption-directory-mode)
- [Systemd Service Sandboxing Conflicts](#systemd-service-sandboxing-conflicts-legacy-kernels)
- [Reclaiming Storage (Sparse Image)](#reclaim-storage)
- [WIFI `Power save: on` make the networking experience sluggish in Android](#nuke-wifi-powersave)
- [Getting Help](#getting-help)

---

<a id="modern-distros"></a>
## Modern Distros (Arch, Fedora, etc.) Failure on Legacy Kernels

This is not a bug in Droidspaces; it is a limitation of the distribution's `systemd` version. Modern distributions like Arch Linux, Fedora, or openSUSE use recent versions of `systemd` (v258 and newer) that require kernel features missing in older versions. On legacy kernels (3.18, 4.4, 4.9, 4.14, 4.19), these distros will either fail to boot with an "Unsupported Kernel" message, crash during initialization, or appear to hang when executing `systemctl` commands.

Systemd's development philosophy increasingly targets modern Linux environments. Starting with v258 (released in September 2025), the codebase was purged of many legacy workarounds and backward-compatibility layers intended for pre-5.4 kernels.

Specifically, developers removed old capability checks, deprecated fallback mechanisms, and deleted obsolete D-Bus methods.

By removing these fallbacks, modern `systemd` assumes modern kernel APIs are present. When they are not (as in legacy kernels), it hard-fails rather than degrading gracefully.

**Cause:** The host kernel is too old to support modern syscalls and features required by newer `systemd` versions.

Legacy kernels lack modern system calls (e.g., `clone3`, `openat2`, or newer `bpf` hooks) that `systemd` now utilizes by default. When `systemd` invokes a syscall that a 4.14 or 4.19 kernel doesn't recognize, the kernel rejects it, leading to a failure.

**Solution:**
- Use any container that utilizes **OpenRC**, **runit**, or **s6** as its init system.
- Use distributions with `systemd` versions older than v258, such as **Ubuntu 22.04**, **Ubuntu 24.04**, **Ubuntu 25.04**, or **Ubuntu 25.10 (which uses v257.9 as of March 2026)**.
- Use **Debian 12 (Bookworm)** or **Debian 13 (Trixie)**.


---

## "Required key not available"

**Symptoms:** The container crashes or filesystem operations fail with "Required key not available" errors. Most commonly seen on Android devices with File-Based Encryption (FBE).

**Cause:** systemd services inside the container attempt to create new session keyrings, which causes the process to lose access to Android's FBE encryption keys.

**Affected kernels:** 3.18, 4.4, 4.9, 4.14, 4.19 (legacy Android kernels)

**Solution:** This is handled automatically by Droidspaces' Adaptive Seccomp Shield on kernels below 5.0. The shield intercepts keyring-related syscalls and returns `ENOSYS`, causing systemd to fall back to the existing session keyring.

If you're still seeing this error:
- Verify your Droidspaces binary is up to date (v4.2.4+)
- Run `droidspaces check` to verify seccomp support
- Ensure `CONFIG_SECCOMP=y` and `CONFIG_SECCOMP_FILTER=y` are in your kernel config
- Move to **rootfs.img mode** (recommended on Android to isolate filesystem keys)
- **Advanced**: Decrypt the `/data` partition by surgically editing the `fstab` file in `boot`/`vendor`/`vendor_boot` partitions (requires advanced Android modding knowledge)

---

## Mount Errors on Kernel 4.14

**Symptoms:** The first container start attempt after stopping fails with a mount error, but the second attempt succeeds.

**Cause:** On kernel 4.14, loop device cleanup is asynchronous. After unmounting a rootfs image, the loop device may not be fully released when the next mount attempt occurs.

**Solution:** Droidspaces v4.2.3+ includes a 3-attempt retry loop with `sync()` calls and 1-second settle delays between attempts. This handles the race condition automatically.

If you're still experiencing issues:
- Update to the latest Droidspaces version
- Wait a few seconds between stopping and starting a container
- Use `sync` before restarting: `sync && droidspaces --name=mycontainer restart`

---

## OverlayFS Not Supported (f2fs)

**Symptoms:** Starting a container with `--volatile` fails with an error about OverlayFS not being supported or f2fs incompatibility.

**Cause:** Most Android devices use f2fs for the `/data` partition. OverlayFS on many Android kernels (4.14, 5.15) does not support f2fs as a lower directory.

**Solution:** Use a rootfs image instead of a directory:

```bash
# This will fail on f2fs:
droidspaces --rootfs=/data/rootfs --volatile start

# This will work (ext4 image provides a compatible lower directory):
droidspaces --name=test --rootfs-img=/data/rootfs.img --volatile start
```

---

## Container Name Conflicts

**Symptoms:** Starting a container fails because a container with the same name is already running, or PID file conflicts occur.

**Solution:**

1. Check what's currently running:
   ```bash
   droidspaces show
   ```

2. If the container is listed but you believe it's actually stopped, clean up stale state:
   ```bash
   droidspaces scan
   ```

3. Use a different name:
   ```bash
   droidspaces --name=mycontainer-2 --rootfs=/path/to/rootfs start
   ```

---

## Systemd Hangs on Older Kernels

**Symptoms:** The entire systemd hangs or becomes unresponsive when starting a container on legacy kernels (3.18, 4.4, 4.9, 4.14, 4.19).

**Cause:** systemd's service sandboxing (`PrivateTmp=yes`, `ProtectSystem=yes`) triggers a race condition in the kernel's VFS `grab_super` path on legacy kernels.

**Solution:** Try enabling the "Deadlock Shield" on App/`--block-nested-namespaces` in CLI, hard reboot your device, and try again.

---

## Container Won't Stop

**Symptoms:** `droidspaces stop` takes more than 15 seconds to stop the container and eventually fail.

**Cause:** Exact same cuase of [Systemd Hangs on Older Kernels](#systemd-hangs-on-older-kernels).

**Solution:** Try enabling the "Deadlock Shield" on App/`--block-nested-namespaces` in CLI, hard reboot your device, and try again.

---

## Rootfs Image I/O Errors on Android

**Symptoms:** Loop-mounting a rootfs image silently fails.

**Cause:** On certain Android devices, the SELinux context of the `.img` file prevents the loop driver from performing I/O.

**Solution:** Droidspaces v4.3.0+ automatically applies the `vold_data_file` SELinux context to image files before mounting. If you're on an older version, update to the latest release.

You can also manually apply the context:
```bash
chcon u:object_r:vold_data_file:s0 /path/to/rootfs.img
```

---

<a id="paranoid-networking"></a>

## Networking is completely dead - ping fails with "socket: permission denied"

**Symptoms:** You have no internet access inside the container. Even basic commands like `ping` fail immediately with a `socket: permission denied` error, even when running as root.

**Cause:** This is caused by **Android Paranoid Networking**, a security feature found in many Android kernels (especially version 4.14 and older). Unlike standard Linux, the Android kernel restricts network socket creation to specific supplementary Group IDs (GIDs). Without these specific IDs, the kernel's security hooks block the process:

* **AID_INET (3003):** Required to create any AF_INET/AF_INET6 socket. Without this, `connect()` and `bind()` calls fail.
* **AID_NET_RAW (3004):** Required for `ping` and other raw networking tasks.
* **AID_NET_ADMIN (3005):** Required for network configuration and routing tasks.

**Solutions:**

- **Kernel Level Fix:** If you are building your own kernel, the most effective solution is to disable this restriction entirely in your kernel configuration:
  ```bash
  CONFIG_ANDROID_PARANOID_NETWORK=n
  ```

- **Userland Fix:** Use a rootfs that has been specifically patched to include these Android-specific GIDs in the group database. Our official rootfs tarballs come pre-configured to handle these permission requirements:

   [Droidspaces-rootfs-builder Releases](https://github.com/ravindu644/Droidspaces-rootfs-builder/releases/latest)

---

## DNS / Name Resolution Issues

**Symptoms:** Internet works (you can ping IPs), but domain names fail to resolve, even though `/etc/resolv.conf` has the correct DNS nameservers. This issue happens especially with Mobile Data, but can also occur on Wi-Fi with some ISPs.

**Cause:** It seems some ISPs don’t like custom DNS setups. They completely block common DNS servers like `8.8.8.8` and `1.1.1.1`.

**Solution:** Use your ISP’s own DNS servers instead of custom ones.

1. Run this command in an Android root shell to get the default DNS addresses your ISP assigned:

   ```shell
   dumpsys connectivity | sed 's/}}/\n/g' | grep 'InterfaceName: wlan0' | grep -o 'DnsAddresses: \[[^]]*\]' | grep -o '/[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*' | tr -d '/'
   ```

2. Add the results as DNS servers by editing the container configuration in the Droidspaces app.

3. Run this command as root inside the container to tell `systemd-resolved` not to use its own DNS proxy server (Droidspaces v5.7.0 already uses this hack. Only applicable to containers created before Droidspaces v5.7.0 was released):

   ```bash
   cat > "/etc/systemd/resolved.conf.d/dns.conf" << 'EOF'
   [Resolve]
   DNSStubListener=no
   EOF
   ```

---

## WiFi/Mobile Data Disconnects

**Symptoms:** WiFi or mobile data permanently stops working on the host device during container start or stop processes. You may be unable to turn them back on without a device reboot.

**Cause:** The container's `systemd-networkd` service may conflict with Android's network management or attempt to override host-side network configurations.

**Solutions:**

- If you are using the host networking mode: Mask the `systemd-networkd` service inside the container to prevent it from starting:

   1. **Via Android App**: Go to **Panel** -> **Container Name** -> **Manage** (Systemd Menu) and find `systemd-networkd`, then tap on 3 dot icon next to the `systemd-networkd` card and select **Mask**.
   2. **Via Terminal**:
      ```bash
      sudo systemctl mask systemd-networkd
      ```
- Use isolated NAT mode for maximum networking freedom without any conflicts to the host's networking.

---

## SELinux-Induced Rootfs Corruption (Directory Mode)

**Symptoms:** Symbolic link sizes changing unexpectedly (e.g., `dpkg` warnings about `libstdc++.so.6`), shared library load failures (`LD_LIBRARY_PATH` issues), or random binary crashes.

**Cause:** On Android, the `/data/local/Droidspaces/Containers` directory often receives a generic SELinux context. This causes the kernel to block or silently interfere with advanced filesystem operations (like creating certain symlinks or special files) when running in **Directory-based mode** (`--rootfs=/path/to/dir`). Because every file and symlink inside the directory tree is exposed directly to the host filesystem, Android's SELinux policy can relabel or restrict individual entries, corrupting the internal Linux filesystem's expected layout.

**Recommended Solution:** Move to **rootfs.img mode** (`--rootfs-img=/path/to/rootfs.img`).

In this mode, the rootfs is stored as a standalone ext4 image and loop-mounted at runtime. SELinux xattr labels for files inside the image are encapsulated within the image's own filesystem metadata, so Android's policy engine cannot relabel or conflict with them. This avoids the core problem of the host assigning a generic context to every file in the directory tree.

> [!Note]
>
> SELinux enforcement still applies at the process level - the container process's domain and access to the loop device or mount point remain subject to host policy. The `.img` mode does not create a fully SELinux-transparent environment, but it does eliminate host-side interference with the internal filesystem's structure and extended attributes.

> [!WARNING]
> While switching to `permissive` mode may seem to fix this, it is **not recommended** as a permanent solution. If the rootfs has already been corrupted by SELinux denials, the damage is often permanent and cannot be undone by simply changing modes.

---

## Systemd Service Sandboxing Conflicts (Legacy Kernels)

**Symptoms:** Services like `redis`, `mysql`, or `apache` fail to start with `exit-code` or `status=226/NAMESPACE`, even though the exact same configuration worked elsewhere.

**Cause:** Modern service files often use advanced systemd sandboxing directives (`PrivateTmp`, `ProtectSystem`, `RestrictNamespaces`). On legacy kernels (3.18 - 4.19), Droidspaces' **Adaptive Seccomp Shield** intercepts these namespace-related syscalls and returns `EPERM` to prevent kernel deadlocks. However, some distributions' versions of systemd treat these errors as fatal and refuse to start the service.

**Solution:** Create a service override to disable the conflicting sandboxing features:

1.  **Identify the service**: e.g., `redis-server`
2.  **Create the override**:
    ```bash
    sudo systemctl edit <service-name>
    ```
3.  **Add these lines** (to the empty space provided by the editor):
    ```ini
    [Service]
    # Disable problematic security sandboxing
    PrivateTmp=no
    PrivateDevices=no
    ProtectSystem=no
    ProtectHome=no
    RestrictNamespaces=no
    MemoryDenyWriteExecute=no
    NoNewPrivileges=no
    CapabilityBoundingSet=
    ```
4.  **Reload and restart**:
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl restart <service-name>
    ```

---

<a id="reclaim-storage"></a>
## Reclaiming Storage (Sparse Image)

**Symptoms:** You have deleted large files or uninstalled heavy packages inside a container using **Sparse Image mode** (`rootfs.img`), but the `rootfs.img` file on your Android internal storage still takes up the same amount of space (it doesn't "shrink" back).

**Cause:** Ext4 sparse images expand as you write data, but the host filesystem cannot automatically detect when blocks are freed inside the image. This is especially common on legacy kernels (e.g., 4.14).

**Solution:** Use `fstrim` inside the container to tell the kernel to "discard" unused blocks, which punches holes in the sparse image and reclaims space on the physical disk.

1. **Start the container** normally.
2. **Run fstrim** as root inside the container:
   ```bash
   sudo fstrim -av
   ```
3. The command will report how many bytes were trimmed. You will notice the `rootfs.img` size on your Android storage has now decreased to match the actual data usage.

---

<a id="nuke-wifi-powersave"></a>

## Wi-Fi `Power save: on` Causing Sluggish Networking on Android

**Symptoms:** Android automatically puts Wi-Fi hardware into power-saving mode when the device's screen turns off. This can cause sluggish networking or dropped connections within containers. Because there is no universal toggle to disable this behavior from the Android userspace, you must explicitly force the power save state to "off" using a background service.

**Solution:** You can create a lightweight, dedicated container in host networking mode (`--net=host`) that runs a simple "watchdog" script to keep Wi-Fi power save disabled. We recommend using a minimal Alpine Linux container for this purpose.

Here is how to set it up:

### 1. Install Required Utilities
First, ensure that the `iw` utility is installed in your container.
- **Alpine:** `apk add iw`
- **Ubuntu/Debian:** `apt install iw`

### 2. Create the Watchdog Script
Create a new file at `/usr/local/bin/wifi-watchdog.sh` with the following content:

```shell
#!/bin/sh
while true; do
    # Check if power save is "on"
    if /usr/sbin/iw dev wlan0 get power_save 2>/dev/null | grep -q "on"; then
        /usr/sbin/iw dev wlan0 set power_save off
        echo "$(date): WiFi Power Save was ON. Forced OFF." >> /tmp/wifi-fix.log
    fi
    sleep 60
done
```

Make the script executable:
```shell
chmod +x /usr/local/bin/wifi-watchdog.sh
```

### 3. Wire Up the Init Service

Depending on your container's init system, configure the script to run as a background service.

**For OpenRC (Alpine Linux)**

Create a service file at `/etc/init.d/wifi-watchdog`:

```shell
#!/sbin/openrc-run

name="WiFi PowerSave Watchdog"
description="Ensures wlan0 power_save stays off"

# Use the path to your loop script
command="/usr/local/bin/wifi-watchdog.sh"

# This tells OpenRC to handle the backgrounding and PID creation
command_background=true
pidfile="/run/${RC_SVCNAME}.pid"

# Ensure it only starts after the network is available
depend() {
    need networking
}
```

Make the service file executable and start it:

```shell
chmod +x /etc/init.d/wifi-watchdog
rc-update add wifi-watchdog default
rc-service wifi-watchdog start
```

**For systemd (Ubuntu/Debian)**

Create a systemd unit file at `/etc/systemd/system/wifi-watchdog.service`:

```ini
[Unit]
Description=WiFi PowerSave Watchdog
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/wifi-watchdog.sh
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start the service:

```shell
# Reload systemd to pick up the new service
systemctl daemon-reload

# Enable it to start on boot
systemctl enable wifi-watchdog

# Start it now
systemctl start wifi-watchdog
```

> [!NOTE]
> This workaround **requires Host Networking Mode** (`--net=host`). The script expects direct access to Android's `wlan0` interface, which is not visible in `NAT` or `None` modes. We recommend dedicating a small "burner" container specifically for this watchdog.

---

## Getting Help

If your issue isn't listed here:

1. Run `droidspaces check` and note any failures
2. Check the container logs: `droidspaces --name=mycontainer run journalctl -n 100`
3. Try starting in foreground mode for more visibility: `droidspaces --name=mycontainer --rootfs=/path/to/rootfs --foreground start`
4. Join the [Telegram channel](https://t.me/Droidspaces) for community support
5. Open an issue on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS/issues)
