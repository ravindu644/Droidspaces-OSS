package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Manages container users listing from /etc/passwd.
 * Uses in-memory caching for fast access.
 */
object ContainerUsersManager {
    private const val TAG = "ContainerUsersManager"

    // In-memory cache for users (container name -> List of users)
    private val cache = mutableMapOf<String, List<String>>()

    /**
     * Get list of users from container's /etc/passwd.
     * Returns users with UID between 1000 and 65534 (regular users).
     * Returns cached value if available for instant access.
     */
    suspend fun getUsers(containerName: String, useCache: Boolean = true): List<String> = withContext(Dispatchers.IO) {
        // Return cached value if available and caching is enabled
        if (useCache) {
            cache[containerName]?.let { return@withContext it }
        }

        try {
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'awk -F: \"\\\$3 >= 1000 && \\\$3 < 65534 && \\\$1 !~ /^(nixbld)/ {print \\\$1}\" /etc/passwd 2>/dev/null | tr \"\\n\" \",\" | sed \"s/,\\$//\"'"
            ).exec()

            if (!result.isSuccess || result.out.isEmpty()) {
                return@withContext emptyList()
            }

            val usersString = result.out.joinToString("").trim()
            if (usersString.isEmpty()) {
                return@withContext emptyList()
            }

            // Split by comma and filter empty strings
            val users = usersString.split(",")
                .map { it.trim() }
                .filter { it.isNotEmpty() }

            // Cache the result
            cache[containerName] = users
            users
        } catch (e: Exception) {
            Log.e(TAG, "Error getting users for $containerName", e)
            emptyList()
        }
    }

    /**
     * Get cached users without fetching (returns null if not cached).
     */
    fun getCachedUsers(containerName: String): List<String>? = cache[containerName]

    /**
     * Clear cache for a specific container or all containers.
     */
    fun clearCache(containerName: String? = null) {
        if (containerName != null) {
            cache.remove(containerName)
        } else {
            cache.clear()
        }
    }
}

