<!--
title: 显示、音频与桌面
section: Guides
order: 4
desc: 在 Android 和 Linux 上为 Droidspaces 容器配置 GPU 加速、PulseAudio 音效与自动启动的桌面环境。
keywords: gpu, acceleration, droidspaces, termux, x11, virgl, turnip, adreno, pulseaudio, sound, audio, desktop, xfce, container, graphics
-->

# 显示、音频与桌面

在 Android 上，只要打开对应的开关，Droidspaces 就会替你启动 Termux:X11 显示服务、VirGL 服务和 PulseAudio 服务，并在容器内设置 `DISPLAY`、`GALLIUM_DRIVER` 和 `PULSE_SERVER`。打开开关、启动容器，图形程序就会在 Termux:X11 应用中打开，声音从手机扬声器输出。本页涵盖一次性设置、三种渲染方式、声音、开机自启的桌面，以及 Linux 端。像 KDE Plasma 这样的 Wayland 桌面还有第二条路径，见 [Anland](./Anland.md) 页面。

> [!IMPORTANT]
> Droidspaces 不附带 Termux:X11、VirGL 或 PulseAudio。它们是 Termux 的软件包。Droidspaces 负责启动和停止它们，并把它们的 socket 桥接进容器。

## 在 Android 上的一次性设置

