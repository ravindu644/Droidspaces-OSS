# Droidspaces — Internal Architecture & Implementation Reference

---

## Abstract

Droidspaces is a lightweight, zero-virtualization container runtime designed to run full Linux distributions (Ubuntu, Alpine, etc.) with systemd or openrc as PID 1, natively on Android devices. It achieves process isolation through Linux PID, IPC, MNT, and UTS namespaces — the same kernel primitives used by Docker and LXC — but targets the constrained and idiosyncratic Android kernel environment where many standard container tools refuse to operate.

This document is a complete internal architecture reference for **Droidspaces v3.2.2**. Every struct, every syscall, every mount, and every design decision is documented here with the intent that a future implementer could rewrite this project from scratch without ever reading the original source. Where the implementation is elegant, I say so. Where it is broken or fragile, I say so with equal honesty.

The codebase is approximately **3,300 lines of C** across 12 `.c` files and 1 master header, compiled as a single static binary against musl libc.


---

## 1. Project Overview

### 1.1 What It Does

Droidspaces takes a Linux rootfs directory (or ext4 image) and boots it inside a set of Linux namespaces as if it were a tiny virtual machine — except there is no hypervisor, no emulated hardware, and no performance penalty. The host kernel is shared. The container gets:

- Its own PID tree (PID 1 = `/sbin/init`)
- Its own mount table (the rootfs becomes `/`)
- Its own hostname (UTS namespace)
- Its own IPC resources (semaphores, shared memory)
- Full networking via the host kernel's network stack (no NET namespace)

### 1.2 What It Does NOT Do

- **No user namespace.** The container runs as root from the host kernel's perspective. This is deliberate — Android kernels often lack user namespace support, and Droidspaces requires root anyway.
- **No network namespace.** The container shares the host's network stack. This simplifies setup enormously but means no per-container firewall rules via network namespaces.
- **No cgroup isolation.** The code creates cgroup mount points for systemd compatibility (both v1 and v2), but does not actually constrain CPU, memory, or I/O.

### 1.3 Source Structure

```
src/
├── droidspace.h        Master header — all structs, constants, prototypes
├── main.c              CLI parsing, command dispatch
├── container.c         start/stop/enter/run/info/show/restart commands
├── boot.c              internal_boot() — the PID 1 boot sequence
├── mount.c             Mount helpers, /dev setup, rootfs.img handling
├── console.c           epoll-based console I/O monitor loop
├── terminal.c          PTY allocation, /dev/console + /dev/ttyN setup
├── network.c           DNS, routing, hostname, IPv6 configuration
├── android.c           Android-specific: SELinux, optimizations, storage
├── environment.c       Environment variables, os-release parsing
├── utils.c             File I/O, UUID generation, firmware path mgmt
├── pid.c               PID file management, workspace, container naming
└── check.c             System requirements checker

```

**Key architectural changes from v2:**
- The `struct ds_config` replaces all global variables — every function receives its configuration explicitly
- PTYs are allocated in the **parent** process before fork (LXC model) — no SCM_RIGHTS FD passing
- `enter_namespace()` is factored out as a shared helper for both `enter` and `run` commands
- Thread pool eliminated — all checks and scans run serially (the overhead was larger than the benefit)
- Stop uses `SIGRTMIN+3` for systemd shutdown (not `system("poweroff")`)
- The `[ds-monitor]` daemon uses `waitpid()` instead of `kill(pid, 0)` polling
- **Hostname resolution:** Automatically maps `127.0.1.1` to the container hostname in `/etc/hosts` to fix `apt` and `sudo` resolution issues.
- **PTY Allocation:** `enter` command allocates PTYs natively *inside* the container namespaces for perfect TTY isolation and `ps aux` accuracy.
- **Security & De-duplication:** Environment setup, `os-release` parsing, and path resolution are consolidated into shared helpers with strict buffer bounds (`snprintf` with precision) and hardened I/O (`write_all`).
- **Volatile Overlay Mode:** Leverages Linux OverlayFS to store all container changes in RAM, ensuring an ephemeral environment that is wiped on exit.
- **Custom Bind Mounts:** Allows mapping host directories into the container at arbitrary mount points with automatic destination creation.
- **Strict Naming Architecture:** Enforces mandatory `--name` for image-based containers to ensure host-side mount points and PID files are perfectly synchronized and descriptive.


---

## 2. System Requirements & Assumptions

Droidspaces performs a formal requirements check via the `check` command (implemented in `check.c`). Here's what actually matters:

### Must-Have (Hard Requirements)

| Requirement | How It's Checked | Why |
|---|---|---|
| Root privileges | `getuid() == 0` | Namespace creation and mount operations require CAP_SYS_ADMIN |
| PID namespace | `access("/proc/self/ns/pid", F_OK) && is_root` | Container PID isolation |
| Mount namespace | `access("/proc/self/ns/mnt", F_OK) && is_root` | Filesystem isolation |
| UTS namespace | `access("/proc/self/ns/uts", F_OK) && is_root` | Hostname isolation |
| IPC namespace | `access("/proc/self/ns/ipc", F_OK) && is_root` | IPC isolation |
| devtmpfs | `grep_file("/proc/filesystems", "devtmpfs")` | Proper HW device node support |
| cgroup support | `access("/sys/fs/cgroup/devices", F_OK) \|\| access("/sys/fs/cgroup/cgroup.controllers", F_OK) \|\| grep_file("/proc/mounts", "cgroup2")` | systemd requires this (v1 or v2) |
| `pivot_root` syscall | `statfs("/", &st)` — not RAMFS_MAGIC | Root filesystem switching |
| `/proc` and `/sys` | `access()` check | Essential virtual filesystems |
| OverlayFS (Optional) | `grep_file("/proc/filesystems", "overlay")` | Required for Volatile Mode (`--volatile`) |

### Recommended

epoll, signalfd, PTY/devpts support, loop device, ext4 — all tested serially in `check_requirements_detailed()`.

### Assumptions

- The binary is compiled statically against musl libc (no glibc dependency)
- The host is Android (detected via `ANDROID_ROOT` env var or `/system/bin/app_process`) or standard Linux
- The rootfs contains `/sbin/init` (systemd or openrc)
- The rootfs contains `/etc/os-release` for auto-naming

---

## 3. Container Lifecycle Overview

Here's the high-level flow from `start` to `stop`, distilled to its essence:

