# Linux CLI Guide

Complete guide to using Droidspaces from the command line on Linux.

> [!TIP]
>
> **Using the CLI on Android:** All command-line arguments work exactly the same on Android.
>
> Once the backend is installed via the app, the `droidspaces` binary is located at `/data/local/Droidspaces/bin/droidspaces`. If the boot module is installed and you've restarted your phone, it will also be available globally in your `$PATH`.
>
> Also, you can view the full interactive, **more advanced** command-line documentation offline at any time by running:
> `droidspaces docs`

---

## Quick Navigation

[1. Getting Started](#getting-started)  
[2. Command Reference](#command-reference)  
[3. Options & Flags](#options-flags)  
[4. Common Workflows](#common-workflows)  
[5. Advanced Usage & Lifecycle](#advanced-usage)  
[6. System Requirements](#system-requirements)  

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
| `--rootfs-img=PATH` | `-i` | Path to an ext4 rootfs image file. Automatically loop-mounted. |

*Note: These are mutually exclusive. `--name` is mandatory when using `--rootfs-img`.*

### Container Identity

| Option | Short | Description |
|--------|-------|-------------|
| `--name=NAME` | `-n` | Unique name for the container. Auto-generated if omitted. |
| `--pidfile=PATH` | `-p` | Custom path for the PID file. Mutually exclusive with `--name`. |
| `--hostname=NAME` | `-h` | Set the container's hostname. Defaults to the container name. |

### Networking

| Option | Short | Description |
|--------|-------|-------------|
| `--dns=SERVERS` | `-d` | Custom DNS servers, comma-separated. Example: `--dns=1.1.1.1,8.8.8.8` |
| `--enable-ipv6` | | Enable IPv6 networking support. |

### Feature Flags

| Option | Short | Description |
|--------|-------|-------------|
| `--foreground` | `-f` | Attach to the container console on start to see init logs. |
| `--volatile` | `-V` | Ephemeral mode. Changes are stored in RAM and lost on exit. |
| `--hw-access` | | Expose host hardware (GPU, USB, etc.). See [Safety Warning](Features.md#hardware-access-mode). |
| `--enable-android-storage`| | Mount `/storage/emulated/0` (Android only). |
| `--selinux-permissive` | | Set host SELinux to permissive for the container session. |

### Bind Mounts

| Option | Short | Description |
|--------|-------|-------------|
| `--bind-mount=S:D` | `-B` | Mount a host directory `S` to container path `D`. |

**Formats:**
- Multiple mounts: `-B /src1:/dst1,/src2:/dst2` or `-B /src1:/dst1 -B /src2:/dst2`
- Max limits: Up to 16 mounts per container.
- Missing host paths are skipped with a warning.

---

<a id="common-workflows"></a>
## 4. Common Workflows

### Persistent Development
```bash
sudo droidspaces \
  --name=dev \
  --rootfs=/path/to/ubuntu-rootfs \
  --hostname=devbox \
  --bind-mount=/home/user/projects:/workspace \
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

---

<a id="advanced-usage"></a>
## 5. Advanced Usage & Lifecycle

### Container Recovery
If a container was started outside the current session or its PID file was lost, use `scan` to re-register it:
```bash
sudo droidspaces scan
```

### Fast Restarts
Droidspaces implements a "fast restart" mechanism that completes in under 200ms by coordinating state between the CLI and the background monitor without unmounting the rootfs image.

### PID File Storage
PID files are stored in:
- **Linux**: `/var/lib/Droidspaces/Pids/`
- **Android**: `/data/local/Droidspaces/Pids/`

---

<a id="system-requirements"></a>
## 6. System Requirements

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
