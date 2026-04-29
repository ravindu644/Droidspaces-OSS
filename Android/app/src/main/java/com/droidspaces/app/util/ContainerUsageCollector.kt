package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Collects real-time usage statistics from running containers.
 * Executes commands inside containers via `droidspaces --name <name> run`.
 */
object ContainerUsageCollector {
    private const val TAG = "ContainerUsageCollector"

    data class ContainerUsage(
        val cpuPercent: Double = 0.0,
        val ramUsedKb: Long = 0,
        val ramTotalKb: Long = 0,
        val ramPercent: Double = 0.0,
        val uptime: String? = null
    )

    // Per-container CPU delta state - Not needed with the new usage command
    // as the backend handles the delta logic and returns permill
    // private val cpuPrevState = mutableMapOf<String, Pair<Long, Long>>()

    /**
     * Collect CPU, RAM, and Uptime stats for a running container.
     * Uses the unified `usage` command for synchronized data.
     */
    suspend fun collectUsage(containerName: String): ContainerUsage = withContext(Dispatchers.IO) {
        try {
            val cmd = ContainerCommandBuilder.buildUsageCommand(containerName)
            val result = Shell.cmd(cmd).exec()

            if (result.isSuccess && result.out.isNotEmpty()) {
                var uptime: String? = null
                var cpuPercent = 0.0
                var ramUsed = 0L
                var ramTotal = 0L

                result.out.forEach { line ->
                    val parts = line.trim().split("=", limit = 2)
                    if (parts.size == 2) {
                        val key = parts[0].trim()
                        val value = parts[1].trim()
                        when (key) {
                            "UPTIME" -> uptime = value.takeIf { it != "NONE" && it.isNotEmpty() }
                            "CPU_PERMILL" -> {
                                val permill = value.toDoubleOrNull() ?: 0.0
                                cpuPercent = (permill / 10.0).coerceIn(0.0, 100.0)
                            }
                            "RAM_USED_KB" -> ramUsed = value.toLongOrNull() ?: 0L
                            "RAM_TOTAL_KB" -> ramTotal = value.toLongOrNull() ?: 0L
                        }
                    }
                }

                val ramPercent = if (ramTotal > 0) {
                    (ramUsed.toDouble() / ramTotal * 100.0).coerceIn(0.0, 100.0)
                } else 0.0

                return@withContext ContainerUsage(
                    cpuPercent = cpuPercent,
                    ramUsedKb = ramUsed,
                    ramTotalKb = ramTotal,
                    ramPercent = ramPercent,
                    uptime = uptime
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error collecting usage for $containerName", e)
        }
        ContainerUsage()
    }

    /**
     * Clear state for a container - legacy, kept for compatibility.
     */
    fun clearState() {
        // Not used with the new usage command
    }
}