1. 安装 [Termux](https://github.com/termux/termux-app) 和 [Termux:X11](https://github.com/termux/termux-x11) 两个应用。请从同一来源（F-Droid 或 GitHub）获取，以确保它们由同一密钥签名。
2. 在 Termux 中运行设置脚本。它会把 Termux:X11、VirGL 和 PulseAudio 安装进 Termux，并针对 Android 的音频栈修改 PulseAudio 的配置：

   ```bash
   curl -fsSL https://github.com/ravindu644/Droidspaces-OSS/raw/refs/heads/dev/scripts/setup-termux.sh | bash
   ```

这样就完成了。之后你不需要在 Termux 里手动启动任何东西。

> [!TIP]
> 仓库中的官方 XFCE 镜像会自动启动 XFCE。安装一个，打开 **配置 Termux:X11**，启动容器并打开 Termux:X11 应用，桌面就在那里。[开机自启的桌面](#开机自启的桌面)一节说明了它的接线方式，方便你为其他桌面做同样的事。

显示相关的变量通过 `/etc/profile.d` 为登录 shell 设置。在 `zsh`、`fish` 或其他会跳过它的 shell 中，请在容器启动后执行一次 `source /run/droidspaces.env`。

## Android 上的三种渲染方式

| | 适用设备 | 速度 | 开关 |
|---|---|---|---|
| **llvmpipe** | 所有设备 | 软件渲染，桌面和 2D 应用够用 | 配置 Termux:X11 |
| **VirGL** | Mali、PowerVR 及其他非高通 GPU | 通过 Termux 中的桥接实现硬件加速 | 配置 Termux:X11 和 配置 VirGL 3D 加速功能 |
| **Turnip** | 高通 Adreno | 接近原生，GPU 由容器内部驱动 | GPU 访问 和 配置 Termux:X11，关闭 VirGL |

### llvmpipe

在容器配置中打开 **配置 Termux:X11**，然后启动容器。当 Termux:X11 应用显示出 X 形光标时，服务就绪。在容器内，`glxgears` 或 `startxfce4` 会在 Termux:X11 中打开。无需其他设置，也不需要硬件访问。

### VirGL

打开 **配置 Termux:X11** 和 **配置 VirGL 3D 加速功能**，然后启动容器。容器内的 OpenGL 调用会发送到 Termux 中的 VirGL 服务，由它运行在手机的 GPU 上。容器内执行 `glxinfo | grep renderer`，正常工作时会显示 VirGL。

如果 VirGL 启动失败，请在 **VirGL 额外参数** 中填入 `--angle-vulkan`，把它切换到 Vulkan 后端。

### Turnip

Turnip 是 Mesa 面向 Adreno 的开源 Vulkan 驱动，配合 Zink 还能提供 OpenGL。容器直接与 GPU 通信，因此需要 GPU 节点以及带 Turnip 的 Mesa 构建。

1. 在容器内安装带 Turnip 的 Mesa，步骤参见 [Mesa for Android Container](https://github.com/lfdevs/mesa-for-android-container)。
2. 在容器配置中打开 **GPU 访问** 和 **配置 Termux:X11**，并确保 **配置 VirGL 3D 加速功能** 已关闭。
3. 在 **高级选项** 下添加两个环境变量：

   ```
   MESA_LOADER_DRIVER_OVERRIDE=kgsl
   TU_DEBUG=noconform
   ```

4. 启动容器。图形程序现在会在 GPU 上渲染。

非 root 用户需要加入 `droidspaces-gpu` 组才能打开 GPU 节点：在容器内执行 `usermod -aG droidspaces-gpu <user>`。

## Android 上的声音

打开 **配置 PulseAudio** 并启动容器。Droidspaces 会在 Termux 中启动 PulseAudio 服务（Android 的音频系统允许它在那里播放），把声音路由到扬声器，将它的 socket 挂载到容器内的 `/tmp/.pulse-socket`，并设置 `PULSE_SERVER`。任何使用 PulseAudio 或 PipeWire 的 PulseAudio 兼容层的程序，声音都会从手机输出。`paplay file.wav` 可以快速测试，`pactl info` 会显示连接状态。

声音能否工作完全取决于手机：Android 版本、厂商的音频 HAL 以及 Termux 的 PulseAudio 构建。在三星 One UI 6.1 及更新版本上，Droidspaces 会预加载音频模块所需的库；你这边无需操作。

## 开机自启的桌面

Droidspaces 会确保 X 服务的 socket `/tmp/.X11-unix/X5` 在容器的 init 到达 `graphical.target` 之前存在，因此 systemd 单元可以在不产生竞争的情况下启动桌面。官方 XFCE 镜像自带这样一个单元；同样的模式适用于任何桌面。

该单元位于 `/etc/systemd/system/xfce-autostart.service`：

```ini
[Unit]
Description=XFCE Autostart
After=graphical.target

[Service]
Type=simple
User=root
ExecCondition=/bin/sh -c "grep -q 'enable_x11=1' /run/droidspaces/container.config"
ExecCondition=/bin/sh -c "test -S /tmp/.X11-unix/X5"
ExecStart=/usr/local/bin/xfce-start
Restart=on-failure

[Install]
WantedBy=graphical.target
```

当 Termux:X11 关闭或 socket 不存在时，这两个条件会让单元保持静默：systemd 会跳过它，而不是报错。

启动脚本 `/usr/local/bin/xfce-start` 会加载 Droidspaces 写入的环境，并以 root 或 `XFCE_USER` 指定的用户身份启动桌面：

```sh
#!/bin/sh

ENV_FILE=/run/droidspaces.env
CONFIG=/run/droidspaces/container.config

if [ -f "$ENV_FILE" ]; then
    . "$ENV_FILE"
    WHITELIST=$(sed -n 's/^export \([A-Za-z_][A-Za-z0-9_]*\)=.*/\1/p' "$ENV_FILE" | tr '\n' ',' | sed 's/,$//')
else
    export DISPLAY=:5
    WHITELIST=DISPLAY
    if grep -q 'enable_pulseaudio=1' "$CONFIG" 2>/dev/null; then
        export PULSE_SERVER=unix:/tmp/.pulse-socket
        WHITELIST="$WHITELIST,PULSE_SERVER"
    fi
    if grep -q 'enable_virgl=1' "$CONFIG" 2>/dev/null; then
        export GALLIUM_DRIVER=virpipe
        WHITELIST="$WHITELIST,GALLIUM_DRIVER"
    fi
fi

if [ -n "$XFCE_USER" ]; then
    exec su -l -w "$WHITELIST" "$XFCE_USER" -c 'exec /usr/bin/startxfce4'
else
    exec /usr/bin/startxfce4
fi
```

要以普通用户运行桌面，请在容器内用 `useradd -m <user>` 创建一个用户，并在容器配置的 **环境变量** 中设置 `XFCE_USER=<user>`。`su -l -w` 只会把显示和音频相关的变量传入该用户的会话。

如果要换成其他桌面，请把这两个文件复制成新名字，把启动脚本里的 `startxfce4` 替换成该桌面的启动命令（例如 `startplasma-x11`），然后用 `systemctl enable` 启用该单元。`XFCE_USER` 这个变量名只是一个约定，你可以随意命名。

## Linux

在 Linux 宿主机上，GPU 加速不需要桥接：容器会像其他程序一样通过 Mesa 使用宿主机的 GPU。

1. 打开 **硬件访问**，或只映射渲染节点时打开 **GPU 访问**，对应 CLI 上的 `--hw-access` 或 `--gpu`。
2. 允许容器访问你的 X 服务：在宿主机上执行 `xhost +local:`。
3. 用 `--x11` 启动容器（它会挂载宿主机的 X socket），并在容器的环境文件中设置 `DISPLAY=:0`，或宿主机实际使用的值。

这样，在容器中启动的图形程序就会带着硬件加速显示在你的桌面上。如果你把宿主机的 PulseAudio 或 PipeWire socket 绑定挂载进去，声音也会通过它输出；Droidspaces 在 Linux 上不管理音频。

---

上一页：[Linux CLI](./Linux-CLI.md) | 下一页：[Anland](./Anland.md)
