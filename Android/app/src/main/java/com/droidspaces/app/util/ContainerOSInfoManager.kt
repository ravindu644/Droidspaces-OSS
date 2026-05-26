package com.droidspaces.app.util

import android.content.Context
import android.util.Log
import androidx.compose.runtime.mutableIntStateOf
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Manages OS information reading from container's /etc/os-release.
 * Uses both in-memory and persistent caching for fast access across app restarts.
 */
object ContainerOSInfoManager {
    private const val TAG = "ContainerOSInfoManager"

    // In-memory cache for OS info (container name -> OSInfo)
    private val cache = mutableMapOf<String, OSInfo>()

    // Increments every time the icon cache is updated — Composables observe this to re-read icons
    val iconCacheVersion = mutableIntStateOf(0)

    // Context for accessing preferences (set when needed)
    @Volatile
    private var context: Context? = null

    /**
     * OS information from /etc/os-release and /etc/hostname.
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
        val ramPercent: Double? = null
    )

    /**
     * Lightweight fetch: reads only /etc/os-release to get PRETTY_NAME, then caches it.
     * Called on container start so the icon is ready before the UI loads.
     * Skips fetch if prettyName is already cached (forever cache).
     * On every start we re-fetch to catch distro upgrades.
     */
    suspend fun prefetchDistroIcon(containerName: String, rootless: Boolean = false, appContext: Context) = withContext(Dispatchers.IO) {
        context = appContext.applicationContext
        try {
            val output = ContainerRuntime.runInContainer(containerName, rootless, "cat /etc/os-release 2>/dev/null || echo")
            if (output.isEmpty() || output.startsWith("ERROR:")) return@withContext
            val lines = output.lines()
            val osInfo = parseOSRelease(lines)
            val existing = cache[containerName]
            if (existing != null) {
                // Merge into existing full entry — preserve hostname + all live data
                val updated = existing.copy(
                    prettyName = osInfo.prettyName ?: existing.prettyName,
                    name = osInfo.name ?: existing.name
                )
                cache[containerName] = updated
                PreferencesManager.getInstance(appContext).saveContainerOSInfo(containerName, updated)
            } else {
                // No existing entry: write to persistent prefs only (for icon rendering on next
                // app start), but skip in-memory cache so getCachedOSInfo returns null and the
                // ViewModel's getOSInfo(useCache=false) loop populates a full entry with hostname.
                PreferencesManager.getInstance(appContext).saveContainerOSInfo(containerName, osInfo)
            }
            iconCacheVersion.intValue++
            Log.i(TAG, "Icon prefetch done for $containerName: ${osInfo.prettyName}")
        } catch (e: Exception) {
            Log.w(TAG, "Icon prefetch failed for $containerName", e)
        }
    }

    /**
     * Returns cached value if available for instant access.
     * Uses both in-memory and persistent cache.
     */
    suspend fun getOSInfo(containerName: String, rootless: Boolean = false, useCache: Boolean = true, appContext: Context? = null): OSInfo = withContext(Dispatchers.IO) {
        // Update context if provided
        if (appContext != null) {
            context = appContext.applicationContext
        }

        // Return in-memory cached value if available and caching is enabled
        if (useCache) {
            cache[containerName]?.let { return@withContext it }

            // Try persistent cache if in-memory cache is empty
            val ctx = context
            if (ctx != null) {
                val prefsManager = PreferencesManager.getInstance(ctx)
                val cachedInfo = prefsManager.loadContainerOSInfo(containerName)
                if (cachedInfo != null) {
                    // Restore to in-memory cache
                    cache[containerName] = cachedInfo
                    return@withContext cachedInfo
                }
            }
        }

        try {
            // Get OS release info
            val osReleaseOutput = ContainerRuntime.runInContainer(containerName, rootless, "cat /etc/os-release 2>/dev/null || echo")
            val osReleaseLines = osReleaseOutput.lines()

            val osInfo = if (osReleaseOutput.isEmpty() || osReleaseOutput.startsWith("ERROR:")) {
                OSInfo(null, null, null, null, null, null, null)
            } else {
                parseOSRelease(osReleaseLines)
            }

            // Get hostname
            val hostnameOutput = ContainerRuntime.runInContainer(containerName, rootless, "cat /etc/hostname 2>/dev/null || hostname 2>/dev/null || echo")
            val hostname = if (hostnameOutput.isNotBlank() && !hostnameOutput.startsWith("ERROR:")) {
                hostnameOutput.lines().firstOrNull()?.trim()?.takeIf { it.isNotEmpty() }
            } else {
                null
            }

            // Get IP addresses (IPv4 only, excluding localhost)
            val ipOutput = ContainerRuntime.runInContainer(containerName, rootless, "ip -4 addr show 2>/dev/null | awk \"/inet / && \\$2 !~ /^127/ {split(\\$2,a,\\\"/\\\"); print a[1]}\" | tr \"\\n\" \" \" || echo")
            val ipAddress = if (ipOutput.isNotBlank() && !ipOutput.startsWith("ERROR:")) {
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

            // Get live metrics via unified usage command
            val usageOutput = ContainerRuntime.getContainerUsage(containerName, rootless)

            var uptimeValue: String? = null
            var cpuUsage: Double? = null
            var ramUsageMb: Long? = null
            var ramPercent: Double? = null

            if (usageOutput.isNotEmpty() && !usageOutput.startsWith("ERROR:")) {
                var ramUsedKb = 0L
                var ramTotalKb = 0L

                usageOutput.lines().forEach { line ->
                    val parts = line.trim().split("=", limit = 2)
                    if (parts.size == 2) {
                        val key = parts[0].trim()
                        val value = parts[1].trim()
                        when (key) {
                            "UPTIME" -> uptimeValue = value.takeIf { it.isNotEmpty() && it != "NONE" }
                            "CPU_PERMILL" -> {
                                val permill = value.toDoubleOrNull() ?: 0.0
                                cpuUsage = (permill / 10.0).coerceIn(0.0, 100.0)
                            }
                            "RAM_USED_KB" -> ramUsedKb = value.toLongOrNull() ?: 0L
                            "RAM_TOTAL_KB" -> ramTotalKb = value.toLongOrNull() ?: 0L
                        }
                    }
                }

                if (ramTotalKb > 0) {
                    ramUsageMb = ramUsedKb / 1024
                    ramPercent = (ramUsedKb.toDouble() / ramTotalKb * 100.0).coerceIn(0.0, 100.0)
                }
            }

            val finalInfo = osInfo.copy(
                hostname = hostname,
                ipAddress = ipAddress,
                uptime = uptimeValue,
                cpuUsage = cpuUsage,
                ramUsageMb = ramUsageMb,
                ramPercent = ramPercent
            )
            // Cache the result (both in-memory and persistent)
            cache[containerName] = finalInfo
            val ctx = context
            if (ctx != null) {
                PreferencesManager.getInstance(ctx).saveContainerOSInfo(containerName, finalInfo)
            }
            finalInfo
        } catch (e: Exception) {
            Log.e(TAG, "Error reading OS info for $containerName", e)
            OSInfo(null, null, null, null, null, null, null)
        }
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
