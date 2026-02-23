# Tested Platforms

This document lists the hardware and distributions where Droidspaces has been verified to work. Droidspaces is brand-agnostic and distribution-agnostic; it will work on any device or distribution as long as the kernel meets the [required configuration](Kernel-Configuration.md).

---

## Android

| Device | Kernel | Root Method | Kernel Source | Status | Notes |
|--------|--------|-------------|---------------|--------|-------|
| Samsung Galaxy S10 (Exynos) | 4.14.113 | KernelSU-Next v1.1.1 | [Repository](https://github.com/ravindu644/samsung_exynos9820_stock/tree/pure-stock) | Stable | No nested container support (kernel too old). |
| Samsung Galaxy A16 5G | 5.15.167 | KernelSU-Next v1.1.1 | [Repository](https://github.com/ravindu644/android_kernel_a166p) | Stable | Full feature support including nested containers. |

---

## Linux Desktop

| Distribution | Kernel | Status |
|-------------|--------|--------|
| Fedora 42 | 6.17 - 6.19 | Stable |
| CachyOS | 6.17 - 6.19 | Stable |
| Arch Linux | 6.17 - 6.19 | Stable |

---

## Community Tested

If you have successfully run Droidspaces on a device or distribution not listed here, please share your success in our [Telegram Channel](https://t.me/Droidspaces)'s Discussion group or open a Pull Request!
