<!--
title: Nix/NixOS
section: Reference
order: 3
desc: 在 Droidspaces 容器中运行 NixOS：构建 tarball、导入兼容性模块，并尝试实验性的 Finix 系统。
keywords: nixos, droidspaces, nix, container, android, finix, containerized
-->

# NixOS

## 构建 tarball

本仓库的 flake 会为 Droidspaces 构建一个最小化的 NixOS rootfs：

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal
```

如果内核为 5.4 或更旧，请改用固定到 systemd 259 的变体。systemd 260 已放弃对这些内核的支持。

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal-with-systemd-v259
```

用 App 的 **从文件安装** 安装构建结果，或在 Linux 上把 `--rootfs` 指向解压出的目录。启动后即可进行配置。

## 为 Droidspaces 配置 NixOS

把 `working-droidspaces-rootfs-minimal` 模块导入你的系统：

```nix
# flake.nix
droidspaces.url = "github:ravindu644/Droidspaces-OSS";

# configuration
{inputs, ...}: {
  imports = [inputs.droidspaces.nixosModules.working-droidspaces-rootfs-minimal];
}
```

如果内核为 5.4 或更旧，请把 nixpkgs 固定到 systemd 仍支持该内核的版本：

```nix
nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
```

该模块也可以把你自己的配置构建成 Droidspaces tarball：

```sh
nix build .#<hostname>.config.system.build.tarball
```

## 不使用 systemd

把容器的自定义 init 设为 `/bin/sh`。你可以进入容器并把它当作 shell 环境使用，不会有任何服务运行。

## Finix（实验性）

Finix 是一个实验性的类 NixOS 系统，使用 finit 而不是 systemd 作为 init：

```sh
nix build github:ravindu644/Droidspaces-OSS#finixDroidspacesTarballs.aarch64-linux.experimental
```

---

上一页：[社区支持的设备](community-supported-devices.md) | 下一页：[卸载](Uninstallation.md)
