package com.droidspaces.app.util

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Collects real-time usage statistics from running containers.
 * Uses JNI-based getContainerUsage and runInContainer.
 */
object ContainerUsageCollector {
    private const val TAG = "ContainerUsageCollector"

    data class ContainerUsage(
        val cpuPercent: Double = 0.0,
        val ramUsedKb: Long = 0,
        val ramTotalKb: Long = 0,
        val ramPercent: Double = 0.0,
        val uptime: String? = null,
        val ipAddress: String? = null
    )

    /**
     * Collect CPU, RAM, Uptime, and IP stats for a running container.
     * Uses JNI getContainerUsage for resource metrics and runInContainer for IP.
     */
    suspend fun collectUsage(containerName: String, rootless: Boolean = false): ContainerUsage = withContext(Dispatchers.IO) {
        try {
            val usageOutput = ContainerRuntime.getContainerUsage(containerName, rootless)

            if (usageOutput.isNotEmpty() && !usageOutput.startsWith("ERROR:")) {
                var uptime: String? = null
                var cpuPercent = 0.0
                var ramUsed = 0L
                var ramTotal = 0L

                usageOutput.lines().forEach { line ->
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

                // Fetch IP address in real-time via JNI runInContainer
                val ipOutput = ContainerRuntime.runInContainer(containerName, rootless,
                    "ip -4 addr show 2>/dev/null | awk \"/inet / && \\$2 !~ /^127/ {split(\\$2,a,\\\"/\\\"); print a[1]}\" | tr \"\\n\" \" \" || echo"
                )

                val ipAddress = if (ipOutput.isNotEmpty() && !ipOutput.startsWith("ERROR:")) {
                    val allIps = ipOutput.trim().split("\\s+".toRegex())
                        .filter {
                            it.isNotEmpty() &&
                            it.matches(Regex("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$")) &&
                            !it.startsWith("127.")
                        }
                        .distinct()

                    if (allIps.isNotEmpty()) {
                        allIps.joinToString(", ")
                    } else {
                        null
                    }
                } else {
                    null
                }

                val ramPercent = if (ramTotal > 0) {
                    (ramUsed.toDouble() / ramTotal * 100.0).coerceIn(0.0, 100.0)
                } else 0.0

                return@withContext ContainerUsage(
                    cpuPercent = cpuPercent,
                    ramUsedKb = ramUsed,
                    ramTotalKb = ramTotal,
                    ramPercent = ramPercent,
                    uptime = uptime,
                    ipAddress = ipAddress
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
