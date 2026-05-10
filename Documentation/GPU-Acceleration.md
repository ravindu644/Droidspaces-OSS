# Droidspaces GPU Acceleration Guide

This guide provides step-by-step instructions for enabling GPU acceleration in your Droidspaces containers. Whether you are running on an Android device or a Linux desktop, Droidspaces offers multiple ways to leverage hardware acceleration for a smooth graphical experience.

### Quick Navigation

- [**Android Devices**](#android)
    - [01. Termux-X11 + llvmpipe (Software Rendering)](#termux-x11)
    - [02. Termux-X11 + VirGL (Non-Qualcomm GPUs)](#virgl)
    - [03. Turnip (Native Qualcomm/Adreno)](#turnip)
- [**Linux Desktop (AMD/Intel)**](#linux)

---

<a id="android"></a>

## Android

Hardware acceleration on Android is achieved by bridging the container's graphics stack with a host-side X server (Termux-X11). Droidspaces handles the complex mount management and security contexts required to make this seamless.

<a id="termux-x11"></a>

### 01. Termux-X11 + llvmpipe

This method uses **software rendering** via `llvmpipe`. While it doesn't provide full hardware acceleration, it is the most stable way to run GUI applications when a compatible GPU driver isn't available.

#### The "Unified Tmpfs Bridge"
When you enable the **Termux X11** toggle in the Droidspaces app, the following sequence occurs:

1. **Host-side Preparation**: Droidspaces creates a `tmpfs` mount on top of Termux's `/data/data/com.termux/files/usr/tmp` in the host's mount namespace.

2. **Bypassing FBE Encryption**: While a direct bind-mount of `/data/data/com.termux/files/usr/tmp` to the container is possible, it frequently breaks applications like `apt` or any tool performing heavy I/O in `/tmp`. This happens because Termux's data directory is protected by Android's File-Based Encryption (FBE), leading to "Required key not available" (ENOKEY) errors. By bridging the path via `tmpfs`, X11 sockets and temporary files become fully readable and writable by the container.

3. **Bind Mounting**: This "Unified Tmpfs Bridge" is then bind-mounted to the container's `/tmp` directory, enabling seamless communication between the container and the Termux-X11 app.

#### Setup Requirements

- **Termux**: `pkg install x11-repo && pkg install termux-x11`
- **Container**: `sudo apt install mesa-utils` (for testing with `glxgears`)

#### Implementation Steps
1. **Configure Container**: In the Droidspaces app, navigate to your container's configuration.

2. **Enable X11**: Toggle **Termux-X11** to `ON` (**Hardware Access** is not required for software rendering).

3. **Environment**: Add `DISPLAY=:0` to the **Environment Variables** section and save.

4. **Start Container**: Launch your container.

5. **Launch X Server**: Open the Termux app and run:

   ```bash
   termux-x11 :0
   ```

6. **Verify**: Run `glxgears` inside the container terminal. The output will render in the Termux-X11 app.

---

<a id="virgl"></a>

### 02. Termux-X11 + VirGL

This method provides **GPU acceleration for non-Qualcomm devices (Mali/PowerVR)** via a `virglrenderer` bridge. It translates OpenGL calls from the container into commands that the host Android OS can execute.

#### Setup Requirements

- **Termux**: `pkg install x11-repo && pkg install termux-x11 virglrenderer-android`
- **Container**: `sudo apt install mesa-utils` (for testing with `glxgears`)

#### Implementation Steps

1. **Container Configuration**: Enable **Termux-X11** in the Droidspaces container settings. Then, add the following to the **Environment Variables** section:
    ```bash
    DISPLAY=:0
    GALLIUM_DRIVER=virpipe
    ```

2. **Start Container**: Launch your container.

3. **Start VirGL Server**: Open Termux and run the server in the background:
   ```bash
   virgl_test_server_android &
   ```

4. **Start X Server**: In Termux, run:
   ```bash
   termux-x11 :0
   ```

5. **Verify Acceleration**: Run `glxinfo -B` and look for "VirGL" in the renderer string.

> [!TIP]
>
> **If the renderer fails to initialize,** try starting the VirGL server with the Vulkan backend:
>
> `virgl_test_server_android --angle-vulkan &`

---

<a id="turnip"></a>

### 03. Turnip (Native Qualcomm/Adreno)

For Qualcomm Adreno GPUs, Droidspaces supports **native hardware acceleration** using the Turnip driver. This bypasses the need for `virgl` and provides near-native performance.

#### Requirements

- **Custom Mesa driver**: Installed following the instructions from the [Mesa for Android Container repository](https://github.com/lfdevs/mesa-for-android-container).
- **Termux**: `pkg install x11-repo && pkg install termux-x11`

#### Implementation Steps

1. **Install the custom Mesa driver** from the [Mesa for Android Container repository](https://github.com/lfdevs/mesa-for-android-container).

2. **Enable GPU Access**: In the container settings, enable **GPU Access** and **Termux X11**.

3. **Set Display**: Add `DISPLAY=:0` to your environment variables.

4. **Launch Sequence**:
    - Start the container via Droidspaces.
    - Open Termux and run `termux-x11 :0`

5. **Permission Management (Non-Root Users)**:
   If you are using a non-root user, you must grant them access to the GPU device nodes:

   ```bash
   sudo usermod -aG droidspaces-gpu <your_username>
   ```

6. **Start Desktop Environment**: To launch the full XFCE desktop (if installed), run:

   ```bash
   dbus-launch --exit-with-session startxfce4
   ```
---

<a id="linux"></a>

## Linux Desktop (AMD/Intel)

On Linux-based hosts, GPU acceleration works natively with zero additional configuration within Droidspaces.

#### Requirements
- An active X11 or Wayland session on your host.
- Functional GPU drivers (Mesa/Intel/AMD).

#### Implementation Steps

1. **Enable Hardware Access**: Ensure the **Hardware Access** toggle is enabled in your container configuration (or use the `--hw-access` CLI flag).

2. **Xhost Permission**: On your host machine, allow the container to connect to your X server:

   ```bash
   xhost +local:
   ```

3. **Set Display Variable**: Add the host's `DISPLAY` number to the container's environment (usually `:0`):

   ```bash
   echo "DISPLAY=:0" >> /etc/environment
   ```

4. **Run Applications**: GUI applications launched from the container will render natively with full hardware acceleration.