```
                          ┌─────────────────────┐
                          │   droidspaces start  │
                          │     (CLI parsing)    │
                          └──────────┬──────────┘
                                     │
                          ┌──────────▼──────────┐
                          │  check_requirements  │
                          │  android_optimizations│
                          │  generate UUID       │
                          │  allocate PTYs       │
                          │    (LXC model)       │
                          └──────────┬──────────┘
                                     │
                               fork() │
                    ┌────────────────┼────────────────┐
                    │ PARENT         │ MONITOR        │
                    │                │ PROCESS        │
                    │                │                │
                    │                ▼                │
                    │     setsid()                    │
                    │     unshare(PID|UTS|IPC)        │
                    │                │                │
                    │           fork()│                │
                    │         ┌──────┼──────┐         │
                    │         │      │ PID 1│         │
                    │         │      │      │         │
                    │         │      ▼      │         │
                    │         │ internal_boot()       │
                    │         │   │ unshare(NEWNS)    │
                    │         │   │ mount(/ PRIVATE)  │
                    │         │   │ setup_volatile_   │
                    │         │   │   overlay()       │
                    │         │   │ bind mount rootfs │
                    │         │   │ setup_custom_     │
                    │         │   │   binds()         │
                    │         │   │ setup_dev()       │
                    │         │   │ mount proc,sys,run│
                    │         │   │ bind-mount PTYs   │
                    │         │   │   (console+ttys)  │
                    │         │   │ write UUID marker │
                    │         │   │ setup_cgroups     │
                    │         │   │ pivot_root(".",   │
                    │         │   │   ".old_root")    │
                    │         │   │ chdir("/")        │
                    │         │   │ setup_devpts      │
                    │         │   │ fix_networking_   │
                    │         │   │   rootfs          │
                    │         │   │ umount .old_root  │
                    │         │   │ redirect stdio    │
                    │         │   │   to /dev/console │
                    │         │   │ execve(/sbin/init)│
                    │         │   ▼                   │
                    │         │ waitpid(init)         │
                    │         │ cleanup_container_    │
                    │         │   resources()         │
                    │         │ (proxy exit)          │
                    │         └──────────────┘        │
                    │                                 │
                    ▼                                 │
          read init_pid from sync_pipe               │
          fix_networking_host()                      │
          find_and_save_pid()                        │
                    │                                │
            ┌───────┴────────┐                       │
            │ FOREGROUND?    │                       │
            ├────YES─────┐   │                       │
            │            ▼   │                       │
            │  console_monitor_loop()                │
            │  (epoll stdin↔pty_master)              │
            │                │                       │
            ├────NO──────┐   │                       │
            │            ▼   │                       │
            │  show_info()                           │
            │  parent exits                          │
            │  monitor holds PTY FDs via waitpid()   │
            └────────────────┘                       │
```

**Key insight:** The monitor process (`[ds-monitor]`) creates the namespace via `unshare()`, then forks the container init (PID 1). The monitor stays alive via `waitpid()` on init — it holds all PTY master FDs open for the lifetime of the container. When init exits, the monitor calls `cleanup_container_resources()` and exits.

**The parent** receives the init PID from the monitor via a sync pipe, saves it to the pidfile, and either enters the foreground console loop or exits after displaying status info.

---

## 4. Start: pivot_root, Namespace Creation, and Mount Setup

### 4.1 Pre-Fork Setup (Parent Process)

Before any forking, `start_rootfs()` in `container.c` performs:

1. **Workspace creation:** `ensure_workspace()` creates `Pids/` directory under the workspace path
2. **SELinux:** If `--selinux-permissive`, set SELinux to permissive mode
3. **Android Storage:** Detect if running on Android and validate storage requirements.
4. **Container Naming (Sync Transition):** 
   - If no `--name` is provided, Droidspaces auto-generates one from `/etc/os-release`.
   - **MANDATORY**: If using a rootfs image (`-i`), the `--name` flag is now mandatory to ensure the host-side infrastructure is predictable.
   - Duplicate names are resolved with a numeric suffix (e.g., `ubuntu-1`) via `find_available_name()`.
   - **Note**: The name is finalized **before** any mounting occurs.
5. **Rootfs image mount:** If `-i` provided, `mount_rootfs_img()` is called:
   - It runs `e2fsck -f -y` to ensure filesystem integrity.
   - It identifies a descriptive mount point at `/mnt/Droidspaces/<name>`.
   - If `--volatile` is active, the image is mounted **Read-Only** (`-o loop,ro`) for maximum safety.
6. **UUID generation:** 32 hex chars from `/dev/urandom`
7. **PTY allocation (LXC model):**
   ```c
   ds_terminal_create(&cfg->console);       // 1 console PTY
   for (int i = 0; i < cfg->tty_count; i++) // 6 TTY PTYs (DS_MAX_TTYS)
       ds_terminal_create(&cfg->ttys[i]);
   ```
   All PTYs are allocated in the **parent** process using `openpty()`. Both master and slave FDs are marked `FD_CLOEXEC`. The slave device paths (e.g., `/dev/pts/3`) are recorded in `tty->name` for later bind-mounting.

### 4.2 Fork Architecture

The parent forks a **monitor process**, not an intermediate throwaway:

```c
pid_t monitor_pid = fork();
if (monitor_pid == 0) {
    /* MONITOR PROCESS */
    setsid();
    prctl(PR_SET_NAME, "[ds-monitor]", 0, 0, 0);
    unshare(CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID);
    
    pid_t init_pid = fork();
    if (init_pid == 0) {
        /* CONTAINER INIT (PID 1) */
        exit(internal_boot(cfg, -1));
    }
    
    /* Send init_pid to parent via sync pipe */
    write(sync_pipe[1], &init_pid, sizeof(pid_t));
    
    /* Wait for init to exit, then cleanup */
    waitpid(init_pid, &status, 0);
    cleanup_container_resources(cfg, init_pid, 0);
    exit(WEXITSTATUS(status));
}
```

**Namespace allocation split:**
- `CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID` — called in the monitor via `unshare()`
- `CLONE_NEWNS` — called inside `internal_boot()` by the container init itself

This split is deliberate: the monitor retains the host mount namespace so it can perform cleanup (unmounting rootfs images, removing pidfiles, and clearing volatile overlays) after the container exits. The container init gets its own private mount namespace inside `internal_boot()`.

### 4.3 Volatile Mode (OverlayFS Implementation)

When `--volatile` (`-V`) is used, Droidspaces wraps the rootfs in an ephemeral writable layer:

