package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.ContainerUsageCollector
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * ViewModel for tracking real-time container usage statistics.
 * Updates stats periodically for running containers.
 */
class ContainerUsageViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "ContainerUsageViewModel"
        private const val UPDATE_INTERVAL_MS = 2000L // Update every 2 seconds
    }

    // Map of container name to usage stats
    private val _usageStats = mutableStateMapOf<String, ContainerUsageCollector.ContainerUsage>()
    val usageStats: Map<String, ContainerUsageCollector.ContainerUsage> = _usageStats

    // Update job - allows cancellation
    private var updateJob: Job? = null

    // Track which containers we're monitoring
    private var monitoredContainers = setOf<String>()

    /**
     * Start monitoring usage for a list of running containers.
     * Automatically stops monitoring containers that are no longer running.
     */
    fun startMonitoring(containerNames: Set<String>) {
        // Update monitored containers
        monitoredContainers = containerNames

        // Cancel existing job
        updateJob?.cancel()

        // Start new monitoring loop
        updateJob = viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                // Collect usage for all monitored containers concurrently, but AWAIT the
                // whole tick before sleeping — so slow collectors can't let successive
                // ticks pile up an unbounded number of in-flight child coroutines.
                val results = coroutineScope {
                    containerNames.map { containerName ->
                        async {
                            try {
                                containerName to ContainerUsageCollector.collectUsage(containerName)
                            } catch (e: Exception) {
                                Log.e(TAG, "Failed to collect usage for $containerName", e)
                                null
                            }
                        }
                    }.awaitAll()
                }

                // Compose snapshot state must be mutated on the main thread.
                withContext(Dispatchers.Main) {
                    results.filterNotNull().forEach { (name, usage) -> _usageStats[name] = usage }
                    _usageStats.keys.removeAll { it !in containerNames }
                }

                // Wait before next update
                delay(UPDATE_INTERVAL_MS)
            }
        }
    }

    /**
     * Stop monitoring usage.
     */
    fun stopMonitoring() {
        updateJob?.cancel()
        updateJob = null
        _usageStats.clear()
        monitoredContainers = emptySet()
    }

    /**
     * Get usage stats for a specific container.
     */
    fun getUsage(containerName: String): ContainerUsageCollector.ContainerUsage {
        return _usageStats[containerName] ?: ContainerUsageCollector.ContainerUsage()
    }

    override fun onCleared() {
        super.onCleared()
        stopMonitoring()
    }
}

