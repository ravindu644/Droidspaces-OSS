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

private fun ContainerProcdManager.CommandResult.toInit() = InitCommandResult(isSuccess, output, error)

private fun ContainerProcdManager.ServiceInfo.toRow(containerName: String): InitServiceRow {
    val uiStatus = when (status) {
        ContainerProcdManager.ServiceStatus.ENABLED_RUNNING -> InitServiceUiStatus.ENABLED_RUNNING
        ContainerProcdManager.ServiceStatus.ENABLED_STOPPED -> InitServiceUiStatus.ENABLED_STOPPED
        ContainerProcdManager.ServiceStatus.DISABLED_STOPPED -> InitServiceUiStatus.DISABLED_STOPPED
        ContainerProcdManager.ServiceStatus.ABNORMAL -> InitServiceUiStatus.ABNORMAL
        ContainerProcdManager.ServiceStatus.UNKNOWN -> InitServiceUiStatus.UNKNOWN
    }
    return InitServiceRow(
        name = name,
        description = description,
        isRunning = isRunning,
        isEnabled = isEnabled,
        isMasked = false,
        isStatic = false,
        status = uiStatus,
        startStop = { (if (isRunning) ContainerProcdManager.stopService(containerName, name) else ContainerProcdManager.startService(containerName, name)).toInit() },
        enableDisable = { (if (isEnabled) ContainerProcdManager.disableService(containerName, name) else ContainerProcdManager.enableService(containerName, name)).toInit() },
        unmask = null,
        menu = listOf(
            InitServiceMenuAction(R.string.restart_service, Icons.Default.Refresh) { ContainerProcdManager.restartService(containerName, name).toInit() },
            InitServiceMenuAction(R.string.reload_service, Icons.Default.Refresh) { ContainerProcdManager.reloadService(containerName, name).toInit() },
        )
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProcdScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerProcdManager.initialize(context) }

    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, Color(0xFF4CAF50), R.string.no_running_services) { it.isRunning && it.isEnabled },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, Color(0xFFFFCA28), R.string.no_enabled_services) { it.isEnabled && !it.isRunning && it.status != InitServiceUiStatus.UNKNOWN },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, Color(0xFFEF5350), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning && it.status != InitServiceUiStatus.UNKNOWN },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, Color(0xFFFF7043), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled },
        InitServiceFilterChip("UNKNOWN", R.string.unknown_legend, Color(0xFF90A4AE), R.string.no_unknown_services) { it.status == InitServiceUiStatus.UNKNOWN },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.openwrt_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerProcdManager.isProcdAvailable(cn) },
        fetchRows = { cn -> ContainerProcdManager.getAllServices(cn).map { it.toRow(cn) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
