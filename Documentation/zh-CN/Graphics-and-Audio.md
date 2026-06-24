<!--
title: 显示、音频与桌面
section: 指南
order: 2
desc: 在 Android 和 Linux 上为 Droidspaces 容器配置 GPU 加速、PulseAudio 音效与桌面环境自动启动。
keywords: gpu, acceleration, droidspaces, termux, virgl, turnip, adreno, pulseaudio, sound, audio, desktop, xfce, container, graphics
-->

# Droidspaces 显示、音频与桌面指南

本指南介绍如何在 Droidspaces 容器中配置显示、GPU 加速、音效（PulseAudio）以及桌面环境自动启动。在 Android 上，自 **v6.3.0** 起，X 服务器、VirGL 服务器和 PulseAudio 守护进程均会在容器启动时自动拉起 - 无需在 Termux 中手动执行任何命令。

> [!IMPORTANT]
>
> **Droidspaces 不提供 PulseAudio、Termux:X11 或 virglrenderer-android 软件包。** 这些均为上游 Termux 软件包，由安装脚本安装。Droidspaces 仅负责管理其生命周期（启动、套接字桥接、环境变量注入）。如遇音频质量、设备兼容性、崩溃或任何这些软件包的渲染问题，请向 [Termux packages](https://github.com/termux/termux-packages) 项目反馈，而非 Droidspaces。

### 快速导航

- [**通用要求（Android）**](#requirements)
- [**Android 显示与 GPU**](#android)
    - [01. Termux-X11 + llvmpipe（软件渲染）](#termux-x11)
    - [02. Termux-X11 + VirGL（非高通 GPU）](#virgl)
    - [03. Turnip（原生高通/Adreno）](#turnip)
- [**Android 音效（PulseAudio）**](#pulseaudio)
- [**桌面环境自动启动**](#de-autoboot)
- [**Linux 桌面（AMD/Intel）**](#linux)

---

<a id="requirements"></a>

## 通用要求（Android）

在设置任何显示、GPU 或音频功能之前，请确保满足以下所有条件：

1. **Droidspaces v6.3.0 或更高版本** — 更新后，**必须完整重启设备**以应用更新的 SELinux 规则。

2. **必须在设备上安装 [Termux](https://github.com/termux/termux-app) 和 [Termux:X11](https://github.com/termux/termux-x11) 两个应用**。

3. **在 Termux 内运行 Droidspaces 安装脚本**（必须执行 — 安装 Termux:X11、VirGL、PulseAudio 并修补音频配置）：

   ```bash
   curl -fsSL https://github.com/ravindu644/Droidspaces-OSS/raw/refs/heads/dev/scripts/setup-termux.sh | bash
   ```

> [!IMPORTANT]
>
> 此脚本只需运行一次，它会将所有必要的显示和音频依赖项安装到 Termux 中。

---

<a id="android"></a>

## Android 显示与 GPU

自 v6.3.0 起，Droidspaces 在容器启动时会自动启动 Termux:X11 X 服务器（以及已配置的 VirGL 服务器），并通过 `/run/droidspaces.env`（从 `/etc/profile.d/droidspaces_env.sh` 符号链接）将所需环境变量注入容器。

> [!TIP]
>
> **使用 `zsh`、`fish` 或其他非登录 Shell？**
>
> 这些 Shell 可能不会自动读取 `/etc/profile.d`。请在容器启动后手动执行：`source /run/droidspaces.env`
>
> 这将设置 `DISPLAY=:5`，以及（启用 VirGL 时）`GALLIUM_DRIVER=virpipe`。

---

> [!TIP]
> **想要开箱即用的桌面体验？**
>
> 我们的官方 XFCE rootfs 压缩包内置了 **XFCE 自动启动支持**。启用 Termux:X11（或 VirGL）并启动容器后，XFCE 将自动启动并显示在 Termux:X11 应用中 — 无需任何终端命令。
>
> 从 [Rootfs 仓库](./Usage-Android-App.md#rootfs-仓库)（搜索"XFCE"）下载，或直接从 [Droidspaces Rootfs Builder Releases](https://github.com/Droidspaces/Droidspaces-rootfs-builder/releases/latest) 下载。

---

<a id="termux-x11"></a>

### 01. Termux-X11 + llvmpipe

通过 `llvmpipe` 进行软件渲染。兼容性最强的方式 — 适用于任何设备，无论 GPU 厂商。

#### 设置步骤

1. 打开 Droidspaces 应用，进入**编辑容器配置**。
2. 启用 **配置 Termux:X11** 开关并保存。
3. 启动容器。Droidspaces 将自动启动 Termux:X11 X 服务器 — Termux:X11 应用将显示 **"X" 光标**，确认服务器已就绪。
4. 在容器内打开终端，运行任意 GUI 程序：

   ```bash
   glxgears
   ```

   ```bash
   startxfce4
   ```

   窗口将显示在 Termux:X11 应用中。

> [!NOTE]
>
> 软件渲染**不需要**启用 **Hardware Access**。

---

<a id="virgl"></a>

### 02. Termux-X11 + VirGL

通过 `virglrenderer` 桥接为**非高通设备（Mali、PowerVR）**提供硬件加速渲染。将容器中的 OpenGL 调用转换为宿主 Android GPU 可执行的命令。

Droidspaces 会自动启动 X 服务器和 VirGL 服务器，并将 `DISPLAY=:5` 和 `GALLIUM_DRIVER=virpipe` 注入容器环境。

#### 设置步骤

1. 进入**编辑容器配置**。
2. 同时启用 **配置 Termux:X11** 和 **配置 VirGL 3D 加速** 开关并保存。
3. 启动容器。两个服务器将自动启动。Termux:X11 应用就绪后会显示 **"X" 光标**。
4. 在容器内运行任意 GUI 程序：

   ```bash
   glxgears
   ```

   在渲染器字符串中查找 **"VirGL"** 以确认加速已生效。

   ```bash
   startxfce4
   ```

> [!TIP]
>
> **如果 VirGL 渲染器初始化失败**，可在容器配置的 **VirGL 额外参数**字段中传入 Vulkan 后端标志：
>
> `--angle-vulkan`

---

<a id="turnip"></a>

### 03. Turnip（原生高通/Adreno）

使用开源 Turnip Mesa 驱动为**高通 Adreno GPU** 提供接近原生的硬件加速。完全绕过 VirGL，直接访问 GPU。

#### 要求

- 按照 [Mesa for Android Container 仓库](https://github.com/lfdevs/mesa-for-android-container)的说明安装自定义 Mesa 驱动。

#### 设置步骤

1. 按照 [Mesa for Android Container](https://github.com/lfdevs/mesa-for-android-container) 的说明安装自定义 Mesa 驱动。

2. 进入**编辑容器配置**并进行以下设置：
   - 启用 **GPU Access** 和 **配置 Termux:X11**。
   - **禁用** **配置 VirGL 3D 加速** 开关（使用 Turnip 时必须关闭 VirGL）。
   - 添加以下两个环境变量：

     ```
     MESA_LOADER_DRIVER_OVERRIDE=kgsl
     TU_DEBUG=noconform
     ```

3. 启动容器。Droidspaces 自动启动 X 服务器。

4. 在容器内运行 GUI 程序 — Turnip GPU 加速将自动生效。

> [!NOTE]
>
> **权限管理（非 root 用户）：** 如果你使用的是非 root 用户，则必须授予其访问 GPU 设备节点的权限：
`sudo usermod -aG droidspaces-gpu <your_username>`

---

<a id="pulseaudio"></a>

## Android 音效（PulseAudio）

Droidspaces 通过 PulseAudio 将 Android 的音频栈桥接到容器中。启用后，PulseAudio 守护进程以 Termux 用户身份在宿主机上运行（从而使 Android 音频 HAL 授予其访问设备扬声器的权限），其 UNIX 套接字以绑定挂载方式挂载到容器的 `/tmp/.pulse-socket`。环境变量 `PULSE_SERVER=unix:/tmp/.pulse-socket` 将自动注入容器，因此容器内支持 PulseAudio 的应用无需任何额外配置即可发出声音。

> [!WARNING]
>
> PulseAudio 音频直通并非在所有设备上均可用。兼容性取决于 Android 版本、OEM 音频 HAL 实现以及 Termux PulseAudio 构建版本。若你的设备无法正常使用音频，这属于上游软件包在该平台上的已知限制。

#### 要求

- Termux 中必须已安装 PulseAudio - [安装脚本](#requirements)会自动完成此操作。

#### 设置步骤

1. 打开 Droidspaces 应用，进入**编辑容器配置**。
2. 启用 **配置 PulseAudio** 开关并保存。
3. 启动容器。Droidspaces 将：
   - 以 Termux 用户身份启动 PulseAudio 守护进程。
   - 等待套接字 `/tmp/.pulse-socket` 出现后再继续。
   - 执行 `pactl set-default-sink AAudio_sink`，将音频路由至设备扬声器。
   - 将套接字绑定挂载到容器并注入 `PULSE_SERVER`。

   也可以通过 CLI 参数 `--pulse-audio` 启用 PulseAudio。

4. 在容器内安装并运行任意音频应用。由于 `PULSE_SERVER` 已在容器环境中设置，大多数应用无需额外配置即可正常使用：

   ```bash
   # 测试音频输出
   paplay /path/to/sound.wav
   ```

   ```bash
   # 验证 PulseAudio 连接
   pactl info
   ```

> [!NOTE]
>
> PulseAudio 音效**仅适用于 Android**。在 Linux 桌面端，音频直通通过宿主机自身的 PulseAudio/PipeWire 实现，Droidspaces 无需特殊配置。

> [!NOTE]
>
> **三星 One UI 6.1+ 设备：** Droidspaces 在启动 PulseAudio 前会自动通过 `LD_PRELOAD` 注入 `libskcodec.so`，以修复三星固件中 OpenSL ES 音频模块的隐藏依赖问题。无需用户进行任何操作。

---

<a id="de-autoboot"></a>

## 桌面环境自动启动

自 v6.3.0 起，当容器配置中启用了 Termux:X11 时，Droidspaces 保证 X 服务器套接字（`/tmp/.X11-unix/X5`）在容器 init 系统到达 `graphical.target` 之前已就绪，使基于 systemd 的桌面环境自动启动得以无竞争条件地运行。

### 官方 XFCE 压缩包的实现方式

我们的官方 XFCE rootfs 压缩包内置了完整的自动启动配置，以下是其具体实现：

**1. `xfce-autostart.service` systemd 单元**安装于 `/etc/systemd/system/xfce-autostart.service`，并在 `graphical.target` 下启用：

```ini
[Unit]
Description=XFCE Autostart
After=graphical.target

[Service]
Type=simple
User=root
ExecCondition=/bin/sh -c "grep -q 'enable_termux_x11=1' /run/droidspaces/container.config"
ExecCondition=/bin/sh -c "test -S /tmp/.X11-unix/X5"
ExecStart=/usr/local/bin/xfce-start
Restart=on-failure

[Install]
WantedBy=graphical.target
```

两个 `ExecCondition` 守卫确保 XFCE 仅在条件满足时启动：容器配置中必须启用 Termux:X11，且 X 服务器套接字必须实际存在。若任一条件不满足，systemd 将静默跳过该服务，不产生错误或崩溃循环。

**2. `/usr/local/bin/xfce-start` 启动脚本**负责环境变量读取和用户切换：

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

脚本首先尝试读取 `/run/droidspaces.env`（由 Droidspaces 在启动时写入；包含 `DISPLAY=:5`，若启用 VirGL 则含 `GALLIUM_DRIVER=virpipe`，若启用 PulseAudio 则含 `PULSE_SERVER`）。若环境文件因任何原因缺失，则回退到直接读取容器配置并手动构建环境，确保脚本对 init 系统启动顺序具有鲁棒性。

### `XFCE_USER` 变量

默认情况下，`xfce-start` 以 `root` 身份运行 XFCE。若需以非 root 用户运行，请在 Droidspaces 应用的容器**环境变量**配置中设置 `XFCE_USER`：

```
XFCE_USER=youruser
```

设置后，脚本使用 `su -l -w "$WHITELIST"` 切换到该用户，同时仅透传必要的环境变量（`DISPLAY`、`GALLIUM_DRIVER`、`PULSE_SERVER` 等），保持会话干净，避免 root 环境泄漏到用户会话中。

> [!TIP]
>
> 设置 `XFCE_USER` 前，请确保容器内已存在该用户且有有效的主目录。可通过 `useradd -m youruser` 在容器内创建。

---

### 高级用户：为任意桌面环境配置自动启动

你可以将此模式复制到任意桌面环境（Plasma、GNOME、MATE、i3 等）的任意容器中。

**第一步：** 创建启动脚本 `/usr/local/bin/de-start`：

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

DE_CMD="startplasma-x11"   # 替换为你的桌面环境启动命令

if [ -n "$XFCE_USER" ]; then
    exec su -l -w "$WHITELIST" "$XFCE_USER" -c "exec $DE_CMD"
else
    exec $DE_CMD
fi
```

```bash
chmod +x /usr/local/bin/de-start
```

**第二步：** 创建 systemd 服务 `/etc/systemd/system/de-autostart.service`：

```ini
[Unit]
Description=Desktop Environment Autostart
After=graphical.target

[Service]
Type=simple
ExecCondition=/bin/sh -c "grep -q 'enable_termux_x11=1' /run/droidspaces/container.config"
ExecCondition=/bin/sh -c "test -S /tmp/.X11-unix/X5"
ExecStart=/usr/local/bin/de-start
Restart=on-failure

[Install]
WantedBy=graphical.target
```

**第三步：** 启用服务：

```bash
systemctl enable de-autostart.service
```

下次启用 Termux:X11 启动容器时，你的桌面环境将自动出现在 Termux:X11 应用中。

> [!NOTE]
>
> `XFCE_USER` 变量名是我们官方压缩包的约定，你可以在自己的脚本中自由重命名。关键在于 `su -l -w "$WHITELIST"` 模式，它实现了干净的用户切换与环境变量透传。

---

<a id="linux"></a>

## Linux 桌面端（AMD/Intel）

在基于 Linux 的主机上，GPU 加速在 Droidspaces 中原生运行，无需额外配置。

#### 要求
- 主机上有一个活跃的 X11 或 Wayland 会话。
- 正常工作的 GPU 驱动（Mesa/Intel/AMD）。

#### 实施步骤

1. **启用硬件访问**：确保在容器配置中启用了 **Hardware Access** 开关（或使用 `--hw-access` CLI 参数）。

2. **Xhost 权限**：在你的主机上，允许容器连接到你的 X 服务器：

   ```bash
   xhost +local:
   ```

3. **设置显示变量**：将主机的 `DISPLAY` 编号添加到容器的环境中（通常是 `:0`）：

   ```bash
   echo "DISPLAY=:0" >> /etc/environment
   ```

4. **运行应用程序**：从容器中启动的 GUI 应用程序将以完整的硬件加速进行原生渲染。
