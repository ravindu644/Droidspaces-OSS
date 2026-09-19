package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DataUsage
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerSystemdManager

private fun ContainerSystemdManager.CommandResult.toInit() = InitCommandResult(isSuccess, output, error)

private fun ContainerSystemdManager.ServiceInfo.toRow(
    containerName: String,
    onInspectUnit: (String) -> Unit,
    onEditOverride: (String) -> Unit,
    onViewLogs: (String) -> Unit,
): InitServiceRow {
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
            if (isRunning) add(InitServiceMenuAction.Command(R.string.restart_service, Icons.Default.Refresh) { ContainerSystemdManager.restartService(containerName, name).toInit() })
            add(InitServiceMenuAction.Command(R.string.mask_service, Icons.Default.Lock) { ContainerSystemdManager.maskService(containerName, name).toInit() })
            add(InitServiceMenuAction.Navigate(R.string.inspect_unit, Icons.Default.Info) { onInspectUnit(name) })
            add(InitServiceMenuAction.Navigate(R.string.view_logs, Icons.Default.Terminal) { onViewLogs(name) })
            add(InitServiceMenuAction.Navigate(R.string.edit_override, Icons.Default.Edit) { onEditOverride(name) })
        }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SystemdScreen(
    containerName: String,
    onNavigateBack: () -> Unit,
    onInspectUnit: (String) -> Unit,
    onEditOverride: (String) -> Unit,
    onViewLogs: (String) -> Unit,
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerSystemdManager.initialize(context) }

    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, statusColorFor(InitServiceUiStatus.ENABLED_RUNNING), R.string.no_running_services) { it.isRunning && it.isEnabled && !it.isMasked },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, statusColorFor(InitServiceUiStatus.ENABLED_STOPPED), R.string.no_enabled_services) { it.isEnabled && !it.isRunning && !it.isMasked },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, statusColorFor(InitServiceUiStatus.DISABLED_STOPPED), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning && !it.isMasked && !it.isStatic },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, statusColorFor(InitServiceUiStatus.ABNORMAL), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled && !it.isStatic && !it.isMasked },
        InitServiceFilterChip("STATIC", R.string.static_legend, statusColorFor(InitServiceUiStatus.STATIC), R.string.no_static_services) { it.isStatic },
        InitServiceFilterChip("MASKED", R.string.masked_legend, statusColorFor(InitServiceUiStatus.MASKED), R.string.no_masked_services) { it.isMasked },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.systemd_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerSystemdManager.isSystemdAvailable(cn) },
        fetchRows = { cn -> ContainerSystemdManager.getAllServices(cn).map { it.toRow(cn, onInspectUnit, onEditOverride, onViewLogs) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
