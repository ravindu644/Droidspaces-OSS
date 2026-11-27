package com.droidspaces.app.util

import android.app.ActivityManager
import android.content.Context
import android.system.Os
import android.system.OsConstants
import com.droidspaces.app.R
import java.util.Locale

object ResourceLimits {
    /** The backend's default CFS period, cpu_quota is microseconds per this. */
    const val CPU_PERIOD_US = 100_000L
    const val MEMORY_STEP_MB = 128
    const val DEFAULT_PIDS = 1024L
    /** The backend's floor (DS_MIN_PIDS_LIMIT): below it there is no room for a shell. */
    const val MIN_PIDS = 16L
    /** The kernel's own ceiling for pids.max, and the backend's. */
    const val MAX_PIDS = 4_194_304L

    /** 0 is "no limit" and always fine. */
    fun isValidPidsLimit(limit: Long): Boolean = limit == 0L || limit >= MIN_PIDS

    fun totalMemoryMb(context: Context): Int {
        val info = ActivityManager.MemoryInfo()
        (context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).getMemoryInfo(info)
        return (info.totalMem / (1024 * 1024)).toInt()
    }

    /** Configured cores, not online ones: availableProcessors() drops cores that are hotplugged off. */
    fun cpuCores(): Int = Os.sysconf(OsConstants._SC_NPROCESSORS_CONF).toInt().coerceAtLeast(1)

    fun formatMemory(context: Context, mb: Int): String =
        if (mb < 1024) context.getString(R.string.memory_mb, mb)
        else context.getString(R.string.memory_gb, String.format(Locale.getDefault(), "%.1f", mb / 1024f))

    /** A live figure: "13.50 MB", "2.88 GB", two decimals in whichever unit fits, the way
     * fastfetch prints memory. Limits keep the round [formatMemory] look. */
    /** A byte count for a size chip or a usage line: one decimal, binary units. */
    fun formatBytes(bytes: Long): String = when {
        bytes >= 1L shl 30 -> "%.1f GB".format(Locale.getDefault(), bytes / (1L shl 30).toDouble())
        bytes >= 1L shl 20 -> "%.1f MB".format(Locale.getDefault(), bytes / (1L shl 20).toDouble())
        bytes >= 1L shl 10 -> "%.0f KB".format(Locale.getDefault(), bytes / 1024.0)
        else -> "$bytes B"
    }

    fun formatMemoryUsage(context: Context, kb: Long): String {
        fun two(v: Double) = String.format(Locale.getDefault(), "%.2f", v)
        return when {
            kb < 1024 -> context.getString(R.string.memory_kb, two(kb.toDouble()))
            kb < 1024 * 1024 -> context.getString(R.string.memory_mb_decimal, two(kb / 1024.0))
            else -> context.getString(R.string.memory_gb, two(kb / 1048576.0))
        }
    }

    fun formatCores(context: Context, cores: Float): String {
        val n = if (cores % 1f == 0f) cores.toInt().toString() else String.format(Locale.getDefault(), "%.1f", cores)
        return context.resources.getQuantityString(R.plurals.cpu_cores, if (cores == 1f) 1 else 2, n)
    }

    /** The limits a container has set, as short display values. Null where unlimited. */
    fun memoryLabel(context: Context, c: ContainerInfo): String? =
        c.memoryLimit.takeIf { it > 0 }?.let { formatMemory(context, (it / (1024 * 1024)).toInt()) }

    fun cpuLabel(context: Context, c: ContainerInfo): String? =
        c.cpuQuota.takeIf { it > 0 }?.let { formatCores(context, it.toFloat() / CPU_PERIOD_US) }

    fun pidsLabel(context: Context, c: ContainerInfo): String? =
        c.pidsLimit.takeIf { it > 0 }?.let { context.getString(R.string.limit_pids_value, it) }
}
