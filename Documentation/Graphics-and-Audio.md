<!--
title: Display, audio and desktop
section: Guides
order: 4
desc: GPU acceleration, PulseAudio sound and a desktop environment that starts on its own, for Droidspaces containers on Android and Linux.
keywords: gpu, acceleration, droidspaces, termux, x11, virgl, turnip, adreno, pulseaudio, sound, audio, desktop, xfce, container, graphics
-->

# Display, audio and desktop

On Android, Droidspaces starts the Termux:X11 display server, the VirGL server and the PulseAudio server for you when the matching toggles are on, and sets `DISPLAY`, `GALLIUM_DRIVER` and `PULSE_SERVER` inside the container. Turn on the toggles, start the container, and graphical programs open in the Termux:X11 app with sound from the phone's speaker. This page covers the one-time setup, the three ways to render, sound, a desktop that starts at boot, and the Linux side. For a Wayland desktop such as KDE Plasma there is a second path, covered on the [Anland](Anland.md) page.

> [!IMPORTANT]
> Droidspaces does not ship Termux:X11, VirGL or PulseAudio. They are Termux packages. Droidspaces starts and stops them and bridges their sockets into the container.

## One-time setup on Android

1. Install the [Termux](https://github.com/termux/termux-app) and [Termux:X11](https://github.com/termux/termux-x11) apps. Get both from the same source, F-Droid or GitHub, so they are signed by the same key.
2. In Termux, run the setup script. It installs Termux:X11, VirGL and PulseAudio into Termux and patches PulseAudio's configuration for Android's audio stack:

   ```bash
   curl -fsSL https://github.com/ravindu644/Droidspaces-OSS/raw/refs/heads/dev/scripts/setup-termux.sh | bash
   ```

That is all. You do not start anything in Termux yourself afterwards.

> [!TIP]
> The official XFCE images in the repository start XFCE on their own. Install one, turn on **Configure Termux:X11**, start the container and open the Termux:X11 app. The desktop is there. The [auto-boot section](#a-desktop-that-starts-at-boot) explains how it is wired, in case you want the same for another desktop.

The display variables are set for login shells through `/etc/profile.d`. In `zsh`, `fish` or another shell that skips it, run `source /run/droidspaces.env` once after the container boots.

## Three ways to render on Android

| | Works on | Speed | Toggles |
|---|---|---|---|
| **llvmpipe** | every device | software rendering, fine for a desktop and 2D apps | Configure Termux:X11 |
| **VirGL** | Mali, PowerVR and other non-Qualcomm GPUs | hardware accelerated through a bridge in Termux | Configure Termux:X11 and Configure VirGL 3D Acceleration |
| **Turnip** | Qualcomm Adreno | near native, the GPU is driven from inside the container | GPU Access and Configure Termux:X11, VirGL off |

### llvmpipe

Turn on **Configure Termux:X11** in the container's configuration and start the container. When the Termux:X11 app shows its X-shaped cursor, the server is ready. Inside the container, `glxgears` or `startxfce4` opens in Termux:X11. Nothing else to set up, and no hardware access needed.

### VirGL

Turn on **Configure Termux:X11** and **Configure VirGL 3D Acceleration**, then start the container. OpenGL calls in the container are sent to the VirGL server in Termux, which runs them on the phone's GPU. `glxinfo | grep renderer` inside the container names VirGL when it is working.

If VirGL fails to start, put `--angle-vulkan` in the **VirGL Extra Flags** field to switch it to the Vulkan backend.

### Turnip

Turnip is Mesa's open source Vulkan driver for Adreno, and with Zink on top it gives OpenGL as well. The container talks to the GPU directly, so it needs the GPU nodes and a Mesa build with Turnip.

1. Install Mesa with Turnip inside the container, following [Mesa for Android Container](https://github.com/lfdevs/mesa-for-android-container).
2. In the container's configuration, turn on **GPU Access** and **Configure Termux:X11**, and make sure **Configure VirGL 3D Acceleration** is off.
3. Add two environment variables under **Advanced Options**:

   ```
   MESA_LOADER_DRIVER_OVERRIDE=kgsl
   TU_DEBUG=noconform
   ```

4. Start the container. Graphical programs now render on the GPU.

A user other than root needs to be in the `droidspaces-gpu` group to open the GPU nodes: `usermod -aG droidspaces-gpu <user>` inside the container.

## Sound on Android

Turn on **Configure PulseAudio** and start the container. Droidspaces starts a PulseAudio server in Termux, where Android's audio system allows it to play, routes it to the speaker, mounts its socket into the container at `/tmp/.pulse-socket` and sets `PULSE_SERVER`. Anything that uses PulseAudio or PipeWire's PulseAudio compatibility plays through the phone. `paplay file.wav` is a quick test, and `pactl info` shows the connection.

Whether sound works at all depends on the phone: the Android version, the vendor's audio HAL and the Termux PulseAudio build. On Samsung One UI 6.1 and newer, Droidspaces preloads a library the audio module needs; nothing to do on your side.

## A desktop that starts at boot

Droidspaces makes sure the X server's socket, `/tmp/.X11-unix/X5`, exists before the container's init reaches `graphical.target`, so a systemd unit can start a desktop without racing it. The official XFCE images ship such a unit; the same pattern works for any desktop.

The unit, `/etc/systemd/system/xfce-autostart.service`:

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

The two conditions keep the unit quiet when Termux:X11 is off or the socket is missing: systemd skips it instead of failing.

The launcher, `/usr/local/bin/xfce-start`, loads the environment Droidspaces wrote and starts the desktop, as root or as the user named in `XFCE_USER`:

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

To run the desktop as a normal user, create one in the container with `useradd -m <user>` and set `XFCE_USER=<user>` under **Environment Variables** in the container's configuration. `su -l -w` passes only the display and audio variables into that user's session.

For another desktop, copy both files under new names, replace `startxfce4` in the launcher with the desktop's start command, such as `startplasma-x11`, and enable the unit with `systemctl enable`. The variable name `XFCE_USER` is only a convention; call it what you like.

## Linux

On a Linux host, GPU acceleration needs no bridge: the container uses the host's GPU through Mesa like any other program.

1. Turn on **Hardware Access**, or **GPU Access** for the render nodes alone, with `--hw-access` or `--gpu` on the CLI.
2. Allow the container to reach your X server: `xhost +local:` on the host.
3. Start the container with `--x11`, which mounts the host's X socket, and set `DISPLAY=:0`, or whatever the host uses, in the container's environment file.

Graphical programs started in the container then open on your desktop with hardware acceleration. Sound goes through the host's own PulseAudio or PipeWire socket if you bind-mount it; Droidspaces does not manage audio on Linux.

---

Previous: [Linux CLI](Linux-CLI.md) | Next: [Anland](Anland.md)
