package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.regex.Pattern

/**
 * Collects real-time Android system statistics directly from the device.
 * Reads from /proc files on Android system, not from containers.
 */
object AndroidSystemStatsCollector {
    private const val TAG = "AndroidSystemStatsCollector"

    /**
     * Android system usage statistics.
     */
    data class SystemUsage(
        val cpuPercent: Double = 0.0,
        val ramPercent: Double = 0.0, // Total system RAM used %
        val activeRamKb: Long = 0,    // Total system RAM used in KB
        val totalRamKb: Long = 0,     // Total system RAM in KB
        val temperature: String = "N/A"
    )

    data class ContainerUsage(
        val cpuPercent: Double = 0.0,
        val ramKb: Long = 0,
        val relativeRamPercent: Double = 0.0 // RAM % relative to TOTAL system usage
    )

    // Previous values for calculating CPU usage
    private var prevCpuTotal = 0L
    private var prevCpuIdle = 0L

    /**
     * Collect usage statistics from Android system.
     */
    suspend fun collectUsage(): SystemUsage = withContext(Dispatchers.IO) {
        try {
            val (ramPercent, activeRam, totalRam) = getDetailedRamUsage()
            val cpuPercent = getCpuUsage()
            val temperature = getTemperature()

            SystemUsage(
                cpuPercent = cpuPercent,
                ramPercent = ramPercent,
                activeRamKb = activeRam,
                totalRamKb = totalRam,
                temperature = temperature
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to collect system usage", e)
            SystemUsage() // Return zeros on error
        }
    }

    /**
     * Get CPU usage percentage from Android /proc/stat.
     */
    private suspend fun getCpuUsage(): Double = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("cat /proc/stat | head -1").exec()

            if (result.isSuccess && result.out.isNotEmpty()) {
                val parts = result.out[0].trim().split("\\s+".toRegex())
                if (parts.size >= 8) {
                    val user = parts[1].toLongOrNull() ?: 0L
                    val nice = parts[2].toLongOrNull() ?: 0L
                    val system = parts[3].toLongOrNull() ?: 0L
                    val idle = parts[4].toLongOrNull() ?: 0L
                    val iowait = parts[5].toLongOrNull() ?: 0L
                    val total = user + nice + system + idle + iowait

                    if (prevCpuTotal > 0 && total > prevCpuTotal) {
                        val totalDelta = total - prevCpuTotal
                        val idleDelta = idle - prevCpuIdle
                        val used = totalDelta - idleDelta
                        val percent = if (totalDelta > 0) {
                            ((used.toDouble() / totalDelta.toDouble()) * 100.0).coerceIn(0.0, 100.0)
                        } else {
                            0.0
                        }

                        prevCpuTotal = total
                        prevCpuIdle = idle
                        return@withContext percent
                    } else {
                        prevCpuTotal = total
                        prevCpuIdle = idle
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting CPU usage", e)
        }
        0.0
    }

    /**
     * Get detailed RAM usage from Android /proc/meminfo.
     * Returns Triple(percent, used_kb, total_kb)
     */
    private suspend fun getDetailedRamUsage(): Triple<Double, Long, Long> = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("cat /proc/meminfo | grep -E 'MemTotal|MemAvailable'").exec()

            if (result.isSuccess && result.out.size >= 2) {
                var memTotal = 0L
                var memAvailable = 0L

                result.out.forEach { line ->
                    when {
                        line.contains("MemTotal") -> {
                            val parts = line.trim().split("\\s+".toRegex())
                            if (parts.size >= 2) {
                                memTotal = parts[1].toLongOrNull() ?: 0L
                            }
                        }
                        line.contains("MemAvailable") -> {
                            val parts = line.trim().split("\\s+".toRegex())
                            if (parts.size >= 2) {
                                memAvailable = parts[1].toLongOrNull() ?: 0L
                            }
                        }
                    }
                }

                if (memTotal > 0) {
                    val memUsed = memTotal - memAvailable
                    val percent = ((memUsed.toDouble() / memTotal.toDouble()) * 100.0).coerceIn(0.0, 100.0)
                    return@withContext Triple(percent, memUsed, memTotal)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting RAM usage", e)
        }
        Triple(0.0, 0L, 0L)
    }

    /**
     * Collect resource usage for a specific container PID.
     */
    suspend fun collectContainerUsage(pid: String, totalSystemUsedRamKb: Long): ContainerUsage = withContext(Dispatchers.IO) {
        if (pid == "NONE" || pid.isEmpty()) return@withContext ContainerUsage()

        try {
            // Get RAM (RSS) from /proc/[pid]/status
            // We use RSS (Resident Set Size) as the most accurate "real" RAM usage.
            val ramResult = Shell.cmd("cat /proc/$pid/status | grep VmRSS").exec()
            var containerRamKb = 0L
            if (ramResult.isSuccess && ramResult.out.isNotEmpty()) {
                val parts = ramResult.out[0].trim().split("\\s+".toRegex())
                if (parts.size >= 2) {
                    containerRamKb = parts[1].toLongOrNull() ?: 0L
                }
            }

            // Calculate relative % compared to what Android is currently using globally
            val relativeRamPercent = if (totalSystemUsedRamKb > 0) {
                ((containerRamKb.toDouble() / totalSystemUsedRamKb.toDouble()) * 100.0).coerceIn(0.0, 100.0)
            } else 0.0

            // CPU usage for container (simplistic - based on top or similar if needed, 
            // but for now we focus on RAM as the most accurate "GIGACHAD" metric requested)
            return@withContext ContainerUsage(
                cpuPercent = 0.0, // TODO: Implement per-pid CPU delta if needed
                ramKb = containerRamKb,
                relativeRamPercent = relativeRamPercent
            )
        } catch (e: Exception) {
            return@withContext ContainerUsage()
        }
    }

    /**
     * Get CPU temperature from Android thermal sensors.
     */
    private suspend fun getTemperature(): String = withContext(Dispatchers.IO) {
        try {
            // Try multiple thermal sensor paths
            val thermalPaths = listOf(
                "/sys/class/thermal/thermal_zone0/temp",
                "/sys/class/thermal/thermal_zone1/temp",
                "/sys/devices/virtual/thermal/thermal_zone0/temp"
            )

            for (path in thermalPaths) {
                val result = Shell.cmd("cat $path 2>/dev/null").exec()
                if (result.isSuccess && result.out.isNotEmpty()) {
                    val tempMilliCelsius = result.out[0].trim().toLongOrNull()
                    if (tempMilliCelsius != null && tempMilliCelsius > 0) {
                        val tempCelsius = tempMilliCelsius / 1000.0
                        return@withContext String.format("%.1f°C", tempCelsius)
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting temperature", e)
        }
        "N/A"
    }


}

