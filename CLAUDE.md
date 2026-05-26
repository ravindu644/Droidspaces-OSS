# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Droidspaces is a lightweight, portable Linux container runtime (<400KB static musl binary) that runs full Linux environments with systemd/OpenRC as PID 1 on Android devices, Linux desktop, and Android recovery/ramdisks. It uses Linux kernel namespaces (PID, MNT, UTS, IPC, Cgroup) for isolation.

## Build System

### Backend (C static binary)
```bash
# Build for native architecture (requires musl toolchain)
make native

# Cross-compile for specific architectures
make x86_64    # amd64
make aarch64   # arm64
make armhf     # arm32
make x86       # i686
make riscv64   # RISC-V

# Build all architectures and create distribution tarball
make all-tarball

# Install musl toolchains for cross-compilation
./scripts/install-musl.sh <arch>   # arch: x86_64, aarch64, armhf, x86, riscv64

# Debug build with sanitizers (run on Linux host, not musl-static)
make debug-hardened

# Clean build artifacts
make clean
```

### socketd Backend (C++ daemon, optional)
```bash
cd src/socketd && make
```

### Android App (Kotlin/Jetpack Compose)
```bash
cd Android && ./build.sh debug     # debug APK
cd Android && ./build.sh release   # release APK (requires signing config)

# After building all backend binaries, sync to Android assets:
make sync-android
```

### Nix
```bash
nix build .#packages.x86_64-linux.default
nix build .#legacyPackages.x86_64-linux.muslBuilds.aarch64
```

## Architecture

### Backend (`src/`) — Single C binary, statically compiled against musl libc

The binary is structured around a single `struct ds_config` that holds all container configuration. The container lifecycle follows: `start_rootfs()` → `internal_boot()` → namespace creation fork → veth/NAT setup → pivot_root → exec init.

Key source modules:

| File | Responsibility |
|------|---------------|
| `main.c` | CLI argument parsing, usage/help, command dispatch |
| `container.c` | Lifecycle: `start_rootfs`, `stop_rootfs`, `enter_namespace`, `enter_rootfs`, `run_in_rootfs`, `restart_rootfs`, `show_info` |
| `boot.c` | `internal_boot()` — the container entry point: cgroup attach, pivot_root, exec init |
| `config.c` | Load/save/validate container `.config` files (KEY=VALUE format, stored in workspace/Containers/<name>/<name>.config) |
| `mount.c` | Bind mounts, OverlayFS volatile mode, jail masks, `/dev` setup, rootfs.img loop mount |
| `network.c` | Host networking fixups, NAT veth/bridge lifecycle, cleanup, DNS, IPv6 detection |
| `ds_netlink.c` | Raw RTNETLINK wrapper — bridge create, veth pairs, IP/route/rule management, kernel capability probing |
| `ds_iptables.c` | iptables-wrapper for MASQUERADE, port forwarding, MSS clamping |
| `ds_dhcp.c` | Single-lease DHCP server (AF_PACKET) for NAT mode |
| `cgroup.c` | Cgroup v1/v2 detection, hierarchy bootstrap, per-container sub-hierarchy, limit application, usage collection |
| `seccomp.c` | BPF seccomp filters (Android FBE keyring resolver, nested namespace deadlock shield) |
| `android.c` | Platform detection (`is_android()`), SELinux patching, Android storage setup, `android_seccomp_setup` |
| `pid.c` | Workspace paths, PID file management, container scanning/metadata sync, container naming |
| `hardware.c` | GPU node mirroring, GPU GID scanning, Termux-X11 socket setup, unified tmpfs for /dev |
| `terminal.c` | PTY allocation (`ds_openpty`), TTY creation, terminal proxy loop |
| `console.c` | Foreground mode console monitor loop, signal handling |
| `environment.c` | `/etc/environment` loading, env file parsing |
| `utils.c` | Path resolution, UUID generation, PID collection, `/etc/os-release` parsing, recursive directory operations |
| `check.c` | Requirements checking, detailed system audit |
| `daemon.c` | Background daemon mode, client communication, daemon probe |
| `documentation.c` | Built-in interactive documentation |
| `virtualize.c` | Container resource virtualization (memory, CPU, PIDs), usage tracking, PID namespace inode tracking |
| `socketd_bridge.c` | Optional bridge to the external C++ socketd process |
| `droidspace.h` | Master header — all structs, enums, constants, and function prototypes |

