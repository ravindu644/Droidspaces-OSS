# android-bridge

A shell on Android that root inside a container can reach. On a homelab phone
nobody touches, it is how a container reboots the phone, runs `pm`, `cmd` or
`dumpsys`, or manages Droidspaces itself. `reboot` inside a container only
reboots the container.

It is a separate project from Droidspaces. It needs nothing from it beyond a
bind mount, and it ships as its own KernelSU module.

## How it works

- A daemon on Android, started from the module's `post-fs-data.sh`, listens on
  `/dev/android-bridge/bridge.sock`. It starts before Droidspaces boots any
  container.
- A container gets that directory as a read-only bind mount at
  `/run/android-bridge`. The client binary sits next to the socket, so nothing
  has to be installed in the container.
- Every connection gets `/system/bin/sh` as the shell user, uid 2000, in
  `u:r:shell:s0`, with adbd's groups and adbd's capability bounding set. That
  is what `adb shell` gives you, and it is never root. Run `su` in it for root,
  which works only if the root manager grants su to Shell.

## Who can connect

Only real root. Three checks, each one denies when it cannot tell:

1. **File permissions.** The directory is 0700 and the socket 0600, both owned
   by root. Any other uid gets `EACCES` from the kernel before the daemon sees
   anything.
2. **Peer uid.** The daemon reads the peer's credentials from the kernel and
   wants uid 0 as the host sees it. Root inside a user namespace, such as
   rootless podman or `unshare -r`, shows up as the host uid it maps to.
3. **User namespace.** The peer must share the daemon's user namespace. This
   catches a process that real root mapped to 0 inside a child namespace,
   which has uid 0 but no power over the host.

Droidspaces containers do not use user namespaces, so container root is host
uid 0 and passes. Every container you bind the directory into can open a
shell on the phone. Do not give it to a container that runs untrusted code as
root.

Keep the bind read-only. Writable, container root could delete the socket for
every container, or swap the client binary that other containers run.

## Install

1. Build the module with `make module`, or take the zip from a release.
2. Flash `out/android-bridge-v*.zip` in the KernelSU manager and reboot.
3. Give a container the bridge. Add this bind mount in the container's
   settings, or this line to its `container.config`, then restart it:

   ```
   bind_mounts=/dev/android-bridge:/run/android-bridge:ro
   ```

## Use

Inside the container, as root:

```
ln -s /run/android-bridge/android-bridge /usr/local/bin/android-bridge

android-bridge                         # interactive shell, like adb shell
android-bridge reboot                  # reboot the phone
android-bridge dumpsys battery
echo hello | android-bridge cat        # pipes and exit codes pass through
android-bridge -t top                  # force a terminal for a command
android-bridge su -c /data/local/Droidspaces/bin/droidspaces show
```

Arguments are joined with spaces and run with `sh -c`, the same as `adb shell`,
so quote the same way. A terminal is allocated for an interactive shell on a
terminal. `-t` forces one, `-T` refuses one. The exit code is the remote
command's, or 255 when the bridge itself failed.

## Logs

`/data/adb/android-bridge.log`, with the previous boot in `.old`. One line per
connection: peer pid, host or container, mode, and exit code. Commands are not
logged.

## Build

The musl toolchains are the ones Droidspaces uses, found the same way through
`MUSL_CROSS` or `~/toolchains`. Run `../scripts/install-musl.sh <arch>` if one
is missing.

```
make                 # host build into out/android-bridge
make aarch64         # also armhf, x86_64, x86
make module          # every arch, then the KernelSU zip
make test            # smoke test, as root on Linux
make format
```

`make test` runs a real daemon on a temporary directory. It needs root and
`/system/bin/sh`, so link that to `/bin/sh` on a machine where that is fine.

## Status

Tested on Linux: the smoke test, a simulated container with its own mount and
PID namespaces, and AddressSanitizer runs with malformed and random frames.
Not yet tested on a phone. The SELinux side is the part most likely to need
work. If the shell refuses to start, the log says why, and `sepolicy.rule` is
where the fix goes. Only KernelSU is targeted. Magisk and APatch run modules
in other domains, and Magisk also wants a `META-INF` installer.

## Protocol

Frames of one type byte, a 4 byte big-endian length and the payload. The
client sends a hello with the protocol version, flags, window size, `TERM`
and the command, then stdin, stdin EOF and window changes. The daemon sends
stdout, stderr and a final exit code. Both ends buffer without blocking, so a
shell that stops reading cannot wedge the other direction.
