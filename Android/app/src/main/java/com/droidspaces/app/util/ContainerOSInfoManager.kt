package com.droidspaces.app.util

import com.droidspaces.app.R
import android.content.Context
import android.util.Log
import androidx.compose.runtime.mutableIntStateOf
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

/**
 * Live container status (OS name, hostname, IP, uptime, CPU, RAM) from one
 * `show --json` call, with in-memory and persistent caching so cards render
 * instantly on the next app start.
 */
object ContainerOSInfoManager {
    private const val TAG = "ContainerOSInfoManager"

    // In-memory cache for OS info (container name -> OSInfo)
    private val cache = mutableMapOf<String, OSInfo>()

    // Increments every time the icon cache is updated, Composables observe this to re-read icons
    val iconCacheVersion = mutableIntStateOf(0)

    // Context for accessing preferences (set when needed)
    @Volatile
    private var context: Context? = null

    /**
     * One container's status as reported by `show --json`.
     */
    data class OSInfo(
        val prettyName: String?,
        val name: String?,
        val version: String?,
        val versionId: String?,
        val id: String?,
        val hostname: String?,
        val ipAddress: String?,
        val uptime: String? = null,
        val cpuUsage: Double? = null,
        val ramUsedKb: Long? = null,
        val ramPercent: Double? = null,
        /** The memory limit in force. Null when unlimited, and then [ramPercent] is of the host's RAM. */
        val ramLimitKb: Long? = null,
        val anlandSocket: String? = null,
        /** Booted with Termux:X11, from the boot snapshot like [anlandSocket]. */
        val x11: Boolean = false
    ) {
        /** "13.50 MB / 1.50 GB (1%)" under a limit, "13.50 MB (0.2%)" of the host without one.
         * Each side picks its own unit, the way fastfetch prints memory. */
        fun ramLabel(context: Context): String? {
            val used = ResourceLimits.formatMemoryUsage(context, ramUsedKb ?: return null)
            val percent = ramPercent ?: 0.0
            return if (ramLimitKb != null) context.getString(R.string.ram_used_of_limit_label, used, ResourceLimits.formatMemoryUsage(context, ramLimitKb), percent)
            else context.getString(R.string.ram_used_label, used, percent)
        }
    }

