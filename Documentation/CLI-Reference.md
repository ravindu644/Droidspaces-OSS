# CLI Reference

Complete reference for all Droidspaces commands, options, and flags.

```
Usage: droidspaces [options] <command> [args]
```

> [!TIP]
> You can view the full interactive command-line documentation offline at any time by running:
> `droidspaces docs`

---

## Commands

### `start`

Start a new container.

Requires either `--rootfs` or `--rootfs-img` to specify the root filesystem. If `--name` is not provided, a name is auto-generated from the rootfs's `/etc/os-release`. If using `--rootfs-img`, the `--name` flag is mandatory.

```bash
# From a directory
droidspaces --rootfs=/path/to/rootfs start

# From an ext4 image
droidspaces --name=ubuntu --rootfs-img=/path/to/rootfs.img start

# With all features enabled
droidspaces --name=full --rootfs=/path/to/rootfs \
  --hw-access --enable-ipv6 --enable-android-storage \
  --selinux-permissive --hostname=full-box \
  --foreground start
```

**What happens:**
1. Validates the rootfs and system requirements
2. Allocates PTYs for console and TTY devices
3. Forks a monitor process that creates isolated namespaces
4. Boots the init system (systemd/OpenRC) as PID 1
5. Saves the container PID to a PID file
6. Displays connection information (or attaches console if `--foreground`)

---

### `stop`

Stop one or more running containers.

Sends `SIGRTMIN+3` (systemd halt signal), waits up to 8 seconds with escalation to `SIGTERM`, then `SIGKILL` as a last resort. Cleans up PID files, unmounts images, and restores host settings.

```bash
# Stop a single container
droidspaces --name=mycontainer stop

# Stop multiple containers at once
droidspaces --name=web,db,cache stop

# Stop using PID file
droidspaces --pidfile=/path/to/container.pid stop
```

---

### `restart`

Restart a container.

Performs a fast restart by preserving the loop mount and coordinating state between the CLI and the background monitor. Restarts complete in under 200ms.

```bash
# Restart directory-based containers
droidspaces --name=mycontainer --rootfs=/path/to/rootfs restart

# Restart rootfs.img-based containers
droidspaces --name=mycontainer --rootfs-img=/path/to/rootfs.img restart
```

---

### `enter [user]`

Enter a running container with an interactive shell.

Opens a PTY session inside the container's namespaces. If a username is provided, uses `su -l <user>` to log in as that user.

```bash
# Enter as root
droidspaces --name=mycontainer enter

# Enter as a specific user
droidspaces --name=mycontainer enter developer
```

**Shell selection order:** `/bin/bash` > `/bin/ash` > `/bin/sh`

---

### `run <command> [args]`

Run a command inside a running container without opening an interactive shell.

The command is executed directly. For commands with pipes or redirection, wrap them with `sh -c`.

```bash
# Simple commands
droidspaces --name=mycontainer run uname -a
droidspaces --name=mycontainer run ls -la /tmp

# Commands with pipes
droidspaces --name=mycontainer run sh -c "ps aux | grep init"
droidspaces --name=mycontainer run sh -c "cat /etc/os-release | grep ID"

# Service management
droidspaces --name=mycontainer run systemctl status
droidspaces --name=mycontainer run journalctl -n 50
```

---

### `status`

Show the running status of a container.

Reads the PID file, validates the process is alive, and confirms it's a valid Droidspaces container.

```bash
droidspaces --name=mycontainer status

# Auto-detect (if only one container is running)
droidspaces status
```

---

### `info`

Show detailed information about a container.

Displays PID, guest OS info, active features (hardware access, IPv6, SELinux, Android storage), rootfs path, and more. Auto-detects the container if only one is running.

```bash
droidspaces --name=mycontainer info

# Auto-detect
droidspaces info
```

---

### `show`

List all currently running containers in a formatted table.

```bash
droidspaces show
```

**Example output:**
```
┌──────────────┬──────────┐
│ NAME         │ PID      │
├──────────────┼──────────┤
│ ubuntu-24.04 │ 12345    │
│ alpine-3.23  │ 23456    │
└──────────────┴──────────┘
```

---

### `scan`

Scan for untracked (orphaned) containers.

Searches all running processes for Droidspaces containers that are running but don't have a PID file. Found containers are automatically registered with auto-generated names.

```bash
droidspaces scan
```

---

### `check`

Check system requirements.

