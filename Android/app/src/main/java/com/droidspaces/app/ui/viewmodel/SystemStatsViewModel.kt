package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.AndroidSystemStatsCollector
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerUsageCollector
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class SystemStatsViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "SystemStatsViewModel"
        private const val SYSTEM_INTERVAL_MS = 2000L
        private const val CONTAINER_INTERVAL_MS = 3000L
    }

    var systemUsage by mutableStateOf(AndroidSystemStatsCollector.SystemUsage())
        private set

    // Per-container usage stats (containerName -> ContainerUsage)
    var containerUsageMap = mutableStateMapOf<String, ContainerUsageCollector.ContainerUsage>()
        private set

    private var systemJob: Job? = null
    private var containerJob: Job? = null

    fun startMonitoring() {
        systemJob?.cancel()
        systemJob = viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                try {
                    systemUsage = AndroidSystemStatsCollector.collectUsage()
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to collect system usage", e)
                }
                delay(SYSTEM_INTERVAL_MS)
            }
        }
    }

    fun startContainerMonitoring(containers: List<ContainerInfo>) {
        containerJob?.cancel()
        val running = containers.filter { it.isRunning }
        if (running.isEmpty()) return

        containerJob = viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                running.forEach { container ->
                    try {
                        val usage = ContainerUsageCollector.collectUsage(container.name)
                        containerUsageMap[container.name] = usage
                    } catch (e: Exception) {
                        Log.e(TAG, "Failed to collect usage for ${container.name}", e)
                    }
                }
                delay(CONTAINER_INTERVAL_MS)
            }
        }
    }

    fun stopContainerMonitoring() {
        containerJob?.cancel()
        containerJob = null
    }

    fun stopMonitoring() {
        systemJob?.cancel()
        systemJob = null
        stopContainerMonitoring()
    }

    override fun onCleared() {
        super.onCleared()
        stopMonitoring()
    }
}
