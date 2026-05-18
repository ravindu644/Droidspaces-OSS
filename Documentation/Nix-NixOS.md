# Getting started with NixOS on Droidspaces

To build a minimal tarball archive to be used with Droidspaces, run:
```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal
```

If your device has a kernel version 5.4 or below, use `minimal-with-systemd-v259` instead. (Systemd v260 and above have dropped support for kernel 5.4 and below.)

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal-with-systemd-v259
```

If the container boots, you can proceed to configuring NixOS for Droidspaces.


# Configuring NixOS for Droidspaces

To run NixOS on Droidspaces smoothly, import the `working-droidspaces-rootfs-minimal` module into your NixOS system.

```nix
# flake.nix
droidspaces.url = "github:ravindu644/Droidspaces-OSS";

# configuration
{inputs, ...}: {
  imports = [inputs.droidspaces.nixosModules.working-droidspaces-rootfs-minimal];
}
```

**NOTE:** As previously mentioned, kernel 5.4 and below will not be able to run NixOS systems from newer nixpkgs, so use a pinned nixpkgs version:
```nix
nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
```

Adding this module will also let you build your system as a tarball archive:
```sh
nix build .#<hostname>.config.system.build.tarball
```


# Systemd issues on older kernels

Newer systemd versions may have trouble running on older kernels. You may need to find and use an older nixpkgs release that still supports your kernel.


# NixOS without systemd

If you don't want to run systemd, you can change the init path in Droidspaces to `/bin/sh`. This will allow you to enter the container normally, but systemd services will not be running.


# Finix (no more systemd) (experimental)

Finix is an experimental NixOS-like system running finit instead of systemd.

To build a Finix tarball:
```sh
nix build github:ravindu644/Droidspaces-OSS#finixDroidspacesTarballs.aarch64-linux.experimental
```