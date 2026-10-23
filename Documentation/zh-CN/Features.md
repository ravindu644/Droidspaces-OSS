<!--
title: 容器设置
section: Guides
order: 2
desc: 按 App 显示顺序介绍 Droidspaces 的每一项容器设置：每个开关或参数做什么、什么时候打开，以及它对内核有什么要求。
keywords: droidspaces, settings, features, network, mode, hardware, access, gpu, volatile, bind, mount, resource, limits, privileged, sandboxing, run, at, boot
-->

# 容器设置

下面每一项设置在 App 的容器配置里是一个开关，在命令行上是一个参数。它们按 App 显示的顺序排列。每一项都给出 CLI 参数，以及它写入容器配置文件的键名，因此本页同时服务于 App 和 CLI 用户。

## 设置如何保存

每个容器都有一个 `container.config` 文件，内容由若干 `key=value` 行组成。App 根据配置表单写入它。在 CLI 上，第一次 `start` 传入的参数会被保存进该文件，之后启动只需要 `--name`。之后的 `start` 或 `restart` 传入的参数会更新该文件；`--reset` 会把它恢复为默认值，同时保留容器名和 rootfs。

改动在容器下次启动时生效。在 App 中，容器菜单里有 **编辑容器配置**。

## 网络

### 网络模式

`--net=MODE`，键 `net_mode`。默认是 `nat`。

| 模式 | 容器获得什么 | 什么时候用 |
|------|-------------------------|-------------|
| **NAT**（`nat`） | 独立的网络栈和私有地址，通过宿主机上网。无需任何配置。会跟随宿主机当前连接在 Wi-Fi、移动数据和以太网之间的切换。 | 你只想让容器能上网。几乎所有场景都该选它。 |
| **网关**（`gateway`） | 独立的网络栈，位于另一个运行中的容器（通常是 OpenWrt）所拥有的 LAN 上，由后者为它提供 DHCP、DNS、防火墙、路由和 VPN。 | 你想要 VPN 断网保护、DNS 过滤，或者让多个容器处于同一台路由器后面的同一个隔离 LAN 中。 |
| **Macvlan**（`macvlan`） | 手机所接入的有线网络上的独立 MAC 地址，以及来自该网络路由器的地址。没有 NAT。 | 容器应当成为家庭 LAN 上的一等设备，无需端口转发即可访问。手机端需要 USB 以太网适配器。 |
| **主机**（`host`） | 直接共享宿主机自身的网络栈。 | 某个程序需要只有宿主机才有的接口。服务会直接绑定宿主机端口，而容器内的网络管理器可能破坏手机的连接。 |
| **无**（`none`） | 只有 loopback 的网络栈。 | 容器必须离线。 |

[从零开始的网络基础](./Networking-From-Zero.md)详细讲解了这五种模式，包括如何搭建 OpenWrt 网关。

### 静态 IP 地址

`--nat-ip=IP`，键 `static_nat_ip`。仅限 NAT 模式。把容器的地址固定在 `172.28.0.0/16` 范围内。不设置时，地址由容器的 PID 推导而来，可能在两次启动之间变化；当其他设备或路由器上的端口转发记录了这个地址时，就会成为问题。

### 上行接口

`--upstream=IFACE[,IFACE...]`，键 `upstream_interfaces`。仅限 NAT 模式。留空时容器跟随宿主机当前使用的连接。设置后，容器的互联网只经列出的接口出去，使用第一个已启用且有互联网的接口。支持通配符，`rmnet*` 是给移动数据接口命名的方式，它的编号每次重连后都会变化。

- `tun0`：容器只能通过手机上运行的 VPN 应用访问互联网。VPN 断开时，容器即离线。
- `rmnet*`：手机用着 Wi-Fi，但容器留在移动数据上。为此需要在开发者选项中打开"始终保持移动数据连接"。
- `wlan0,rmnet*`：有 Wi-Fi 时走 Wi-Fi，否则走移动数据。

### 端口转发

