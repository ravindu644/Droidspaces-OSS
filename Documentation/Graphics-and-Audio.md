<!--
title: Display, Audio & Desktop
section: Guides
order: 2
desc: GPU acceleration, PulseAudio sound, and desktop environment auto-boot for Droidspaces containers on Android and Linux.
keywords: gpu, acceleration, droidspaces, termux, virgl, turnip, adreno, pulseaudio, sound, audio, desktop, xfce, container, graphics
-->

# Droidspaces Display, Audio & Desktop Guide

This guide covers display setup, GPU acceleration, sound (PulseAudio), and desktop environment auto-boot for Droidspaces containers. On Android, as of **v6.3.0**, the X server, VirGL server, and PulseAudio daemon all launch automatically when a container starts - no manual Termux commands required.

> [!IMPORTANT]
>
> **Droidspaces does not ship PulseAudio, Termux:X11, or virglrenderer-android.** These are upstream Termux packages installed by the setup script. Droidspaces only manages their lifecycle (launching, socket bridging, environment injection). If you encounter issues with audio quality, device compatibility, crashes, or rendering problems with any of these packages, report them to the [Termux packages](https://github.com/termux/termux-packages) project, not to Droidspaces.

### Quick Navigation

- [**Universal Requirements (Android)**](#requirements)
- [**Android Display & GPU**](#android)
    - [01. Termux-X11 + llvmpipe (Software Rendering)](#termux-x11)
    - [02. Termux-X11 + VirGL (Non-Qualcomm GPUs)](#virgl)
    - [03. Turnip (Native Qualcomm/Adreno)](#turnip)
- [**Android Sound (PulseAudio)**](#pulseaudio)
- [**Desktop Environment Auto-Boot**](#de-autoboot)
- [**Linux Desktop (AMD/Intel)**](#linux)

---

<a id="requirements"></a>

## Universal Requirements (Android)

Before setting up any display, GPU, or audio feature, ensure all of the following are met:

1. **Droidspaces v6.3.0 or higher** - after updating, a **full reboot is mandatory** to apply the updated SELinux rules.

2. **Both [Termux](https://github.com/termux/termux-app) and [Termux:X11](https://github.com/termux/termux-x11) apps must be installed** on your device.

3. **Run the Droidspaces setup script inside Termux** (mandatory - installs Termux:X11, VirGL, PulseAudio, and patches the audio config):

   ```bash
   curl -fsSL https://github.com/ravindu644/Droidspaces-OSS/raw/refs/heads/dev/scripts/setup-termux.sh | bash
   ```

> [!IMPORTANT]
>
> This script only needs to be run once. It installs all required display and audio dependencies into Termux.

---

<a id="android"></a>

## Android Display & GPU

As of v6.3.0, Droidspaces automatically launches the Termux:X11 X server (and VirGL server, if configured) when a container starts. It also injects the required environment variables into the container via `/run/droidspaces.env`, which is symlinked from `/etc/profile.d/droidspaces_env.sh` for automatic sourcing.

> [!TIP]
>
> **Using `zsh`, `fish`, or another non-login shell?**
>
> These shells may not source `/etc/profile.d` automatically. Run this manually inside your container after booting: `source /run/droidspaces.env`
>
> This sets `DISPLAY=:5` and (if VirGL is enabled) `GALLIUM_DRIVER=virpipe`.

---

> [!TIP]
> **Want an out-of-the-box desktop experience?**
>
> Our official XFCE rootfs tarballs have **built-in XFCE auto-boot support**. When you start the container with Termux:X11 (or VirGL) enabled, XFCE launches automatically and appears in the Termux:X11 app - no terminal commands needed.
>
> Download from the [Rootfs Repository](Usage-Android-App.md#rootfs-repository) (search "XFCE") or directly from [Droidspaces Rootfs Builder Releases](https://github.com/Droidspaces/Droidspaces-rootfs-builder/releases/latest).

---

<a id="termux-x11"></a>

### 01. Termux-X11 + llvmpipe

Software rendering via `llvmpipe`. The most compatible method - works on any device regardless of GPU vendor.

#### Setup

1. Open the Droidspaces app and navigate to **Edit container configuration**.
2. Enable the **Configure Termux:X11** toggle and save.
3. Start the container. Droidspaces will automatically launch the Termux:X11 X server - the Termux:X11 app will display the **"X" cursor**, confirming the server is ready.
4. Open a terminal inside the container and run any GUI application:

   ```bash
   glxgears
   ```

   ```bash
   startxfce4
   ```

   The window will appear in the Termux:X11 app.

> [!NOTE]
>
> **Hardware Access/GPU Access** is not required for software rendering.

---

<a id="virgl"></a>

### 02. Termux-X11 + VirGL

Hardware-accelerated rendering for **non-Qualcomm devices (Mali, PowerVR)** via a `virglrenderer` bridge. Translates OpenGL calls from the container into commands the host Android GPU can execute.

Droidspaces automatically starts both the X server and the VirGL server, and injects `DISPLAY=:5` and `GALLIUM_DRIVER=virpipe` into the container environment.

#### Setup

1. Open **Edit container configuration**.
2. Enable both **Configure Termux:X11** and **Configure VirGL 3D Acceleration** toggles and save.
3. Start the container. Both servers launch automatically. The Termux:X11 app will show the **"X" cursor** once ready.
4. Run any GUI application inside the container:

   ```bash
   glxgears
   ```

   Look for **"VirGL"** in the renderer string to confirm acceleration is active.

   ```bash
   startxfce4
   ```

> [!TIP]
>
> **If the VirGL renderer fails to initialize**, you can pass the Vulkan backend flag via the **VirGL Extra Flags** field in the container configuration:
>
> `--angle-vulkan`

---

<a id="turnip"></a>

### 03. Turnip (Native Qualcomm/Adreno)

Near-native hardware acceleration for **Qualcomm Adreno GPUs** using the open-source Turnip Mesa driver. This bypasses VirGL entirely for direct GPU access.

#### Requirements

- A custom Mesa driver installed from the [Mesa for Android Container repository](https://github.com/lfdevs/mesa-for-android-container).

#### Setup

1. Install the custom Mesa driver following the instructions at [Mesa for Android Container](https://github.com/lfdevs/mesa-for-android-container).

2. Open **Edit container configuration** and apply the following:
   - Enable **GPU Access** and **Configure Termux:X11**.
   - **Disable** the **Configure VirGL 3D Acceleration** toggle (VirGL must be off for Turnip).
   - Add these two environment variables:

     ```
     MESA_LOADER_DRIVER_OVERRIDE=kgsl
     TU_DEBUG=noconform
     ```

3. Start the container. Droidspaces launches the X server automatically.

4. Run a GUI application inside the container - Turnip GPU acceleration will be active.

> [!NOTE]
>
> **Permission Management (Non-Root Users):** If you are using a non-root user, you must grant them access to the GPU device nodes: `sudo usermod -aG droidspaces-gpu <your_username>`

---

<a id="pulseaudio"></a>

## Android Sound (PulseAudio)

Droidspaces bridges Android's audio stack into your container using PulseAudio. When enabled, a PulseAudio daemon runs on the host as the Termux user (so Android's audio HAL grants it access to the device speaker), and its UNIX socket is bind-mounted into the container at `/tmp/.pulse-socket`. The environment variable `PULSE_SERVER=unix:/tmp/.pulse-socket` is injected automatically, so any application inside the container that speaks PulseAudio will produce sound with no manual configuration.

> [!WARNING]
>
> PulseAudio audio passthrough may not work on all devices. Compatibility depends on the Android version, OEM audio HAL implementation, and the Termux PulseAudio build. If audio does not work on your device, this is a known limitation of the upstream packages on that platform.

#### Requirements

- PulseAudio must be installed in Termux - the [setup script](#requirements) handles this automatically.

#### Setup

1. Open the Droidspaces app and navigate to **Edit container configuration**.
2. Enable the **Configure PulseAudio** toggle and save.
3. Start the container. Droidspaces will:
   - Launch the PulseAudio daemon as the Termux user.
   - Wait for the socket at `/tmp/.pulse-socket` to appear before proceeding.
   - Run `pactl set-default-sink AAudio_sink` to route audio to the device speaker.
   - Bind-mount the socket into the container and inject `PULSE_SERVER`.

   You can also enable PulseAudio via the CLI flag `--pulse-audio`.

4. Install and run any audio application inside the container. Because `PULSE_SERVER` is already set in the container environment, most apps work without any additional configuration:

   ```bash
   # Test audio output
   paplay /path/to/sound.wav
   ```

   ```bash
   # Verify the PulseAudio connection
   pactl info
   ```

> [!NOTE]
>
> PulseAudio sound is **Android-only**. On Linux desktop hosts, audio passthrough works via the host's own PulseAudio/PipeWire setup - no special configuration is needed in Droidspaces.

> [!NOTE]
>
> **Samsung One UI 6.1+ devices:** Droidspaces automatically injects `libskcodec.so` via `LD_PRELOAD` before starting PulseAudio. This fixes a hidden dependency in the OpenSL ES audio module specific to Samsung firmware. No action is required from you.

---

<a id="de-autoboot"></a>

## Desktop Environment Auto-Boot

As of v6.3.0, when Termux:X11 is enabled in a container's configuration, Droidspaces guarantees the X server socket (`/tmp/.X11-unix/X5`) is live before the container's init system reaches `graphical.target`. This makes proper systemd-based DE auto-start possible with no race conditions.

### How the Official XFCE Tarballs Wire It

Our official XFCE rootfs tarballs ship with a fully pre-configured auto-boot setup. Here's exactly how it works:

**1. The `xfce-autostart.service` systemd unit** is installed at `/etc/systemd/system/xfce-autostart.service` and enabled under `graphical.target`:

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

Two `ExecCondition` guards ensure XFCE only starts when it makes sense: the container must have Termux:X11 enabled in its config, and the X server socket must actually exist. If either condition fails, systemd skips the service silently - no errors, no crash loops.

**2. The `/usr/local/bin/xfce-start` launcher script** handles environment sourcing and user switching:

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

The script first tries to source `/run/droidspaces.env` (written by Droidspaces at boot; contains `DISPLAY=:5`, `GALLIUM_DRIVER=virpipe` if VirGL is enabled, and `PULSE_SERVER` if PulseAudio is enabled). If the env file is missing for any reason, it falls back to reading the container config directly and building the environment manually. This makes the script robust against any init system startup ordering.

### The `XFCE_USER` Variable

By default, `xfce-start` runs XFCE as `root`. If you want XFCE to run as a non-root user, set the `XFCE_USER` environment variable in your container's **Environment Variables** configuration in the Droidspaces app:

```
XFCE_USER=youruser
```

When `XFCE_USER` is set, the script uses `su -l -w "$WHITELIST"` to switch to that user while explicitly passing through only the required environment variables (`DISPLAY`, `GALLIUM_DRIVER`, `PULSE_SERVER`, etc.). This keeps the session clean - no root env leaking into the user session.

> [!TIP]
>
> Make sure `youruser` exists inside the container and has a valid home directory before setting `XFCE_USER`. You can create one with `useradd -m youruser` inside the container.

---

### Power Users: Wire Any Desktop Environment

You can replicate this exact pattern for any DE (Plasma, GNOME, MATE, i3, etc.) in any container.

**Step 1:** Create the launcher script at `/usr/local/bin/de-start`:

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

DE_CMD="startplasma-x11"   # replace with your DE's start command

if [ -n "$XFCE_USER" ]; then
    exec su -l -w "$WHITELIST" "$XFCE_USER" -c "exec $DE_CMD"
else
    exec $DE_CMD
fi
```

```bash
chmod +x /usr/local/bin/de-start
```

**Step 2:** Create the systemd service at `/etc/systemd/system/de-autostart.service`:

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

**Step 3:** Enable it:

```bash
systemctl enable de-autostart.service
```

On next container boot with Termux:X11 enabled, your DE will appear in the Termux:X11 app automatically.

> [!NOTE]
>
> The `XFCE_USER` variable name is a convention from our official tarballs. You can rename it to anything in your own script - what matters is the `su -l -w "$WHITELIST"` pattern for clean user switching with env passthrough.

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
