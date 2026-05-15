package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.util.Log
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.Constants
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * ViewModel for container list management - optimized for performance and correctness.
 *
 * Key improvements over previous version:
 * 1. NO static companion object - state is instance-scoped (proper MVVM)
 * 2. Uses proper Compose state management inside ViewModel
 * 3. Single refresh job with cancellation support
 * 4. Caches counts in preferences for instant display on restart
 * 5. Thread-safe state updates via Main dispatcher
 *
 * Performance characteristics:
 * - fetchContainerList(): ~50-200ms (IO bound)
 * - State reads: ~0 overhead (Compose snapshot system)
 * - Memory: Single list instance, no unnecessary copies
 */
class ContainerViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "ContainerViewModel"
    }

    private val prefsManager = PreferencesManager.getInstance(application)

    // Container list state - properly scoped to ViewModel instance
    private var _containers by mutableStateOf<List<ContainerInfo>>(emptyList())

    // Refresh state
    var isRefreshing by mutableStateOf(false)
        private set

    // Current refresh job for cancellation
    private var refreshJob: Job? = null

    // Public container list - uses cached fallback for instant display on restart
    val containerList: List<ContainerInfo>
        get() = if (_containers.isEmpty()) prefsManager.cachedContainers else _containers

    // Derived counts with cached fallback for instant display on restart
    // Uses derivedStateOf for efficient recomposition
    val containerCount by derivedStateOf {
        if (_containers.isEmpty()) prefsManager.cachedContainerCount else _containers.size
    }

    val runningCount by derivedStateOf {
        if (_containers.isEmpty()) prefsManager.cachedRunningCount else _containers.count { it.isRunning }
    }

    /**
     * Fetch container list from backend.
     * Cancels any ongoing fetch and starts a new one.
     *
     * Performance: ~50-200ms (IO dispatcher)
     * Thread safety: Updates state on Main dispatcher
     */
    fun fetchContainerList() {
        // Cancel any ongoing refresh
        refreshJob?.cancel()

        refreshJob = viewModelScope.launch {
            isRefreshing = true

            try {
                val result = withContext(Dispatchers.IO) {
                    ContainerManager.listContainers()
                }

                // Update state (already on Main dispatcher)
                _containers = result

                // Cache full list for instant display on next app start
                prefsManager.saveCachedContainers(result)
                prefsManager.cachedContainerCount = result.size
                prefsManager.cachedRunningCount = result.count { it.isRunning }

                // Proactive user fetch for running containers - primes cache for terminal picker
                result.filter { it.isRunning }.forEach { container ->
                    launch(Dispatchers.IO) {
                        try {
                            com.droidspaces.app.util.ContainerUsersManager.getUsers(container.name)
                        } catch (e: Exception) {
                            Log.w(TAG, "Failed to prime user cache for ${container.name}")
                        }
                    }
                }

                Log.i(TAG, "Container list refreshed: ${result.size} containers, ${result.count { it.isRunning }} running")

            } catch (e: Exception) {
                Log.e(TAG, "Failed to fetch container list", e)
                // Keep existing containers on error - don't clear

            } finally {
                isRefreshing = false
            }
        }
    }

    /**
     * Suspendable version of fetchContainerList for sequential logic (like scanning).
     */
    suspend fun refreshContainersSuspend(): List<ContainerInfo> {
        val result = withContext(Dispatchers.IO) {
            ContainerManager.listContainers()
        }
        updateState(result)
        Log.i(TAG, "Suspend refresh completed: ${result.size} containers")
        return result
    }

    /**
     * Updates the internal state and cache. Main-thread safe.
     */
    fun updateState(result: List<ContainerInfo>) {
        _containers = result
        prefsManager.saveCachedContainers(result)
        prefsManager.cachedContainerCount = result.size
        prefsManager.cachedRunningCount = result.count { it.isRunning }

        // Proactive user fetch for running containers
        result.filter { it.isRunning }.forEach { container ->
            viewModelScope.launch(Dispatchers.IO) {
                try {
                    com.droidspaces.app.util.ContainerUsersManager.getUsers(container.name)
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to prime user cache for ${container.name}")
                }
            }
        }
    }

    /**
     * Runs the 'scan' command and updates the container list.
     * Intended to be called manually during UI refresh flows.
     */
    suspend fun runScan() {
        isRefreshing = true
        try {
            withContext(Dispatchers.IO) {
                Log.i(TAG, "Executing manual scan...")
                val command = "${Constants.getDroidspacesCommand()} scan"
                com.topjohnwu.superuser.Shell.cmd(command).exec()

                // Fetch new list after scan
                val result = ContainerManager.listContainers()
                withContext(Dispatchers.Main) {
                    updateState(result)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Scan command failed", e)
        } finally {
            isRefreshing = false
        }
    }

    /**
     * Runs the 'scan' command silently in the background.
     * Updates state if new containers are found, but doesn't show UI indicators.
     */
    suspend fun silentScan() {
        withContext(Dispatchers.IO) {
            try {
                Log.i(TAG, "Executing silent background scan...")
                val command = "${Constants.getDroidspacesCommand()} scan"
                com.topjohnwu.superuser.Shell.cmd(command).exec()

                // Fetch new list and update state
                val result = ContainerManager.listContainers()
                withContext(Dispatchers.Main) {
                    updateState(result)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Silent scan failed", e)
            }
        }
    }

    /**
     * Silent background refresh - no UI indicators.
     * Perfect for post-operation refreshes where we don't want spinner.
     */
    fun silentRefresh() {
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    ContainerManager.listContainers()
                }

                _containers = result

                // Cache counts
                prefsManager.cachedContainerCount = result.size
                prefsManager.cachedRunningCount = result.count { it.isRunning }

                Log.i(TAG, "Silent refresh completed: ${result.size} containers")

            } catch (e: Exception) {
                Log.e(TAG, "Silent refresh failed", e)
            }
        }
    }

    /**
     * Alias for fetchContainerList() for semantic clarity.
     */
    fun refresh() = fetchContainerList()

    /**
     * Clear container list (for logout or reset scenarios).
     */
    fun clear() {
        refreshJob?.cancel()
        _containers = emptyList()
        isRefreshing = false
    }

    init {
        // Trigger initial fetch on creation to prime state
        fetchContainerList()
    }
}