`--port=[HOST:]CONTAINER[/tcp|udp]`，键 `port_forwards`。仅限 NAT 模式。让容器内的服务可以通过宿主机的某个端口访问，宿主机自身及其所在的任何网络都能访问。`--port=22` 表示两端使用同一端口，`--port=8080:80` 把宿主机 8080 映射到容器 80，`--port=1000-2000:1000-2000` 映射一段大小相同的端口范围。未指定协议时 TCP 和 UDP 都会转发。可以重复该参数，或用逗号分隔多个条目。

### 禁用 IPv6

`--disable-ipv6`，键 `disable_ipv6`。在容器内关闭 IPv6。在 NAT 模式下，只要内核支持 IPv6 NAT，容器通常同时获得 IPv6 和 IPv4；不支持时 App 会强制打开此开关。在主机模式下，该设置会作用于宿主机自身的网络栈，可能破坏手机上的 VPN 应用。

### DNS 服务器

`--dns=SERVER[,SERVER...]`，键 `dns_servers`。不设置时，NAT、主机和无模式的容器使用 Cloudflare 和 Google 的公共解析器。网关和 Macvlan 容器使用其 LAN 的 DHCP 服务器下发的 DNS；当网关负责过滤广告或解析本地名称时，这正是你想要的，所以只有确实要覆盖它时才设置这里。

## 集成与硬件

### Android 存储

`--enable-android-storage`，键 `enable_android_storage`。仅限 Android。把手机的共享存储挂载到容器内的 `/storage/emulated/0`，这样你的照片、下载内容和文档在 Linux 中也能访问。

<a id="hardware-access-mode"></a>

### 硬件访问

`--hw-access`，键 `enable_hw_access`。把宿主机真实的 `/dev` 交给容器：GPU、USB 设备、传感器、输入设备，以及所有块设备，包括分区和 SD 卡。容器表现得像一台拥有这些硬件的 Linux 机器。需要 USB 外设、在 Linux 上需要带 GPU 加速的桌面，或需要任何 GPU 开关覆盖不到的设备节点时，就打开它。

> [!CAUTION]
>
> 这里面包含裸块设备。容器内一次错误的 `dd` 或 `mkfs` 会毁掉手机的分区。只对信任的容器使用它，并且不要上报在开启它时出现的问题。App 要求你输入确认语句，正是出于这个原因。

即使打开硬件访问，六个宿主机虚拟终端 `tty1` 到 `tty6` 仍然隐藏，因为 systemd 容器否则会把登录提示打到宿主机控制台上。`--allow-vts` 可以暴露它们，App 没有对应开关。

### GPU 访问

`--gpu`，键 `enable_gpu_mode`。折中方案。容器保留私有的 `/dev`，只把 GPU 渲染节点镜像进去，因此程序可以使用 GPU，而宿主机上的其他设备都不会暴露。镜像进来的节点属于 root 所在的 `droidspaces-gpu` 组；用 `usermod` 把其他用户加入该组。硬件访问也会一并打开它。

已知支持的 GPU 系列：Intel、AMD 和多数 ARM SoC 上的 Mesa DRI 渲染节点，高通 Adreno（`kgsl`），ARM Mali，NVIDIA，PowerVR，以及这些驱动所需的 DMA heap。

### 配置 Termux:X11

`--x11`，键 `enable_x11`。Android：容器启动时启动 Termux:X11 服务，把它的 socket 挂载进容器，并为每个登录 shell 设置 `DISPLAY`。需要 Termux 和 Termux:X11 两个应用，以及[显示指南](./Graphics-and-Audio.md)里的一次性设置。Linux：挂载宿主机的 X11 socket，让容器内的图形程序显示在你的桌面上。

### Anland 显示

`--anland`，键 `enable_anland`。仅限 Android。为容器启动 Anland 显示守护进程，这样为 Anland 打过补丁的 Wayland 合成器就能把桌面显示在 Anland 应用里。需要 GPU 访问、容器内打过补丁的合成器，以及手机上的 Anland 应用；它会取代 Termux:X11，而不是叠加。[Anland 指南](./Anland.md)有完整步骤。

### 配置 VirGL 3D 加速

`--virgl`，键 `enable_virgl`。仅限 Android。在 Termux 中与 Termux:X11 一起启动 VirGL 服务，并在容器内设置 `GALLIUM_DRIVER=virpipe`，让 OpenGL 程序在手机的 GPU 上渲染，而不是用软件渲染。这是 Mali 和 PowerVR GPU 上的正确选择。在高通设备上，容器内配合 GPU 访问使用 Turnip 更快；[显示指南](./Graphics-and-Audio.md)对两者做了对比。

