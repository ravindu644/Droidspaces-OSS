<!--
title: Linux CLI 指南
section: 指南
order: 5
desc: Droidspaces Linux CLI 完整参考手册。涵盖每个命令、参数和配置选项的说明。
keywords: droidspaces, cli, linux, container, command, line, reference, bind, mount, nat, networking
-->

# Linux CLI 指南

在 Linux 上通过命令行使用 Droidspaces 的完整指南。

> [!TIP]
>
> **在 Android 上使用 CLI：** 所有命令行参数在 Android 上的用法完全一致。
>
> 通过 App 安装后端后，`droidspaces` 二进制文件位于 `/data/local/Droidspaces/bin/droidspaces`。
>
> 此外，你可以随时运行以下命令，离线查看完整的交互式、**更高级**的命令行文档：
> `droidspaces docs`

---

## 快速导航

[1. 入门指南](#getting-started)  
[2. 命令参考](#command-reference)  
[3. 选项与参数](#options-flags)  
[4. 配置文件](#configs)  
[5. 常见工作流](#common-workflows)  
[6. 高级用法与生命周期](#advanced-usage)  
[7. 系统要求](#system-requirements)

---

<a id="getting-started"></a>
## 1. 入门指南

### 启动你的第一个容器
```bash
# 从 rootfs 目录启动
sudo droidspaces --rootfs=/path/to/rootfs start

# 从 ext4 镜像启动
sudo droidspaces --name=mycontainer --rootfs-img=/path/to/rootfs.img start
```

### 进入容器
```bash
# 以 root 身份进入
sudo droidspaces --name=mycontainer enter

# 以指定用户身份进入
sudo droidspaces --name=mycontainer enter username
```

### 停止容器
```bash
# 停止单个容器
sudo droidspaces --name=mycontainer stop

# 停止多个容器
sudo droidspaces --name=web,db,app stop
```

---

<a id="command-reference"></a>
## 2. 命令参考

| 命令 | 说明 |
|---------|--------|
| `start` | 启动一个新容器。需指定 `--rootfs` 或 `--rootfs-img`。 |
| `stop` | 优雅地关闭一个或多个容器。 |
| `restart` | 快速重启（200 毫秒内），保留 loop 挂载。 |
| `enter [user]` | 在运行中的容器内打开交互式 shell。 |
| `run <cmd>` | 执行单个命令而不打开完整 shell。使用 `-u`/`--user` 以指定容器用户身份运行。 |
| `info` | 显示容器的详细技术信息。 |
| `show` | 以表格形式列出所有当前运行中的容器。 |
| `scan` | 检测并注册孤立/未跟踪的容器。 |
| `check` | 验证系统和内核要求。 |
| `docs` | 打开交互式终端文档。 |
| `help` | 显示帮助信息。 |
| `version` | 输出版本字符串。 |

---

<a id="options-flags"></a>
## 3. 选项与参数

### Rootfs 选择

| 选项 | 简写 | 说明 |
|--------|------|-------------|
| `--rootfs=PATH` | `-r` | rootfs 目录的路径。必须包含 `/sbin/init`。 |
| `--rootfs-img=PATH` | `-i` | ext4 rootfs 镜像文件或块设备的路径。将自动挂载。 |

*注意：这两者是互斥的。使用 `--rootfs-img` 时，`--name` 为必填项。*

### 容器标识与配置

| 选项 | 简写 | 说明 |
|--------|------|-------------|
| `--name=NAME` | `-n` | 容器的唯一名称。在基于目录的 rootfs 模式下若省略则自动生成。 |
| `--hostname=NAME` | `-h` | 设置容器的主机名。默认为容器名称。 |
| `--conf=PATH` | `-C` | 直接从配置文件加载容器配置。 |
| `--reset` | | 将容器配置重置为默认值（保留名称和 rootfs 路径）。 |

### 网络

| 选项 | 简写 | 说明 |
|--------|------|-------------|
| `--net=MODE` | | 网络模式：`host`（默认）、`nat` 或 `none`。 |
| `--port HOST:CONT[/proto]` | | 将主机端口转发到容器（NAT 模式）。支持 TCP/UDP。 |
| `--dns=SERVERS` | `-d` | 自定义 DNS 服务器，逗号分隔。示例：`--dns=1.1.1.1,8.8.8.8` |
| `--disable-ipv6` | | 禁用 IPv6 网络支持（仅限 Host 模式）。 |

### 功能开关

| 选项 | 简写 | 说明 |
|--------|------|-------------|
| `--foreground` | `-f` | 启动时附加到容器控制台以查看 init 日志。 |
| `--volatile` | `-V` | 临时模式。更改存储在 RAM 中，退出后丢失。 |
| `--hw-access` | `-H` | 暴露主机硬件（GPU、USB 等）。自动检测 GPU 组 ID 并在容器内创建匹配的组。挂载 X11 socket 以支持 GUI 应用（Android 上为 Termux X11，Linux 上为 `/tmp/.X11-unix`）。参见[安全警告](./Features.md#hardware-access-mode)。 |
| `--gpu` | | 仅启用 GPU 加速。扫描主机 `/dev` 中的已知 GPU 节点，并仅将其安全映射到容器中，不暴露其他主机硬件。（若已传入 `-H`，此选项将被忽略）。 |
| `--termux-x11`| `-X` | 挂载 X11 socket 以支持 Termux-X11 显示（仅限 Android）。 |
| `--enable-android-storage`| | 挂载 `/storage/emulated/0`（仅限 Android）。 |
| `--selinux-permissive` | | 在容器会话期间将主机 SELinux 设置为宽容模式。 |
| `--force-cgroupv1` | | 强制使用旧版 Cgroup V1 层级。若主机内核存在损坏或不完整的 Cgroups V2 实现（常见于旧版 Android 4.x 内核），则需要此选项。 |
| `--privileged=TAGS` | | 放宽安全保护。接受逗号分隔的标签列表：`nomask`、`nocaps`、`noseccomp`、`shared`、`unfiltered-dev`、`full`。请极其谨慎地使用。 |

### 绑定挂载

| 选项 | 简写 | 说明 |
|--------|------|-------------|
| `--bind-mount=S:D` | `-B` | 将主机目录 `S` 挂载到容器路径 `D`。 |

**格式：**
- 多个挂载：`-B /src1:/dst1,/src2:/dst2` 或 `-B /src1:/dst1 -B /src2:/dst2`
- 最大限制：每个容器最多 16 个挂载。
- 不存在的主机路径将被跳过并发出警告。

---

<a id="configs"></a>
## 4. 配置文件

与其完全依赖冗长的命令行参数，Droidspaces 允许你在 `.config` 文件中定义容器环境，并使用 `--conf` 参数加载它。

以下是配置文件中每个受支持键的参考说明：

```ini
# Droidspaces 容器配置
# 自动生成 - 修改可能被覆盖

# 容器的唯一标识符
name=ubuntu

# 容器环境的主机名
hostname=ubuntu-devbox

# rootfs 目录或 .img 文件的绝对路径
rootfs_path=/home/user/ubuntu-24.04-rootfs.img

# 存储容器 PID 文件的自定义路径
pidfile=/var/lib/Droidspaces/Pids/ubuntu.pid

# 要绑定挂载的主机目录列表，逗号分隔 (src:dest)
bind_mounts=/home/user:/mnt/host,/tmp:/mnt/tmp

# 包含要加载的环境变量的文件的绝对路径
env_file=/path/to/env.list

# 唯一 UUID（自动生成，请勿手动修改）
uuid=d88107dab4ef48a8874a93897188982d

# 自定义 DNS 服务器（逗号分隔）
dns_servers=1.1.1.1,8.8.8.8

# -------- 布尔标志 (0 或 1) --------

# 禁用 IPv6 网络
disable_ipv6=0

# 向容器暴露主机硬件节点 (/dev)
enable_hw_access=0

# 自动检测并安全映射 GPU 节点，无需完整硬件访问权限
enable_gpu_mode=0

# Android：设置 Termux X11 socket
enable_termux_x11=0

# Android：设置 Android 内部共享存储挂载
enable_android_storage=0

# 在启动期间将主机 SELinux 策略设置为宽容模式
selinux_permissive=0

# 临时模式：退出后更改丢失
volatile_mode=1

# 在前台运行容器而非 fork 到后台
foreground=0

# ----------------------------------------
# Android App 配置
# CLI 引擎无法识别的任何行将被安全地保留在
# 配置文件底部，并传回给宿主端。
```

---

<a id="common-workflows"></a>
## 5. 常见工作流

### 使用配置文件运行
无需传入大量参数，你可以从 `.config` 文件加载所有配置：
```bash
sudo droidspaces --conf=./my-container.config start
```

### 持久化开发环境
```bash
sudo droidspaces \
  --name=dev \
  --rootfs=/path/to/ubuntu-rootfs \
  --hostname=devbox \
  --bind-mount=/home/user/projects:/workspace \
  start
```

### NAT 隔离与端口转发
```bash
sudo droidspaces \
  --name=server \
  --rootfs-img=/path/to/rootfs.img \
  --net=nat \
  --port=8080:80 \
  start
```

### 临时测试环境
```bash
sudo droidspaces --name=test --rootfs=/path/to/rootfs --volatile start
```

### 一次性命令
```bash
sudo droidspaces --name=mycontainer run uname -a
# 使用 sh -c 执行管道命令：
sudo droidspaces --name=mycontainer run sh -c "ps aux | grep init"
# 使用 -u/--user 以特定用户身份运行：
sudo droidspaces --name=mycontainer -u myuser run whoami
sudo droidspaces --name=mycontainer -u myuser run env
sudo droidspaces --name=mycontainer -u myuser run sh -c "id && env"
```

### GPU 加速
确保仅安全暴露 GPU 节点（而非所有主机硬件）：
```bash
sudo droidspaces --name=gpu-app --rootfs=/path/to/rootfs --gpu start
```

### 深度定制（特权模式）
适用于需要非掩码访问的场景：
```bash
sudo droidspaces --name=privileged-box --rootfs=/path/to/rootfs --privileged=full start
# 或混合使用标签：
sudo droidspaces --name=dev-box --rootfs=/path/to/rootfs --privileged=nocaps,noseccomp start
```

---

<a id="advanced-usage"></a>
## 6. 高级用法与生命周期

### 容器恢复
如果容器是在当前会话之外启动的，或者其宿主端 PID 文件 / 配置文件丢失或损坏，可以使用 `scan` 从容器隔离的 `/run` 内存中恢复它：
```bash
sudo droidspaces scan
```

---

<a id="system-requirements"></a>
## 7. 系统要求

始终运行内置检查器以验证你的内核是否支持所需的 namespace 和功能：
```bash
sudo droidspaces check
```

有关技术要求的深入探讨，请参阅[内核配置指南](./Kernel-Configuration.md)。

---

## 下一步
- [功能深入解析](./Features.md)
- [故障排除](./Troubleshooting.md)
- [Android App 使用指南](./Usage-Android-App.md)
