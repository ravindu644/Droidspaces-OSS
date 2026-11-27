package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.PowerSettingsNew
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerRunitManager
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
    startStop = { if (isRunning) ContainerRunitManager.stopService(containerName, name) else ContainerRunitManager.startService(containerName, name) },
    enableDisable = { if (isEnabled) ContainerRunitManager.disableService(containerName, name) else ContainerRunitManager.enableService(containerName, name) },
    unmask = null,
    menu = buildList {
        if (isRunning) add(InitServiceMenuAction.Command(R.string.restart_service, Icons.Default.Refresh) { ContainerRunitManager.restartService(containerName, name) })
        add(InitServiceMenuAction.Command(R.string.toggle_run_at_boot, Icons.Rounded.PowerSettingsNew) { ContainerRunitManager.toggleDefaultState(containerName, name) })
    }
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RunitScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContainerRunitManager.initialize(context) }

    val filters = listOf(
        InitServiceFilterChip("RUNNING", R.string.running, statusColorFor(ServiceStatus.ENABLED_RUNNING), R.string.no_running_services) { it.isRunning && it.isEnabled },
        InitServiceFilterChip("ENABLED", R.string.enabled_legend, statusColorFor(ServiceStatus.ENABLED_STOPPED), R.string.no_enabled_services) { it.isEnabled && !it.isRunning },
        InitServiceFilterChip("DISABLED", R.string.disabled_legend, statusColorFor(ServiceStatus.DISABLED_STOPPED), R.string.no_disabled_services) { !it.isEnabled && !it.isRunning },
        InitServiceFilterChip("ALL", R.string.all_legend, null, R.string.no_services_found) { true },
    )

    InitServiceScreen(
        containerName = containerName,
        titleRes = R.string.runit_services,
        onNavigateBack = onNavigateBack,
        isAvailable = { cn -> ContainerRunitManager.isAvailable(cn) },
        fetchRows = { cn -> ContainerRunitManager.getAllServices(cn).map { it.toRow(cn) } },
        filters = filters,
        defaultFilterId = "RUNNING",
    )
}
