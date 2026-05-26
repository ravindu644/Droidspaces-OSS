package com.droidspaces.app.util

import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object SELinuxChecker {
    // Cache string constants to avoid allocations
    private const val STATUS_DISABLED = "Disabled"
    private const val STATUS_UNKNOWN = "Unknown"
    private const val STATUS_ENFORCING = "Enforcing"
    private const val STATUS_PERMISSIVE = "Permissive"

    /**
     * Optimized SELinux status check with minimal allocations.
     * Uses cached string constants and direct file I/O.
     */
    suspend fun getSELinuxStatus(): String = withContext(Dispatchers.IO) {
        val enforceFile = File("/sys/fs/selinux/enforce")

        return@withContext when {
            !enforceFile.exists() -> STATUS_DISABLED
            !enforceFile.isFile -> STATUS_UNKNOWN
            !enforceFile.canRead() -> STATUS_ENFORCING
            else -> {
                runCatching {
                    enforceFile.inputStream().bufferedReader().use { reader ->
                        reader.readLine()?.trim()?.toIntOrNull()
                    }
                }.getOrNull()?.let { value ->
                    when (value) {
                        1 -> STATUS_ENFORCING
                        0 -> STATUS_PERMISSIVE
                        else -> STATUS_UNKNOWN
                    }
                } ?: STATUS_UNKNOWN
            }
        }
    }
}