1. **Probe**: `setup_volatile_overlay()` checks `/proc/filesystems` for `overlay` support.
2. **Workspace**: Creates a temporary structure in `/var/lib/Droidspaces/Volatile/<name>/`.
3. **Layering**: 
   - **Lowerdir**: The original rootfs (or RO image mount).
   - **Upperdir/Workdir**: Managed in a dedicated `tmpfs` mounted at the Volatile workspace path.
    - **Merged View**: Mounts the OverlayFS to `<workspace>/merged`.
    - **SELinux Fix (Android)**: On Android, the overlay is mounted with `context="u:object_r:tmpfs:s0"` to allow standard write operations within the upperdir.
5. **Redirection**: The configuration's `rootfs_path` is updated to point to this merged view for the duration of the boot.

All file modifications happen in the `tmpfs`-backed `upperdir`. On container exit, the monitor process unmounts the overlay and recursively deletes the workspace, ensuring no changes persist. Droidspaces ensures that the overlay is cleaned up *before* the underlying rootfs image is unmounted.

**Known Limitation — f2fs (Android):**
Most Android devices use f2fs for `/data`. OverlayFS on many Android kernels (4.14, 5.15) does not support f2fs as a `lowerdir`. This means **volatile mode + directory rootfs (`-r`) will fail** when the rootfs lives on f2fs. Droidspaces detects this at runtime (via `statfs()` magic `0xF2F52010`) and prints a clear diagnostic. **Workaround**: Use a rootfs image (`-i`) instead — the ext4 loop mount provides a compatible lowerdir.

**What is NOT used and why:**
- `CLONE_NEWNET` — The container shares the host network. Deliberate design choice for simplicity on Android.
- `CLONE_NEWUSER` — Not used because Android kernels often lack support, and the tool requires root anyway.

### 4.4 The internal_boot() Sequence

This is the critical function — it runs as PID 1 inside the new PID namespace. Here is the exact order of operations:

**Step 1 — Unshare mount namespace:**
```c
unshare(CLONE_NEWNS);
```
This gives the container its own private mount table.

**Step 2 — Make root mount private:**
```c
mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
```
This prevents mount events from propagating back to the host. Without this, every mount inside the container would be visible to Android.

**Step 3 — Bind mount rootfs to itself:**
```c
mount(cfg->rootfs_path, cfg->rootfs_path, NULL, MS_BIND | MS_REC, NULL);
```
This is required by `pivot_root(2)` — the new root must be a mount point. For rootfs.img, the ext4 image is already loop-mounted, so this is redundant but harmless.

**Step 4 — Custom Bind Mounts (Optional):**
```c
setup_custom_binds(cfg, cfg->rootfs_path);
```
Iterates through `--bind-mount` entries and performs recursive bind mounts from host to container. It uses `mkdir_p` to automatically create parent directories inside the rootfs if they don't exist. 

**Resilience (v3.2.0+)**: If a host source path is missing or the mount fails, Droidspaces issues a warning and skips the specific entry rather than failing the entire boot sequence ("soft-fail" model).
```c
chdir(cfg->rootfs_path);
```

**Step 5 — Create `.old_root` directory:**
```c
mkdir(".old_root", 0755);
```

**Step 6 — Setup /dev:**
```c
setup_dev(".", cfg->hw_access);
```
This mounts either `tmpfs` (isolated mode) or `devtmpfs` (hardware access mode) at `<rootfs>/dev`, then calls `create_devices()` to populate device nodes via `mknod()`. See Section 8 for details.

**Step 7 — Mount /proc:**
```c
domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
```

**Step 8 — Mount /sys:**
- **Without `--hw-access`**: sysfs is mounted RW initially, then a separate sysfs instance is mounted at `sys/devices/virtual/net` for networking tools, and finally the parent `/sys` is remounted read-only.
- **With `--hw-access`**: Core `sysfs` is mounted RW, then **dynamically iterated**. Every subdirectory (devices, bus, class, etc.) is self-bind mounted to "pin" it as an independent RW entry. Finally, the top-level `/sys` is remounted RO.

Aligning with the official **systemd Container Interface**, the top-level `/sys` must be RO for systemd 258+ to correctly identify the environment as a container. The "dynamic hole-punching" ensures hardware access remains RW.

**Step 9 — Mount /run as tmpfs:**
```c
domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755");
```

**Step 10 — Bind-mount PTYs (BEFORE pivot_root):**
```c
mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL);
for (int i = 0; i < cfg->tty_count; i++) {
    snprintf(tty_target, sizeof(tty_target), "dev/tty%d", i + 1);
    mount(cfg->ttys[i].name, tty_target, NULL, MS_BIND, NULL);
}
```
This is the LXC model: PTY slaves allocated in the parent (e.g., `/dev/pts/3`) are bind-mounted to their container targets (e.g., `dev/console`, `dev/tty1`..`dev/tty6`) **before** `pivot_root`. This is critical because after `pivot_root`, the host `/dev/pts/N` paths would no longer be accessible.

**Step 11 — Write UUID marker:**
```c
write_file("run/<uuid>", "init");
write_file("run/droidspaces", DS_VERSION);
```
The UUID marker is used by the parent for PID discovery (see Section 5). The `run/droidspaces` file is polled by the parent to confirm the boot sequence has passed `pivot_root`.

**Step 12 — Setup cgroups:**
```c
setup_cgroups();
```
Detects cgroup v2 (unified hierarchy) or falls back to cgroup v1 legacy directories: `cpu`, `cpuacct`, `devices`, `memory`, `freezer`, `blkio`, `pids`, `systemd`.

**Step 13 — Optional: Android storage bind mount:**
```c
if (cfg->android_storage) android_setup_storage(".");
```
Bind-mounts `/storage/emulated/0` into the container.

