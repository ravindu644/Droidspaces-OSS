package com.droidspaces.app.util

import com.droidspaces.app.util.SuExec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object StorageChecker {
    private const val BUSYBOX_PATH = Constants.BUSYBOX_BINARY_PATH

    /**
     * Check available space in /data partition.
     * Returns free space in GB, or null if unable to determine.
     *
     * Performance: ~10-50ms (runs in background)
     */
    suspend fun getFreeSpaceGB(): Int? = withContext(Dispatchers.IO) {
        try {
            // Try using stat first (more accurate)
            val statResult = SuExec.cmd("stat -f -c '%a %S' /data 2>&1").exec()
            if (statResult.isSuccess && statResult.out.isNotEmpty()) {
                val parts = statResult.out[0].trim().split(" ")
                if (parts.size == 2) {
                    val availBlocks = parts[0].toLongOrNull()
                    val blockSize = parts[1].toLongOrNull()
                    if (availBlocks != null && blockSize != null) {
                        val freeGB = (availBlocks * blockSize / 1024 / 1024 / 1024).toInt()
                        return@withContext freeGB
                    }
                }
            }

            // Fallback: use busybox df
            val dfResult = SuExec.cmd("$BUSYBOX_PATH df /data 2>&1 | $BUSYBOX_PATH tail -n1 | $BUSYBOX_PATH awk '{print \$4}'").exec()
            if (dfResult.isSuccess && dfResult.out.isNotEmpty()) {
                val freeKB = dfResult.out[0].trim().toLongOrNull()
                if (freeKB != null && freeKB > 0) {
                    val freeGB = (freeKB / 1024 / 1024).toInt()
                    return@withContext freeGB
                }
            }

            null
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Check if sufficient space is available (default 4GB minimum).
     * Returns true if space is sufficient, false otherwise.
     * Returns null if unable to determine.
     */
    suspend fun hasSufficientSpace(requiredGB: Int = Constants.MIN_STORAGE_GB): Boolean? = withContext(Dispatchers.IO) {
        val freeGB = getFreeSpaceGB() ?: return@withContext null
        freeGB >= requiredGB
    }
}

