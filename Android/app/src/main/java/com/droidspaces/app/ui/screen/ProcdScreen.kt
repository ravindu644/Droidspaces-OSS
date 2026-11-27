package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerProcdManager
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
    startStop = { if (isRunning) ContainerProcdManager.stopService(containerName, name) else ContainerProcdManager.startService(containerName, name) },
    enableDisable = { if (isEnabled) ContainerProcdManager.disableService(containerName, name) else ContainerProcdManager.enableService(containerName, name) },
    unmask = null,
    menu = listOf(
        InitServiceMenuAction.Command(R.string.restart_service, Icons.Default.Refresh) { ContainerProcdManager.restartService(containerName, name) },
        InitServiceMenuAction.Command(R.string.reload_service, Icons.Default.Refresh) { ContainerProcdManager.reloadService(containerName, name) },
    )
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProcdScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerProcdManager.initialize(context) }

    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, statusColorFor(ServiceStatus.ENABLED_RUNNING), R.string.no_running_services) { it.isRunning && it.isEnabled },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, statusColorFor(ServiceStatus.ENABLED_STOPPED), R.string.no_enabled_services) { it.isEnabled && !it.isRunning && it.status != ServiceStatus.UNKNOWN },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, statusColorFor(ServiceStatus.DISABLED_STOPPED), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning && it.status != ServiceStatus.UNKNOWN },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, statusColorFor(ServiceStatus.ABNORMAL), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled },
        InitServiceFilterChip("UNKNOWN", R.string.unknown_legend, statusColorFor(ServiceStatus.UNKNOWN), R.string.no_unknown_services) { it.status == ServiceStatus.UNKNOWN },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.openwrt_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerProcdManager.isAvailable(cn) },
        fetchRows = { cn -> ContainerProcdManager.getAllServices(cn).map { it.toRow(cn) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
