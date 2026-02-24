# Troubleshooting

Common issues, their causes, and how to fix them.

---

## "Required key not available" (ENOKEY)

**Symptoms:** The container crashes or filesystem operations fail with "Required key not available" errors. Most commonly seen on Android devices with File-Based Encryption (FBE).

**Cause:** systemd services inside the container attempt to create new session keyrings, which causes the process to lose access to Android's FBE encryption keys.

**Affected kernels:** 3.18, 4.4, 4.9, 4.14, 4.19 (legacy Android kernels)

**Solution:** This is handled automatically by Droidspaces' Adaptive Seccomp Shield on kernels below 5.0. The shield intercepts keyring-related syscalls and returns `ENOSYS`, causing systemd to fall back to the existing session keyring.

If you're still seeing this error:
- Verify your Droidspaces binary is up to date (v4.2.4+)
- Run `droidspaces check` to verify seccomp support
- Ensure `CONFIG_SECCOMP=y` and `CONFIG_SECCOMP_FILTER=y` are in your kernel config

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

**Solution:**

1. Try stopping normally first:
   ```bash
   droidspaces --name=mycontainer stop
   ```

2. If that hangs, kill the process directly:
   ```bash
   # Find the container PID
   droidspaces --name=mycontainer status
   
   # Force kill
   kill -9 <PID>
   ```

3. Clean up any leftover state:
   ```bash
   droidspaces scan
   ```

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

## Bind Mount Permission Errors

**Symptoms:** Files in bind-mounted directories are not accessible or writable from inside the container, or the bind mount silently fails.

**Cause:** Several possible causes:
- The host source path doesn't exist (Droidspaces skips the mount with a warning)
- SELinux is blocking access to the mounted path
- File ownership doesn't match inside the container

**Solution:**

1. Verify the host path exists before starting:
   ```bash
   ls -la /path/to/host/directory
   ```

2. If SELinux is the issue, try running with `--selinux-permissive`:
   ```bash
   droidspaces --name=mycontainer --rootfs=/path/to/rootfs \
     --bind-mount=/host/path:/container/path \
     --selinux-permissive start
   ```

3. Check file ownership inside the container:
   ```bash
   droidspaces --name=mycontainer run ls -la /container/path
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

Note: Droidspaces auto-resolves name conflicts by appending a numeric suffix (e.g., `ubuntu-24.04` becomes `ubuntu-24.04-1`).

---

## System Hangs on Older Kernels

**Symptoms:** The entire system hangs or becomes unresponsive when starting a container on kernel 4.9 or 4.14.

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

---
220: 
221: ## Modern Distros (Ubuntu/Debian) on Legacy Kernels (3.18 - 4.4)
222: 
223: **Symptoms:** Container starts but hangs during boot, fails to start services, or crashes immediately even after a successful bootup.
224: 
225: **Cause:** Modern distributions like Ubuntu 22.04+ or Debian 12+ rely on kernel features (seccomp filters, cgroup v2, namespace isolation) that are incomplete or buggy on kernels older than 4.9.
226: 
227: **Solution:** 
228: - Use **Alpine Linux** for these devices; it is minimalist and highly compatible with legacy kernels.
229: - If you must use a modern distro, consider finding or compiling a newer kernel (4.14+) for your device.
230: 
231: ---
232: 
233: ---

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

## Getting Help

If your issue isn't listed here:

1. Run `droidspaces check` and note any failures
2. Check the container logs: `droidspaces --name=mycontainer run journalctl -n 100`
3. Try starting in foreground mode for more visibility: `droidspaces --name=mycontainer --rootfs=/path/to/rootfs --foreground start`
4. Join the [Telegram channel](https://t.me/Droidspaces) for community support
5. Open an issue on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS/issues)
