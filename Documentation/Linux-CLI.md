# Linux CLI Guide

Complete guide to using Droidspaces from the command line on Linux.

> [!TIP]
>
> **Using the CLI on Android:** All command-line arguments work exactly the same on Android.
>
> Once the backend is installed via the app, the `droidspaces` binary is located at `/data/local/Droidspaces/bin/droidspaces`.
>
> Also, you can view the full interactive, **more advanced** command-line documentation offline at any time by running:
> `droidspaces docs`

---

## Quick Navigation

[1. Getting Started](#getting-started)  
[2. Command Reference](#command-reference)  
[3. Options & Flags](#options-flags)  
[4. Configuration Files](#configs)  
[5. Common Workflows](#common-workflows)  
[6. Advanced Usage & Lifecycle](#advanced-usage)  
[7. System Requirements](#system-requirements)

---

<a id="getting-started"></a>
## 1. Getting Started

### Start Your First Container
```bash
# From a rootfs directory
sudo droidspaces --rootfs=/path/to/rootfs start

# From an ext4 image
sudo droidspaces --name=mycontainer --rootfs-img=/path/to/rootfs.img start
```

### Enter the Container
```bash
# Enter as root
sudo droidspaces --name=mycontainer enter

# Enter as a specific user
sudo droidspaces --name=mycontainer enter username
```

### Stop the Container
```bash
# Stop a single container
sudo droidspaces --name=mycontainer stop

# Stop multiple containers
sudo droidspaces --name=web,db,app stop
```

---

<a id="command-reference"></a>
## 2. Command Reference

| Command | Action |
|---------|--------|
| `start` | Start a new container. Requires either `--rootfs` or `--rootfs-img`. |
| `stop` | Gracefully shut down one or more containers. |
| `restart` | Fast restart (under 200ms) by preserving loop mounts. |
| `enter [user]` | Open an interactive shell inside a running container. |
| `run <cmd>` | Execute a single command without opening a full shell. |
| `status` | Show if a specific container is running. |
| `info` | Show deep technical details about a container. |
| `show` | List all currently running containers in a table. |
| `scan` | Detect and register orphaned/untracked containers. |
| `check` | Verify system and kernel requirements. |
| `docs` | Open the interactive terminal-based documentation. |
| `help` | Display the help message. |
| `version` | Print the version string. |

---

<a id="options-flags"></a>
## 3. Options & Flags

### Rootfs Selection

| Option | Short | Description |
|--------|-------|-------------|
| `--rootfs=PATH` | `-r` | Path to a rootfs directory. Must contain `/sbin/init`. |
| `--rootfs-img=PATH` | `-i` | Path to an ext4 rootfs image file or block device. Automatically mounted. |

*Note: These are mutually exclusive. `--name` is mandatory when using `--rootfs-img`.*

### Container Identity & Configuration

| Option | Short | Description |
|--------|-------|-------------|
| `--name=NAME` | `-n` | Unique name for the container. Auto-generated if omitted in directory-based rootfs mode. |
| `--hostname=NAME` | `-h` | Set the container's hostname. Defaults to the container name. |
| `--conf=PATH` | `-C` | Load container configuration directly from a config file. |
| `--reset` | | Reset container config to defaults (preserves name, rootfs path). |

### Networking

| Option | Short | Description |
|--------|-------|-------------|
| `--net=MODE` | | Networking mode: `host` (default), `nat`, or `none`. |
| `--upstream IFACE[,..]` | | Upstream internet interface(s) for NAT mode (e.g., `wlan0,rmnet0`). Wildcards are supported (e.g., `rmnet*`, `v4-rmnet_data*`). **Mandatory for NAT**. |
| `--port HOST:CONT[/proto]` | | Forward host port to container (NAT mode). Supports TCP/UDP. |
| `--dns=SERVERS` | `-d` | Custom DNS servers, comma-separated. Example: `--dns=1.1.1.1,8.8.8.8` |
| `--disable-ipv6` | | Disable IPv6 networking support (Host mode only). |

### Feature Flags

| Option | Short | Description |
|--------|-------|-------------|
| `--foreground` | `-f` | Attach to the container console on start to see init logs. |
| `--volatile` | `-V` | Ephemeral mode. Changes are stored in RAM and lost on exit. |
| `--hw-access` | `-H` | Expose host hardware (GPU, USB, etc.). Auto-detects GPU group IDs and creates matching groups inside the container. Mounts X11 socket for GUI apps (Termux X11 on Android, `/tmp/.X11-unix` on Linux). See [Safety Warning](Features.md#hardware-access-mode). |
| `--gpu` | | Exclusively enable GPU acceleration. Scans the host `/dev` for known GPU nodes and securely maps only them into the container without exposing other host hardware. (Ignored if `-H` is passed). |
| `--termux-x11`| `-X` | Mount X11 socket for Termux-X11 display (Android only). |
| `--enable-android-storage`| | Mount `/storage/emulated/0` (Android only). |
| `--selinux-permissive` | | Set host SELinux to permissive for the container session. |
| `--force-cgroupv1` | | Force legacy Cgroup V1 hierarchy. Required if the host kernel has a broken or partial Cgroups V2 implementation (common on older Android 4.x kernels). |
| `--privileged=TAGS` | | Relax security protections. Accepts a comma-separated list of tags: `nomask`, `nocaps`, `noseccomp`, `shared`, `unfiltered-dev`, `full`. Use with extreme caution. |

### Bind Mounts

| Option | Short | Description |
|--------|-------|-------------|
| `--bind-mount=S:D` | `-B` | Mount a host directory `S` to container path `D`. |

**Formats:**
- Multiple mounts: `-B /src1:/dst1,/src2:/dst2` or `-B /src1:/dst1 -B /src2:/dst2`
- Max limits: Up to 16 mounts per container.
- Missing host paths are skipped with a warning.

---

<a id="configs"></a>
## 4. Configuration Files

Instead of relying solely on long command-line arguments, Droidspaces allows you to define container environments in a `.config` file and load it using the `--conf` flag.

Below is a reference of every supported key in the configuration file:

```ini
# Droidspaces Container Configuration
# Generated automatically - Changes may be overwritten

# Unique identifier for the container
name=ubuntu

# Hostname of the container environment
hostname=ubuntu-devbox

# Absolute path to the rootfs directory or .img file
rootfs_path=/home/user/ubuntu-24.04-rootfs.img

# Custom path to store the container's PID file
pidfile=/var/lib/Droidspaces/Pids/ubuntu.pid

# Comma-separated list of host directories to bind mount (src:dest)
bind_mounts=/home/user:/mnt/host,/tmp:/mnt/tmp

# Absolute path to a file containing environment variables to load
env_file=/path/to/env.list

# Unique UUID (Automatically generated, do not change manually)
uuid=d88107dab4ef48a8874a93897188982d

# Custom DNS servers (comma separated)
dns_servers=1.1.1.1,8.8.8.8

# -------- Boolean Flags (0 or 1) --------

# Disable IPv6 networking
disable_ipv6=0

# Expose host hardware nodes to the container (/dev)
enable_hw_access=0

# Auto-detect and securely map GPU nodes without full hardware access
enable_gpu_mode=0

# Android: Setup Termux X11 socket
enable_termux_x11=0

# Android: Setup Android internal shared storage mount
enable_android_storage=0

# Set the host SELinux policy to permissive during boot
selinux_permissive=0

# Ephemeral mode: changes are lost on exit
volatile_mode=1

# Run the container in the foreground instead of forking
foreground=0

# ----------------------------------------
# Android App Configuration
# Any lines that the CLI engine does not recognize will be safely
# preserved at the bottom of the config file and passed back to the Host.
```

---

<a id="common-workflows"></a>
## 5. Common Workflows

### Running with a Config File
Instead of passing many flags, you can load everything from a `.config` file:
```bash
sudo droidspaces --conf=./my-container.config start
```

### Persistent Development
```bash
sudo droidspaces \
  --name=dev \
  --rootfs=/path/to/ubuntu-rootfs \
  --hostname=devbox \
  --bind-mount=/home/user/projects:/workspace \
  start
```

### NAT Isolation with Port Forwarding
```bash
sudo droidspaces \
  --name=server \
  --rootfs-img=/path/to/rootfs.img \
  --net=nat \
  --upstream=wlan0,rmnet0 \
  --port=8080:80 \
  start
```

### Ephemeral Testing
```bash
sudo droidspaces --name=test --rootfs=/path/to/rootfs --volatile start
```

### One-Off Commands
```bash
sudo droidspaces --name=mycontainer run uname -a
# Use sh -c for pipes:
sudo droidspaces --name=mycontainer run sh -c "ps aux | grep init"
```

### GPU Acceleration
Ensure only the GPU nodes (and not all host hardware) are securely exposed:
```bash
sudo droidspaces --name=gpu-app --rootfs=/path/to/rootfs --gpu start
```

### Deep Customization (Privileged Mode)
For cases where you need unmasked access:
```bash
sudo droidspaces --name=privileged-box --rootfs=/path/to/rootfs --privileged=full start
# Or mix and match tags:
sudo droidspaces --name=dev-box --rootfs=/path/to/rootfs --privileged=nocaps,noseccomp start
```

---

<a id="advanced-usage"></a>
## 6. Advanced Usage & Lifecycle

### Container Recovery
If a container was started outside the current session, or its host-side PID file / config file was lost or corrupted, use `scan` to resurrect it from the container's isolated `/run` memory:
```bash
sudo droidspaces scan
```

---

<a id="system-requirements"></a>
## 7. System Requirements

Always run the built-in checker to verify your kernel supports the required namespaces and features:
```bash
sudo droidspaces check
```

See the [Kernel Configuration Guide](Kernel-Configuration.md) for a deep dive into technical requirements.

---

## Next Steps
- [Feature Deep Dives](Features.md)
- [Troubleshooting](Troubleshooting.md)
- [Android App Usage Guide](Usage-Android-App.md)
