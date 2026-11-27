package com.droidspaces.app.ui.viewmodel

import android.app.Application
import androidx.compose.runtime.mutableStateMapOf
import androidx.lifecycle.AndroidViewModel
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import kotlinx.coroutines.delay

/**
 * Holds per-container stats and exposes the polling loop as a suspend function.
 * The loop is NOT self-managed here: the screen drives it via
 * repeatOnLifecycle(STARTED), so polling runs only while the Panel tab is
 * actually on screen AND the app is in the foreground, and is cancelled
 * structurally when either stops being true.  This avoids the old
 * ProcessLifecycleOwner observer, which stopped on background but never
 * restarted on return (polling stayed dead until a tab switch re-composed it).
 */
class SystemStatsViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val CONTAINER_INTERVAL_MS = 2000L
    }

    // Per-container OS info (containerName -> OSInfo), single source of truth for all UI
    var containerUsageMap = mutableStateMapOf<String, ContainerOSInfoManager.OSInfo>()
        private set

    /**
     * Poll OS info for every running container until cancelled.  Returns
     * immediately when nothing is running; the caller re-invokes with a fresh
     * list whenever the running set changes. [onStopped] runs on a tick that found a
     * container gone, so the list can mark it stopped.
     */
    suspend fun monitorContainers(containers: List<ContainerInfo>, onStopped: () -> Unit) {
        val running = containers.filter { it.isRunning }
        if (running.isEmpty()) return

        while (true) {
            // One `show --json` call covers every container, so a tick costs the same
            // for ten containers as for one. Absence from the result means it died.
            val live = ContainerOSInfoManager.fetchAll(getApplication())
            val dead = running.filter { it.name !in live }
            dead.forEach { container ->
                com.droidspaces.app.service.TerminalSessionService.stopContainerSessions(getApplication(), container.name)
                containerUsageMap.remove(container.name)
            }
            containerUsageMap.putAll(live)
            if (dead.isNotEmpty()) onStopped()
            delay(CONTAINER_INTERVAL_MS)
        }
    }

    fun clearContainerUsage(containerName: String) {
        containerUsageMap.remove(containerName)
    }
}
