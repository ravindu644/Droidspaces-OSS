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
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.PreferencesManager
import com.topjohnwu.superuser.Shell
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

    // Seeded from the cache so a restart shows the last list at once, then replaced by
    // the first fetch. An empty list here really means no containers.
    private var _containers by mutableStateOf(prefsManager.cachedContainers)

    // Cached configs all read as stopped, so until the first fetch lands the running
    // count comes from the number saved with them.
    private var loaded by mutableStateOf(false)

    // Refresh state
    var isRefreshing by mutableStateOf(false)
        private set

    // Current refresh job for cancellation
    private var refreshJob: Job? = null

    val containerList: List<ContainerInfo>
        get() = _containers

    val containerCount by derivedStateOf { _containers.size }

    val runningCount by derivedStateOf {
        if (loaded) _containers.count { it.isRunning } else prefsManager.cachedRunningCount
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

                updateState(result)

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
     * Updates the internal state and cache. Main-thread safe.
     */
    fun updateState(result: List<ContainerInfo>) {
        _containers = result
        loaded = true
        prefsManager.saveCachedContainers(result)
        prefsManager.cachedRunningCount = result.count { it.isRunning }

        // Prefetch distro icons for running containers (updates persistent cache)
        result.filter { it.isRunning }.forEach { container ->
            viewModelScope.launch(Dispatchers.IO) {
                ContainerOSInfoManager.prefetchDistroIcon(container.name, getApplication())
            }
        }

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

    /** The backend's scan, then a fresh list. The indicator is for the user's own pull. */
    suspend fun scan(showIndicator: Boolean) {
        if (showIndicator) isRefreshing = true
        try {
            withContext(Dispatchers.IO) {
                Shell.cmd("${Constants.getDroidspacesCommand()} scan").exec()
                val result = ContainerManager.listContainers()
                withContext(Dispatchers.Main) { updateState(result) }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Scan failed", e)
        } finally {
            if (showIndicator) isRefreshing = false
        }
    }

    /**
     * Alias for fetchContainerList() for semantic clarity.
     */
    fun refresh() = fetchContainerList()

    init {
        // Trigger initial fetch on creation to prime state
        fetchContainerList()
    }
}
