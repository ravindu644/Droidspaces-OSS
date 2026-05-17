package com.droidspaces.app.util

import android.content.Context
import android.util.Log
import com.topjohnwu.superuser.Shell
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
     * Read OS information from container's /etc/os-release and hostname from /etc/hostname.
     * Returns cached value if available for instant access.
     * Uses both in-memory and persistent cache.
     */
    suspend fun getOSInfo(containerName: String, useCache: Boolean = true, appContext: Context? = null): OSInfo = withContext(Dispatchers.IO) {
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
            val osReleaseResult = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'cat /etc/os-release 2>/dev/null || echo'"
            ).exec()

            val osInfo = if (!osReleaseResult.isSuccess || osReleaseResult.out.isEmpty()) {
                OSInfo(null, null, null, null, null, null, null)
            } else {
                parseOSRelease(osReleaseResult.out)
            }

            // Get hostname
            val hostnameResult = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'cat /etc/hostname 2>/dev/null || hostname 2>/dev/null || echo'"
            ).exec()

            val hostname = if (hostnameResult.isSuccess && hostnameResult.out.isNotEmpty()) {
                hostnameResult.out.firstOrNull()?.trim()?.takeIf { it.isNotEmpty() }
            } else {
                null
            }

            // Get IP addresses (IPv4 only, excluding localhost)
            // Filter out 127.x.x.x addresses and get all other IPv4 addresses
            val ipResult = Shell.cmd(ContainerCommandBuilder.buildGetIpCommand(containerName)).exec()

            val ipAddress = if (ipResult.isSuccess && ipResult.out.isNotEmpty()) {
                // Get all lines, filter valid IPv4 addresses (excluding localhost), join with comma
                val allIps = ipResult.out
                    .flatMap { it.trim().split("\\s+".toRegex()) }
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
            val usageCommand = ContainerCommandBuilder.buildUsageCommand(containerName)
            val usageResult = Shell.cmd(usageCommand).exec()

            var uptimeValue: String? = null
            var cpuUsage: Double? = null
            var ramUsageMb: Long? = null
            var ramPercent: Double? = null

            if (usageResult.isSuccess && usageResult.out.isNotEmpty()) {
                var ramUsedKb = 0L
                var ramTotalKb = 0L

                usageResult.out.forEach { line ->
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
