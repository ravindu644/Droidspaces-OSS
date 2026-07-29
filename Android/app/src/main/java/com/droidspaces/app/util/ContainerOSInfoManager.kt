package com.droidspaces.app.util

import android.content.Context
import android.util.Log
import androidx.compose.runtime.mutableIntStateOf
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

/**
 * Live container status (OS name, hostname, IP, uptime, CPU, RAM) from one
 * `show --format` call, with in-memory and persistent caching so cards render
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
     * One container's status as reported by `show --format`.
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
        val ramUsageMb: Long? = null,
        val ramPercent: Double? = null,
        val anlandSocket: String? = null
    )

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
     * One `show --format` round trip: live status of every running container, keyed by
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
        return OSInfo(
            prettyName = obj.optString("os").ifEmpty { null },
            name = null,
            version = null,
            versionId = null,
            id = null,
            hostname = obj.optString("hostname").ifEmpty { null },
            ipAddress = obj.optString("ip").ifEmpty { null },
            anlandSocket = obj.optString("anland_sock").ifEmpty { null },
            uptime = obj.optString("uptime").ifEmpty { null },
            cpuUsage = (obj.optLong("cpu_permill") / 10.0).coerceIn(0.0, 100.0),
            ramUsageMb = if (ramTotalKb > 0) ramUsedKb / 1024 else null,
            ramPercent = if (ramTotalKb > 0) (ramUsedKb.toDouble() / ramTotalKb * 100.0).coerceIn(0.0, 100.0) else null
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

    /**
     * Clear cache for a specific container or all containers.
     * Clears both in-memory and persistent cache.
     */
    fun clearCache(containerName: String? = null, appContext: Context? = null) {
        val ctx = appContext ?: context
        if (containerName != null) {
            cache.remove(containerName)
            if (ctx != null) {
                PreferencesManager.getInstance(ctx).clearContainerOSInfo(containerName)
            }
        } else {
            cache.clear()
            // Note: Clearing all persistent cache would require tracking all container names
            // For now, just clear in-memory cache. Individual containers can be cleared by name.
        }
    }

    /**
     * Parse /etc/os-release output.
     */
    private fun parseOSRelease(output: List<String>): OSInfo {
        var prettyName: String? = null
        var name: String? = null
        var version: String? = null
        var versionId: String? = null
        var id: String? = null

        for (line in output) {
            val trimmed = line.trim()
            when {
                trimmed.startsWith("PRETTY_NAME=") -> {
                    prettyName = extractValue(trimmed.substringAfter("="))
                }
                trimmed.startsWith("NAME=") -> {
                    name = extractValue(trimmed.substringAfter("="))
                }
                trimmed.startsWith("VERSION=") -> {
                    version = extractValue(trimmed.substringAfter("="))
                }
                trimmed.startsWith("VERSION_ID=") -> {
                    versionId = extractValue(trimmed.substringAfter("="))
                }
                trimmed.startsWith("ID=") -> {
                    id = extractValue(trimmed.substringAfter("="))
                }
            }
        }

        return OSInfo(prettyName, name, version, versionId, id, null, null, null)
    }

    /**
     * Extract and clean value from os-release line (removes quotes, whitespace).
     */
    private fun extractValue(value: String): String? {
        if (value.isEmpty()) return null

        var cleaned = value.trim()
        // Remove surrounding quotes
        if (cleaned.startsWith("\"") && cleaned.endsWith("\"")) {
            cleaned = cleaned.substring(1, cleaned.length - 1)
        }
        if (cleaned.startsWith("'") && cleaned.endsWith("'")) {
            cleaned = cleaned.substring(1, cleaned.length - 1)
        }

        return cleaned.takeIf { it.isNotEmpty() }
    }
}
