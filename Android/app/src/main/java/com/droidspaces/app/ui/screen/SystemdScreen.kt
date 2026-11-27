package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DataUsage
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerSystemdManager
import com.droidspaces.app.util.ServiceInfo
import com.droidspaces.app.util.ServiceStatus

private fun ServiceInfo.toRow(
    containerName: String,
    onInspectUnit: (String) -> Unit,
    onEditOverride: (String) -> Unit,
    onViewLogs: (String) -> Unit,
) = InitServiceRow(
    name = name,
    description = description,
    isRunning = isRunning,
    isEnabled = isEnabled,
    isMasked = isMasked,
    isStatic = isStatic,
    status = status,
    startStop = { if (isRunning) ContainerSystemdManager.stopService(containerName, name) else ContainerSystemdManager.startService(containerName, name) },
    enableDisable = { if (isEnabled) ContainerSystemdManager.disableService(containerName, name) else ContainerSystemdManager.enableService(containerName, name) },
    unmask = if (isMasked) { { ContainerSystemdManager.unmaskService(containerName, name) } } else null,
    menu = buildList {
        if (isRunning) add(InitServiceMenuAction.Command(R.string.restart_service, Icons.Default.Refresh) { ContainerSystemdManager.restartService(containerName, name) })
        add(InitServiceMenuAction.Command(R.string.mask_service, Icons.Default.Lock) { ContainerSystemdManager.maskService(containerName, name) })
        add(InitServiceMenuAction.Navigate(R.string.inspect_unit, Icons.Default.Info) { onInspectUnit(name) })
        add(InitServiceMenuAction.Navigate(R.string.view_logs, Icons.AutoMirrored.Filled.Article) { onViewLogs(name) })
        add(InitServiceMenuAction.Navigate(R.string.edit_override, Icons.Default.Edit) { onEditOverride(name) })
    }
)

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
        InitServiceFilterChip("RUNNING", R.string.running, statusColorFor(ServiceStatus.ENABLED_RUNNING), R.string.no_running_services) { it.isRunning && it.isEnabled && !it.isMasked },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, statusColorFor(ServiceStatus.ENABLED_STOPPED), R.string.no_enabled_services) { it.isEnabled && !it.isRunning && !it.isMasked },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, statusColorFor(ServiceStatus.DISABLED_STOPPED), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning && !it.isMasked && !it.isStatic },
        InitServiceFilterChip("ABNORMAL", R.string.abnormal_legend, statusColorFor(ServiceStatus.ABNORMAL), R.string.no_abnormal_services) { it.isRunning && !it.isEnabled && !it.isStatic && !it.isMasked },
        InitServiceFilterChip("STATIC", R.string.static_legend, statusColorFor(ServiceStatus.STATIC), R.string.no_static_services) { it.isStatic },
        InitServiceFilterChip("MASKED", R.string.masked_legend, statusColorFor(ServiceStatus.MASKED), R.string.no_masked_services) { it.isMasked },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.systemd_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerSystemdManager.isAvailable(cn) },
        fetchRows = { cn -> ContainerSystemdManager.getAllServices(cn).map { it.toRow(cn, onInspectUnit, onEditOverride, onViewLogs) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
