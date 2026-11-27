package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerOpenRCManager
import com.droidspaces.app.util.ServiceInfo
import com.droidspaces.app.util.ServiceStatus

private fun ServiceInfo.toRow(containerName: String) = InitServiceRow(
    name = name,
    description = description,
    isRunning = isRunning,
    isEnabled = isEnabled,
    isMasked = false,
    isStatic = false,
    status = status,
    startStop = { if (isRunning) ContainerOpenRCManager.stopService(containerName, name) else ContainerOpenRCManager.startService(containerName, name) },
    enableDisable = { if (isEnabled) ContainerOpenRCManager.disableService(containerName, name) else ContainerOpenRCManager.enableService(containerName, name) },
    unmask = null,
    menu = buildList {
        if (isRunning) add(InitServiceMenuAction.Command(R.string.restart_service, Icons.Default.Refresh) { ContainerOpenRCManager.restartService(containerName, name) })
    }
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OpenRCScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerOpenRCManager.initialize(context) }

    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, statusColorFor(ServiceStatus.ENABLED_RUNNING), R.string.no_running_services) { it.isRunning && it.isEnabled },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, statusColorFor(ServiceStatus.ENABLED_STOPPED), R.string.no_enabled_services) { it.isEnabled && !it.isRunning },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, statusColorFor(ServiceStatus.DISABLED_STOPPED), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, statusColorFor(ServiceStatus.ABNORMAL), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.openrc_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerOpenRCManager.isAvailable(cn) },
        fetchRows = { cn -> ContainerOpenRCManager.getAllServices(cn).map { it.toRow(cn) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
