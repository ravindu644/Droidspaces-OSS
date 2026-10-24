<!--
title: Linux CLI
section: Guides
order: 3
desc: droidspaces 命令行参考：每条命令、每个参数，以及配置文件中的键。
keywords: droidspaces, cli, linux, container, command, line, reference, flags, config, file
-->

# 命令行参考

`droidspaces` 二进制在 Linux 和 Android 上是同一个，命令和参数也一样。在 Android 上，应用会把它安装到 `/data/local/Droidspaces/bin/droidspaces`，**将 Droidspaces 集成到系统路径中** 设置会把它放进 root shell 的 `PATH`。

参数和命令可以以任意顺序出现，短形式和长形式可以互换，`=` 是可选的：`droidspaces -n web start` 和 `droidspaces start --name=web` 等价。`droidspaces docs` 会在分页器中离线显示同一份参考。

## 命令

| 命令 | 作用 |
|---------|--------------|
| `start` | 启动容器。第一次启动需要 `--name` 和 `--rootfs` 或 `--rootfs-img`；之后会使用已保存的配置。 |
| `stop` | 干净地关闭容器。`--name=a,b,c` 可以停止多个。 |
| `restart` | 停止后再次启动。镜像挂载会保留，因此很快。 |
| `enter [user]` | 在运行中的容器里打开 shell，默认以 root 身份。 |
| `run [--] <cmd>` | 在运行中的容器里执行一条命令然后返回。`--user` 选择账号，`--stdin` 把你的输入管道进去。`run` 之后的内容都属于该命令，`--` 可以让命令本身以短横线开头。 |
| `show` | 列出正在运行的容器。配合 `--json` 时输出 JSON，包含每个容器的发行版、主机名、IP、运行时间、CPU 和内存。 |
| `info` | 显示容器的设置和状态。配合 `--json` 时输出 JSON。 |
| `pid` | 打印容器 init 的 PID，或 `NONE`。 |
| `scan` | 找出宿主侧记录丢失的运行中容器，并重新登记。 |
| `check` | 探测内核是否具备 Droidspaces 使用的一切。配合 `--json` 时输出 JSON，每项探测为 `0` 或 `1`，另有 `requirements_met`。 |
| `daemon` | 启动守护进程。`--foreground` 让它附着在终端上，供服务管理器使用。 |
| `mode` | 如果有守护进程在应答，打印 `daemon`，否则打印 `direct`。 |
| `docs`、`help`、`version` | 内置文档、参数摘要和版本。 |

除 `check`、`docs`、`help`、`version` 和 `mode` 外，每个命令都需要 root。当恰好只有一个容器在运行时，`stop`、`restart`、`enter`、`run`、`info` 和 `pid` 可以省略 `--name`。

如果守护进程在运行，CLI 会把命令交给它，因此 `droidspaces` 组的成员无需 `sudo` 即可使用 CLI。在环境中设置 `DS_NO_PROXY=1` 会让二进制自己完成工作。

## 参数

每个用于配置容器的参数都会在 `start` 时保存到其配置文件中，因此只需提供一次。[容器设置](./Features.md)说明了每个参数的作用和使用场景；这里的表格是语法。

**根文件系统与身份**

| 参数 | 值 | 配置键 |
|------|-------|------------|
| `-r`, `--rootfs` | 含 rootfs 的目录 | `rootfs_path` |
| `-i`, `--rootfs-img` | ext4 镜像文件或块设备 | `rootfs_path` |
| `-n`, `--name` | 容器名；`stop` 时可用逗号分隔多个 | `name` |
| `-h`, `--hostname` | 容器内主机名，默认与容器名相同 | `hostname` |
| `-C`, `--conf`, `--config` | 要加载的配置文件 | |
| `--reset` | 将配置重置为默认值，保留容器名和 rootfs | |
| `--init` | init 二进制，默认 `/sbin/init` | `custom_init` |
| `-E`, `--env` | 由 `KEY=value` 行组成的文件 | `env_file` |
| `-B`, `--bind` | `SRC:DEST[:ro]`，可重复或用逗号分隔 | `bind_mounts` |

**网络**

| 参数 | 值 | 配置键 |
|------|-------|------------|
| `--net` | `nat`（默认）、`gateway`、`macvlan`、`host`、`none` | `net_mode` |
| `--nat-ip` | `172.28.0.0/16` 内的固定地址，仅 NAT | `static_nat_ip` |
| `--upstream` | 发送 NAT 流量所用的接口，逗号分隔，支持通配符 | `upstream_interfaces` |
| `--port` | `[HOST:]CONTAINER[/tcp\|udp]`，范围写作 `a-b:a-b`，可重复，仅 NAT | `port_forwards` |
| `-d`, `--dns` | 解析器，逗号分隔 | `dns_servers` |
| `--disable-ipv6` | | `disable_ipv6` |
| `--gateway` | 充当路由器的运行中容器，网关模式必需 | `gateway_container` |
| `--gateway-net` | LAN 段名称，默认 `lan` | `gateway_net` |
| `--gateway-iface` | LAN 接口在网关内的名称，默认 `eth1` | `gateway_lan_ifname` |
| `--gateway-bridge` | 宿主机网桥名称，默认 `ds-<gateway-net>` | `gateway_bridge` |
| `--macvlan-parent` | 宿主机网卡，Macvlan 模式必需 | `macvlan_parent` |
| `--macvlan-mode` | `bridge`（默认）、`private`、`vepa`、`passthru` | `macvlan_mode` |