#### The `struct ds_config` (defined in `droidspace.h`)
Central configuration structure holding: rootfs paths, container name/hostname, networking mode, DNS, flags (foreground, hw_access, gpu, volatile, etc.), runtime state (PID, mount point, init type), bind mounts, port forwards, environment variables, resource limits, and more.

### Android App (`Android/`) — Kotlin + Jetpack Compose

Min API 26 (Android 8), target SDK 34. Uses MVVM architecture.

**Key packages:**
- `com.droidspaces.app.ui.screen.*` — Compose screens (container list, config, details, terminal, installation, settings, etc.)
- `com.droidspaces.app.ui.component.*` — Reusable Compose components (cards, dialogs, pickers)
- `com.droidspaces.app.ui.viewmodel.*` — ViewModels (AppState, ContainerInstallation, ContainerUsage, Container, RootfsRepo, SystemStats)
- `com.droidspaces.app.util.*` — Utilities: `ContainerManager` (CLI wrapper), `BinaryInstaller`, `ContainerCommandBuilder`, `RootfsRepository`, `RootfsDownloadManager`, `SparseImageInstaller`, `PreferencesManager`, etc.
- `com.droidspaces.app.service.TerminalSessionService` — Terminal backend service
- `com.droidspaces.app.ui.terminal.*` — Terminal emulator UI (based on ReTerminal/Termux)

**Assets** (`Android/app/src/main/assets/`): pre-built busybox/magiskpolicy binaries per arch, boot-module scripts (Magisk module), shell scripts for systemd/openrc checks, sparse image management, bug reports.

### socketd (`src/socketd/`) — C++ backend daemon (optional, private)

Provides a TCP API server (default 2375) for external management. Components:
- `api_server.cpp/.h` — TCP listener, client handling
- `backend_client.cpp/.h` — Client to communicate with the main droidspaces socketd
- `container_list.cpp/.h` — Container list management
- `snapshot_lists.cpp/.h` — Container state snapshots
- `event_log.cpp/.h` — Event logging

### Init System Integration (`init/`)

- `android-service/` — SELinux CIL policies for boot-time daemon mode, init.rc scripts for native Android system integration (auto-spawn containers at boot via `vendor/etc/init/init.droidspaces.rc`)
- `systemd-service/` — systemd service unit for Linux desktop
- `README.md` — Developer guide for init configuration

### Nix (`nix/` and `flake.nix`)

Flake-based builds for both the binary (via nix) and NixOS rootfs images used as container templates:
- `bin.nix` — Package derivation for the droidspaces binary (musl-cross for all architectures)
- `nixos.nix` — NixOS module for container rootfs images (minimal systemd configs for Android-compatible containers)
- `vm.nix`, `finix.nix`, `systemd.nix`, `android.nix`, `defaults.nix`

## Key Design Constraints

1. **Min supported kernel: 3.10** — No `clone3`, `openat2`, `pidfd_*`, or features gated behind 5.x allowed without fallback
2. **Dual platform** — `is_android()` must guard all Android-specific code; Linux desktop code must be guarded similarly
3. **Cgroup v1 AND v2** — Both hierarchies must remain functional
4. **Static musl build** — No libc dependencies; single ~400KB binary
5. **Android min API 26** — App must work on Android 8+
6. **Config files** — Container configs stored as KEY=VALUE in `workspace/Containers/<name>/<name>.config`; CLI flags override config values

## Testing and Validation

- Builds are validated via GitHub Actions CI (`.github/workflows/ci.yml`) — builds all architectures + Android APK
- Check kernel requirements: `sudo ./droidspaces check`
- PRs must be tested on real Android hardware with old kernels (3.10+) — state devices/kernels tested
- Bug reports should include device, SOC, kernel version, Android version
- No formal test framework exists; validation is manual on real hardware

## Documentation Files

Key docs in `Documentation/`:
- `Linux-CLI.md` — Full CLI reference
- `Features.md` — Feature deep dives
- `Kernel-Configuration.md` — Kernel config requirements for Android
- `GPU-Acceleration.md` — GPU setup guide
- `Installation-Android.md`, `Installation-Linux.md` — Installation guides
- `Troubleshooting.md` — Common issues
- `Nix-NixOS.md` — Nix build documentation
