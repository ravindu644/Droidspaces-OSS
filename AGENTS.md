# AGENTS.md

Instructions for AI coding agents working in this repository. Humans should read
[CONTRIBUTING.md](./CONTRIBUTING.md), which carries the full inventory of what already
exists here.

## What this is

Droidspaces is a container runtime. Two halves:

- `src/` is the C backend, statically linked against musl, one binary for Android and Linux.
- `Android/` is the Compose app that drives that binary over a root shell.

The compiled backend ships inside the APK at `Android/app/src/main/assets/binaries`.

## Build

C backend:

```
make native            # build for the host arch, this is the real build command
make aarch64           # cross builds: aarch64, x86_64, armhf, x86, riscv64
make debug-hardened    # ASan, UBSan, LSan
make all-build         # every arch, then syncs into the APK assets
make format            # clang-format over src/
```

Bare `make` only prints help. It does not build anything.

Android app, from `Android/`:

```
./build.sh             # debug
./build.sh release     # signed release
```

Use the script, not gradle directly. CI runs `make all-tarball` then gradle, and does not
check formatting, so `make format` is on you.

Adding a new `.c` file means adding it to `SRCS` in the Makefile. There is no wildcard.

The Makefile does not track header dependencies. Run `make clean` before building after a
header edit or a branch switch, or stale objects give you a binary that fails in ways the
diff cannot explain.

## Testing and debugging on a device

Run `adb devices` first, every time. If no line ends in `device`, stop and say so.

### Running a test build without touching the daemon

On the phone the CLI forwards every command to the running `droidspaces daemon`, and the
daemon serves it with its own installed binary (`/data/local/Droidspaces/bin/droidspaces`).
A freshly pushed build called by absolute path therefore still runs the installed code.

`DS_NO_PROXY=1` skips the forwarding, so the binary you invoked does the work itself. The
daemon keeps running and nothing gets reinstalled.

```
make aarch64                                   # match `adb shell uname -m`
adb push output/droidspaces /data/local/tmp/ds-test
adb shell su <<'EOF'
DS_NO_PROXY=1 /data/local/tmp/ds-test -n NAME start
EOF
```

- Pipe a script into `adb shell su` as above. `adb shell su -c 'a; b'` runs only `a` as
  root and `b` as the shell user.
- To confirm your build is the one that started a container, look for its monitor process:
  `ls -l /proc/[0-9]*/exe 2>/dev/null | grep ds-test`. The PID that `show` prints is the
  container's init, whose `exe` is the guest's own, so it tells you nothing.
- Container logs are at `/data/local/Droidspaces/Logs/<name>/log`, timestamps in UTC. The
  daemon log is `Logs/droidspacesd.log`.
- Do not pass config flags such as `--upstream` in a test run without asking. They can be
  saved into the container's `container.config` and outlive the test.

### Running a command inside a container

Use this helper. It quotes once per layer (adb, `su -c`, the container), so arguments with
spaces or shell syntax arrive intact.

```bash
ds() {
    local name=$1; shift
    local inner="${DS:-/data/local/Droidspaces/bin/droidspaces} -n ${name@Q} run -- ${*@Q}"
    adb shell "su -c ${inner@Q}"
}

ds Docmost fastfetch
ds Alpine-3.23 'ip -6 route; cat /etc/resolv.conf'
DS='DS_NO_PROXY=1 /data/local/tmp/ds-test' ds Alpine-3.23 ip -6 route
```

A single argument containing a space is handed to the container's `/bin/sh -c` by `run`
itself, so a pipeline or a `;` list goes in as one quoted string, no `sh -c` wrapper.

The helper calls the installed binary by absolute path, because `droidspaces` is only on
the PATH when the app's "Integrate Droidspaces to the system path" toggle is on. Set `DS`
as in the last line to run the command through a pushed test build instead.

Bare `ds` prints the table of running containers, which is how you find the names. The
function is bash only, and an agent's shell does not keep functions between tool calls, so
define it in the same command that uses it.

## Commits

- Run `make format` before committing any change to a `.c` or `.h` file.
- Sign off every commit: `git commit -s`.
- Never add a `Co-Authored-By:` trailer for an AI agent. Human co-authors are fine.
- Prefixes, matching the existing history:
  - `app:` for the Android app, with `app: fix:` and `app: refactor:` for those cases
  - `fix:`, `refactor:`, `feat:`, `docs:` or a subsystem name (`net:`, `mount:`, `seccomp:`,
    `daemon:`, `socketd:`) for the backend
  - `fix(security):` for anything security related, either half

## Style

Four rules. They apply to code, comments, commit messages, and documentation.

**No em-dashes.** Use a comma, a full stop, or rewrite the sentence.