Verifies that all required kernel features are available: namespaces, devtmpfs, OverlayFS support, PTY/devpts, loop devices, and more. Does not require root for basic checks.

```bash
droidspaces check
```

---

### `docs`

Open the interactive documentation browser.

Displays a paginated, terminal-based documentation viewer with arrow key navigation. Covers basic, medium, advanced, and expert usage patterns.

```bash
droidspaces docs
```

---

### `help`

Display the help message with all commands and options.

```bash
droidspaces help
droidspaces --help
```

---

### `version`

Print the version string.

```bash
droidspaces version
```

---

## Options

### Rootfs Options

| Option | Short | Argument | Description |
|--------|-------|----------|-------------|
| `--rootfs=PATH` | `-r` | Required | Path to a rootfs directory. Must contain `/sbin/init`. |
| `--rootfs-img=PATH` | `-i` | Required | Path to an ext4 rootfs image file. Automatically loop-mounted. |

These two options are **mutually exclusive**. You must use one or the other with the `start` command.

When using `--rootfs-img`, the `--name` flag is **mandatory** to ensure host-side infrastructure (mount points, PID files) is predictable.

---

### Container Identity

| Option | Short | Argument | Description |
|--------|-------|----------|-------------|
| `--name=NAME` | `-n` | Optional | Container name. Auto-generated from `/etc/os-release` if omitted. Mandatory for `--rootfs-img`. |
| `--pidfile=PATH` | `-p` | Optional | Custom path for the PID file. Mutually exclusive with `--name`. |
| `--hostname=NAME` | `-h` | Optional | Set the container's hostname. Defaults to the container name. Automatically mapped to `127.0.1.1` in `/etc/hosts`. |

`--name` and `--pidfile` are **mutually exclusive**. Use one or the other.

---

### Network Options

| Option | Short | Argument | Description |
|--------|-------|----------|-------------|
| `--dns=SERVERS` | `-d` | Optional | Custom DNS servers, comma-separated. Overrides default host DNS. Example: `--dns=1.1.1.1,8.8.8.8,9.9.9.9` |
| `--enable-ipv6` | | None | Enable IPv6 networking in the container. |

---

### Feature Flags

| Option | Short | Argument | Description |
|--------|-------|----------|-------------|
| `--foreground` | `-f` | None | Attach to the container console on start. Exit with `Ctrl+C`. |
| `--hw-access` | | None | Expose host hardware devices (GPU, cameras, USB, sensors) via devtmpfs. |
| `--enable-android-storage` | | None | Bind-mount `/storage/emulated/0` into the container. Android only. |
| `--selinux-permissive` | | None | Set SELinux to permissive mode before starting the container. |
| `--volatile` | `-V` | None | Ephemeral mode. All changes stored in RAM via OverlayFS and discarded on exit. |

---

### Bind Mounts

| Option | Short | Argument | Description |
|--------|-------|----------|-------------|
| `--bind-mount=SRC:DEST` | `-B` | Required | Bind mount a host directory into the container. |

**Formats:**

```bash
# Single mount
-B /host/path:/container/path

# Multiple mounts (comma-separated)
-B /src1:/dst1,/src2:/dst2,/src3:/dst3

# Multiple mounts (chained flags)
-B /src1:/dst1 -B /src2:/dst2 -B /src3:/dst3

# Mix and match
-B /src1:/dst1,/src2:/dst2 -B /src3:/dst3
```

**Limits:** Up to 16 bind mounts per container.

**Rules:**
- Destination must be an absolute path (starting with `/`)
- Path traversal (`..`) in destinations is rejected
- Missing destination directories are created automatically
- If a host source path doesn't exist, the bind is skipped with a warning (soft-fail)

---

## Notes

1. `--name` and `--pidfile` are mutually exclusive.
2. `--rootfs` and `--rootfs-img` are mutually exclusive.
3. Only one command can be specified at a time.
4. Multi-stop (comma-separated names) only works with the `stop` command.
5. Container names are auto-generated from `/etc/os-release` if `--name` is not provided.
6. PID files are stored in the workspace directory (`/var/lib/Droidspaces/Pids/` on Linux, `/data/local/Droidspaces/Pids/` on Android).
7. Rootfs images are automatically mounted and unmounted.
8. The `scan` command can detect containers started outside the tool.
9. All commands require root privileges except: `check`, `show`, `docs`, `version`, `help`.
10. Foreground mode attaches the terminal to the container console with full resize support.