### 配置 PulseAudio

`--pulse-audio`，键 `enable_pulseaudio`。仅限 Android。在 Termux 中启动 PulseAudio 服务，把它的 socket 挂载进容器并设置 `PULSE_SERVER`，让容器内任何播放声音的程序都从手机扬声器出声。

## 资源限制

`--memory=SIZE`、`--cpus=COUNT` 和 `--pids-limit=N`，键分别为 `memory_limit`、`cpu_quota`（配合 `cpu_period`）和 `pids_limit`。默认关闭；容器共享设备的全部资源。

- **限制内存**：`--memory=512M` 或 `--memory=2G`。容器无法越过该上限使用设备的 swap，因此到达上限时，是容器自己的 OOM killer 终止容器内的某个进程，而不是让手机随着 zram 被填满而变慢。容器内的一切，包括 `free` 和 fastfetch，都会依据该上限报告用量，并且看不到 swap。
- **允许 swap**：`--allow-swap`，键 `allow_swap`。让受内存限制的容器越过上限使用设备的 swap，就像设置上限之前那样，并让它看到设备的 swap。`droidspaces info` 和 `show --json` 会显示运行中的容器是否可以使用 swap。
- **限制 CPU**：`--cpus=1.5` 给容器一个半核心的计算时间，分摊到所有核心上。
- **限制进程数**：容器同时可以拥有的最大进程和线程数，最小为 16。一个 systemd 发行版需要几百个。

每个限制都需要内核中对应的 cgroup 控制器：`CONFIG_MEMCG`、`CONFIG_CFS_BANDWIDTH` 和 `CONFIG_CGROUP_PIDS`。内核无法执行某项限制时，App 会把它置灰并指出缺少的选项；CLI 会在启动时跳过它并给出警告。原厂 Android 内核常常缺少 CPU 和进程这两项。`droidspaces info` 会打印当前生效的限制和当前用量。

有一个缺口：直接通过 `sysinfo()` 向内核查询、而不读取 `/proc` 的程序，看到的仍是宿主机的内存和负载。OpenWrt 的 LuCI 就是这样。限制本身依然照常执行。

## 安全与启动

### SELinux 宽容模式

`--selinux-permissive`，键 `selinux_permissive`。仅限 Android 和受支持的 Linux 发行版。在容器运行期间把宿主机的 SELinux 切换为宽容模式，并在最后一个请求它的容器停止后恢复为强制模式。

<a id="sandboxing"></a>

### 允许沙箱

`--allow-sandboxing`，键 `allow_sandboxing`。默认关闭。当容器内的程序要构建自己的沙箱时打开：rootless Docker 或 Podman、Flatpak、bwrap、Firefox、Chromium。它们都会创建用户命名空间，并在其中挂载自己的 `/proc`，而默认配置的容器会同时拦截这两步。打开后，用户命名空间被允许，容器会获得内核为此所需的干净 `/proc` 和只读 `/sys` 实例，位于 `/run/droidspaces/` 下。

代价是容器内的 root 可以通过那份干净的 `/proc` 触达宿主机实时的 sysctl 树，因此这相当于对容器 root 用户的信任声明。需要 `CONFIG_USER_NS`，`droidspaces check` 会把缺失报告为 user namespace 支持缺失。容器内使用 Docker 时，请在 `/etc/docker/daemon.json` 中加入 `{"userns-remap": "default"}` 并重启它。

### 易失模式

`--volatile`，键 `volatile_mode`。所有写入都进入 RAM，并在容器停止时丢弃；rootfs 永远不会改变。适合试用软件包、每次启动都要干净环境，或者故意搞坏东西。需要内核支持 OverlayFS。在 Android 上需要稀疏镜像 rootfs，因为多数 Android 内核上的 OverlayFS 拒绝以 f2fs 目录作为基底。

### 强制使用 Cgroup V1

`--force-cgroupv1`，键 `force_cgroupv1`。即使宿主机提供 v2，也让容器使用旧版 cgroup v1 层级。用于旧的 Android 内核（4.9 及更早），它们的 cgroup v2 缺少 systemd 期望的控制器，会导致 systemd 误判环境并启动失败。除非 systemd 容器在这类内核上无法启动，否则不要打开。

