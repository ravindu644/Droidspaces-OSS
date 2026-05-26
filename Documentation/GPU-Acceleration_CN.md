<!--
title: Droidspaces GPU 加速指南
section: 指南
order: 2
desc: 在 Android 和 Linux 上为 Droidspaces 容器启用 GPU 加速。涵盖 Termux X11、VirGL、用于原生 Adreno 的 Turnip，以及桌面 GPU 直通。
keywords: gpu, acceleration, droidspaces, termux, virgl, turnip, adreno, container, graphics
-->

# Droidspaces GPU 加速指南

本指南提供了在 Droidspaces 容器中启用 GPU 加速的逐步说明。无论你是在 Android 设备上还是在 Linux 桌面上运行，Droidspaces 都提供多种方式来利用硬件加速，以获得流畅的图形体验。

### 快速导航

- [**Android 设备**](#android)  
    - [01. Termux-X11 + llvmpipe（软件渲染）](#termux-x11)  
    - [02. Termux-X11 + VirGL（非 Qualcomm GPU）](#virgl)  
    - [03. Turnip（原生 Qualcomm/Adreno）](#turnip)  
- [**Linux 桌面（AMD/Intel）**](#linux)  

---

<a id="android"></a>

## Android

在 Android 上的硬件加速是通过将容器的图形栈与宿主端 X 服务器（Termux-X11）桥接来实现的。Droidspaces 处理了实现此无缝衔接所需的复杂挂载管理和安全上下文。

> [!TIP]
>
> 如果你想享受开箱即用的 XFCE 桌面环境体验，可以从官方的 [Droidspaces Rootfs Builder Releases](https://github.com/Droidspaces/Droidspaces-rootfs-builder/releases) 下载我们预配置的 XFCE 压缩包。

<a id="termux-x11"></a>

### 01. Termux-X11 + llvmpipe

此方法通过 `llvmpipe` 使用**软件渲染**。虽然它不提供完整的硬件加速，但在没有兼容 GPU 驱动可用时，这是运行 GUI 应用程序的最稳定方式。

#### "统一 Tmpfs 桥接"
当你在 Droidspaces App 中启用 **Termux X11** 开关时，会发生以下流程：

1. **宿主端准备**：Droidspaces 在宿主端的挂载命名空间中，于 Termux 的 `/data/data/com.termux/files/usr/tmp` 之上创建一个 `tmpfs` 挂载。

2. **绕过 FBE 加密**：虽然可以直接将 `/data/data/com.termux/files/usr/tmp` 绑定挂载到容器中，但这经常会导致 `apt` 等应用程序或任何在 `/tmp` 中进行大量 I/O 操作的工具出现问题。这是因为 Termux 的数据目录受到 Android 基于文件的加密（FBE）保护，导致出现"Required key not available"（ENOKEY）错误。通过 `tmpfs` 桥接该路径，X11 socket 和临时文件将对容器变为完全可读可写。

3. **绑定挂载**：此"统一 Tmpfs 桥接"随后被绑定挂载到容器的 `/tmp` 目录，从而实现容器与 Termux-X11 App 之间的无缝通信。

#### 设置要求

- **Termux**：`pkg install x11-repo && pkg install termux-x11`
- **容器**：`sudo apt install mesa-utils`（用于使用 `glxgears` 测试）

#### 实施步骤
1. **配置容器**：在 Droidspaces App 中，导航到容器的配置页面。

2. **启用 X11**：将 **Termux-X11** 开关切换为 `ON`（软件渲染不需要 **Hardware Access**）。

3. **环境变量**：在**环境变量**部分添加 `DISPLAY=:0` 并保存。

4. **启动容器**：启动你的容器。

5. **启动 X 服务器**：打开 Termux App 并运行：

   ```bash
   termux-x11 :0
   ```

6. **验证**：在容器终端内运行 `glxgears`。输出将渲染在 Termux-X11 App 中。

7. **启动桌面环境**：要启动完整的 XFCE 桌面（如果已安装），运行：

   ```bash
   dbus-launch --exit-with-session startxfce4
   ```

---

<a id="virgl"></a>

### 02. Termux-X11 + VirGL

此方法通过 `virglrenderer` 桥接为**非高通设备（Mali/PowerVR）** 提供 **GPU 加速**。它将容器中的 OpenGL 调用转换为宿主 Android 操作系统可以执行的命令。

#### 设置要求

- **Termux**：`pkg install x11-repo && pkg install termux-x11 virglrenderer-android`
- **容器**：`sudo apt install mesa-utils`（用于使用 `glxgears` 测试）

#### 实施步骤

1. **容器配置**：在 Droidspaces 容器设置中启用 **Termux-X11**。然后，在**环境变量**部分添加以下内容：
    ```bash
    DISPLAY=:0
    GALLIUM_DRIVER=virpipe
    ```

2. **启动容器**：启动你的容器。

3. **启动 VirGL 服务器**：打开 Termux 并在后台运行服务器：
   ```bash
   virgl_test_server_android &
   ```

4. **启动 X 服务器**：在 Termux 中运行：
   ```bash
   termux-x11 :0
   ```

5. **验证加速**：运行 `glxinfo -B` 并在渲染器字符串中查找"VirGL"。

6. **启动桌面环境**：要启动完整的 XFCE 桌面（如果已安装），运行：

   ```bash
   dbus-launch --exit-with-session startxfce4
   ```

> [!TIP]
>
> **如果渲染器初始化失败，** 请尝试使用 Vulkan 后端启动 VirGL 服务器：
>
> `virgl_test_server_android --angle-vulkan &`

---

<a id="turnip"></a>

### 03. Turnip（原生高通/Adreno）

对于高通 Adreno GPU，Droidspaces 支持使用 Turnip 驱动实现**原生硬件加速**。这绕过了对 `virgl` 的需求，并提供接近原生的性能。

#### 要求

- **自定义 Mesa 驱动**：按照 [Mesa for Android Container 仓库](https://github.com/lfdevs/mesa-for-android-container) 中的说明进行安装。
- **Termux**：`pkg install x11-repo && pkg install termux-x11`

#### 实施步骤

1. 从 [Mesa for Android Container 仓库](https://github.com/lfdevs/mesa-for-android-container) **安装自定义 Mesa 驱动**。

2. **启用 GPU 访问**：在容器设置中，启用 **GPU Access** 和 **Termux X11**。

3. **设置显示变量**：将 `DISPLAY=:0` 添加到你的环境变量中。

4. **启动顺序**：
    - 通过 Droidspaces 启动容器。
    - 打开 Termux 并运行 `termux-x11 :0`

5. **权限管理（非 Root 用户）**：
   如果你使用的是非 root 用户，必须授予他们对 GPU 设备节点的访问权限：

   ```bash
   sudo usermod -aG droidspaces-gpu <your_username>
   ```

6. **启动桌面环境**：要启动完整的 XFCE 桌面（如果已安装），运行：

   ```bash
   dbus-launch --exit-with-session startxfce4
   ```

> [!TIP]
> **如果你遇到任何与 DRI3 相关的问题，** 请尝试编辑 `/data/adb/modules/droidspaces/etc/droidspaces.te` 并取消注释以下行：
>
> `allow untrusted_app_27 droidspacesd fd use`

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
