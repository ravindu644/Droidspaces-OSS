package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Collects real-time Android system statistics from /proc on the host. */
object AndroidSystemStatsCollector {
    private const val TAG = "AndroidSystemStatsCollector"

    data class SystemUsage(
        val cpuPercent: Double = 0.0,
        val ramPercent: Double = 0.0,
        val activeRamKb: Long = 0,
        val totalRamKb: Long = 0,
        val temperature: String = "N/A"
    )

    private var prevCpuTotal = 0L
    private var prevCpuIdle = 0L

    suspend fun collectUsage(): SystemUsage = withContext(Dispatchers.IO) {
        try {
            val (ramPercent, activeRam, totalRam) = getDetailedRamUsage()
            val cpuPercent = getCpuUsage()
            val temperature = getTemperature()
            SystemUsage(cpuPercent, ramPercent, activeRam, totalRam, temperature)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to collect system usage", e)
            SystemUsage()
        }
    }

    private suspend fun getCpuUsage(): Double = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("cat /proc/stat | head -1").exec()
            if (result.isSuccess && result.out.isNotEmpty()) {
                val parts = result.out[0].trim().split("\\s+".toRegex())
                if (parts.size >= 8) {
                    val user   = parts[1].toLongOrNull() ?: 0L
                    val nice   = parts[2].toLongOrNull() ?: 0L
                    val system = parts[3].toLongOrNull() ?: 0L
                    val idle   = parts[4].toLongOrNull() ?: 0L
                    val iowait = parts[5].toLongOrNull() ?: 0L
                    val total  = user + nice + system + idle + iowait
                    if (prevCpuTotal > 0 && total > prevCpuTotal) {
                        val totalDelta = total - prevCpuTotal
                        val idleDelta  = idle  - prevCpuIdle
                        val percent = ((totalDelta - idleDelta).toDouble() / totalDelta * 100.0)
                            .coerceIn(0.0, 100.0)
                        prevCpuTotal = total; prevCpuIdle = idle
                        return@withContext percent
                    }
                    prevCpuTotal = total; prevCpuIdle = idle
                }
            }
        } catch (e: Exception) { Log.e(TAG, "Error getting CPU usage", e) }
        0.0
    }

    private suspend fun getDetailedRamUsage(): Triple<Double, Long, Long> = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("cat /proc/meminfo | grep -E 'MemTotal|MemAvailable'").exec()
            if (result.isSuccess && result.out.size >= 2) {
                var memTotal = 0L; var memAvailable = 0L
                result.out.forEach { line ->
                    val parts = line.trim().split("\\s+".toRegex())
                    if (parts.size >= 2) when {
                        line.contains("MemTotal")     -> memTotal     = parts[1].toLongOrNull() ?: 0L
                        line.contains("MemAvailable") -> memAvailable = parts[1].toLongOrNull() ?: 0L
                    }
                }
                if (memTotal > 0) {
                    val memUsed = memTotal - memAvailable
                    val percent = (memUsed.toDouble() / memTotal * 100.0).coerceIn(0.0, 100.0)
                    return@withContext Triple(percent, memUsed, memTotal)
                }
            }
        } catch (e: Exception) { Log.e(TAG, "Error getting RAM usage", e) }
        Triple(0.0, 0L, 0L)
    }

    private suspend fun getTemperature(): String = withContext(Dispatchers.IO) {
        val paths = listOf(
            "/sys/class/thermal/thermal_zone0/temp",
            "/sys/class/thermal/thermal_zone1/temp",
            "/sys/devices/virtual/thermal/thermal_zone0/temp"
        )
        for (path in paths) {
            try {
                val result = Shell.cmd("cat $path 2>/dev/null").exec()
                if (result.isSuccess && result.out.isNotEmpty()) {
                    val mC = result.out[0].trim().toLongOrNull()
                    if (mC != null && mC > 0) return@withContext String.format("%.1f°C", mC / 1000.0)
                }
            } catch (_: Exception) {}
        }
        "N/A"
    }
}