**Step 14 — pivot_root:**
```c
syscall(SYS_pivot_root, ".", ".old_root");
chdir("/");
```
The current directory (`.`, which is the rootfs path) becomes the new root filesystem. The old root (`/` from Android's perspective) is moved to `.old_root` under the new root.

**Step 15 — Setup devpts:**
```c
setup_devpts(cfg->hw_access);
```
This MUST happen after `pivot_root` because devpts `newinstance` needs to be mounted inside the new root. Mounts at `/dev/pts` with options `gid=5,newinstance,ptmxmode=0666,mode=0620`. Then virtualizes `/dev/ptmx` by bind-mounting `/dev/pts/ptmx` over it.

**Step 16 — Network configuration (rootfs side):**
```c
fix_networking_rootfs(cfg);
```
Sets hostname (from `--hostname`), writes `/etc/hostname`, and generates `/etc/hosts`.

**The apt/sudo hostname fix:**
To prevent `apt` warnings and `sudo` resolution delays, Droidspaces explicitly maps the container's hostname to `127.0.1.1` in `/etc/hosts`. This is a critical fix for many Linux distributions where the loopback alias is expected.

It then reads DNS from `.old_root/.dns_servers` (written by `fix_networking_host()` before boot), writes `/run/resolvconf/resolv.conf` (ensuring proper null-termination for system stability), symlinks `/etc/resolv.conf`, and appends Android network groups (`aid_inet`, `aid_net_raw`, `aid_net_admin`) to `/etc/group`.


**Step 17 — Unmount old root:**
```c
umount2("/.old_root", MNT_DETACH);
rmdir("/.old_root");
```

**Step 18 — Write container marker:**
```c
write_file("/run/systemd/container", "droidspaces");
```
This is how systemd detects it's running in a container, and how Droidspaces validates a PID belongs to one of its containers (`is_valid_container_pid()` checks this file).

**Step 19 — Setup environment:**
```c
clearenv();
setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
setenv("TERM", "xterm-256color", 1);
setenv("HOME", "/root", 1);
setenv("container", "droidspaces", 1);
setenv("container_ttys", "/dev/pts/0 /dev/pts/1 ...", 1);
```
The `container_ttys` string is built from the PTY slave names recorded during allocation.

**Step 20 — Redirect stdio to console:**
```c
int console_fd = open("/dev/console", O_RDWR);
ds_terminal_set_stdfds(console_fd);     // dup2 to 0, 1, 2
ds_terminal_make_controlling(console_fd); // setsid() + TIOCSCTTY
```

**Step 21 — Exec init:**
```c
execve("/sbin/init", argv, environ);
// Fallback:
execve("/bin/sh", argv, environ);
```

After `execve`, the container is running. PID 1 is now systemd (or openrc), its stdio is connected to the console PTY, and the monitor process holds the master side of all PTYs.

---

## 5. UUID-Based PID Discovery and Multi-Container Support

### 5.1 The Problem

After `fork() + unshare() + fork()`, the parent process needs to know the _global_ PID of the container's init process. The monitor process knows the init PID in its local view, but due to PID namespace translation, this may differ from the host view.

### 5.2 The Sync Pipe Approach

In v3, the PID discovery has been simplified. The monitor process sends the init's PID directly to the parent via a sync pipe:

```c
/* Monitor side */
pid_t init_pid = fork();
write(sync_pipe[1], &init_pid, sizeof(pid_t));

/* Parent side */
read(sync_pipe[0], &cfg->container_pid, sizeof(pid_t));
```

Since the monitor has not entered a PID namespace itself (only its children do), the `init_pid` it observes is the host-global PID.

### 5.3 The UUID Marker (Boot Confirmation)

The UUID marker file at `/run/<uuid>` still serves an important purpose: boot confirmation. After saving the PID, the parent polls for the marker via `/proc/<pid>/root/run/droidspaces` to confirm the container has completed `pivot_root` before displaying status information:

```c
char marker[PATH_MAX];
snprintf(marker, sizeof(marker), "/proc/%d/root/run/droidspaces", cfg->container_pid);
for (int i = 0; i < 50; i++) {    /* 5 seconds max */
    if (access(marker, F_OK) == 0) break;
    usleep(100000);                /* 100ms */
}
```

### 5.4 UUID Scanning for Container Recovery

The `find_container_init_pid()` function retains the full UUID scan for container recovery and the `scan` command. It iterates over all PIDs in `/proc` and checks `build_proc_root_path(pid, "/run/<uuid>", ...)` with retry logic (20 retries, 200ms delay).

### 5.5 Why This Works for Multi-Container

Each container gets a unique UUID. The marker file exists only inside that specific container's mount namespace (in its `/run` tmpfs). The `/proc/<pid>/root` path traverses into the container's root filesystem from the host. So two containers with different rootfs paths will have different UUIDs, and the scan will find the correct PID for each.

### 5.6 PID Persistence and Strict Naming

The discovered PID is written to a pidfile at `<workspace>/Pids/<name>.pid`:
- Android: `/data/local/Droidspaces/Pids/ubuntu-24.04.pid`
- Linux: `/var/lib/Droidspaces/Pids/ubuntu-24.04.pid`

**Strict Naming Rules (v3.2.0+):**
- **Image Mode**: `--name` is now **mandatory** for containers starting from an image (`-i`). This ensures host-side infrastructure (like `/mnt/Droidspaces/<name>`) is perfectly descriptive.
- **Rootfs Mode**: If no name is provided, it is auto-generated from `/etc/os-release`.
- **Synchronization**: The final container name (resolved via `find_available_name()`) is used consistently for the PID file, the hostname, and the host-side mount point.

---

## 6. Init Execution and Terminal I/O Forwarding

### 6.1 The exec Call

```c
char *argv[] = {"/sbin/init", NULL};
execve("/sbin/init", argv, environ);
```

No arguments are passed to init. The environment is clean. The `container=droidspaces` environment variable tells systemd it's in a container. Fallback to `/bin/sh` if `/sbin/init` is not found.

### 6.2 Foreground Mode

When `--foreground` is specified:

1. The parent process puts the terminal into raw mode via `ds_setup_tios()`:
   ```c
   new_tios.c_iflag |= IGNPAR;
   new_tios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
   new_tios.c_lflag &= ~(TOSTOP | ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
   new_tios.c_oflag &= ~ONLCR;
   new_tios.c_oflag |= OPOST;
   ```

2. Then it enters `console_monitor_loop()`, which uses epoll to shuttle data between:
   - `STDIN_FILENO` (user input) → `console_master` (container input)
   - `console_master` (container output) → `STDOUT_FILENO` (user display)

3. The epoll loop also monitors a `signalfd` for:
   - `SIGCHLD` — detects when the intermediate or container process exits
   - `SIGINT` / `SIGTERM` — forwarded to the container's init PID
   - `SIGWINCH` — terminal resize is forwarded via `ioctl(TIOCSWINSZ)`

4. The loop terminates on `EPOLLHUP`/`EPOLLERR` on the master fd, or when `SIGCHLD` indicates the container has exited.

### 6.3 Background Mode

When foreground is not requested:

1. The **monitor process** (forked during start) is already running. It calls `waitpid(init_pid, ...)` and blocks until the container exits. The monitor inherits all PTY master FDs from the parent, keeping them alive for the container's lifetime.

2. The parent polls for the boot marker, calls `show_info()` to display container details, logs connection instructions, and exits.

3. When the container exits, the monitor process calls `cleanup_container_resources()` to remove pidfiles, unmount images, restore firmware paths, and restore Android optimizations.

### 6.4 How Getty/Login Works

In the current implementation, the login process (getty) attaches directly to `/dev/console`. Even after a user logs in, the session remains attached to the console.

**Note:** This is a stable but primitive implementation. A future improvement (TODO) is to properly spawn `agetty` on individual `/dev/ttyN` nodes. Pull Requests are welcome to refine this behavior!

---

## 7. TTY/PTY/Console Setup

### 7.1 The LXC Model

Droidspaces v3 allocates all PTYs **in the parent process** before forking, following the LXC approach. This is a fundamental improvement over v2, which allocated PTYs inside the container and sent master FDs back via `SCM_RIGHTS`.

```c
// terminal.c: ds_terminal_create()
int ds_terminal_create(struct ds_tty_info *tty) {
    openpty(&tty->master, &tty->slave, tty->name, NULL, NULL);
    fcntl(tty->master, F_SETFD, FD_CLOEXEC);
    fcntl(tty->slave, F_SETFD, FD_CLOEXEC);
    return 0;
}
```

Key points:
- `openpty()` allocates a master/slave pair from the host's `/dev/ptmx`
- Both FDs are marked `FD_CLOEXEC` so they don't leak to the container's init via `execve()`
- The slave name (e.g., `/dev/pts/3`) is recorded in `tty->name`

### 7.2 Console PTY

One dedicated PTY is allocated for `/dev/console`. Its slave path is bind-mounted to `dev/console` inside the rootfs before `pivot_root`:

```c
mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL);
```

After pivot_root, `internal_boot()` opens `/dev/console`, redirects stdin/stdout/stderr to it, and makes it the controlling terminal via `TIOCSCTTY`.

### 7.3 TTY PTYs (Placeholders)

Up to 6 PTYs are allocated for `/dev/tty1` through `/dev/tty6` and bind-mounted into the container. However, in the current version, these are **non-functional placeholders**. 

They exist primarily to satisfy Init systems like OpenRC, which expect these devices to exist and would otherwise flood the log with "not found" errors. They are currently "nuking errors" rather than providing multiple login seats.

### 7.4 devpts newinstance

After `pivot_root`, a **new** devpts instance is mounted at `/dev/pts` with the `newinstance` flag. This gives the container its own PTY numbering space, independent of the host. The container's `/dev/ptmx` is virtualized by bind-mounting `/dev/pts/ptmx` over it (or symlink as fallback).

The mount options tried (in fallback order):
1. `gid=5,newinstance,ptmxmode=0666,mode=0620`
2. `newinstance,ptmxmode=0666,mode=0620`
3. `gid=5,newinstance,mode=0620`
4. `newinstance,ptmxmode=0666`
5. `newinstance`

### 7.5 Why Parent-Side Allocation is Better

The v2 approach of allocating PTYs inside the container had several problems:
- Required `SCM_RIGHTS` FD passing over Unix sockets (complex and fragile)
- Required a socketpair to be created before fork and managed across process boundaries
- The `[ds-monitor]` daemon in v2 used a `while(kill(pid, 0) == 0) sleep(60)` polling loop just to hold FDs

The v3 approach is simpler: PTYs are allocated before `fork()`, inherited by the monitor process, and bind-mounted before `pivot_root`. No FD passing is needed. The monitor holds the master FDs alive naturally via `waitpid()`.

---

## 8. Hardware Access Mode

### 8.1 The Default: Isolated /dev (tmpfs)

Without `--hw-access`, the container gets a minimal `/dev` on a private tmpfs:

```c
domount("none", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC, "size=8M,mode=755");
```

Followed by `mknod()` for: `null`, `zero`, `full`, `random`, `urandom`, `tty`, `console`, `ptmx`, plus `net/tun`, `fuse`, and `tty1`-`tty6` mount targets.

### 8.2 Hardware Access: devtmpfs

With `--hw-access`, the container mounts the kernel's `devtmpfs`:

```c
domount("devtmpfs", dev_path, "devtmpfs", MS_NOSUID | MS_NOEXEC, "mode=755");
```

**CRITICAL:** `devtmpfs` is a shared singleton in the kernel. All instances share the same backing store. This means:
- The container can see **all** host devices (Binder, GPU, USB, etc.)
- Changes to the devtmpfs (unlink, mknod) affect **all** mountpoints

### 8.3 The Conflict Resolution Strategy

After mounting `devtmpfs`, certain nodes conflict with the container's needs (the container needs its own `console`, `ptmx`, etc. for PTY isolation). The solution:

```c
const char *conflicts[] = {"console", "tty", "full", "null", "zero",
                           "random", "urandom", "ptmx", NULL};
for (int i = 0; conflicts[i]; i++) {
    umount2(path, MNT_DETACH);   // Unmount any bind-mounts
    unlink(path);                 // Remove the node
}
```

Then `create_devices()` immediately recreates them as **real character devices** using `mknod()`:

```c
mknod(path, S_IFCHR | 0666, makedev(1, 3));  // null = 1:3
mknod(path, S_IFCHR | 0666, makedev(5, 2));  // ptmx = 5:2
// etc.
```

This is the legacy Droidspaces v2 strategy — proven safe because:
1. The `unlink` + `mknod` is atomic per-node (the host sees a brief gap, but the node is immediately restored)
2. The new nodes have the correct major:minor numbers, so they work identically
3. The `console` node (5:1) created by `mknod` serves as a mount target for the PTY bind-mount

### 8.4 devpts in HW Access Mode

When setting up `/dev/ptmx` after devpts mount, the HW access path differs:

```c
if (hw_access) {
    /* /dev is devtmpfs (shared). Do NOT unlink ptmx. Bind-mount over the
     * node that create_devices() already created. */
    mount("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND, NULL);
} else {
    /* /dev is private tmpfs. Safe to unlink and replace. */
    unlink("/dev/ptmx");
    write_file("/dev/ptmx", "");
    mount("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND, NULL);
}
```

### 8.5 /sys in HW Access Mode (The systemd 258+ Getty Fix)

Starting with version **258**, systemd changed its container detection logic. It now uses the Read-Only status of the top-level `/sys` mount as the primary indicator of a virtualized environment. 

**The Problem:**
In standard `--hw-access` mode, `/sys` was previously Read-Write. Systemd 258+ would misidentify the container as a "Physical Host" and trigger a hardware console resolution logic (`resolve_dev_console`). This logic reads `/sys/class/tty/console/active` and attempts to open the host's actual console (e.g., `/dev/tty0`). Since this device node is either missing or an invalid directory in the container, the startup of `console-getty.service` would fail with an **`EISDIR`** (Is a directory) error, causing an infinite restart loop.

**The Fix (Dynamic Hole-Punching):**
Droidspaces solves this by strictly adhering to the systemd Container Interface while preserving hardware transparency:
1. **Dynamic Iteration**: Droidspaces opens the `/sys` directory and iterates through all sub-entries using `readdir`.
2. **RW Pinning**: Every subdirectory (e.g., `/sys/devices`, `/sys/bus`, `/sys/class`, `/sys/block`) is **self-bind mounted** onto itself (`MS_BIND | MS_REC`). This "pins" them as independent mount points that preserve their Read-Write state.
3. **Container Signaling**: The top-level `/sys` mount is then remounted as **Read-Only** (`MS_RDONLY`).
4. **Result**: Systemd sees a RO `/sys` and correctly identifies the container environment, while the container processes retain full RW access to the hardware subsystems via the pinned sub-mounts.

Additionally, `/dev/null` is bind-mounted over `/sys/class/tty/console/active` to mask host TTY discovery entirely.

---

## 9. Entering a Running Container

### 9.1 The enter_namespace() Helper

Droidspaces v3 factored out namespace entry into a shared function used by both `enter` and `run`:

```c
int enter_namespace(pid_t pid) {
    const char *ns_names[] = {"mnt", "uts", "ipc", "pid"};
    int ns_fds[4];
    
    /* 1. Open ALL namespace FDs first (before any setns) */
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, ns_names[i]);
        ns_fds[i] = open(path, O_RDONLY);
    }
    
    /* 2. Enter namespaces */
    for (int i = 0; i < 4; i++) {
        if (ns_fds[i] >= 0) {
            setns(ns_fds[i], 0);
            close(ns_fds[i]);
        }
    }
    return 0;
}
```

**Critical detail:** All namespace FDs are opened **before** any `setns()` call. This is because entering one namespace (e.g., mount) changes the view of `/proc`, which could make subsequent namespace FD opens fail.

The mount namespace is mandatory; others are optional with warnings.

### 9.2 The "Native PTY" Attachment Model

In v3.1.2, the `enter` and `run` commands use a sophisticated PTY attachment model to bridge the host and container:

1. **Namespace Entry:** The intermediate process joins the container namespaces (`mnt`, `pid`, `ipc`, `uts`) using `enter_namespace()`.
2. **Native PTY Allocation:** While *inside* the container's private mount and PID namespaces, it allocates a new PTY pair using `openpty()`. This ensures the PTY is part of the container's private `devpts` instance.
3. **FD Passing:** The master side of this native PTY is passed back to the host process via `SCM_RIGHTS` over a Unix domain socket.
4. **Proxy Loop:** The host process remains on the host and runs an `epoll` proxy loop between its own terminal and the received container PTY master.
5. **Controlling TTY:** Inside the container, the intermediate process calls `setsid()` and `ioctl(TIOCSCTTY)` on the native PTY slave before forking the shell.

This fixes the "not a tty" errors and ensures that `tty` and `ps aux` show the correct `/dev/pts/N` device *relative to the container*.

### 9.3 The Double-Fork for PID Namespace

After `setns(CLONE_NEWPID)`, a `fork()` is required because the calling process remains in its original PID namespace:

```c
pid_t child = fork();           // First fork: joins PID namespace
if (child == 0) {
    enter_namespace(pid);
    pid_t shell_pid = fork();   // Second fork: actually in the container PID tree
    if (shell_pid == 0) {
        /* In PID 1 namespace view */
        execve("/bin/bash", argv, environ);
    }
    waitpid(shell_pid, NULL, 0);
}
waitpid(child, NULL, 0);
```


### 9.4 Shell Selection

The `enter` command tries shells in order: `/bin/bash` → `/bin/ash` → `/bin/sh`. If a user argument is provided, it uses `su -l <user>` instead.

### 9.5 Environment Setup

The `enter` and `run` commands set a clean environment:
- Standard `PATH`, `TERM=xterm-256color`, `HOME=/root`, `container=droidspaces`, `LANG=C.UTF-8`
- Sources `/etc/environment` if present (with proper KEY=VALUE and quote handling)

### 9.6 Run Command

`run_in_rootfs()` follows the same namespace entry pattern but executes the user-specified command instead of a shell. If the command string contains spaces and is a single argument, it's wrapped with `/bin/sh -c`.

---

## 10. Stop/Restart

### 10.1 Stop Sequence

`stop_rootfs()` in `container.c`:

**Step 1 — Resolve PID:**
Read pidfile, validate with `kill(pid, 0)`. Capture rootfs path for firmware cleanup while the process is still alive.

**Step 2 — Graceful shutdown:**
```c
kill(pid, SIGRTMIN + 3);
```
`SIGRTMIN+3` is the documented systemd signal for "halt immediately, don't shut down services". This is faster than running `poweroff` inside the container.

**Step 3 — Wait with escalation:**
```c
for (int i = 0; i < DS_STOP_TIMEOUT * 5; i++) {
    if (kill(pid, 0) < 0 && errno == ESRCH) { stopped = 1; break; }
    usleep(200000);  /* 200ms */
    if (i == 10) {
        kill(pid, SIGTERM);  /* Fallback after 2s */
    }
}
```

**Step 4 — Force kill:**
```c
if (!stopped) {
    kill(pid, SIGKILL);
}
```

`SIGKILL` to the container's init PID causes the kernel to tear down the entire PID namespace and kill all processes within it.

**Step 5 — Cleanup:**
```c
cleanup_container_resources(cfg, 0, skip_unmount);
```
- Restore Android optimizations (`android_optimizations(0)`)
- **Robust Cleanup (v3.2.1+)**: Adds a settle-time for lazy unmounts (`MNT_DETACH`) before attempting to remove host-side mount directories. This prevents leftover empty directories in `/mnt/Droidspaces/` when the kernel is slow to release loop devices.
- Remove firmware path entry
- **Cleanup Volatile Overlay**: Calls `cleanup_volatile_overlay()` to unmount the OverlayFS, unmount the workspace `tmpfs`, and recursively delete the temporary directory.
- Unmount rootfs.img if applicable (unless `skip_unmount` for restart)
- Remove `.mount` sidecar file
- Remove pidfile

### 10.2 Restart

Restart is simply stop + start with the `skip_unmount` flag:

```c
int restart_rootfs(struct ds_config *cfg) {
    stop_rootfs(cfg, 1);  // skip_unmount to keep rootfs.img attached
    return start_rootfs(cfg);
}
```

### 10.3 Multi-Container Stop

The `--name` flag accepts comma-separated names:
```
droidspaces --name=web,db,cache stop
```

Each name is parsed with `strtok()`, a subcfg is created, and `stop_rootfs()` is called individually.

---

## 11. Status Reporting

### 11.1 Status Command

`check_status()` resolves the pidfile (auto-detect if only one container running), reads the PID, validates with `kill(pid, 0)`, and checks `is_valid_container_pid()` (which reads `/proc/<pid>/root/run/systemd/container` for "droidspaces").

### 11.2 Info Command

`show_info()` provides detailed introspection:

1. **Auto-Resolution:** If no container name is specified, it auto-detects the running container if only one is found. If multiple are running, it lists them.
2. **Feature Introspection:** It detects active features by probing the container's `/proc/<pid>/root`:
   - **SELinux:** Reads `/sys/fs/selinux/enforce`.
   - **IPv6:** Checks `/proc/sys/net/ipv6/conf/all/disable_ipv6` inside the container.
   - **Android Storage:** Verifies the mount at `/storage/emulated/0`.
   - **HW Access:** Checks the `/dev` filesystem type.
3. **Guest OS Info:** Reads `/etc/os-release` from the container's rootfs (even if stopped).

### 11.3 Show Command

`show_containers()` lists all running containers in a table:

```
┌──────────────┬──────────┐
│ NAME         │ PID      │
├──────────────┼──────────┤
│ ubuntu-24.04 │ 12345    │
│ alpine-3.19  │ 23456    │
└──────────────┴──────────┘
```

It reads all `.pid` files from the Pids directory, validates each PID, removes stale pidfiles, and renders a Unicode box-drawing table with dynamic column widths.

### 11.4 Scan Command

`scan_containers()` scans *all* PIDs in `/proc` for processes that:
1. Pass `is_valid_container_pid()` (have `/run/systemd/container` containing "droidspaces")
2. Are init processes in their namespace (checked via `/proc/<pid>/status` NSpid field — last value is 1)
3. Are not already tracked in the Pids directory

Found containers are automatically registered with auto-generated names from `/proc/<pid>/root/etc/os-release`.

---

## 12. Build System Notes

### Compiler and Libc

The Makefile compiles with musl libc for maximum portability across Android devices:

```makefile
CFLAGS  = -Wall -Wextra -O2 -flto -std=gnu99 -Isrc -no-pie -pthread
CFLAGS += -Wno-unused-parameter -Wno-unused-result
LDFLAGS = -static -no-pie -flto -pthread
LIBS    = -lutil
```

Key flags:
- **`-static`**: Statically linked binary. No shared library dependencies.
- **`-no-pie`**: Non-position-independent executable. Some Android kernels have issues with PIE static binaries.
- **`-flto`**: Link-time optimization for smaller binary size.
- **`-std=gnu99`**: GNU C99 dialect (needed for `_GNU_SOURCE` features).
- **`-lutil`**: Links `libutil` for `openpty()` support.

### Cross-Compilation

Four architectures are supported:
- `x86_64`: `x86_64-linux-musl-gcc`
- `x86`: `i686-linux-musl-gcc`
- `aarch64`: `aarch64-linux-musl-gcc`
- `armhf`: `arm-linux-musleabihf-gcc`

Cross-compiler search order: `$MUSL_CROSS`, `~/toolchains/<triple>-cross/bin/`, `PATH`, `/opt/cross/bin/`.

### Output

All binaries go to `output/droidspaces`. An `all-build` target creates architecture-specific binaries (`droidspaces-x86_64`, `droidspaces-aarch64`, etc.). The `all-tarball` target creates a unified distribution with all architectures in a single archive.

---

## 13. Data Structures

### 13.1 struct ds_config

The central configuration struct, passed to every function (replaces all global variables from v2):

```c
struct ds_config {
    /* Paths */
    char rootfs_path[PATH_MAX];       /* --rootfs=   */
    char rootfs_img_path[PATH_MAX];   /* --rootfs-img= */
    char pidfile[PATH_MAX];           /* --pidfile= or auto-resolved */
    char container_name[256];         /* --name= or auto-generated */
    char hostname[256];               /* --hostname= or container_name */
    char uuid[DS_UUID_LEN + 1];       /* UUID for PID discovery */

    /* Flags */
    int foreground;                   /* --foreground */
    int hw_access;                    /* --hw-access */
    int enable_ipv6;                  /* --enable-ipv6 */
    int android_storage;              /* --enable-android-storage */
    int selinux_permissive;           /* --selinux-permissive */
    char prog_name[64];               /* argv[0] for logging */

    /* Runtime state */
    pid_t container_pid;              /* PID 1 of the container (host view) */
    pid_t intermediate_pid;           /* intermediate fork pid */
    int is_img_mount;                 /* 1 if rootfs was loop-mounted from .img */
    char img_mount_point[PATH_MAX];   /* where the .img was mounted */

    /* Terminal (console + ttys) */
    struct ds_tty_info console;
    struct ds_tty_info ttys[DS_MAX_TTYS];
    int tty_count;                    /* how many TTYs are active */
};
```

### 13.2 struct ds_tty_info

```c
struct ds_tty_info {
    int master;            /* master fd (stays in parent/monitor) */
    int slave;             /* slave fd (bind-mounted into container) */
    char name[PATH_MAX];   /* slave device path (e.g. /dev/pts/3) */
};
```

### 13.3 Constants

| Constant | Value | Purpose |
|---|---|---|
| `DS_MAX_TTYS` | 6 | Maximum TTY devices per container |
| `DS_UUID_LEN` | 32 | UUID hex string length |
| `DS_MAX_CONTAINERS` | 1024 | Maximum auto-naming suffix |
| `DS_STOP_TIMEOUT` | 8 | Seconds before SIGKILL |
| `DS_PID_SCAN_RETRIES` | 20 | UUID scan retry count |
| `DS_PID_SCAN_DELAY_US` | 200000 | 200ms between retries |

---

## 14. Recommended Reimplementation Approach

If you're rewriting Droidspaces from scratch, here is the checklist of every decision and every syscall, in order.

### Phase 1: Configuration

1. Parse CLI arguments into a `struct ds_config`:
   - `rootfs_path` or `rootfs_img_path`
   - `container_name` (auto-generate from os-release if not provided)
   - `hostname`
   - `foreground`, `hw_access`, `enable_ipv6`, `android_storage`, `selinux_permissive`

2. Validate rootfs path exists and contains `/sbin/init`

3. Resolve pidfile path: `<workspace>/Pids/<name>.pid`

4. Check if already running: `kill(saved_pid, 0)` + validate

5. Run requirements check: namespaces, devtmpfs, pivot_root

### Phase 2: Pre-Fork Setup (in parent)

6. Generate UUID (32 hex chars from `/dev/urandom`)

7. **Rootfs image mount**: If `-i` provided:
   - Identify host-side mount point at `/mnt/Droidspaces/<name>`.
   - `e2fsck -f -y <img>`
   - `mount -o loop,ro <img> <mount_point>` (RO is mandatory if `--volatile`).

8. **Allocate PTYs in the parent** (LXC model):
   - `openpty()` × (1 console + N TTYs)
   - Record slave names (e.g., `/dev/pts/0`, `/dev/pts/1`, ...)
   - Mark all FDs `FD_CLOEXEC`

9. Create sync pipe for PID communication

10. Android optimizations: `max_phantom_processes`, disable `deviceidle`

### Phase 3: Fork Monitor + Fork Init

11. `fork()` → monitor process

12. In monitor: `setsid()`, `prctl(PR_SET_NAME, "[ds-monitor]")`, `unshare(CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID)`

13. `fork()` again → PID 1 child (this is the process that becomes init)

14. Monitor sends `init_pid` to parent via sync pipe

15. Monitor: `waitpid(init_pid)`, then `cleanup_container_resources()`, then exit

### Phase 4: internal_boot() (runs as PID 1)

16. `unshare(CLONE_NEWNS)` — own mount namespace

17. `mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)` — prevent mount propagation

18. **Optional: Setup Volatile Overlay** (`setup_volatile_overlay()`):
    - Verify OverlayFS support.
    - Setup `upperdir`/`workdir` in a private `tmpfs`.
    - Mount OverlayFS over the rootfs and update `cfg->rootfs_path`.

19. Bind mount rootfs to itself: `mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL)`

20. **Optional: Custom Bind Mounts** (`setup_custom_binds()`):
    - Iterate entries and bind-mount host paths to container targets.
    - Use `mkdir_p` for automatic destination creation.

21. `chdir(rootfs)`

22. `mkdir(".old_root", 0755)`

23. Setup `/dev`:
    -   Without `--hw-access`: `mount("none", "<rootfs>/dev", "tmpfs", ...)` + `mknod()` for null, zero, full, random, urandom, tty, console, ptmx, net/tun, fuse
    -   With `--hw-access`: `mount("devtmpfs", "<rootfs>/dev", "devtmpfs", ...)`, unlink conflicting nodes, then recreate with `mknod()`

24. `mount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)`

25. Mount sysfs:
    -   Without `--hw-access`: mount RW → mount separate sysfs instance at `sys/devices/virtual/net` → remount parent RO
    -   With `--hw-access`: mount RW → **dynamic self-bind mount** all subdirectories → remount parent RO

26. `mount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755")`

27. **Bind-mount PTY slaves** from parent:
    -   `mount(cfg->console.name, "dev/console", NULL, MS_BIND, NULL)`
    -   `mount(cfg->ttys[i].name, "dev/tty<N>", NULL, MS_BIND, NULL)` for each TTY

28. `write_file("run/<uuid>", "init")` — UUID marker for PID discovery/boot confirmation

29. Setup cgroups: detect v2 or fall back to v1 with individual controllers

30. Optional: bind mount Android storage

31. **`syscall(SYS_pivot_root, ".", ".old_root")`**

32. `chdir("/")`

33. `mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "gid=5,newinstance,ptmxmode=0666,mode=0620")`

34. Setup `/dev/ptmx`: bind mount `/dev/pts/ptmx` → `/dev/ptmx`

35. Configure networking (rootfs side): hostname, DNS, resolv.conf, Android groups

36. `umount2("/.old_root", MNT_DETACH)`, `rmdir("/.old_root")`

37. `write_file("/run/systemd/container", "droidspaces")`

38. `clearenv()`, set `PATH`, `TERM`, `HOME`, `container`, `container_ttys`

39. Redirect stdio:
    ```c
    int fd = open("/dev/console", O_RDWR);
    dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
    setsid();
    ioctl(0, TIOCSCTTY, 0);
    ```

40. `execve("/sbin/init", {"init", NULL}, environ)`

### Phase 5: Parent — Post-Fork

41. Read `init_pid` from sync pipe

42. Configure host-side networking (ip_forward, IPv6, DNS file, iptables)

43. Save PID to pidfile

44. If foreground: enter console monitor loop (epoll: stdin↔pty_master, signalfd for SIGCHLD/SIGINT/SIGTERM/SIGWINCH)

45. If background: show info and exit (monitor process handles cleanup)

### Phase 6: Stop

46. Read PID from pidfile, validate

47. `kill(pid, SIGRTMIN + 3)` for systemd

48. Wait up to 8 seconds with 200ms polling

49. Escalate to `SIGTERM` after 2 seconds

50. If still alive: `kill(pid, SIGKILL)`

51. Cleanup: remove pidfile, unmount rootfs.img, restore firmware path, restore Android settings

### Phase 7: Enter

52. Read PID from pidfile
53. Open `/proc/<pid>/ns/{mnt,uts,ipc,pid}` — all FDs at once
54. `setns()` for each
55. `fork()` (required after `setns(CLONE_NEWPID)`)
56. In child: `fork()` again to actually be in the new PID namespace
57. `clearenv()`, set environment, source `/etc/environment`
58. `execve()` shell: try `/bin/bash`, `/bin/ash`, `/bin/sh`

---

## 13. Project Licensing & SPDX

The Droidspaces source code is licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**. 

Every source file in the `src/` directory and the `Makefile` contain standard SPDX-License-Identifier tags and copyright headers. A full copy of the license is provided in the `LICENSE` file at the root of the project.

**Copyright (C) 2026 Ravindu <ravindu644>**

---

## 14. Document License

This document is licensed under the Creative Commons Attribution 4.0 International (CC BY 4.0) License.
You are free to share and adapt the material for any purpose, even commercially, provided you give appropriate credit,
provide a link to the license, and indicate if changes were made.

---

**End of Document**

*This document was written by analyzing v3.2.0 of the Droidspaces source code — approximately 3,300 lines of C across 13 files. Every syscall, every mount, and every design decision described here was verified against the actual implementation.*
