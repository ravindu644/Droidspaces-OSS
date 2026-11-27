<!--
title: Troubleshooting
section: Reference
order: 1
desc: Fixes for common Droidspaces problems: a modern distribution that will not boot on an old kernel, no network inside the container, DNS failures, SELinux damage, sparse images that do not shrink, and resource limits that do not apply.
keywords: droidspaces, troubleshooting, systemd, hang, fix, network, dns, selinux, overlayfs, f2fs, container, errors, sparse, image, limits
-->

# Troubleshooting

Each entry is a symptom, what causes it, and what to do. If yours is not here, the [last section](#getting-help) says how to collect what a bug report needs.

## A modern distribution will not boot on an old kernel

**Symptom.** Arch, Fedora, openSUSE or another current distribution refuses to boot on a 4.x kernel with "Unsupported Kernel", crashes early, or hangs with services that never start.

**Cause.** systemd 258, released in September 2025, dropped its fallbacks for kernels older than 5.4. It now calls system calls such as `clone3` and `openat2` that a 4.14 or 4.19 kernel does not have, and fails instead of falling back.

**Fix.** Use a distribution whose systemd is older than 258, such as Ubuntu 22.04 or 24.04, Debian 12 or 13, or one with another init, such as Alpine with OpenRC or Void with runit. The kernel is the limit, not Droidspaces.

## "Required key not available"

**Symptom.** On a phone with file-based encryption and a kernel older than 5.0, the container crashes or file operations fail with this message.

**Cause.** systemd creates new session keyrings for its services, and on these kernels a process that does so loses access to Android's encryption keys.

**Fix.** Droidspaces already intercepts the keyring system calls on affected kernels so systemd keeps the existing keyring. If you still see the error, make sure the backend is current and that `droidspaces check` passes the seccomp probe, which needs `CONFIG_SECCOMP_FILTER`. A sparse image rootfs avoids the problem entirely.

## Volatile mode fails on f2fs

**Symptom.** Starting a directory container with volatile mode fails with an OverlayFS error.

**Cause.** Most phones keep `/data` on f2fs, and OverlayFS on most Android kernels refuses f2fs as its lower layer.

**Fix.** Use a sparse image rootfs. The ext4 inside it is a lower layer OverlayFS accepts.

## No network at all, "socket: permission denied"

**Symptom.** Even `ping` as root fails at once with `socket: permission denied`.

**Cause.** Android's paranoid networking, present in many kernels up to 4.14, lets only processes in certain groups open network sockets: `aid_inet` (3003), `aid_net_raw` (3004) and `aid_net_admin` (3005).

**Fix.** Containers installed by the app, and the official rootfs images, already have these groups and root is in them. For a rootfs you prepared yourself, add the groups to `/etc/group` and put your users in them. When building a kernel, `CONFIG_ANDROID_PARANOID_NETWORK=n` removes the restriction.

## Addresses work but names do not resolve

**Symptom.** `ping 1.1.1.1` works, `ping google.com` does not, even though `/etc/resolv.conf` lists a nameserver. Most often on mobile data.

**Cause.** Some carriers block public resolvers.

**Fix.** Set the container's **DNS Servers** to the ones the carrier assigned. On the phone, a root shell prints them:

```sh
dumpsys connectivity | sed 's/}}/\n/g' | grep 'InterfaceName: wlan0' | grep -o 'DnsAddresses: \[[^]]*\]' | grep -o '/[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*' | tr -d '/'
```

Swap `wlan0` for the mobile data interface, usually `rmnet_data0` or similar, when on cellular.

## Wi-Fi or mobile data drops on the phone when a container starts

**Symptom.** The phone's own connection stops working when a host mode container starts or stops, and sometimes only a reboot brings it back.

**Cause.** A network manager inside the container, usually systemd-networkd or NetworkManager, is configuring the phone's interfaces, because in host mode they are the container's interfaces too.

**Fix.** Use NAT mode, where the container has its own interfaces and cannot touch the phone's. If the container must stay in host mode, mask the network manager inside it: from the container's **Manage** screen in the app, or with `systemctl mask systemd-networkd`.

## Binaries crash at random in a directory container

**Symptom.** In a directory rootfs on Android, symlinks change size, `dpkg` warns about `libstdc++.so.6`, libraries fail to load, programs crash for no reason.

**Cause.** The directory under `/data/local/Droidspaces` gets a generic SELinux label, and the kernel then interferes with some file operations inside the rootfs. The damage is permanent once it has happened.

**Fix.** Install into a sparse image. The labels inside the image belong to the image's own filesystem, out of the host policy's reach. The app's **Migrate to a sparse image** does this for an existing container, but a rootfs that is already damaged needs reinstalling. Switching SELinux to permissive hides new denials and repairs nothing.

## A rootfs image fails to mount on Android

**Symptom.** The container does not start and the log shows the loop mount failing.

**Cause.** The image file has an SELinux label the loop driver may not read.

**Fix.** Droidspaces labels images before mounting them, so an up-to-date backend does not hit this. By hand, the label is `chcon u:object_r:vold_data_file:s0 /path/to/rootfs.img`.

## A container with the same name is "already running"

**Symptom.** `start` refuses because of a running container of that name, but nothing is running.

**Fix.** `droidspaces show` lists what is running. If the name is listed and should not be, `droidspaces scan` reconciles the records with the processes that exist. Otherwise pick another name.

## systemd hangs, or a container will not stop, on a 4.14 kernel

**Symptom.** On kernel 4.14 and neighbours, systemd freezes during boot, or `stop` runs for more than fifteen seconds and fails.

**Cause.** A kernel bug in `grab_super()`, triggered by systemd's service sandboxing (`PrivateTmp`, `ProtectSystem`). Droidspaces cannot work around it.

**Fix.** Use a kernel that carries the upstream fix.

## The sparse image does not shrink after deleting files

**Symptom.** You removed gigabytes inside the container, but `rootfs.img` on the phone is as large as before.

**Cause.** The host cannot know which blocks inside the image are free until the filesystem says so.

**Fix.** Run `fstrim -av` as root inside the container. It reports how much it discarded, and the image file shrinks by that amount.

## LuCI, or another tool, shows the host's RAM and load despite a limit

**Symptom.** A container has a memory or CPU limit. `free` and fastfetch inside it show the limited figures, but OpenWrt's LuCI shows the phone's full RAM and real load.

**Cause.** LuCI asks the kernel through the `sysinfo()` system call rather than reading `/proc/meminfo`, and a system call cannot be replaced with a file.

**Fix.** None needed. The limit is enforced either way; only the number displayed is wrong. `droidspaces -n <name> info` on the host shows the real usage.

## A resource limit is unavailable although the kernel option is on

**Symptom.** `droidspaces check` reports memory, CPU or process limit support missing, or a start prints `memory.max is not available on this kernel`, yet `/proc/config.gz` has `CONFIG_MEMCG`, `CONFIG_CFS_BANDWIDTH` or `CONFIG_CGROUP_PIDS`.

**Cause.** The controller was switched off at boot. In `/proc/cgroups`, a `0` in the last column of the controller's row means the kernel command line carries `cgroup_disable=memory`, `cgroup_disable=cpu` or `cgroup_disable=pids`. `cat /proc/cmdline` confirms it. Vendor kernels often disable the memory controller to save a little memory.

**Fix.** Remove the argument. It lives in `CONFIG_CMDLINE` in the kernel config, in the boot image header, or in the bootloader. The container starts either way; only the limit is skipped.

If the controller has no row at all in `/proc/cgroups`, the kernel is 6.12 or newer and was built without that controller's cgroup v1 code. It is still there on cgroup v2, and Droidspaces uses it from there.

## Getting help

1. Run `droidspaces check` and note anything marked missing.
2. Read the container's log: the log icon on its card in the app, or `Logs/<name>/` in the workspace.
3. From the CLI, start with `--foreground` to watch the boot on your terminal.
4. In the app, **Settings**, **Generate a bug report** collects logs and device details into one file.
5. Ask in the [Telegram channel](https://t.me/Droidspaces) or open a [GitHub issue](https://github.com/ravindu644/Droidspaces-OSS/issues) with that file attached.

---

Previous: [Recipes](Cool-things-you-can-do.md) | Next: [Community-supported devices](community-supported-devices.md)
