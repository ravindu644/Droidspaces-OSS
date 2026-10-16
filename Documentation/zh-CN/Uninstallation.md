<!--
title: 卸载指南
section: Reference
order: 5
desc: 从 Android 和 Linux 上安全卸载 Droidspaces。彻底删除容器、后端数据、APK 和系统文件。
keywords: uninstall, droidspaces, remove, android, delete, rootfs, cleanup, container, runtime, backend, data
-->

# 卸载

## Android

卸载应用只会移除应用本身。后端、启动模块和所有容器仍保留在 `/data/local/Droidspaces` 下，容器也会继续运行。这是有意设计的，因此更新或重装应用永远不会影响你的容器。若要彻底删除：

1. 在 **容器** 标签页中停止每个容器，然后长按它的卡片并选择 **卸载容器**。这会删除该容器的 rootfs 和设置。对每个容器重复此操作。
2. 在 root shell（Termux 配合 `su` 或其他方式）中删除后端及其数据：

   ```sh
   rm -rf /data/local/Droidspaces
   ```

3. 在你的 root 管理器（KernelSU、Magisk 或 APatch）中移除 "Droidspaces: Daemon & Init" 模块。它是在启动时拉起守护进程和容器的启动模块。
4. 卸载应用。
5. 重启。

如果你曾把 Droidspaces 加入 vendor 镜像以做原生 init 集成，也需要一并撤销，参见 [init/README_CN.md](../../init/README_CN.md)。

App 下载的 rootfs tarball 仍留在你的下载文件夹中。

## Linux

```bash
sudo droidspaces stop --name=a,b,c       # 所有正在运行的容器
sudo systemctl disable --now droidspaces  # 如果你安装了守护进程单元
sudo rm -f /etc/systemd/system/droidspaces.service
sudo systemctl daemon-reload
sudo rm -rf /var/lib/Droidspaces
sudo apt remove droidspaces               # 如果是从 .deb 安装的
sudo rm -f /usr/local/bin/droidspaces     # 如果是从 tarball 安装的
```

你自己创建的 rootfs 目录和镜像在你自己放置的位置。

---

上一页：[NixOS](Nix-NixOS.md) | 回到开头：[Android 安装](Installation-Android.md)
