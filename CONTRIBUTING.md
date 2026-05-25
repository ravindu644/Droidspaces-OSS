# Contributing to Droidspaces

## Philosophy

> A feature that doesn't exist is better than a broken implementation.

Droidspaces runs on two distinct platforms - Android (on hardware ranging from ancient
vendor-frozen 3.10 kernels to modern GKI devices, across dozens of SoCs and OEMs) and
Linux desktop environments. A patch that works on your setup and breaks on someone
else's is not a contribution - it is a regression. Every change introduced into core must
uphold this contract without exception.

---

## Platform Scope

Droidspaces core must work correctly on both Android and Linux. These are not interchangeable
environments and must be treated as separate targets.

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

---

## Kernel Compatibility

**Minimum supported kernel: 3.10**

This is a hard floor, not a suggestion. If your implementation depends on a syscall, a
`/proc` interface, a namespace feature, or a kernel config that does not exist on 3.10,
it will not be merged into core.

- Do not use `openat2(2)` - not available before 5.6.
- Do not rely on cgroup v2 exclusively - cgroup v1 must remain functional.
- Do not assume `clone3(2)`, `pidfd_*`, or any API gated behind 5.x.
- If a fallback path exists, implement it. If it does not, the feature does not belong in core.

This applies to both Android and Linux targets. A modern desktop kernel does not exempt your
patch from this requirement.

Test your changes on real hardware running old kernels. Emulators and modern stock kernels are
not sufficient validation.

---

## SoC and OEM Coverage (Android)

Droidspaces runs on Qualcomm, Exynos, MediaTek, and Unisoc silicon, under OEM kernels that
deviate significantly from mainline. Your patch must be tested across a representative spread
of this landscape before submission.

State explicitly in your PR which devices and kernel versions you have tested on. Untested
claims of compatibility will be treated as untested.

Patches that address a quirk specific to one SoC family or OEM kernel are acceptable **only
if Droidspaces can adapt to the quirk at runtime** - via detection, a conditional code path,
or a graceful fallback - without regressing behavior on unaffected hardware. If the fix
cannot be generalized in this way, it belongs in a downstream fork, not in core.

---

## Android App Changes

The app has a minimum requirement of **Android 8 (API 26)**. Changes to the Android app must
not introduce any dependency, API call, or behavior that breaks on Android 8.

Test on Android 8 before opening a PR. Testing only on a recent Android release is not
sufficient.

---

## PR Requirements

Every feature PR must include:

1. **A clear description of the real-world problem being solved.** "I wanted this" is not a
   problem statement. Explain what breaks, fails, or is missing for real users on real hardware.

2. **Screenshots or terminal output** demonstrating the feature working as intended.

3. **Explicit list of tested environments.** For Android: device name, SoC, kernel version,
   and OEM/Android version. For Linux: distro, kernel version, and architecture.

4. **No regressions.** Run the existing behavior through your change. If something that worked
   before no longer works, fix it before opening a PR.

---

## Code Ownership

If your feature is merged, you are responsible for it going forward.

When a new kernel version, a new SoC quirk, or a platform behavior change breaks your
contribution, you are expected to address it. If a feature you submitted starts causing issues
and you are unreachable or unwilling to maintain it, it will be removed.

Users do not know who wrote a feature. When something breaks, they blame the project.
Understand what your code does before submitting it. If you cannot explain why a specific
implementation choice was made, that choice should not be in production.

---

## What Gets Merged

- Features that solve a real problem, work on kernel 3.10+, are correctly platform-guarded,
  and are validated across multiple environments.
- Bug fixes with a clear reproduction case and a verified resolution.
- Security improvements. These are always welcome.
- Performance improvements with measurable, non-regressing impact.
- Documentation corrections.

## What Gets Rejected

- Patches that only work on kernel 5.x+ with no fallback.
- Features that solve a problem no real user has reported or that cannot be reproduced
  outside a narrow hardware or platform configuration.
- Android-specific or Linux-specific code that is not guarded with `is_android()`.
- Code the author cannot explain or defend under review.
- App changes that break Android 8 compatibility.
- Anything that introduces a regression, regardless of how useful the new behavior is.

---

## Repeat Rejections

If a contributor submits multiple PRs that are rejected for the same reasons - features that
solve no real problem, fail universality requirements, or add unnecessary complexity to the
codebase - they will be blocked from contributing further.

There is no fixed strike count. The threshold is pattern recognition: if it is clear that a
contributor is not reading feedback, not testing properly, or is deliberately padding the
codebase, the decision to block is at maintainer discretion and is final.

---

## Security Vulnerabilities

Security fixes and hardening patches are always welcome and will be reviewed with priority.

If you discover a vulnerability - particularly a **container escape that is reproducible in
non-hardware-access mode without privileged flags** - do not open a public issue.

Report it privately:

- **Email:** droidcasts@protonmail.com
- **Telegram:** [t.me/ravindu](https://t.me/ravindu)

Include a reproduction case, affected configurations, and kernel/SoC details if relevant.
Public disclosure should wait until a fix is available.

---

## Process

1. Fork the repository and work in a dedicated branch.
2. Open a PR against `main` with the information described above.
3. Be responsive during review. Unresponsive PRs will be closed.
4. Address review feedback directly. Do not open a new PR for the same change.

There is no formal CLA. By submitting a PR you agree that your contribution may be distributed
under the project's existing license.