**No ASCII banner comments.** No rows of `-----`, no `=====`, no boxed section headers. The
SPDX licence block at the top of each file stays. Existing banners in `src/` predate this
rule and are being removed separately, do not add more.

**Comments sound like a person.** Say why the code does something, or what breaks if it
does not. Skip comments that restate the line below them.

```c
/* Bad: increment the counter */
count++;

/* Good: netd resets our rules on restart, so re-assert them every cycle */
install_policy_rules(cfg);
```

**Ten lines that work beat a hundred that do the same thing.** Delete before you add. A
smaller diff in the right place is the goal, not a smaller diff anywhere.

## UI changes

Anything visual in `Android/` follows [DESIGN.md](./DESIGN.md). Colours, type, spacing, radii and
the shared components are all specified there, with the Material 3 token each value comes from,
so following them is also the smallest diff.

The app is mid-conversion to Material 3 Expressive. [ROADMAP-MD3E.md](./ROADMAP-MD3E.md) holds
the phase ledger, the per-screen decisions and what each shared component becomes. Read it
before touching UI under `Android/`, work in its phase order, tick items there as they land,
and delete that file and this paragraph at its last ledger item.

If a rule genuinely does not fit your case, deviate, but leave a comment at the site saying what
the deviation buys and say it in the PR. An undocumented deviation is drift and the next
contributor will "fix" it. DESIGN.md's "Decided exceptions" section lists the deviations that
are decisions, do not "fix" those.

## Before you write it at all

Work down this list and stop at the first answer that holds.

1. Does this need to exist? A flag nobody asked for, a knob for a value that never changes,
   an interface with one implementation: skip it and say so in one line.
2. Does it already exist here? See "Reuse before you write" below. Grep first.
3. Does the platform already do it? musl and the syscalls on the backend, the Kotlin
   and Java stdlib and the Android framework on the app side. Mind the kernel 3.10
   floor and `minSdk = 26`.
4. Does an installed dependency cover it? libsu, Compose, navigation, lifecycle, the Termux
   terminal on the app side. Never add a new one for what a few lines can do.
5. Can it be one line? Then it is one line.
6. Only then write the smallest thing that works.

Two answers work? Take the higher one and move on.

The list shortens the solution, never the reading. Trace the flow the change touches before
you pick a rung. A small diff in the wrong place is a second bug, not a lazy fix. Same for
bug reports: a report names a symptom, so grep every caller before you edit. One guard in
the shared function is smaller than a guard in each caller, and it fixes the siblings the
report did not mention.

Never simplify away input validation at a trust boundary, a fail-closed check, error
handling that loses state, or anything the requester asked for by name.

## Reuse before you write

The largest cleanup this project ever needed was caused by writing new code beside existing
code instead of extending it: a 700-line duplicate of the container config form, three
copies of the init system screen, one bottom bar pasted seven times. Grep before you write.
`CONTRIBUTING.md` lists the shared components and helpers in both halves.

These are choke points. Bypassing one is a bug, not a shortcut.

- `ContainerCommandBuilder.quote()` in `Android/app/src/main/java/com/droidspaces/app/util/ContainerCommandBuilder.kt`
  wraps every dynamic value that reaches a root shell.
- `ServiceManagerBase.isSafeServiceName()` allow-lists service names that came from inside a
  container. It fails closed. So does `ValidationUtils`.
- `ds_peer_authorized()` in `src/utils.c` is the only authorization gate in the tree, and
  `ds_peer_in_pidns()` backs it. Both must fail closed. A "cannot determine, so allow"
  branch there was a root container escape once already.
- `src/include/droidspace.h` is the catch-all header and every `.c` file includes only it.
  Grep it before writing a helper.
- Logging is `ds_log`, `ds_warn`, `ds_error`, `ds_die`. Never a bare `fprintf` or `exit`.
- `run_command`, `run_command_quiet`, `run_command_log` are the only sanctioned way to run
  an external binary. There is no `system()` in this tree and there must not be one.
- Workspace paths come from `get_workspace_dir`, `get_pids_dir`, `get_net_dir`,
  `get_logs_dir`. They switch between the Android and Linux roots. Never hardcode either.

## Hard constraints

- Minimum kernel is 3.10. No `openat2`, no `clone3`, no `pidfd_*`, no cgroup v2 only paths.
- Anything platform specific is guarded with `is_android()`, both directions.
- The app targets `minSdk = 26`. Test on Android 8 behaviour before assuming an API exists.
- Autoboot scripts under `init/android-service/vendor/bin/` are strict POSIX sh. No bash
  arrays, no bashisms.
- No new dependency when an installed one covers it. The app already has libsu, Compose,
  navigation, lifecycle, and the Termux terminal.
