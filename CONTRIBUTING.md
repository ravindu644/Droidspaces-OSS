English | [简体中文](./Documentation/zh-CN/CONTRIBUTING.md)

# Contributing to Droidspaces

## Philosophy

> A feature that doesn't exist is better than a broken implementation.

Droidspaces runs on two distinct platforms: Android, on hardware ranging from ancient
vendor-frozen 3.10 kernels to modern GKI devices across dozens of SoCs and OEMs, and Linux
desktop environments. A patch that works on your setup and breaks on someone else's is not a
contribution, it is a regression. Every change introduced into core must uphold this
contract without exception.

The second half of that philosophy is about the codebase itself. Ten lines that do the job
beat a hundred that do the same job. Reuse beats rewriting. The largest cleanup this project
ever needed came from writing new code beside existing code instead of extending it, and it
cost 1,550 deleted lines and a class of host-root command injection bugs to undo. The
[Reuse Inventory](#reuse-inventory) below exists so you never have to guess whether
something already exists.

## Platform Scope

Droidspaces core must work correctly on both Android and Linux. These are not
interchangeable environments and must be treated as separate targets.

### Platform Detection

The codebase provides `is_android()` for runtime platform detection:

```c
int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  /* Priority 1: Check for recovery environment (e.g., TWRP) */
  if (access("/system/bin/recovery", F_OK) == 0) {
    cached_result = 0;
  }
  /* Priority 2: Check for core Android system markers */
  else if (access("/system/build.prop", F_OK) == 0 ||
           access("/system/bin/app_process", F_OK) == 0) {
    cached_result = 1;
  }
  /* Fallback: Not a standard Android environment */
  else {
    cached_result = 0;
  }

  return cached_result;
}
```

**Any feature or behavior exclusive to Android must be guarded with `is_android()`.**
**Any feature or behavior exclusive to Linux desktop must likewise be guarded.**

Do not assume the runtime environment. Do not let Android-specific code execute on Linux or
vice versa. Unguarded platform assumptions will cause the PR to be rejected.

## Kernel Compatibility

**Minimum supported kernel: 3.10**

This is a hard floor, not a suggestion. If your implementation depends on a syscall, a
`/proc` interface, a namespace feature, or a kernel config that does not exist on 3.10, it
will not be merged into core.

- Do not use `openat2(2)`, not available before 5.6.
- Do not rely on cgroup v2 exclusively. cgroup v1 must remain functional.
- Do not assume `clone3(2)`, `pidfd_*`, or any API gated behind 5.x.
- If a fallback path exists, implement it. If it does not, the feature does not belong in
  core.

This applies to both Android and Linux targets. A modern desktop kernel does not exempt your
patch from this requirement.

Test your changes on real hardware running old kernels. Emulators and modern stock kernels
are not sufficient validation.

## SoC and OEM Coverage (Android)

Droidspaces runs on Qualcomm, Exynos, MediaTek, and Unisoc silicon, under OEM kernels that
deviate significantly from mainline. Your patch must be tested across a representative
spread of this landscape before submission.

State explicitly in your PR which devices and kernel versions you have tested on. Untested
claims of compatibility will be treated as untested.

Patches that address a quirk specific to one SoC family or OEM kernel are acceptable **only
if Droidspaces can adapt to the quirk at runtime**, via detection, a conditional code path,
or a graceful fallback, without regressing behavior on unaffected hardware. If the fix
cannot be generalized in this way, it belongs in a downstream fork, not in core.

## Android App Changes

The app has a minimum requirement of **Android 8 (API 26)**. Changes to the Android app must
not introduce any dependency, API call, or behavior that breaks on Android 8.

Test on Android 8 before opening a PR. Testing only on a recent Android release is not
sufficient.

## Building

### C backend

```
make native            # build for the host architecture
make aarch64           # cross builds: aarch64, x86_64, armhf, x86, riscv64
make debug-hardened    # ASan, UBSan, LSan build for chasing memory bugs
make all-build         # every architecture, then syncs into the APK assets
make all-tarball       # all-build plus a release tarball
make format            # clang-format over src/, using the repo .clang-format
make clean
```

Bare `make` prints help and builds nothing. `make native` needs a musl toolchain; if it is
missing, run `./install-musl.sh <arch>` as the error message tells you.

Useful options: `V=1` for full compiler command lines, `ENABLE_SOCKETD_BACKEND=0` for a
minimal build without the private API bridge.

The build runs with `-Wall -Wextra -Wpedantic -Werror` plus format, shadow, and truncation
warnings. A warning is a build failure, not a review comment.

Adding a new `.c` file means adding it to `SRCS` in the Makefile. There is no wildcard.

The version string is scraped from `DS_VERSION` in `src/include/droidspace.h`. Bump it
there, not in the Makefile.

### Android app

From `Android/`:

```
./build.sh             # debug APK
./build.sh release     # signed release APK
```

Use the script rather than calling gradle directly. It sets up the wrapper, cleans, and
places the APK where the rest of the tooling expects it.

### CI

`.github/workflows/ci.yml` runs `make all-tarball`, `scripts/mkdeb.sh`, and the gradle
release build. It does **not** check formatting, so running `make format` is your
responsibility.

## Code Style

### No em-dashes

Not in code, not in comments, not in commit messages, not in documentation. Use a comma, a
full stop, or rewrite the sentence.

### No ASCII banner comments

No rows of `-----`, no `=====`, no boxed section headers. The SPDX licence block at the top
of each file stays. Existing banners in `src/` predate this rule and are being removed
separately. Do not add more.

### Comments sound like a person

Say why the code does what it does, or what breaks if it does not. A comment that restates
the line below it is noise.

```c
/* Bad */
/* increment the counter */
count++;

/* Good */
/* netd wipes our rules whenever it restarts, so re-assert them every cycle */
install_policy_rules(cfg);
```

The backend already does this well in places. `src/include/droidspace.h` documents ordering
constraints (`ds_ksu_neutralize_root_escape` must run before `ds_seccomp_apply_minimal`, and
says why), and `ds_bind_mount_socket` explains the `O_NOFOLLOW` choice in terms of the race
it closes. That is the bar.

### Ten lines beat a hundred

Delete before you add. The smallest change in the right place is the goal. The smallest
change in the wrong place is a second bug.

### C conventions

- C89 block comments (`/* ... */`). There are no `//` comments in `src/`.
- `static` by default. A function becomes non-static only when it is added to
  `src/include/droidspace.h`.
- New cross-module functions take the `ds_` prefix. Legacy un-prefixed names (`read_file`,
  `mkdir_p`, `run_command`, `domount`) stay as they are; do not rename them and do not
  imitate them for new work.
- `is_*` for predicates, `check_*` for probes that may print, `setup_*` and `cleanup_*` for
  paired lifecycle, `print_*` for pure output, `free_*` for teardown.
- Constants are `DS_` plus SCREAMING_SNAKE. Structs are `struct ds_*`. The `_t` suffix is
  only for typedefs.
- Header guards are named `#ifndef DROIDSPACE_H` style with a matching
  `#endif /* DROIDSPACE_H */`. No `#pragma once`.
- There is no `xmalloc` family. Use plain `malloc` with an explicit NULL check, and prefer
  fixed-size buffers wherever the size is bounded. `struct ds_config` is almost entirely
  fixed-size for this reason.
- Every heap-owning API documents its free function. Follow that pattern.

### Kotlin conventions

- State lives in a ViewModel, not in a stateful composable and not in a `util` singleton.
- Lists render with `LazyColumn` and stable keys.
- Colors come from `MaterialTheme.colorScheme`, type from `MaterialTheme.typography`, animation
  timings from `AnimationUtils`. Do not hardcode them. Corner radii, spacing and every other
  visual value come from [DESIGN.md](./DESIGN.md).
- Any value that reaches a root shell goes through `ContainerCommandBuilder.quote()` or an
  allow-list validator. No exceptions.

## Commit Conventions

- Run `make format` before committing any `.c` or `.h` change.
- Sign off every commit: `git commit -s`.
- Do not add a `Co-Authored-By:` trailer for an AI agent. Human co-authors are fine.
- Write a subject in the imperative mood, and a body explaining why when the change is not
  self-evident.

| Prefix | Use for |
| --- | --- |
| `app:` | Android app changes, with `app: fix:` and `app: refactor:` for those cases |
| `fix:` | backend bug fixes |
| `refactor:` | backend restructuring with no behavior change |
| `feat:` | new backend features |
| `docs:` | documentation |
| `net:` `mount:` `seccomp:` `daemon:` `socketd:` | scoped backend subsystem changes |
| `fix(security):` | anything security related, either half |

## Reuse Inventory

Grep this list before you write anything new. If something close already exists, extend it
rather than adding a sibling. Android paths are relative to
`Android/app/src/main/java/com/droidspaces/app/`, C paths to the repository root.

This list answers "what do I call". [DESIGN.md](./DESIGN.md) answers "what should it look like",
for the case where nothing here fits and you have to build something new.

### Android: forms and reusable screens

| Symbol | Path | Use it when |
| --- | --- | --- |
| `ContainerConfigForm(state, onStateChange, ...)` | `ui/component/ContainerConfigForm.kt` | Any screen that edits container settings. Add new options inside this file, never in a second form |
| `ContainerConfigState` | `util/ContainerConfigState.kt` | The single source of truth for editable config. Add a field here, not to a parallel state class |
| `ContainerInfo.toConfigState()` / `.withConfig(state)` | `util/ContainerConfigState.kt` | Prefill the form from a container, or write edits back |
| `InitServiceScreen(containerName, titleRes, isAvailable, fetchRows, filters, ...)` | `ui/screen/InitServiceScreen.kt` | Supporting a new init system. Supply the lambdas, do not write a new screen |
| `InitServiceRow`, `InitServiceUiStatus`, `InitCommandResult`, `InitServiceMenuAction`, `InitServiceFilterChip` | `ui/screen/InitServiceScreen.kt` | Mapping a manager's service list into the shared screen. Status colors and filter counts derive from these, so they cannot drift |
| `GatewaySettingsSection(visible, config, onConfigChange, ...)` + `GatewayConfig` | `ui/component/GatewaySettingsSection.kt` | The gateway networking block. Already embedded in the config form |
| `PortForwardingList(portForwards, onPortForwardsChange)` | `ui/component/PortForwardingList.kt` | Editable port forward list, add dialog included |
| `UpstreamInterfaceList(upstreamInterfaces, onInterfacesChange)` | `ui/component/UpstreamInterfaceList.kt` | Editable upstream interface chips |
| `DsDropdown(label, selected, options, displayName, onSelect, ...)` | `ui/component/DsDropdown.kt` | Any select field. Never hand-roll `ExposedDropdownMenuBox` |
| `DsMenuTheme { }` + `Modifier.dsMenuBorder()` | `ui/component/DsMenuTheme.kt` | Any `DropdownMenu` that needs the opaque menu surface. `DsDropdown` already applies it |
| `DsTextFieldDefaults.colors()` / `.surfaceColors()` | `ui/component/DsTextFieldDefaults.kt` | Every `OutlinedTextField`. `colors()` on screens, `surfaceColors()` inside dialogs |
| `FocusUtils`, `rememberClearFocus()`, `ClearFocusOnClickOutside` | `ui/util/FocusUtils.kt` | IME actions and dismissing the keyboard on outside taps |

### Android: dialogs

| Symbol | Path | Use it when |
| --- | --- | --- |
| `DsDialog(onDismiss, modifier, borderColor, scrollableContent, footer) { }` | `ui/component/DsDialog.kt` | Every dialog. Actions go in `footer`, never in the content, or they get squeezed off a short screen. Never set a width, padding or scroll |
| `DialogDismissButton(label, onDismiss)` | `ui/component/DialogFooterRow.kt` | The `footer` of a dialog whose only action is close |
| `DialogCloseButton(onClick, enabled)` | `ui/component/DialogCloseButton.kt` | The 36.dp close square in a dialog's header row, for info pages that close from the top (terminal log viewer, About) |
| `DialogFooterRow(dismissLabel, confirmLabel, onDismiss, onConfirm, confirmEnabled, destructive)` | `ui/component/DialogFooterRow.kt` | Every dialog's cancel and confirm row, thirteen call sites. Pass `destructive = true` for a delete or a wipe, never a colour |
| `FilePickerDialog(onDismiss, onConfirm, title, showFiles)` | `ui/component/FilePickerDialog.kt` | Picking a host path or file |
| `EnvironmentVariablesDialog(initialContent, onConfirm, onDismiss, ...)` | `ui/component/EnvironmentVariablesDialog.kt` | Key and value environment editor |
| `PrivilegedModeDialog`, `HardwareAccessDialog` | `ui/component/` | Opt-in flows that need a typed confirmation phrase |
| `DangerousWarningCard(title, text)` + `ConfirmPhraseField(value, onValueChange, isError)` | `ui/component/DangerousActionConfirm.kt` | Building a destructive confirmation. Compose these two with `DialogFooterRow` |
| `TerminalDialog(title, logs, onDismiss, onClear, isBlocking)` | `ui/component/TerminalDialog.kt` | Showing live or streaming command output |
| `ProgressDialog(message)` / `ErrorLogsDialog(logs)` | `ui/util/DialogUtils.kt` | Blocking spinner, or a failed command's log lines |
| `RootfsRepoSheet(onDismiss, onInstall)` | `ui/component/RootfsRepoSheet.kt` | The rootfs repo browser, search and repo manager included |
| `BugReportDialog(onDismiss)` | `ui/component/BugReportDialog.kt` | Bug report capture |

Some dialogs are private to their screen (`UninstallConfirmationDialog` and `SparseSizeDialog`
in `ui/screen/ContainersScreen.kt`, `UserPickerDialog` in `ui/screen/ContainerTerminalScreen.kt`,
the language and about dialogs in `ui/screen/SettingsScreen.kt`). Do not import or copy them.

### Android: bars, scaffolds, feedback

| Symbol | Path | Use it when |
| --- | --- | --- |
| `PrimaryActionBottomBar(label, icon, onClick, ...)` | `ui/component/PrimaryActionBottomBar.kt` | Any wizard or full screen "Next", "Install", "Continue" bar. Seven screens use it. The overload taking a `content` slot is for buttons that swap their contents |
| `SaveActionBottomBar(isSaved, isSaving, canSave, onSave, ...)` | `ui/component/SaveActionBottomBar.kt` | A save bar with the save, saving and saved states |
| `PullToRefreshWrapper(onRefresh) { ... }` | `ui/component/PullToRefreshWrapper.kt` | Any pull to refresh list or tab body |
| `showSuccess/showError/showInfo(snackbarHostState, message)` | `ui/util/SnackbarUtils.kt` | All snackbars. Never call `showSnackbar` directly |

### Android: cards and list items

| Symbol | Path | Use it when |
| --- | --- | --- |
| `SettingsCard(title, onClick, icon, subtitleContent, trailing, ...)` | `ui/component/SettingsCard.kt` | The base for every settings or option row. Build new variants on top of it |
| `SettingsRowCard`, `ToggleCard` | `ui/component/` | Clickable row, or switch row. Both are thin wrappers over `SettingsCard` |
| `SwitchItem` | `ui/component/SwitchItem.kt` | Flat `ListItem` switch row, used on the Settings screen. See the duplicates note below |
| `ContainerCard(container, actions, ...)` + `ContainerCardActions` | `ui/component/ContainerCard.kt` | The expandable container row. Add new actions to `ContainerCardActions`, not as new parameters |
| `RunningContainerCard(container, onEnter, onTerminalClick, osInfo)` | `ui/component/RunningContainerCard.kt` | Compact running container card on the control panel |
| `DroidspacesStatusCard(status, version, ...)` + `DroidspacesStatus` | `ui/component/DroidspacesStatusCard.kt` | Backend or module status hero card |
| `SystemInfoCard`, `ContainerUsersCard`, `HelpCard` | `ui/component/` | Host resource card, container user management, getting started block |
| `EmptyState(icon, title, description)`, `ErrorState`, `RootUnavailableState` | `ui/component/EmptyState.kt` | Any empty list, backend unavailable state, or missing root state |

### Android: status and indicators

| Symbol | Path | Use it when |
| --- | --- | --- |
| `StatusPill(label, color)` | `ui/component/StatusPill.kt` | Any small status chip or badge |
| `SectionHeader(text)` | `ui/component/SectionHeader.kt` | Any heading above a group of cards. Spacing goes on the modifier |
| `CardContentPadding`, `CardHeaderHeight` | `ui/component/CardMetrics.kt` | Any card with a title-and-pill header. Keeps the dividers aligned across tabs, do not retype the values |
| `LoadingIndicator(size, color)` + `LoadingSize` | `ui/util/LoadingIndicator.kt` | Inline spinners. Pick a `LoadingSize`, never a raw `.size(n.dp)` |
| `FullScreenLoading(message)` | `ui/util/LoadingIndicator.kt` | Whole screen loading state |
| `ContainedLoadingIndicator`, `LoadingIndicatorDefaults`, `MaterialShapes` | `ui/util/LoadingIndicator.kt` | Determinate and morphing indicators, and their tokens |
| `TerminalConsole(logs, isProcessing, maxHeight)` | `ui/component/TerminalConsole.kt` | Inline scrolling log view |
| `ShimmerAnimation(enabled) { ... }` | `ui/component/TerminalConsole.kt` | Skeleton loading effect |
| `PercentCircle(percent, size, strokeWidth, ...)` | `ui/component/PercentCircle.kt` | Circular percentage gauge. Currently unused, reuse it before writing another |

### Android: theme

| Symbol | Path | Use it when |
| --- | --- | --- |
| `DroidspacesTheme(darkTheme, dynamicColor, amoledMode, themePalette)` | `ui/theme/Theme.kt` | The single theme root, applied in `MainActivity` |
| `rememberThemeState()` + `ThemeState` | `ui/theme/ThemeStateHolder.kt` | Reading live theme preferences |
| `ThemePalette` | `ui/theme/Color.kt` | Adding an accent palette. Here and nowhere else |
| `MaterialTheme.colorScheme.*` | | All colors. `ui/theme/Color.kt` holds only `AMOLED_BLACK` and the palettes now |
| `MaterialTheme.typography.*`, `JetBrainsMono` | `ui/theme/Type.kt` | All text styles, and the mono font for terminal, log, and code text |
| Corner radii, spacing, type roles | [DESIGN.md](./DESIGN.md) | Every visual value. There is no shape token object, the numbers live in DESIGN.md |
| `AnimationUtils` | `util/AnimationUtils.kt` | Durations, easing, and tween specs. Never a literal `tween(300)` |
| `AccentColorPicker`, `ColorPaletteSwatch` | `ui/component/` | The palette picker in settings |

There is no spacing token object. Padding is written as literal dp, following the existing
conventions: 24.dp for dialog and screen horizontal padding, 16.dp for card inner padding,
8.dp and 12.dp between rows.

### Android: navigation

| Symbol | Path | Use it when |
| --- | --- | --- |
| `Screen` sealed class | `ui/navigation/DroidspacesNavigation.kt` | Adding a destination. Never a raw route string |
| `Screen.X.createRoute(...)` | same | Building a route. Arguments are URI encoded for you |
| `DroidspacesNavigation(navController, ...)` | same | The single `NavHost`. Register new screens here |

### Android: shell and root execution

| Symbol | Path | Use it when |
| --- | --- | --- |
| `ContainerCommandBuilder.quote(value)` | `util/ContainerCommandBuilder.kt` | **Every** dynamic value going into a root command. POSIX single-quote wrap that escapes embedded quotes |
| `ContainerCommandBuilder.buildStart/Stop/Restart/Usage/GetIpCommand(...)` | same | Driving the backend binary. Do not assemble these by hand |
| `ContainerCommandBuilder.getConfigPath(container)` | same | The canonical container config path |
| `ContainerOperationExecutor.executeCommand(command, operation, logger, ...)` | `util/ContainerOperationExecutor.kt` | Any long root command whose output must stream into the UI |
| `ContainerOperationExecutor.checkCommandSuccess(command)` | same | Fire and check, no logging |
| `ContainerLogger` / `ViewModelLogger(onLog)` | `util/ContainerLogger.kt` | The log sink every installer and executor takes |
| `ContainerRuntime.scan()` | `util/ContainerRuntime.kt` | Reconciling on-disk containers. The intended gateway to the backend binary |
| `Constants.getDroidspacesCommand()` | `util/Constants.kt` | Resolving the binary in PATH versus its full path |

The global libsu configuration lives in `DroidspacesApplication.kt`. That is the only place
it should be set.

### Android: validation and security

| Symbol | Path | Use it when |
| --- | --- | --- |
| `ServiceManagerBase.isSafeServiceName(name)` | `util/ServiceManagerBase.kt` | Any service or unit name that came from inside a container, before it reaches a shell. Fails closed |
| `ValidationUtils.validateContainerName` / `isSafeContainerName` / `normalizeContainerName` | `util/ValidationUtils.kt` | Container names. Normalize first, then validate. Both fail closed |
| `ValidationUtils.validateHostname` / `sanitizeHostname` | same | Hostnames |
| `ValidationUtils.validateConfigValues(config)` | same | Before writing a config. Rejects control characters in single-line values |
| `ValidationUtils.validateGatewayConfig(...)` + `GatewayErrors` | same | Gateway collision rules across containers |
| `ValidationUtils.effGatewayNet/Iface/Bridge` | same | Deriving gateway defaults. These mirror the C runtime, do not re-derive them |
| `ValidationResult` | same | The shared success and error result type |
| `ContainerManager.sanitizeContainerName(name)` | `util/ContainerManager.kt` | Path shaping only. This is **not** a security validator, pair it with `isSafeContainerName` |

`ContainerInstaller.validateRootfsTarball` fails closed if the validation script is missing.
`FilePickerUtils.isValidTarball` checks the file extension only and is not a security
boundary.

### Android: data, repositories, preferences

| Symbol | Path | Use it when |
| --- | --- | --- |
| `ContainerManager` | `util/ContainerManager.kt` | Container discovery, config parsing, status, updates, uninstall |
| `ContainerInfo`, `BindMount`, `PortForward`, `ContainerStatus` | same | The container model. `ContainerInfo.toConfigContent()` is the single config serializer, never hand-write config lines |
| `DaemonModeRepository` | `util/DaemonModeRepository.kt` | Reading and writing the daemon mode flag |
| `RootfsRepository.fetchAllAssets(context)` + `RootfsAsset` | `util/RootfsRepository.kt` | Fetching official and user rootfs repos |
| `PreferencesManager.getInstance(context)` | `util/PreferencesManager.kt` | All settings persistence. Collect `daemonModeFlow` and `symlinkEnabledFlow` rather than registering your own preference listener |
| `Constants` | `util/Constants.kt` | Every path, preference key, and default. Never re-declare a literal |
| `ContributorManager`, `Contributor`, `Language` | `util/` | Contributor list and language model |

### Android: device and platform

| Symbol | Path | Use it when |
| --- | --- | --- |
| `DeviceArch.suffix()` / `.displayName()` | `util/DeviceArch.kt` | Any ABI mapping. Matches both the binary suffix and the rootfs repo architecture field |
| `SystemInfoManager` | `util/SystemInfoManager.kt` | Kernel version, architecture, Android version, SELinux status, root provider version, backend version and mode. All cached |
| `RootChecker` / `RootStatus` | `util/RootChecker.kt` | Root availability |
| `StorageChecker` | `util/StorageChecker.kt` | Free space checks |
| `DroidspacesChecker` / `DroidspacesBackendStatus` | `util/DroidspacesChecker.kt` | Backend install state and update availability |
| `LocaleHelper` | `util/LocaleHelper.kt` | Language listing and switching |
| `SELinuxChecker` | `util/SELinuxChecker.kt` | Overlaps `SystemInfoManager.getSELinuxStatus()`. Prefer the latter, it caches |

### Android: init system managers and container introspection

All three init managers share the same shape. The container name is always quoted through
`ContainerCommandBuilder`, and the service name is always allow-listed through
`ServiceManagerBase`.

| Symbol | Path | Use it when |
| --- | --- | --- |
| `ContainerSystemdManager` | `util/ContainerSystemdManager.kt` | systemd units. Also owns unit inspection and drop-in override read, write, delete. The base64 streaming in `setOverrideConf` is the pattern to copy for arbitrary text |
| `ContainerOpenRCManager` | `util/ContainerOpenRCManager.kt` | OpenRC services |
| `ContainerProcdManager` | `util/ContainerProcdManager.kt` | procd services. The only manager that also allow-lists the action |
| `ContainerProcessManager` | `util/ContainerProcessManager.kt` | Process list and kill inside a container |
| `ContainerUsersManager` | `util/ContainerUsersManager.kt` | Container user list, cached |
| `ContainerOSInfoManager` | `util/ContainerOSInfoManager.kt` | Distro name, version, icon |
| `ContainerUsageCollector` | `util/ContainerUsageCollector.kt` | CPU, RAM, uptime, IP in one call |
| `ContainerDiskUsageManager` | `util/ContainerDiskUsageManager.kt` | Sparse image disk usage |

The raw passthrough entry points (`executeSystemctlCommand`, `executeRCCommand`) do no name
validation. Prefer the wrappers.

### Android: ViewModels

| Symbol | Path | Owns |
| --- | --- | --- |
| `AppStateViewModel` | `ui/viewmodel/AppStateViewModel.kt` | Backend status, root status, backend and module installation |
| `ContainerViewModel` | `ui/viewmodel/ContainerViewModel.kt` | The container list, counts, refresh and scan |
| `ContainerOperationsViewModel` | `ui/viewmodel/ContainerOperationsViewModel.kt` | Start, stop, restart, uninstall, export, sparse migrate and resize, plus their logs |
| `ContainerInstallationViewModel` | `ui/viewmodel/ContainerInstallationViewModel.kt` | The install wizard state, shared across back stack entries |
| `ContainerUsageViewModel` | `ui/viewmodel/ContainerUsageViewModel.kt` | Live usage polling |
| `SystemStatsViewModel` | `ui/viewmodel/SystemStatsViewModel.kt` | Per-container OS info |
| `RootfsRepoViewModel` | `ui/viewmodel/RootfsRepoViewModel.kt` | Repo listing and per-asset download state |

### Android: terminal

| Symbol | Path | Use it when |
| --- | --- | --- |
| `TerminalSessionService` + `SessionBinder` | `service/TerminalSessionService.kt` | Creating, fetching, and terminating terminal sessions. Call `detachAllClients()` on screen disposal so the Activity is not retained |
| `TerminalSessionService.globalSessionList` | same | The process-scoped, Compose-observable session registry |
| `DroidspacesTerminalSession.create(client, containerName, containerUser)` | `ui/terminal/DroidspacesTerminalSession.kt` | The only place a container shell is spawned. It re-validates the container name and user |
| `TerminalBackEnd`, `TerminalScreenState`, `NoOpTerminalSessionClient` | `ui/terminal/` | Terminal view plumbing |
| `AnsiColorParser.parseAnsi/stripAnsi` | `util/AnsiColorParser.kt` | Rendering or cleaning ANSI output |

### Android: rootfs download and install

| Symbol | Path | Use it when |
| --- | --- | --- |
| `RootfsDownloadManager` | `util/RootfsDownloadManager.kt` | Enqueueing, polling, and cancelling a rootfs download, plus battery exemption |
| `ContainerInstaller.installContainer(...)` | `util/ContainerInstaller.kt` | Installing from a tarball. Validates, checks storage, extracts, writes config, cleans up on failure |
| `SparseImageInstaller.extract(...)` | `util/SparseImageInstaller.kt` | The sparse image truncate, format, mount, extract, unmount lifecycle |
| `BinaryInstaller` / `InstallationStep` | `util/BinaryInstaller.kt` | Installing the backend binary and signalling the daemon |
| `ModuleInstaller` / `ModuleInstallationStep` | `util/ModuleInstaller.kt` | Installing the Magisk module |
| `SymlinkInstaller` | `util/SymlinkInstaller.kt` | Enabling and disabling the binary symlink |
| `FilePickerUtils`, `IconUtils` | `util/` | File name resolution, distro icon lookup |

### C backend: logging

| Symbol | Header line | Use it when |
| --- | --- | --- |
| `ds_log(fmt, ...)` | `droidspace.h` | Normal progress output |
| `ds_warn(fmt, ...)` | | Recoverable problem. Goes to stderr, never silenced |
| `ds_error(fmt, ...)` | | Failure the caller handles |
| `ds_die(fmt, ...)` | | Fatal. Logs then exits. Never write `fprintf` plus `exit` by hand |
| `ds_log_silent` | | Set to suppress non-error terminal output around a noisy call. Save and restore it |
| `C_RED`, `C_GREEN`, `C_YELLOW`, `C_RESET` and friends | | Colour. Never hardcode an escape sequence |
| `rotate_log(path, max_size)` | | Size-capped log rotation |
| `write_monitor_debug_log(name, fmt, ...)` | | Logging from the detached monitor, file only |
| `ds_open_container_log` / `ds_close_container_log` | | Tee `ds_log` output into the per-container log |
| `ds_spawn_log_relay(fd, log_file, tag)` | | Timestamping a child's output into the logs directory |

The debug channel is a bracket prefix, not a flag. A message starting with `[DEBUG]`,
`[NET]`, `[CGROUP]`, `[SEC]`, `[IPT]`, `[VIRT]`, `[GPU]`, `[FW]`, `[DHCP]`, `[X11]`,
`[VirGL]`, or `[PulseAudio]` goes to the log file but not the terminal. Use that, do not add
a verbosity flag.

### C backend: strings

| Symbol | Use it when |
| --- | --- |
| `safe_strncpy(dst, src, size)` | The replacement for `strcpy` and `strncpy`. NULL safe, always terminates, warns on truncation |
| `snprintf` with a checked return | The replacement for `sprintf` and `strcat`. There is no wrapper, and `-Wformat-truncation=2 -Werror` means you must handle truncation |
| `ds_split_flags` / `ds_free_split_flags` | Tokenizing user flag strings. Rejects shell metacharacters |
| `ds_parse_iface_csv` | Parsing a comma separated interface list |
| `ds_format_uptime`, `ds_parse_size`, `ds_format_size` | Uptime strings, and byte counts like `1G` or `512M` |
| `sanitize_container_name`, `validate_container_name`, `reject_container_name`, `parse_and_validate_names` | Container names from user input. `reject_container_name` is the one-call form for argument parsing |
| `validate_bind_destination` | Rejecting bind targets that would clobber container internals |
| `ds_shell_metachars` (`src/utils.c`) | The single source of truth for the rejected metacharacter set. Reference it, do not retype it |

### C backend: files and paths

| Symbol | Use it when |
| --- | --- |
| `write_file_atomic(path, content)` | Anything persistent. `mkstemp` plus `fsync` plus `rename`, so a pre-planted symlink at a predictable temp name cannot win |
| `write_file`, `read_file`, `write_all` | One-shot write, read to buffer, EINTR-safe full write. Never loop on bare `write()` |
| `grep_file(path, pattern)` | Substring test over a file |
| `mkdir_p`, `remove_recursive`, `copy_file` | The `mkdir -p`, `rm -rf`, and stream copy equivalents |
| `is_subpath(parent, child)` | Path escape validation. Realpath based, use this instead of `strncmp` |
| `is_ramfs`, `is_mountpoint` | Filesystem type and mountpoint tests |
| `build_proc_root_path(pid, suffix, buf, size)` | Building `/proc/<pid>/root/...` with truncation checked |
| `ds_resolve_path_arg`, `ds_resolve_argv_paths` | Making user-supplied paths absolute before the daemonize boundary changes the working directory |
| `access(path, F_OK)` | Existence checks. There is no wrapper and none is wanted |

`O_CLOEXEC` is expected on every `open()` in new code. When a path can be attacker
influenced, copy the `ds_bind_mount_socket` pattern: open with `O_NOFOLLOW|O_CLOEXEC`, then
`fchown` and `fchmod` the opened inode so there is no second path resolution to race.

### C backend: processes and daemons

| Symbol | Use it when |
| --- | --- |
| `run_command`, `run_command_quiet`, `run_command_log` | Running an external binary. These are the only sanctioned way. There is no `system()` in this tree |
| `ds_spawn_daemon(child_fn, user_data, log_file, tag, label)` | Forking a long-lived helper. Verifies `execv` succeeded through a ready pipe and attaches a log relay |
| `ds_daemon_child_preamble()` | First call inside such a child, while still root |
| `ds_oom_protect()` | Best effort OOM score protection |
| `ds_daemon_read_pid` / `write_pid` / `remove_pid`, `ds_resolve_daemon_pid` | Pidfile lifecycle. The read form liveness-checks the pid |
| `ds_global_daemon_stop(...)` | Unified SIGTERM, poll, SIGKILL, reap, unlink teardown |
| `wait_for_socket_or_death(pid, path, timeout_ms, interval_us)` | Waiting for a socket to appear. Bails early if the server dies. Use instead of a sleep loop |
| `ds_send_fd` / `ds_recv_fd` | SCM_RIGHTS descriptor passing |
| `collect_pids`, `read_and_validate_pid` | Snapshotting `/proc`, and reading a pidfile with a liveness check |
| `is_external_lock_active(name)` | Checking the lock sidecar. Auto-removes a stale lock whose holder is dead |
| `DS_SIG_STOP`, `ds_init_type_t`, `detect_container_init()` | Graceful stop. Each init family has its own stop and reboot signal, and under procd `SIGTERM` means reboot |

### C backend: platform gates

| Symbol | Use it when |
| --- | --- |
| `is_android()` | The mandatory gate for every Android-only or Linux-only path, both directions |
| `is_running_in_termux()` | Termux environment |
| `get_kernel_version`, `check_kernel_recommendation` | Version gating against `DS_MIN_KERNEL_MAJOR` and `DS_MIN_KERNEL_MINOR` |
| `check_ns(flag, name)` | Probing whether a `CLONE_NEW*` namespace is usable |
| `ds_cgroup_v2_usable`, `ds_cgroup_kernel_supports_v2`, `ds_cgroup_host_is_v2` | cgroup generation gates |
| `ds_nl_probe_nat_capability(reason, size)` | Kernel bridge, veth, and NAT capability. Fork free. Run before any NAT setup |
| `ds_get_selinux_status()`, `is_systemd_rootfs(path)` | SELinux mode, and rootfs flavour |

### C backend: config

`struct ds_config` is the single owner of runtime state and is passed by pointer through the
whole tree. It is almost entirely fixed-size, with three heap-owned lists.

| Symbol | Use it when |
| --- | --- |
| `ds_config_load` / `ds_config_load_by_name` | Loading a config. Prefer the by-name form over building the path yourself |
| `ds_config_save` / `ds_config_save_by_name` | Saving |
| `ds_config_validate` | Before acting on a loaded config |
| `ds_config_add_bind(cfg, src, dest, ro)` | The only way to add a bind mount. Grows, de-dups, validates |
| `ds_config_free` | Teardown. It calls the three per-list free functions |
| `sort_bind_mounts(cfg)` | Before applying binds, so parents mount before children |
| `parse_privileged(value, cfg)` | Parsing `--privileged` into the `DS_PRIV_*` bitmask |
| `ds_config_auto_path`, `apply_reset_config` | Deriving the default config path, and resetting |
| `load_etc_environment`, `ds_env_boot_setup`, `ds_env_save`, `parse_env_file_to_config` | Environment file handling |

A new config key must be added to **both** `ds_config_load()` and
`ds_config_serialize_known()` in `src/config.c`. Miss the second and the key round-trips as
an unknown line.

### C backend: mount, cgroup, workspace, seccomp

| Symbol | Use it when |
| --- | --- |
| `domount`, `domount_silent`, `bind_mount` | Mounting. Use these instead of raw `mount(2)` |
| `ds_apply_jail_mask(hw_access, privileged_mask)` | Masking `/proc` and `/sys` entries |
| `setup_dev`, `create_devices`, `setup_devpts`, `ds_fix_host_ptys` | Device node setup |
| `setup_custom_binds(cfg, rootfs)` | Applying `cfg->binds` |
| `setup_volatile_overlay`, `cleanup_volatile_overlay`, `check_volatile_mode` | Volatile mode |
| `mount_rootfs_img`, `unmount_rootfs_img` | Sparse image loop device lifecycle |
| `setup_cgroups`, `ds_cgroup_host_bootstrap` | cgroup setup |
| `ds_cgroup_attach`, `ds_cgroup_detach`, `ds_cgroup_cleanup_container` | Moving a process in, and cleanup |
| `ds_cgroup_apply_limits`, `ds_cgroup_get_usage`, `print_cgroup_status` | Limits and usage |
| `ds_cg_word_in_list(list, name)` | Testing for a controller name. Do not `strstr` a controller list |
| `get_workspace_dir`, `get_pids_dir`, `get_net_dir`, `get_logs_dir` | The only sanctioned way to build a workspace path. They switch between the Android and Linux roots |
| `ensure_workspace` | Creating the tree |
| `is_container_running`, `find_container_by_name`, `find_container_init_pid`, `is_container_init` | Container lookup |
| `ds_feature_needs(offsetof(struct ds_config, field))` | The generic feature scanner. A new global daemon adds a two-line wrapper here, not a new scanner |
| `ds_seccomp_apply_minimal`, `android_seccomp_setup` | The seccomp filters |
| `ds_ksu_neutralize_root_escape` | KernelSU hardening. Must run **before** `ds_seccomp_apply_minimal`, whose magic-reboot block would otherwise deny the reboot it needs |
| `ds_apply_capability_hardening` | Capability dropping |

### C backend: networking

Everything in `src/net/` talks to the kernel directly. There is no `ip` or `iptables`
shell-out on the fast path.

| Symbol | Use it when |
| --- | --- |
| `ds_nl_open` / `ds_nl_close` | Opening the netlink context every link, address, route, and rule call needs |
| `ds_nl_create_bridge`, `ds_nl_create_veth`, `ds_nl_set_master`, `ds_nl_link_up/down`, `ds_nl_del_link`, `ds_nl_rename`, `ds_nl_set_mac` | Link operations |
| `ds_nl_add_addr4`, `ds_nl_add_route4` | Addresses and routes |
| `ds_nl_move_to_netns`, `ds_nl_move_to_netns_named` | Moving an interface into a namespace |
| `ds_nl_add_rule4`, `ds_nl_del_rule4` | FIB policy rules. Priorities come from `DS_RULE_PRIO_TO_SUBNET`, `DS_RULE_PRIO_TETHER`, `DS_RULE_PRIO_FROM_SUBNET`, which must sit above the OEM reserved range and below Android's VPN range |
| `ds_nl_get_iface_table`, `ds_nl_get_table_default_oif`, `ds_nl_get_android_default` | Routing table introspection |
| `ds_nl_flush_stale_veths`, `ds_nl_list_ifaces`, `ds_nl_count_ifaces_with_prefix` | Enumeration and garbage collection |
| `ds_ipt_ensure_masquerade`, `ds_ipt_ensure_forward_accept`, `ds_ipt_ensure_input_accept`, `ds_ipt_ensure_mss_clamp` | Installing netfilter rules |
| `ds_ipt_host_rules_present(iface, src_cidr, expect_dnat)` | The fork-free probe for the whole host rule set. The route monitor gates reinstallation on it |
| `ds_ipt_remove_iface_rules`, `ds_ipt_remove_ds_rules` | Teardown |
| `ds_ipt_add_portforwards`, `ds_ipt_remove_portforwards` | Port forwarding |
| `parse_cidr(cidr, ip_out, mask_out)` | The shared CIDR splitter |
| `fix_networking_host`, `fix_networking_rootfs`, `setup_veth_host_side`, `setup_veth_child_side_named`, `setup_gateway_veth_side` | Network bring-up |
| `ds_net_start_route_monitor`, `ds_net_mark_local_forward_active` | The reconciler that re-asserts our rules after netd wipes them |
| `ds_net_cleanup`, `ds_net_gateway_teardown`, `ds_net_rewire_gateway_clients` | Teardown and gateway client rewiring |
| `ds_net_validate_static_ip`, `ds_net_check_ip_collision`, `ds_net_resolve_static_ip` | Static NAT IP handling. After `resolve`, the config must be saved to persist the result |
| `ds_dhcp_server_start`, `ds_dhcp_server_stop` | The single-lease DHCP server. Stop it before veth teardown so the receive unblocks |
| `ds_get_dns_servers`, `detect_ipv6_in_container`, `ds_net_disable_tx_checksum` | DNS, IPv6 detection, checksum offload |

### C backend: security guards

| Symbol | Contract |
| --- | --- |
| `ds_peer_authorized(fd, group_name)` | The only authorization gate in the tree. Allows root or a member of the group, but only when the peer shares our PID namespace. Every failure path denies. Callers are `src/daemon.c` and `src/socketd_bridge.c` |
| `ds_peer_in_pidns(peer_pid)` | Must fail closed. A pid of zero is not translatable into our namespace, and a failed readlink means we cannot prove membership. Both deny. Failing open here let a caller recycle a dead pid and escape to host root |
| `ds_bind_mount_socket(src, dst, uid, label)` | The symlink-race-safe write pattern for anything landing in a container-controlled directory |
| `is_dangerous_node(name)` | The device node blocklist |
| `set_selinux_context`, `get_selinux_context`, `ds_selinux_dyntransition`, `ds_selinux_enter_domain`, `ds_drop_privileges`, `ds_resolve_termux_uid` | SELinux and privilege handling |

Any comment on a security check that reads "cannot determine, so allow" is a bug. Inability
to prove something at a trust boundary means deny.

## Known duplicates: do not add a third copy

These exist today. They are on the cleanup list. Extend the shared version, do not add
another.

- `ToggleCard` and `SwitchItem` are two shapes of the same switch row.
- `ContainersScreen` has an inline copy of the typed-confirmation gate that
  `ConfirmPhraseField` already provides. It also inlines error-tinted field colors, because
  `DsTextFieldDefaults` has no error variant. Add the variant rather than a third copy.
- `SummaryItem` exists as three private overloads in `InstallationSummaryScreen.kt`. Promote
  it to `ui/component/` before a second screen wants it.
- `labelFontSize` on `PrimaryActionBottomBar` has one caller, `RootCheckScreen`, pushing its
  call to action to 16sp. Either the type scale covers it or the parameter should go. Do not
  add a second caller.
- Several installer and checker classes still interpolate paths into shell strings with
  literal quotes or no quotes at all (`BinaryInstaller`, `ContainerInstaller`,
  `SparseImageInstaller`, `ModuleInstaller`, `SymlinkInstaller`). They are on the list. Do
  not copy the pattern; use `ContainerCommandBuilder.quote()`.
- `AppStateViewModel`, `ContainerOperationsViewModel`, and `FilePickerDialog` execute root
  commands directly. That is the wrong layer. New root calls go through a repository or
  `ContainerOperationExecutor`.
- Several `util` singletons hold mutable caches and Compose state that belong in a ViewModel
  (`ContainerOSInfoManager`, `ContainerUsersManager`, `ContainerDiskUsageManager`,
  `SystemInfoManager`, `TerminalScreenState`).

## Adding something new

1. Search the inventory above, then grep the tree. Something close usually exists.
2. Extend the shared thing. Adding a parameter to one component beats adding a sibling
   component.
3. If you are about to copy a block and change two fields, parameterize it instead.
4. Fix bugs at the choke point every caller routes through, not at the one call site the
   report happens to name.
5. Never add a second way to build a shell command.
6. New state goes in a ViewModel, not in a composable and not in a `util` object.

## PR Requirements

Every feature PR must include:

1. **A clear description of the real-world problem being solved.** "I wanted this" is not a
   problem statement. Explain what breaks, fails, or is missing for real users on real
   hardware.

2. **Screenshots or terminal output** demonstrating the feature working as intended.

3. **Explicit list of tested environments.** For Android: device name, SoC, kernel version,
   and OEM or Android version. For Linux: distro, kernel version, and architecture.

4. **No regressions.** Run the existing behavior through your change. If something that
   worked before no longer works, fix it before opening a PR.

5. **For UI changes, the DESIGN.md rules you followed.** If you deviated from one, say which and
   why. A radius or a colour that disagrees with [DESIGN.md](./DESIGN.md) without a reason gets
   sent back.

## Code Ownership

If your feature is merged, you are responsible for it going forward.

When a new kernel version, a new SoC quirk, or a platform behavior change breaks your
contribution, you are expected to address it. If a feature you submitted starts causing
issues and you are unreachable or unwilling to maintain it, it will be removed.

Users do not know who wrote a feature. When something breaks, they blame the project.
Understand what your code does before submitting it. If you cannot explain why a specific
implementation choice was made, that choice should not be in production.

## What Gets Merged

- Features that solve a real problem, work on kernel 3.10+, are correctly platform-guarded,
  and are validated across multiple environments.
- Bug fixes with a clear reproduction case and a verified resolution.
- Security improvements. These are always welcome.
- Performance improvements with measurable, non-regressing impact.
- Deletions. Removing duplicated code or dead flexibility is a contribution.
- Documentation corrections.

## What Gets Rejected

- Patches that only work on kernel 5.x+ with no fallback.
- Features that solve a problem no real user has reported or that cannot be reproduced
  outside a narrow hardware or platform configuration.
- Android-specific or Linux-specific code that is not guarded with `is_android()`.
- Code the author cannot explain or defend under review.
- App changes that break Android 8 compatibility.
- A new component or helper that duplicates one already in the inventory above.
- Any dynamic value reaching a root shell without quoting or an allow-list.
- Anything that introduces a regression, regardless of how useful the new behavior is.

## Repeat Rejections

If a contributor submits multiple PRs that are rejected for the same reasons, features that
solve no real problem, fail universality requirements, or add unnecessary complexity to the
codebase, they will be blocked from contributing further.

There is no fixed strike count. The threshold is pattern recognition: if it is clear that a
contributor is not reading feedback, not testing properly, or is deliberately padding the
codebase, the decision to block is at maintainer discretion and is final.

## Security Vulnerabilities

Security fixes and hardening patches are always welcome and will be reviewed with priority.

If you discover a vulnerability, particularly a **container escape that is reproducible in
non-hardware-access mode without privileged flags**, do not open a public issue.

Report it privately:

- **Email:** droidcasts@protonmail.com
- **Telegram:** [t.me/ravindu](https://t.me/ravindu)

Include a reproduction case, affected configurations, and kernel or SoC details if relevant.
Public disclosure should wait until a fix is available.

## Process

1. Fork the repository and work in a dedicated branch.
2. Open a PR against `main` with the information described above.
3. Be responsive during review. Unresponsive PRs will be closed.
4. Address review feedback directly. Do not open a new PR for the same change.

There is no formal CLA. By submitting a PR you agree that your contribution may be
distributed under the project's existing license.

AI agents working in this repository should read [AGENTS.md](./AGENTS.md), which is the short
form of the rules above.