### 特权模式

`--privileged=TAG[,TAG...]`，键 `privileged`。逐个移除安全防线：

- `nomask`：不遮蔽 `/proc` 和 `/sys`。除非打开硬件访问，`/sys` 仍为只读。
- `nocaps`：不丢弃任何 capabilities；容器内的 root 是完整的 root。
- `noseccomp`：不做系统调用过滤。
- `shared`：容器内的挂载会传播到宿主机。
- `full`：以上全部。

每一项都是容器隔离上的一个洞。日常使用都不需要它们；Flatpak、bwrap 和 rootless Docker 单独用允许沙箱就够了。开启特权模式时发现的问题我们不予受理，App 也会要求你确认这一点。

### 开机自启动

键 `run_at_boot`，配合 `run_at_boot_priority`。仅限 App。手机开机时、在锁屏之前，通过 App 安装的启动模块拉起容器。容器按 **设置**、**自启动优先级** 中设定的顺序启动；把网关容器排在使用它做路由的容器之前。

## 高级选项

### 环境变量

`--env=PATH`，键 `env_file`。一个由 `KEY=value` 行组成的文件，每行一个，`#` 开头为注释。这些变量会为容器的 init 以及每个 `enter` 和 `run` 会话设置。App 会替你编辑该文件。

### Bind 挂载

`--bind=SRC:DEST[:ro]`，键 `bind_mounts`。让宿主机的目录在容器内 `DEST` 处可见，加 `:ro` 则为只读。可以重复该参数，或用逗号分隔多个条目。目标必须是容器内的绝对路径，且不能是 `/`。启动时缺失的源会被跳过并给出警告，而不是让启动失败。

### 自定义 init 二进制

`--init=PATH`，键 `custom_init`。用来代替 `/sbin/init` 作为 PID 1 运行的程序。适用于 init 放在别处的发行版，或在调试无法启动的 rootfs 时以 shell 作为 PID 1 启动。

### Termux:X11 额外参数与 VirGL 额外参数

`--tx11-flags="..."`，键 `tx11_extra_flags`；`--virgl-flags="..."`，键 `virgl_extra_flags`。传给 Droidspaces 启动的 Termux:X11 和 VirGL 服务的额外参数。只有在对应开关打开时，App 才会显示它们。

## 存储

安装容器时选定一次。

**稀疏镜像**：rootfs 存放在一个 ext4 镜像文件里，由 Droidspaces 在启动时 loop 挂载。CLI 上对应 `--rootfs-img=PATH`，也可以接受块设备。无论宿主机使用什么文件系统，镜像的行为都一致，这在 Android 的 f2fs 上很重要；迁移容器时也只有一个文件需要复制。镜像的超级块表明需要时，会用 `e2fsck` 检查。你选择的大小是上限；文件随容器占用空间增长。App 之后可以 **调整稀疏镜像容量**，也可以用 **迁移至稀疏镜像** 把目录形式的容器转换过去。

**目录**：rootfs 就是一个普通目录，CLI 上是 `--rootfs=PATH`。从宿主机查看更简单。在 Android 上，只有在无法使用镜像时才用它。

**存储位置**：默认内部存储，或你选择的任何文件夹，包括 SD 卡和 U 盘。无法容纳目录形式 rootfs 的位置会强制使用稀疏镜像。

## 没有开关的设置项

**init 系统：** 任何能作为 PID 1 运行的 init 都可以：systemd、OpenRC、runit、s6、SysVinit、OpenWrt 的 procd。容器的 `/sbin/init` 必须存在，或者你用自定义 init 二进制指定另一个。App 的容器详情能识别 systemd、OpenRC、procd 和 runit，并让你管理它们的服务。

**主机名：** `--hostname=NAME`，键 `hostname`。默认为容器名。

**前台模式：** CLI 上的 `--foreground` 会把你的终端在整个启动过程中附着到容器控制台，这是排查容器起不来的最佳方式。

---

上一页：[Android 应用指南](./Usage-Android-App.md) | 下一页：[Linux CLI](./Linux-CLI.md)
