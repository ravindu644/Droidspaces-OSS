package com.droidspaces.app.util

import android.content.Context
import android.os.Build

/**
 * Single source of truth for mapping the device's primary ABI to the arch
 * strings Droidspaces uses (the bundled-binary suffix, which also matches the
 * arch field in rootfs.json), plus a human-readable name. Previously this
 * mapping was duplicated across BinaryInstaller and RootfsRepository.
 */
object DeviceArch {
    private fun nativeSuffix(): String {
        val abi = Build.SUPPORTED_ABIS[0]
        return when {
            abi.contains("arm64") || abi.contains("aarch64") -> "aarch64"
            abi.contains("armeabi") || abi.contains("arm")   -> "armhf"
            abi.contains("x86_64")                           -> "x86_64"
            abi.contains("x86")                              -> "x86"
            else                                             -> "aarch64"
        }
    }

    /**
     * True on a 32-bit userspace whose kernel can run 64-bit binaries, so the
     * "use the 64-bit backend" switch is worth showing. A 32-bit ARM kernel calls
     * itself armv7l even on a v8 core; armv8l is the arm64 kernel's compat name and
     * aarch64 what mainline reports to compat tasks. x86 has no such tell (an i686
     * kernel and a compat task both say i686), so the ABI alone decides there.
     */
    fun is32Bit(): Boolean = when (nativeSuffix()) {
        "armhf" -> SystemInfoManager.architecture.let { it == "armv8l" || it == "aarch64" }
        "x86"   -> true
        else    -> false
    }

    /**
     * Binary-suffix / rootfs arch: aarch64, armhf, x86_64, x86. Some budget devices
     * run a 32-bit OS on a 64-bit kernel, so the user can opt into the 64-bit
     * binaries and rootfs images; the kernel runs them fine.
     */
    fun suffix(context: Context): String {
        val native = nativeSuffix()
        if (!PreferencesManager.getInstance(context).treatAs64Bit) return native
        return when (native) {
            "armhf" -> "aarch64"
            "x86"   -> "x86_64"
            else    -> native
        }
    }

    /** Human-readable architecture name for display. */
    fun displayName(context: Context): String = when (val s = suffix(context)) {
        "aarch64" -> "ARM64 (aarch64)"
        "armhf"   -> "ARM (armhf)"
        else      -> s
    }
}
