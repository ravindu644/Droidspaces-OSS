package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerSystemdManager

private fun ContainerSystemdManager.CommandResult.toInit() = InitCommandResult(isSuccess, output, error)

private fun ContainerSystemdManager.ServiceInfo.toRow(containerName: String): InitServiceRow {
    val uiStatus = when (status) {
        ContainerSystemdManager.ServiceStatus.ENABLED_RUNNING -> InitServiceUiStatus.ENABLED_RUNNING
        ContainerSystemdManager.ServiceStatus.ENABLED_STOPPED -> InitServiceUiStatus.ENABLED_STOPPED
        ContainerSystemdManager.ServiceStatus.DISABLED_STOPPED -> InitServiceUiStatus.DISABLED_STOPPED
        ContainerSystemdManager.ServiceStatus.STATIC -> InitServiceUiStatus.STATIC
        ContainerSystemdManager.ServiceStatus.ABNORMAL -> InitServiceUiStatus.ABNORMAL
        ContainerSystemdManager.ServiceStatus.MASKED -> InitServiceUiStatus.MASKED
    }
    return InitServiceRow(
        name = name,
        description = description,
        isRunning = isRunning,
        isEnabled = isEnabled,
        isMasked = isMasked,
        isStatic = isStatic,
        status = uiStatus,
        startStop = { (if (isRunning) ContainerSystemdManager.stopService(containerName, name) else ContainerSystemdManager.startService(containerName, name)).toInit() },
        enableDisable = { (if (isEnabled) ContainerSystemdManager.disableService(containerName, name) else ContainerSystemdManager.enableService(containerName, name)).toInit() },
        unmask = if (isMasked) { { ContainerSystemdManager.unmaskService(containerName, name).toInit() } } else null,
        menu = buildList {
            if (isRunning) add(InitServiceMenuAction(R.string.restart_service, Icons.Default.Refresh) { ContainerSystemdManager.restartService(containerName, name).toInit() })
            add(InitServiceMenuAction(R.string.mask_service, Icons.Default.Lock) { ContainerSystemdManager.maskService(containerName, name).toInit() })
        }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SystemdScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerSystemdManager.initialize(context) }

    val maskedColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, Color(0xFF4CAF50), R.string.no_running_services) { it.isRunning && it.isEnabled && !it.isMasked },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, Color(0xFFFFCA28), R.string.no_enabled_services) { it.isEnabled && !it.isRunning && !it.isMasked },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, Color(0xFFEF5350), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning && !it.isMasked && !it.isStatic },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, Color(0xFFFF7043), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled && !it.isStatic && !it.isMasked },
        InitServiceFilterChip("STATIC", R.string.static_legend, Color(0xFF607D8B), R.string.no_static_services) { it.isStatic },
        InitServiceFilterChip("MASKED", R.string.masked_legend, maskedColor, R.string.no_masked_services) { it.isMasked },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.systemd_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerSystemdManager.isSystemdAvailable(cn) },
        fetchRows = { cn -> ContainerSystemdManager.getAllServices(cn).map { it.toRow(cn) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
