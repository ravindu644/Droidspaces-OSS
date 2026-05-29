package com.droidspaces.app.util

import android.content.Context
import android.os.Build
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.InputStream
import java.security.MessageDigest

sealed class DroidspacesBackendStatus {
    object Checking : DroidspacesBackendStatus()
    object Available : DroidspacesBackendStatus()
    object UpdateAvailable : DroidspacesBackendStatus()
    object NotInstalled : DroidspacesBackendStatus()
    object Corrupted : DroidspacesBackendStatus()
    object ModuleMissing : DroidspacesBackendStatus()
}

object DroidspacesChecker {
    private const val DROIDSPACES_BINARY_PATH = Constants.DROIDSPACES_BINARY_PATH
    private const val BUSYBOX_BINARY_PATH = Constants.BUSYBOX_BINARY_PATH
    private const val MAGISK_MODULE_PATH = "/data/adb/modules/droidspaces"
    private const val MODULE_PROP_PATH = "$MAGISK_MODULE_PATH/module.prop"

    /**
     * Check if Magisk module is installed.
     * Returns true if both module directory and module.prop exist.
     *
     * Performance: ~10-20ms (runs in background, never blocks UI)
     */
    private suspend fun checkModuleInstalled(): Boolean = withContext(Dispatchers.IO) {
        try {
            val dirCheck = Shell.cmd("test -d '$MAGISK_MODULE_PATH'").exec()
            if (!dirCheck.isSuccess) {
                return@withContext false
            }

            val propCheck = Shell.cmd("test -f '$MODULE_PROP_PATH'").exec()
            propCheck.isSuccess
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Check if both droidspaces and busybox binaries are available and executable (non-blocking).
     * Returns status without blocking the calling thread.
     *
     * Performance: ~10-50ms (runs in background, never blocks UI)
     */
    suspend fun checkBackendStatus(): DroidspacesBackendStatus = withContext(Dispatchers.IO) {
        try {
            if (!checkModuleInstalled()) {
                return@withContext DroidspacesBackendStatus.ModuleMissing
            }

            fun checkBinary(binaryPath: String): Boolean {
                val existsCheck = Shell.cmd("test -f '$binaryPath'").exec()
                if (!existsCheck.isSuccess) return false

                val execCheck = Shell.cmd("test -x '$binaryPath'").exec()
                if (!execCheck.isSuccess) return false

                return true
            }


            val droidspacesOk = checkBinary(DROIDSPACES_BINARY_PATH)
            val busyboxOk = checkBinary(BUSYBOX_BINARY_PATH)

            when {
                droidspacesOk && busyboxOk -> DroidspacesBackendStatus.Available
                !droidspacesOk || !busyboxOk -> DroidspacesBackendStatus.NotInstalled
                else -> DroidspacesBackendStatus.Corrupted
            }
        } catch (e: Exception) {
            DroidspacesBackendStatus.NotInstalled
        }
    }

    /**
     * Get droidspaces version by executing 'droidspaces version' command (non-blocking).
     * Returns version string or null if unavailable.
     *
     * Performance: ~10-50ms (runs in background, never blocks UI)
     */
    suspend fun getDroidspacesVersion(): String? = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("$DROIDSPACES_BINARY_PATH version 2>&1").exec()
            if (result.isSuccess && result.out.isNotEmpty()) {
                // Version command outputs "v2.5.0-beta" - trim whitespace
                result.out[0].trim()
            } else {
                null
            }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Check if backend update is available by comparing installed binary hash with asset binary hash.
     * Returns true if hashes differ (update available), false otherwise.
     *
     * Performance: ~50-200ms (reads both files and calculates hashes)
     */
    suspend fun checkUpdateAvailable(context: Context): Boolean = withContext(Dispatchers.IO) {
        try {
            // Get architecture-specific binary name
            val arch = Build.SUPPORTED_ABIS[0]
            val binaryName = when {
                arch.contains("arm64") || arch.contains("aarch64") -> "droidspaces-aarch64"
                arch.contains("armeabi") || arch.contains("arm") -> "droidspaces-armhf"
                arch.contains("x86_64") -> "droidspaces-x86_64"
                arch.contains("x86") -> "droidspaces-x86"
                else -> "droidspaces-aarch64"
            }

            // Calculate hash of installed binary
            val installedHashResult = Shell.cmd("md5sum $DROIDSPACES_BINARY_PATH 2>&1 | cut -d' ' -f1").exec()
            if (!installedHashResult.isSuccess || installedHashResult.out.isEmpty()) {
                return@withContext false
            }
            val installedHash = installedHashResult.out[0].trim()

            // Calculate hash of asset binary
            val assetManager = context.assets
            val assetHash = try {
                assetManager.open("binaries/$binaryName").use { inputStream ->
                    calculateMD5(inputStream)
                }
            } catch (e: Exception) {
                return@withContext false
            }

            // Compare hashes - if different, update is available
            installedHash != assetHash
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Calculate MD5 hash of an InputStream.
     */
    private fun calculateMD5(inputStream: InputStream): String {
        val md = MessageDigest.getInstance("MD5")
        val buffer = ByteArray(8192)
        var bytesRead: Int
        while (inputStream.read(buffer).also { bytesRead = it } != -1) {
            md.update(buffer, 0, bytesRead)
        }
        val digest = md.digest()
        return digest.joinToString("") { "%02x".format(it) }
    }

    /**
     * Quick synchronous check (for cached state).
     * Returns null if check hasn't been performed yet.
     */
    fun quickCheck(): DroidspacesBackendStatus? {
        // This is just a placeholder - actual check must be async
        // Used for initial state before async check completes
        return null
    }
}