    /**
     * Lightweight fetch: reads only /etc/os-release to get PRETTY_NAME, then caches it.
     * Called on container start so the icon is ready before the UI loads.
     * Skips fetch if prettyName is already cached (forever cache).
     * On every start we re-fetch to catch distro upgrades.
     */
    suspend fun prefetchDistroIcon(containerName: String, appContext: Context) = withContext(Dispatchers.IO) {
        context = appContext.applicationContext
        try {
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'cat /etc/os-release 2>/dev/null || echo'"
            ).exec()
            if (!result.isSuccess || result.out.isEmpty()) return@withContext
            val osInfo = parseOSRelease(result.out)
            val existing = cache[containerName]
            if (existing != null) {
                // Merge into existing full entry, preserve hostname + all live data
                val updated = existing.copy(
                    prettyName = osInfo.prettyName ?: existing.prettyName,
                    name = osInfo.name ?: existing.name
                )
                cache[containerName] = updated
                PreferencesManager.getInstance(appContext).saveContainerOSInfo(containerName, updated)
            } else {
                // No existing entry: write to persistent prefs only (for icon rendering on next
                // app start), but skip in-memory cache so getCachedOSInfo returns null and the
                // ViewModel's fetchAll() loop populates a full entry with hostname.
                PreferencesManager.getInstance(appContext).saveContainerOSInfo(containerName, osInfo)
            }
            iconCacheVersion.intValue++
            Log.i(TAG, "Icon prefetch done for $containerName: ${osInfo.prettyName}")
        } catch (e: Exception) {
            Log.w(TAG, "Icon prefetch failed for $containerName", e)
        }
    }

    /**
     * One `show --json` round trip: live status of every running container, keyed by
     * name. A container missing from the result is not running.
     */
    suspend fun fetchAll(appContext: Context): Map<String, OSInfo> = withContext(Dispatchers.IO) {
        context = appContext.applicationContext
        try {
            val result = Shell.cmd(ContainerCommandBuilder.buildShowCommand()).exec()
            if (!result.isSuccess) return@withContext emptyMap()
            val root = JSONObject(result.out.joinToString(""))
            val ramTotalKb = root.optLong("ram_total_kb")
            val running = root.getJSONArray("running")
            val out = (0 until running.length())
                .map { running.getJSONObject(it) }
                .associate { it.getString("name") to toOSInfo(it, ramTotalKb) }
            val prefs = PreferencesManager.getInstance(appContext)
            out.forEach { (name, info) ->
                cache[name] = info
                // Only persist if we got valid OS info; don't overwrite with a failed fetch
                if (info.prettyName != null) prefs.saveContainerOSInfo(name, info)
            }
            out
        } catch (e: Exception) {
            Log.e(TAG, "Error fetching container status", e)
            emptyMap()
        }
    }

    private fun toOSInfo(obj: JSONObject, ramTotalKb: Long): OSInfo {
        val ramUsedKb = obj.optLong("ram_used_kb")
        // Limits in force, 0 when unlimited or when the backend predates them.
        // Usage is then shown against the container's own allowance instead of
        // the whole host.
        val ramLimitKb = obj.optLong("ram_limit_kb")
        val cpuLimitPermill = obj.optLong("cpu_limit_permill")
        val ramBaseKb = if (ramLimitKb > 0) ramLimitKb else ramTotalKb
        val cpuPermill = obj.optLong("cpu_permill")
        return OSInfo(
            prettyName = obj.optString("os").ifEmpty { null },
            name = null,
            version = null,
            versionId = null,
            id = null,
            hostname = obj.optString("hostname").ifEmpty { null },
            ipAddress = obj.optString("ip").ifEmpty { null },
            anlandSocket = obj.optString("anland_sock").ifEmpty { null },
            x11 = obj.optInt("x11") == 1,
            uptime = obj.optString("uptime").ifEmpty { null },
            cpuUsage = (if (cpuLimitPermill > 0) cpuPermill * 100.0 / cpuLimitPermill else cpuPermill / 10.0).coerceIn(0.0, 100.0),
            ramUsedKb = if (ramTotalKb > 0) ramUsedKb else null,
            ramPercent = if (ramTotalKb > 0) (ramUsedKb.toDouble() / ramBaseKb * 100.0).coerceIn(0.0, 100.0) else null,
            ramLimitKb = ramLimitKb.takeIf { it > 0 }
        )
    }

    /**
     * Get cached OS info without fetching (returns null if not cached).
     * Checks both in-memory and persistent cache.
     */
    fun getCachedOSInfo(containerName: String, appContext: Context? = null): OSInfo? {
        // Check in-memory cache first
        cache[containerName]?.let { return it }

        // Check persistent cache
        val ctx = appContext ?: context
        if (ctx != null) {
            val prefsManager = PreferencesManager.getInstance(ctx)
            val cachedInfo = prefsManager.loadContainerOSInfo(containerName)
            if (cachedInfo != null) {
                // Restore to in-memory cache
                cache[containerName] = cachedInfo
                return cachedInfo
            }
        }

        return null
    }

    /** Forgets one container, in memory and in the persistent cache. */
    fun clearCache(containerName: String, appContext: Context? = null) {
        cache.remove(containerName)
        (appContext ?: context)?.let { PreferencesManager.getInstance(it).clearContainerOSInfo(containerName) }
    }

    /** KEY=value lines, quotes stripped; the five fields the card shows. */
    private fun parseOSRelease(output: List<String>): OSInfo {
        val kv = output.filter { "=" in it }.associate {
            it.substringBefore("=").trim() to
                it.substringAfter("=").trim().removeSurrounding("\"").removeSurrounding("'").ifEmpty { null }
        }
        return OSInfo(kv["PRETTY_NAME"], kv["NAME"], kv["VERSION"], kv["VERSION_ID"], kv["ID"], null, null, null)
    }
}
