package com.droidspaces.app.util

import android.os.Build

/**
 * Single source of truth for mapping the device's primary ABI to the arch
 * strings Droidspaces uses (the bundled-binary suffix, which also matches the
 * arch field in rootfs.json), plus a human-readable name. Previously this
 * mapping was duplicated across BinaryInstaller and RootfsRepository.
 */
object DeviceArch {
    /** Binary-suffix / rootfs arch: aarch64, armhf, x86_64, x86 (defaults to aarch64). */
    fun suffix(): String {
        val abi = Build.SUPPORTED_ABIS[0]
        return when {
            abi.contains("arm64") || abi.contains("aarch64") -> "aarch64"
            abi.contains("armeabi") || abi.contains("arm")   -> "armhf"
            abi.contains("x86_64")                           -> "x86_64"
            abi.contains("x86")                              -> "x86"
            else                                             -> "aarch64"
        }
    }

    /** Human-readable architecture name for display. */
    fun displayName(): String {
        val abi = Build.SUPPORTED_ABIS[0]
        return when {
            abi.contains("arm64") || abi.contains("aarch64") -> "ARM64 (aarch64)"
            abi.contains("armeabi") || abi.contains("arm")   -> "ARM (armhf)"
            abi.contains("x86_64")                           -> "x86_64"
            abi.contains("x86")                              -> "x86"
            else                                             -> abi
        }
    }
}
