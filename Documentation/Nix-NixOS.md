<!--
title: Nix/NixOS
section: Reference
order: 3
desc: Run NixOS in a Droidspaces container: build a tarball, import the compatibility module, and try the experimental Finix system.
keywords: nixos, droidspaces, nix, container, android, finix, containerized
-->

# NixOS

## Build a tarball

The repository's flake builds a minimal NixOS rootfs for Droidspaces:

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal
```

On a kernel of 5.4 or older, build the variant pinned to systemd 259 instead. systemd 260 dropped support for those kernels.

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal-with-systemd-v259
```

Install the result with **Install from file** in the app, or point `--rootfs` at the extracted directory on Linux. Once it boots, configure it.

## Configure NixOS for Droidspaces

Import the `working-droidspaces-rootfs-minimal` module into your system:

```nix
# flake.nix
droidspaces.url = "github:ravindu644/Droidspaces-OSS";

# configuration
{inputs, ...}: {
  imports = [inputs.droidspaces.nixosModules.working-droidspaces-rootfs-minimal];
}
```

On a kernel of 5.4 or older, pin nixpkgs to a release whose systemd still supports it:

```nix
nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
```

The module can also build your own configuration as a Droidspaces tarball:

```sh
nix build .#<hostname>.config.system.build.tarball
```

## Without systemd

Set the container's custom init to `/bin/sh`. You can enter the container and use it as a shell environment, and no services run.

## Finix (experimental)

Finix is an experimental NixOS-like system that uses finit as its init instead of systemd:

```sh
nix build github:ravindu644/Droidspaces-OSS#finixDroidspacesTarballs.aarch64-linux.experimental
```

---

Previous: [Community-supported devices](community-supported-devices.md) | Next: [Uninstallation](Uninstallation.md)
