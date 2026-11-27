<!--
title: 故障排查
section: Reference
order: 1
desc: 常见 Droidspaces 问题的解决方法：现代发行版在旧内核上无法启动、容器内没有网络、DNS 解析失败、SELinux 损坏、稀疏镜像无法收缩，以及资源限制不生效。
keywords: droidspaces, troubleshooting, systemd, hang, fix, network, dns, selinux, overlayfs, f2fs, container, errors, sparse, image, limits
-->

# 故障排查

每条都包含症状、原因和处理方法。如果这里没有你的问题，[最后一节](#获取帮助)说明了如何收集一份错误报告所需的信息。

## 现代发行版在旧内核上无法启动

**症状：** Arch、Fedora、openSUSE 或其他当前发行版在 4.x 内核上拒绝启动，提示 "Unsupported Kernel"，很早就崩溃，或者卡在服务始终无法启动的状态。

**原因：** 2025 年 9 月发布的 systemd 258 取消了针对 5.4 之前内核的回退路径。它现在会调用 `clone3`、`openat2` 等 4.14 或 4.19 内核没有的系统调用，并且直接失败，而不是回退。

**解决方法：** 使用 systemd 早于 258 的发行版，例如 Ubuntu 22.04 或 24.04、Debian 12 或 13，或者使用其他 init 的发行版，例如带 OpenRC 的 Alpine 或带 runit 的 Void。限制来自内核，而不是 Droidspaces。

## "Required key not available"

**症状：** 在启用文件级加密、内核早于 5.0 的手机上，容器崩溃或文件操作失败，并报出这条信息。

**原因：** systemd 会为其服务创建新的会话密钥环，而在这些内核上，这样做的进程会失去对 Android 加密密钥的访问权限。

**解决方法：** 在受影响的内核上，Droidspaces 已经拦截了密钥环系统调用，使 systemd 保留现有的密钥环。如果仍然看到该错误，请确认后端是最新的，并且 `droidspaces check` 通过了 seccomp 探测，后者需要 `CONFIG_SECCOMP_FILTER`。使用稀疏镜像 rootfs 可以完全避开这个问题。

## 易失模式在 f2fs 上失败

**症状：** 用易失模式启动目录形式的容器时，报 OverlayFS 错误并失败。

**原因：** 多数手机把 `/data` 放在 f2fs 上，而多数 Android 内核上的 OverlayFS 拒绝把 f2fs 作为下层。

**解决方法：** 使用稀疏镜像 rootfs。里面的 ext4 是 OverlayFS 能接受的下层。

## 完全没有网络，报 "socket: permission denied"

**症状：** 即使以 root 运行 `ping`，也会立刻报 `socket: permission denied` 失败。

**原因：** Android 的 paranoid networking（存在于许多 4.14 及更早的内核中）只允许特定用户组中的进程打开网络 socket：`aid_inet`（3003）、`aid_net_raw`（3004）和 `aid_net_admin`（3005）。

**解决方法：** 通过 App 安装的容器以及官方 rootfs 镜像已经包含这些组，root 也在其中。对于你自己准备的 rootfs，请把这些组加入 `/etc/group`，并把你的用户放进去。编译内核时，`CONFIG_ANDROID_PARANOID_NETWORK=n` 会移除该限制。

## 地址能通，但域名无法解析

**症状：** `ping 1.1.1.1` 正常，`ping google.com` 不行，即使 `/etc/resolv.conf` 中列出了 nameserver。最常见于移动数据网络。

**原因：** 部分运营商会封锁公共解析器。

**解决方法：** 把容器的 **DNS 服务器** 设置为运营商分配的地址。在手机上，可以用 root shell 打印出来：

```sh
dumpsys connectivity | sed 's/}}/\n/g' | grep 'InterfaceName: wlan0' | grep -o 'DnsAddresses: \[[^]]*\]' | grep -o '/[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*' | tr -d '/'
```

使用蜂窝网络时，把 `wlan0` 换成移动数据接口，通常是 `rmnet_data0` 或类似名称。

## 容器启动时手机的 Wi-Fi 或移动数据断开

**症状：** 主机模式容器启动或停止时，手机自身的连接停止工作，有时只有重启才能恢复。

**原因：** 容器内的网络管理器（通常是 systemd-networkd 或 NetworkManager）正在配置手机的接口，因为在主机模式下，这些接口同时也是容器的接口。

**解决方法：** 改用 NAT 模式，容器拥有自己的接口，无法碰手机的接口。如果容器必须留在主机模式，就在容器内屏蔽网络管理器：在 App 中通过容器的 **管理** 界面操作，或使用 `systemctl mask systemd-networkd`。

## 目录形式的容器中二进制文件随机崩溃

**症状：** 在 Android 上使用目录形式的 rootfs 时，符号链接的大小会变化，`dpkg` 对 `libstdc++.so.6` 发出警告，库加载失败，程序无故崩溃。

**原因：** `/data/local/Droidspaces` 下的目录会获得通用的 SELinux 标签，随后内核会干扰 rootfs 内的某些文件操作。一旦发生，损坏是永久性的。

**解决方法：** 安装到稀疏镜像中。镜像内部的标签属于镜像自己的文件系统，不受宿主机策略约束。App 的 **迁移至稀疏镜像** 可以对现有容器执行此操作，但已经损坏的 rootfs 需要重装。把 SELinux 切换为宽容模式只会隐藏新的拒绝记录，不会修复任何东西。

## rootfs 镜像在 Android 上挂载失败

**症状：** 容器无法启动，日志显示 loop 挂载失败。

**原因：** 镜像文件的 SELinux 标签可能无法被 loop 驱动读取。

**解决方法：** Droidspaces 会在挂载前为镜像设置标签，因此保持后端为最新就不会遇到此问题。手动处理时，标签为 `chcon u:object_r:vold_data_file:s0 /path/to/rootfs.img`。

## 同名容器提示"已在运行"

**症状：** `start` 因为存在同名运行中容器而拒绝执行，但实际上没有任何容器在运行。

**解决方法：** `droidspaces show` 会列出正在运行的容器。如果名称在列表中且不该在，用 `droidspaces scan` 让记录与实际存在的进程对账。否则换一个名字。

## 在 4.14 内核上 systemd 卡住，或容器无法停止

**症状：** 在 4.14 及相近内核上，systemd 在启动过程中冻结，或者 `stop` 运行超过十五秒后失败。

**原因：** `grab_super()` 中的内核 bug，由 systemd 的服务沙箱（`PrivateTmp`、`ProtectSystem`）触发。Droidspaces 无法绕过。

**解决方法：** 使用包含上游修复的内核。

## 删除文件后稀疏镜像没有收缩

**症状：** 你在容器内删除了几个 GB，但手机上的 `rootfs.img` 还是和以前一样大。

**原因：** 在文件系统告知之前，宿主机无法知道镜像内哪些块是空闲的。

**解决方法：** 在容器内以 root 运行 `fstrim -av`。它会报告丢弃了多少空间，镜像文件会相应收缩。

## LuCI 或其他工具在有限制时仍显示宿主机的内存和负载

**症状：** 容器设置了内存或 CPU 限制。容器内的 `free` 和 fastfetch 显示的是限制后的数字，但 OpenWrt 的 LuCI 显示的是手机的全部内存和真实负载。

**原因：** LuCI 通过 `sysinfo()` 系统调用向内核询问，而不是读取 `/proc/meminfo`，而系统调用无法用文件替换。

**解决方法：** 无需处理。限制无论如何都会执行；只是显示的数字不对。在宿主机上运行 `droidspaces -n <name> info` 可以看到真实用量。

## 内核选项已开启，但资源限制不可用

**症状：** `droidspaces check` 报告内存、CPU 或进程限制不受支持，或者启动时打印 `memory.max is not available on this kernel`，但 `/proc/config.gz` 中已有 `CONFIG_MEMCG`、`CONFIG_CFS_BANDWIDTH` 或 `CONFIG_CGROUP_PIDS`。

**原因：** 该控制器在启动时被关闭了。在 `/proc/cgroups` 中，控制器所在行最后一列为 `0`，表示内核命令行带有 `cgroup_disable=memory`、`cgroup_disable=cpu` 或 `cgroup_disable=pids`。`cat /proc/cmdline` 可以确认。厂商内核常常为了省一点内存而关闭内存控制器。

**解决方法：** 移除该参数。它位于内核配置的 `CONFIG_CMDLINE`、启动镜像头部或引导程序中。容器仍会启动；只是对应的限制会被跳过。

如果 `/proc/cgroups` 中根本没有该控制器的行，说明内核是 6.12 或更新版本，并且在构建时没有包含该控制器的 cgroup v1 代码。它在 cgroup v2 上仍然存在，Droidspaces 会从那里使用它。

## 获取帮助

1. 运行 `droidspaces check`，记下所有标记为缺失的项。
2. 查看容器日志：App 中容器卡片上的日志图标，或工作目录中的 `Logs/<name>/`。
3. 在 CLI 上，用 `--foreground` 启动，以便在终端里观察启动过程。
4. 在 App 的 **设置** 中，**生成错误报告** 会把日志和设备信息收集到一个文件中。
5. 在 [Telegram 频道](https://t.me/Droidspaces)提问，或附上该文件开一个 [GitHub issue](https://github.com/ravindu644/Droidspaces-OSS/issues)。

---

上一页：[实用配方](./Cool-things-you-can-do.md) | 下一页：[社区支持的设备](./community-supported-devices.md)
