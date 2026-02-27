# Troubleshooting

Common issues, their causes, and how to fix them.

### Quick Navigation
- [Modern Distros (Arch, Fedora, etc.) Failure on Legacy Kernels](#modern-distros-arch-fedora-etc-failure-on-legacy-kernels)
- ["Required key not available" (ENOKEY)](#required-key-not-available-enokey)
- [Mount Errors on Kernel 4.14](#mount-errors-on-kernel-414)
- [OverlayFS Not Supported (f2fs)](#overlayfs-not-supported-f2fs)
- [Container Won't Stop](#container-wont-stop)
- [PTY Issues (Login Hangs)](#pty-issues-login-hangs)
- [Container Name Conflicts](#container-name-conflicts)
- [Systemd Hangs on Older Kernels](#systemd-hangs-on-older-kernels)
- ["Not a TTY" Errors](#not-a-tty-errors)
- [Rootfs Image I/O Errors on Android](#rootfs-image-io-errors-on-android)
- [DNS / Name Resolution Issues](#dns--name-resolution-issues)
- [WiFi/Mobile Data Disconnects](#wifimobile-data-disconnects)
- [SELinux-Induced Rootfs Corruption](#selinux-induced-rootfs-corruption-directory-mode)
- [Systemd Service Sandboxing Conflicts](#systemd-service-sandboxing-conflicts-legacy-kernels)
- [Getting Help](#getting-help)

---

<a id="modern-distros"></a>
## Modern Distros (Arch, Fedora, etc.) Failure on Legacy Kernels

This is not a bug with Droidspaces; it is a limitation of the specific distribution's `systemd` version. Modern distributions like Arch Linux, Fedora, or OpenSUSE use very recent versions of `systemd` that require kernel features missing in older versions. On legacy kernels (3.18, 4.4, 4.9, 4.14, 4.19), these distros will either fail to boot with an "Unsupported Kernel" message in the foreground boot screen or crash during initialization.

**Cause:** The host kernel is too old to support the cgroup and namespace requirements of modern `systemd`.

**Solution:**
- Use **Alpine Linux** (extremely stable on legacy kernels).
- Use **Ubuntu 22.04 LTS** (extensively tested and stable on Android kernels as old as 4.14).

**Warning:** Using Distros newer than Ubuntu 22.04-era (e.g., 24.04) on legacy kernels often results in buggy cgroup hierarchies, resource leaks, kernel panics, or `rootfs.img` corruption.

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

## Container Won't Stop

**Symptoms:** `droidspaces stop` hangs or reports that the container is still running.

**Cause:** The container's init system may not be responding to shutdown signals, or processes inside the container are blocking the shutdown.

**Solution:** Restart the Device.

---

## PTY Issues (Login Hangs)

**Symptoms:** After entering a container with `droidspaces enter`, the terminal hangs or `login`/`su` commands don't respond.

**Cause:** This can happen if the entering process is not properly attached to the container's cgroup hierarchy. `systemd-logind` and `sd-pam` need the process to be in the container's cgroup to create a valid session.

**Solution:** This was fixed in Droidspaces v4.2.0+. The `enter` command now automatically attaches the process to the container's host-side cgroup before joining namespaces.

If you're still experiencing issues:
- Update to the latest Droidspaces version
- Try entering as root first: `droidspaces --name=mycontainer enter`
- Check if the container is fully booted: `droidspaces --name=mycontainer status`

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

**Solution:** The Adaptive Seccomp Shield (active on kernels < 5.0) prevents this by intercepting namespace-related syscalls from sandboxed services. systemd gracefully runs services without sandboxing instead.

If you're experiencing hangs:
- Verify the seccomp shield is active: `droidspaces check` should show seccomp support
- Update to the latest Droidspaces version
- Consider upgrading the kernel if possible

---

## "Not a TTY" Errors

**Symptoms:** Commands like `sudo` or interactive programs complain about "not a tty" when run inside the container.

**Cause:** The terminal is not properly set up as a controlling TTY inside the container.

**Solution:** Use `droidspaces enter` to get a proper interactive session. If running commands with `droidspaces run`, note that the run command does not allocate a full PTY for non-interactive execution.

For interactive commands that need a TTY:
```bash
# Use enter instead of run
droidspaces --name=mycontainer enter

# Or use run with a shell wrapper
droidspaces --name=mycontainer run sh -c "your-command"
```

---

## Rootfs Image I/O Errors on Android

**Symptoms:** Loop-mounting a rootfs image silently fails, or the container starts but filesystem operations cause errors.

**Cause:** On certain Android devices, the SELinux context of the `.img` file prevents the loop driver from performing I/O.

**Solution:** Droidspaces v4.3.0+ automatically applies the `vold_data_file` SELinux context to image files before mounting. If you're on an older version, update to the latest release.

You can also manually apply the context:
```bash
chcon u:object_r:vold_data_file:s0 /path/to/rootfs.img
```

---

## DNS / Name Resolution Issues

**Symptoms:** Internet works (IPs can be pinged), but domain names fail to resolve. `resolv.conf` is overwritten with "127.0.0.53" or other incorrect settings even after using `--dns`.

**Cause:** `systemd-resolved` is running inside the container and attempting to manage DNS locally, often overwriting the static `/etc/resolv.conf` provided by Droidspaces.

**Solution:** Mask the `systemd-resolved` service to allow the container to use Droidspaces' static DNS configuration:

1. **Via Android App**: Go to **Panel** -> **Container Name** -> **Manage** (Systemd Menu), find `systemd-resolved`, tap the 3-dot icon, and select **Mask**.
2. **Via Terminal**:
   ```bash
   sudo systemctl mask systemd-resolved
   ```

---

## WiFi/Mobile Data Disconnects

**Symptoms:** WiFi or mobile data permanently stops working on the host device during container start or stop processes. You may be unable to turn them back on without a device reboot.

**Cause:** The container's `systemd-networkd` service may conflict with Android's network management or attempt to override host-side network configurations.

**Solution:** Mask the `systemd-networkd` service inside the container to prevent it from starting:

1. **Via Android App**: Go to **Panel** -> **Container Name** -> **Manage** (Systemd Menu) and find `systemd-networkd`, then tap on 3 dot icon next to the `systemd-networkd` card and select **Mask**.
2. **Via Terminal**:
   ```bash
   sudo systemctl mask systemd-networkd
   ```

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

## Getting Help

If your issue isn't listed here:

1. Run `droidspaces check` and note any failures
2. Check the container logs: `droidspaces --name=mycontainer run journalctl -n 100`
3. Try starting in foreground mode for more visibility: `droidspaces --name=mycontainer --rootfs=/path/to/rootfs --foreground start`
4. Join the [Telegram channel](https://t.me/Droidspaces) for community support
5. Open an issue on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS/issues)
