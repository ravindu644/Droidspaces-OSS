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

            if (!result.isSuccess) return@withContext emptyList()

            // A container with only root yields an empty list. Cache that too, or a
            // stale entry from a previous container of the same name outlives refresh.
            val users = result.out.joinToString("").trim()
                .split(",")
                .map { it.trim() }
                .filter { it.isNotEmpty() }

            cache[containerName] = users
            users
        } catch (e: Exception) {
            Log.e(TAG, "Error getting users for $containerName", e)
            emptyList()
        }
    }

    // Each `run` joins the container's namespaces, so the details page asks for
    // the init system and the users in one entry instead of four
    private val PROBE_SCRIPT = """
        if command -v systemctl >/dev/null 2>&1; then echo systemd
        elif [ -f /etc/openwrt_release ] && { [ -x /sbin/procd ] || [ -x /sbin/ubusd ] || command -v ubus >/dev/null 2>&1 || command -v service >/dev/null 2>&1; }; then echo procd
        elif command -v rc-service >/dev/null 2>&1; then echo openrc
        elif command -v sv >/dev/null 2>&1; then echo runit
        else echo none; fi
        awk -F: '$3 >= 1000 && $3 < 65534 && $1 !~ /^(nixbld)/ {print $1}' /etc/passwd 2>/dev/null
    """.trimIndent()

    private val INIT_SYSTEMS = setOf("systemd", "procd", "openrc", "runit", "none")

    /**
     * The container's init system ("systemd", "procd", "openrc", "runit" or "none") and its
     * regular users, from one `run`. Null if the container could not be entered.
     */
    suspend fun probe(containerName: String): Pair<String, List<String>>? = withContext(Dispatchers.IO) {
        try {
            val out = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run ${ContainerCommandBuilder.quote(PROBE_SCRIPT)}"
            ).exec().out.map { it.trim() }.filter { it.isNotEmpty() }

            val init = out.firstOrNull()?.takeIf { it in INIT_SYSTEMS } ?: return@withContext null
            val users = out.drop(1)
            cache[containerName] = users
            init to users
        } catch (e: Exception) {
            Log.e(TAG, "Error probing $containerName", e)
            null
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

