# Security Policy

## What Droidspaces is

Droidspaces is a privileged container runtime. Root inside a container is root on the
device. The masks, seccomp filter, and dropped capabilities are there to stop accidents and
to keep a container from breaking the host by mistake, not to contain a root user who wants
out. Read the rest of this page with that in mind.

## What counts as a vulnerability

A bug in Droidspaces that lets an unprivileged user reach root, using only what Droidspaces
itself provides. Examples:

- A non-root user inside a container becomes root through something Droidspaces put there:
  a socket, a mount, a device node, a file, or a helper daemon.
- A non-root user or Android app on the host gets root through the Droidspaces daemon, the
  CLI, the app, or anything they expose.
- Root inside a container that is supposed to be isolated takes over a host process that
  Droidspaces runs, such as the daemon or the Anland broker, through a flaw in how that
  process talks to the container.

## What does not

- **Root doing root things.** Container root creating a block device node with `mknod`,
  mounting the host's partitions, and `chroot`ing into them. Root inside a container is
  already root on the device; this is the design, not an escape.
- **Kernel bugs.** A Linux kernel local privilege escalation compromises everything on the
  device, Droidspaces or not. Report those to the kernel or your device vendor.
- **Options that remove isolation on purpose.** Privileged mode, Hardware Access,
  `--allow-sandboxing`, SELinux permissive, disabled seccomp, and similar settings do what
  they say. What they expose is documented.
- **Root on the host.** Anyone who already has root on the device can do anything to
  Droidspaces and its containers.

## Reporting

Do not open a public issue. Report it privately through
[GitHub's private vulnerability reporting](https://github.com/ravindu644/Droidspaces-OSS/security/advisories/new).

Include the Droidspaces version, your device and kernel version, the container
configuration, and the steps to reproduce. A working proof of concept gets the fastest fix.

Only the latest release is supported. Fixes land on `main` and ship in the next release.
