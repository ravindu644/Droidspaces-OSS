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
        val ramPercent: Double = 0.0
    )

    // Per-container CPU delta state (containerName -> Pair(prevTotal, prevIdle))
    private val cpuPrevState = mutableMapOf<String, Pair<Long, Long>>()

    /**
     * Collect CPU and RAM stats for a running container.
     * CPU uses two-sample delta against cached previous values.
     */
    suspend fun collectUsage(containerName: String): ContainerUsage = withContext(Dispatchers.IO) {
        val cmd = Constants.DROIDSPACES_BINARY_PATH
        val name = ContainerCommandBuilder.quote(containerName)

        val cpuPercent = getCpuUsage(cmd, name, containerName)
        val (ramUsed, ramTotal, ramPercent) = getRamUsage(cmd, name)

        ContainerUsage(
            cpuPercent = cpuPercent,
            ramUsedKb = ramUsed,
            ramTotalKb = ramTotal,
            ramPercent = ramPercent
        )
    }

    /**
     * Clear CPU delta state for a container (call on stop/removal).
     */
    fun clearState(containerName: String) {
        cpuPrevState.remove(containerName)
    }

    // --- CPU ---

    private suspend fun getCpuUsage(
        cmd: String,
        name: String,
        containerName: String
    ): Double = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("$cmd --name=$name run 'cat /proc/stat | head -1'").exec()
            if (!result.isSuccess || result.out.isEmpty()) return@withContext 0.0

            val parts = result.out[0].trim().split("\\s+".toRegex())
            if (parts.size < 5) return@withContext 0.0

            val user   = parts[1].toLongOrNull() ?: 0L
            val nice   = parts[2].toLongOrNull() ?: 0L
            val system = parts[3].toLongOrNull() ?: 0L
            val idle   = parts[4].toLongOrNull() ?: 0L
            val iowait = parts.getOrNull(5)?.toLongOrNull() ?: 0L
            val irq    = parts.getOrNull(6)?.toLongOrNull() ?: 0L
            val sirq   = parts.getOrNull(7)?.toLongOrNull() ?: 0L

            val total = user + nice + system + idle + iowait + irq + sirq
            val prev = cpuPrevState[containerName]

            cpuPrevState[containerName] = total to idle

            if (prev != null) {
                val (prevTotal, prevIdle) = prev
                val totalDelta = total - prevTotal
                val idleDelta  = idle  - prevIdle
                if (totalDelta > 0) {
                    return@withContext ((totalDelta - idleDelta).toDouble() / totalDelta * 100.0)
                        .coerceIn(0.0, 100.0)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting CPU for $containerName", e)
        }
        0.0
    }

    // --- RAM ---

    /** Returns Triple(usedKb, totalKb, percent) from `free` inside container */
    private suspend fun getRamUsage(cmd: String, name: String): Triple<Long, Long, Double> =
        withContext(Dispatchers.IO) {
            try {
                // `free -k` outputs KB; fall back to plain `free` (also KB on most distros)
                val result = Shell.cmd(
                    "$cmd --name=$name run 'free -k 2>/dev/null || free'"
                ).exec()

                if (result.isSuccess) {
                    // Find the "Mem:" line
                    val memLine = result.out.firstOrNull { it.trimStart().startsWith("Mem:") }
                    if (memLine != null) {
                        val parts = memLine.trim().split("\\s+".toRegex())
                        // free format: Mem: total used free shared buff/cache available
                        if (parts.size >= 3) {
                            val total = parts[1].toLongOrNull() ?: 0L
                            val used  = parts[2].toLongOrNull() ?: 0L
                            if (total > 0) {
                                val percent = (used.toDouble() / total * 100.0).coerceIn(0.0, 100.0)
                                return@withContext Triple(used, total, percent)
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error getting RAM", e)
            }
            Triple(0L, 0L, 0.0)
        }
}
