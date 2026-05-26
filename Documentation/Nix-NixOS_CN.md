<!--
title: Droidspaces 上的 NixOS 入门指南
section: Reference
order: 4
desc: 在 Droidspaces 容器中运行 NixOS。构建 tarball、配置兼容性，以及尝试实验性的 Finix 系统。
keywords: nixos, droidspaces, nix, container, android, finix, containerized
-->

# Droidspaces 上的 NixOS 入门指南

要构建用于 Droidspaces 的最小 tarball 归档文件，请运行：
```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal
```

如果你的设备内核版本为 5.4 或更低，请改用 `minimal-with-systemd-v259`。（systemd v260 及以上版本已不再支持内核 5.4 及以下版本。）

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal-with-systemd-v259
```

如果容器能够启动，你就可以继续为 Droidspaces 配置 NixOS。


# 为 Droidspaces 配置 NixOS

要在 Droidspaces 上顺利运行 NixOS，请将 `working-droidspaces-rootfs-minimal` 模块导入你的 NixOS 系统中。

```nix
# flake.nix
droidspaces.url = "github:ravindu644/Droidspaces-OSS";

# configuration
{inputs, ...}: {
  imports = [inputs.droidspaces.nixosModules.working-droidspaces-rootfs-minimal];
}
```

**注意：** 如前面所述，内核 5.4 及以下版本将无法运行来自较新 nixpkgs 的 NixOS 系统，因此请使用固定版本的 nixpkgs：
```nix
nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
```

添加此模块后，你还可以将系统构建为 tarball 归档文件：
```sh
nix build .#<hostname>.config.system.build.tarball
```


# 老内核上的 systemd 问题

较新的 systemd 版本可能在旧内核上运行遇到问题。你可能需要寻找并使用仍支持你内核的较旧 nixpkgs 版本。


# 不使用 systemd 的 NixOS

如果你不想运行 systemd，可以将 Droidspaces 中的 init 路径更改为 `/bin/sh`。这将允许你正常进入容器，但 systemd 服务将不会运行。


# Finix（告别 systemd）（实验性）

Finix 是一个实验性的类 NixOS 系统，它使用 finit 而非 systemd。

要构建 Finix tarball，请运行：
```sh
nix build github:ravindu644/Droidspaces-OSS#finixDroidspacesTarballs.aarch64-linux.experimental
```