**硬件与集成**

| 参数 | 值 | 配置键 |
|------|-------|------------|
| `-H`, `--hw-access` | | `enable_hw_access` |
| `--allow-vts` | 配合 `--hw-access`，保留宿主机的 `tty1` 到 `tty6` 可见 | `allow_vts` |
| `--gpu` | | `enable_gpu_mode` |
| `-X`, `--x11` | | `enable_x11` |
| `--tx11-flags` | Termux:X11 的额外参数 | `tx11_extra_flags` |
| `--virgl` | 仅 Android | `enable_virgl` |
| `--virgl-flags` | VirGL 服务的额外参数 | `virgl_extra_flags` |
| `--pulse-audio` | 仅 Android | `enable_pulseaudio` |
| `--anland` | 仅 Android，通过 Anland 应用使用 Wayland 桌面 | `enable_anland` |
| `--enable-android-storage` | 仅 Android | `enable_android_storage` |

**资源限制、安全与启动**

| 参数 | 值 | 配置键 |
|------|-------|------------|
| `--memory` | 带 `K`、`M`、`G` 或 `T` 的大小，最小 `4M` | `memory_limit`，单位为字节 |
| `--allow-swap` | | `allow_swap` |
| `--cpus` | 核心数，允许小数 | `cpu_quota` 和 `cpu_period`，单位为微秒 |
| `--pids-limit` | 进程数，最小 16 | `pids_limit` |
| `-P`, `--selinux-permissive` | | `selinux_permissive` |
| `--allow-sandboxing` | | `allow_sandboxing` |
| `-V`, `--volatile` | | `volatile_mode` |
| `--force-cgroupv1` | | `force_cgroupv1` |
| `--privileged` | `nomask`、`nocaps`、`noseccomp`、`shared`、`full`，逗号分隔 | `privileged` |

**会话参数**，不会被保存

| 参数 | 作用 |
|------|--------------|
| `-f`, `--foreground` | 在整个启动过程中把容器控制台留在你的终端上。 |
| `-u`, `--user` | `run` 使用的账号。 |
| `--stdin` | 把你的输入转发给 `run` 命令。不加它时命令收不到任何输入。 |
| `--json` | `show`、`info` 和 `check` 的 JSON 输出。 |

## 配置文件

一个普通的 `key=value` 文件，每行一个键，键名来自上面的表格。Droidspaces 按以下顺序查找：`--config` 指定的文件，然后 rootfs 旁边的 `container.config`，最后是工作目录中的 `Containers/<name>/container.config`。工作目录在 Linux 上是 `/var/lib/Droidspaces`，在 Android 上是 `/data/local/Droidspaces`。

```ini
name=web
hostname=web
rootfs_path=/var/lib/Droidspaces/Containers/web/rootfs.img
net_mode=nat
port_forwards=80:80,443:443
bind_mounts=/srv/www:/var/www:ro
memory_limit=1073741824
cpu_quota=200000
cpu_period=100000
allow_sandboxing=1
```

布尔键取 `0` 或 `1`。后端不认识的键（例如 App 的 `run_at_boot`）会原样保留。容器运行时，它启动时使用的配置可以在容器内的 `/run/droidspaces/container.config` 看到。

## 示例

```bash
# 第一次从镜像创建容器，并转发一个端口
sudo droidspaces --name=web --rootfs-img=/srv/web.img --port=8080:80 start

# 之后的启动只需要容器名
sudo droidspaces --name=web start

# 在控制台上观察启动过程
sudo droidspaces --name=web --foreground start

# 以指定用户进入 shell，以及带管道的单次命令
sudo droidspaces --name=web enter alice
sudo droidspaces --name=web run sh -c 'ps aux | grep nginx'

# 把脚本通过管道送进容器
sudo droidspaces --name=web --stdin run sh -s < setup.sh

# 一台 OpenWrt 路由器，以及一个位于它后面的客户端
sudo droidspaces --name=openwrt --rootfs=/srv/openwrt --net=nat start
sudo droidspaces --name=kali --rootfs=/srv/kali --net=gateway --gateway=openwrt start

# 接入有线 LAN 并拥有自己地址的容器
sudo droidspaces --name=ubuntu --rootfs=/srv/ubuntu --net=macvlan --macvlan-parent=eth0 start

# 试用一下，不保留
sudo droidspaces --name=scratch --rootfs=/srv/ubuntu --volatile start

# 以 JSON 查看正在运行的容器
sudo droidspaces --json show
```

## 文件位置

| 路径 | 内容 |
|------|----------|
| `<workspace>/Containers/<name>/` | 容器的配置；对于由 App 安装的容器，还包括其 rootfs |
| `<workspace>/Logs/<name>/` | 每次启动、停止和重启的日志 |
| `<workspace>/Logs/droidspacesd.log` | 守护进程日志 |
| `/mnt/Droidspaces/<name>` | 容器运行期间镜像 rootfs 的挂载位置 |

---

上一页：[容器设置](./Features.md) | 下一页：[显示、音频与桌面](./Graphics-and-Audio.md)
