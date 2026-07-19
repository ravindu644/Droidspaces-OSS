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

private fun ContainerOpenRCManager.CommandResult.toInit() = InitCommandResult(isSuccess, output, error)

private fun ContainerOpenRCManager.ServiceInfo.toRow(containerName: String): InitServiceRow {
    val uiStatus = when (status) {
        ContainerOpenRCManager.ServiceStatus.ENABLED_RUNNING -> InitServiceUiStatus.ENABLED_RUNNING
        ContainerOpenRCManager.ServiceStatus.ENABLED_STOPPED -> InitServiceUiStatus.ENABLED_STOPPED
        ContainerOpenRCManager.ServiceStatus.DISABLED_STOPPED -> InitServiceUiStatus.DISABLED_STOPPED
        ContainerOpenRCManager.ServiceStatus.ABNORMAL -> InitServiceUiStatus.ABNORMAL
    }
    return InitServiceRow(
        name = name,
        description = description,
        isRunning = isRunning,
        isEnabled = isEnabled,
        isMasked = false,
        isStatic = false,
        status = uiStatus,
        startStop = { (if (isRunning) ContainerOpenRCManager.stopService(containerName, name) else ContainerOpenRCManager.startService(containerName, name)).toInit() },
        enableDisable = { (if (isEnabled) ContainerOpenRCManager.disableService(containerName, name) else ContainerOpenRCManager.enableService(containerName, name)).toInit() },
        unmask = null,
        menu = buildList {
            if (isRunning) add(InitServiceMenuAction(R.string.restart_service, Icons.Default.Refresh) { ContainerOpenRCManager.restartService(containerName, name).toInit() })
        }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OpenRCScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerOpenRCManager.initialize(context) }

    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, Color(0xFF4CAF50), R.string.no_running_services) { it.isRunning && it.isEnabled },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, Color(0xFFFFCA28), R.string.no_enabled_services) { it.isEnabled && !it.isRunning },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, Color(0xFFEF5350), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, Color(0xFFFF7043), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.openrc_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerOpenRCManager.isOpenRCAvailable(cn) },
        fetchRows = { cn -> ContainerOpenRCManager.getAllServices(cn).map { it.toRow(cn) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
