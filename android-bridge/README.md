# android-bridge

A shell on Android that root inside a container can reach. On a homelab phone
nobody touches, it is how a container reboots the phone, runs `pm`, `cmd` or
`dumpsys`, or manages Droidspaces itself. `reboot` inside a container only
reboots the container.

It is a separate project from Droidspaces and ships as its own KernelSU module.
This side only creates the socket on the host. Droidspaces bind-mounts it into
its containers.

## How it works

- A daemon on Android, started from the module's `post-fs-data.sh`, listens on
  `/dev/android-bridge/bridge.sock`. It starts before Droidspaces boots any
  container.
- Droidspaces bind-mounts that socket into a container at
  `/run/android-bridge/bridge.sock`, where the client looks by default.
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
uid 0 and passes. Every container that gets the socket can open a
shell on the phone. Do not give it to a container that runs untrusted code as
root.

The socket keeps its 0600 root mode through a bind mount, so non-root users
inside a container get `EACCES` on connect the same way. A bind-mounted file
cannot be deleted from inside the container.

A bind mount pins the socket's inode. If the daemon is restarted, it binds a
new socket and containers keep the old one until they restart. Mounting the
directory instead of the file avoids that.

## Install

1. Build the module with `make module`, or take the zip from a release.
2. Flash `android-bridge-module-v*.zip` in the KernelSU manager and reboot.
3. Put the client in the container. It is the same static binary as the
   daemon, `/data/adb/modules/android-bridge/android-bridge` on the phone, or
   the matching arch from `make all-tarball`.

## Use

Inside the container, as root:

```
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

The Makefile is the Droidspaces one, ported. Static musl builds, per-arch
objects under `output/.obj/<arch>`, and incremental rebuilds. Toolchains are
found through `MUSL_CROSS` or `~/toolchains`. `scripts/install-musl.sh <arch>`
installs one.

```
make                 # prints the help, builds nothing
make native          # this machine's arch, into output/android-bridge
make aarch64         # also x86_64, armhf, x86, riscv64
make all-build       # every arch
make all-tarball     # every arch, packed per arch into one tarball
make module          # every arch, then the KernelSU module zip
make debug-hardened  # ASan, UBSan, LSan, for a glibc host
make test            # smoke test against output/android-bridge
make bump NEW=x.y.z  # set AB_VERSION in bridge.h
make format          # clang-format over src/
```

`make test` runs a real daemon on a temporary directory. It needs root and
`/system/bin/sh`, so link that to `/bin/sh` on a machine where that is fine.
`make test BIN=output/android-bridge-hardened` runs it under the sanitizers.

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
