# Linux CLI Usage Guide

This guide covers everyday usage of Droidspaces from the command line on Linux.

## Quick Start

### Start Your First Container

```bash
# From a rootfs directory
sudo droidspaces --rootfs=/path/to/rootfs start

# From an ext4 image
sudo droidspaces --name=mycontainer --rootfs-img=/path/to/rootfs.img start
```

Droidspaces will boot the container with systemd (or OpenRC) as PID 1 and display connection information.

### Enter the Container

```bash
sudo droidspaces --name=mycontainer enter
```

This opens a shell inside the running container. To log in as a specific user:

```bash
sudo droidspaces --name=mycontainer enter username
```

### Stop the Container

```bash
sudo droidspaces --name=mycontainer stop
```

> [!TIP]
> You can view the full interactive command-line documentation offline at any time by running:
> `droidspaces docs`

---

## Common Workflows

### Development Environment

Set up a persistent development container with bind mounts for your project files:

```bash
sudo droidspaces \
  --name=dev \
  --rootfs=/path/to/ubuntu-rootfs \
  --hostname=devbox \
  --bind-mount=/home/user/projects:/workspace \
  start
```

Now your host `projects/` directory is available at `/workspace` inside the container.

### Ephemeral Testing

Use volatile mode to create throwaway containers that reset on every restart:

```bash
sudo droidspaces \
  --name=test \
  --rootfs=/path/to/rootfs \
  --volatile \
  start
```

Install packages, break things, experiment freely. All changes vanish when the container stops.

### Multi-Container Setup

Run multiple containers simultaneously for different purposes:

```bash
# Start a web server container
sudo droidspaces --name=web --rootfs=/path/to/web-rootfs --hostname=web start

# Start a database container
sudo droidspaces --name=db --rootfs=/path/to/db-rootfs --hostname=db start

# Start an app container
sudo droidspaces --name=app --rootfs-img=/path/to/app.img start

# List all running containers
sudo droidspaces show

# Stop all of them at once
sudo droidspaces --name=web,db,app stop
```

### Running One-Off Commands

Execute a command inside a container without opening a full shell:

```bash
# Simple command
sudo droidspaces --name=mycontainer run uname -a

# Command with pipes (use sh -c)
sudo droidspaces --name=mycontainer run sh -c "ps aux | grep init"

# Check systemd status
sudo droidspaces --name=mycontainer run systemctl status

# View recent logs
sudo droidspaces --name=mycontainer run journalctl -n 50
```

### Foreground Mode

Attach directly to the container console. Useful for debugging boot issues:

```bash
sudo droidspaces \
  --name=mycontainer \
  --rootfs=/path/to/rootfs \
  --foreground \
  start
```

Press `Ctrl+C` to detach.

---

## Container Lifecycle

### Full Lifecycle Example

```bash
# 1. Start the container
sudo droidspaces --name=ubuntu --rootfs=/path/to/rootfs start

# 2. Check its status
sudo droidspaces --name=ubuntu status

# 3. Get detailed information
sudo droidspaces --name=ubuntu info

# 4. Enter it
sudo droidspaces --name=ubuntu enter

# 5. Run a command
sudo droidspaces --name=ubuntu run cat /etc/os-release

# 6. Restart it (fast, under 200ms)
sudo droidspaces --name=ubuntu restart

# 7. Stop it
sudo droidspaces --name=ubuntu stop
```

### Container Recovery

If a container was started outside the current session (or its PID file is missing), use the `scan` command to find and re-register it:

```bash
sudo droidspaces scan
```

### System Requirements Check

Verify your system supports all Droidspaces features:

```bash
sudo droidspaces check
```

---

## Feature Flags at a Glance

| Flag | Short | Description |
|------|-------|-------------|
| `--rootfs=PATH` | `-r` | Path to rootfs directory |
| `--rootfs-img=PATH` | `-i` | Path to ext4 rootfs image |
| `--name=NAME` | `-n` | Container name |
| `--hostname=NAME` | `-h` | Custom hostname |
| `--dns=SERVERS` | `-d` | Custom DNS servers (comma-separated) |
| `--foreground` | `-f` | Attach to console on start |
| `--volatile` | `-V` | Ephemeral mode (OverlayFS) |
| `--bind-mount=SRC:DEST` | `-B` | Bind mount host directory |
| `--hw-access` | | Expose host hardware |
| `--enable-ipv6` | | Enable IPv6 |
| `--enable-android-storage` | | Mount Android shared storage |
| `--selinux-permissive` | | Set SELinux to permissive |

For the complete reference, see [CLI Reference](CLI-Reference.md).

## Next Steps

- [CLI Reference](CLI-Reference.md) for the full command and flag reference
- [Feature Deep Dives](Features.md) for in-depth explanations
- [Troubleshooting](Troubleshooting.md) for common issues
