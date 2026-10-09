package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.DsTopAppBar
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.ui.component.DetailPill
import com.droidspaces.app.ui.component.ContainerUsersCard
import com.droidspaces.app.ui.component.DsSnackbarHost
import com.droidspaces.app.ui.component.SectionLabel
import com.droidspaces.app.ui.component.StatRow
import com.droidspaces.app.ui.component.amoledCardBorder
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerSystemdManager
import com.droidspaces.app.util.ContainerProcdManager
import com.droidspaces.app.util.ContainerOpenRCManager
import com.droidspaces.app.util.ContainerDiskUsageManager
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.droidspaces.app.util.IconUtils
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.service.TerminalSessionService
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.graphics.vector.rememberVectorPainter

/**
 * The per-container hub: Details and Usage sections, the shell user, then the rows into
 * the terminal and the init system. Panel is the live glance; this is where you act.
 * Items keep stable keys so the 2s stats poll never rebuilds the users toggles.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun ContainerDetailsScreen(
    container: ContainerInfo,
    onNavigateBack: () -> Unit,
    onNavigateToServices: (InitSystem) -> Unit = {},
    onNavigateToTerminal: () -> Unit = {}
) {
    val context = LocalContext.current
    val snackbarHostState = remember { SnackbarHostState() }
    var refreshTrigger by remember { mutableIntStateOf(0) }

    // Pre-load cached OS info for instant display - zero delay
    // Uses persistent cache that survives app restarts
    var osInfo by remember {
        mutableStateOf(ContainerOSInfoManager.getCachedOSInfo(container.name, context))
    }

    // Init system state - detect systemd first, then OpenWrt/procd, then OpenRC
    var initSystemState by remember { mutableStateOf<InitSystemCardState>(InitSystemCardState.Checking) }

    // Disk usage of the sparse rootfs.img (only relevant for sparse-image containers).
    // Seed from the in-memory cache so a re-opened screen paints instantly.
    var diskUsage by remember {
        mutableStateOf(
            if (container.useSparseImage) ContainerDiskUsageManager.getCached(container.rootfsPath) else null
        )
    }

    LaunchedEffect(container.name) {
        initSystemState = when {
            ContainerSystemdManager.isSystemdAvailable(container.name) ->
                InitSystemCardState.Available(InitSystem.SYSTEMD)
            ContainerProcdManager.isProcdAvailable(container.name) ->
                InitSystemCardState.Available(InitSystem.PROCD)
            ContainerOpenRCManager.isOpenRCAvailable(container.name) ->
                InitSystemCardState.Available(InitSystem.OPENRC)
            else ->
                InitSystemCardState.NotAvailable
        }
    }

    val lifecycleOwner = LocalLifecycleOwner.current

    // Auto-refresh loop - refreshes both container info and uptime every 2s
    // Only updates UI if data actually changed (prevents unnecessary recompositions)
    // Uses repeatOnLifecycle to pause polling when the app is in background
    LaunchedEffect(container.name) {
        lifecycleOwner.lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (true) {
                try {
                    // Missing from `show` means the container died; navigate back
                    val newOSInfo = ContainerOSInfoManager.fetchAll(context)[container.name]
                    if (newOSInfo == null) {
                        // Kill all terminal sessions for this container
                        context.startService(
                            android.content.Intent(context, TerminalSessionService::class.java).apply {
                                action = TerminalSessionService.ACTION_STOP_CONTAINER_SESSIONS
                                putExtra(TerminalSessionService.EXTRA_CONTAINER_NAME, container.name)
                            }
                        )
                        onNavigateBack()
                        break
                    }

                    val currentInfo = osInfo
                    if (currentInfo == null || hasOSInfoChanged(currentInfo, newOSInfo)) {
                        osInfo = newOSInfo
                    }

                    // Refresh users on the first run, then leave it to manual/specific refreshes
                    if (refreshTrigger == 0) refreshTrigger++
                } catch (e: Exception) {
                    // Silently ignore refresh errors to keep the UI smooth
                }
                delay(2000)
            }
        }
    }

    // Disk-usage footprint - computed once when the screen opens (the sparse image's
    // footprint changes slowly, so a snapshot is enough). Runs in its own effect so the
    // first reading isn't blocked behind the heavier OS-info shell calls above.
    if (container.useSparseImage) {
        LaunchedEffect(container.name) {
            ContainerDiskUsageManager.getUsage(container.rootfsPath)?.let { diskUsage = it }
        }
    }

    Scaffold(
        topBar = {
            DsTopAppBar(
                title = container.name,
                onBack = onNavigateBack,
                actions = {
                    // Re-reads the user list; the stats poll on their own every 2s
                    IconButton(onClick = { refreshTrigger++ }) {
                        Icon(Icons.Default.Refresh, context.getString(R.string.refresh))
                    }
                }
            )
        },
        snackbarHost = { DsSnackbarHost(snackbarHostState) }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item(key = "details_${container.name}") {
                Section(context.getString(R.string.details_section)) {
                    val info = osInfo
                    if (info == null) {
                        Text(
                            context.getString(R.string.unable_to_read_container_info),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        StatRow(context.getString(R.string.distribution), info.prettyName ?: info.name ?: "Linux")
                        StatRow(context.getString(R.string.hostname), info.hostname ?: "localhost")
                        info.uptime?.let { StatRow(context.getString(R.string.uptime), it) }
                        info.ipAddress?.takeIf { it.isNotBlank() }?.let { StatRow(context.getString(R.string.ip_address), it) }
                    }
                }
            }

            item(key = "usage_${container.name}") {
                Section(context.getString(R.string.usage_section)) {
                    val info = osInfo
                    info?.cpuUsage?.takeIf { it >= 0.0 }?.let {
                        StatRow(context.getString(R.string.cpu), context.getString(R.string.cpu_percent_label, it), (it / 100).toFloat())
                    }
                    info?.ramLabel(context)?.let {
                        StatRow(context.getString(R.string.property_memory), it, ((info.ramPercent ?: 0.0) / 100).toFloat())
                    }
                    // Only a sparse image has a footprint worth measuring
                    if (container.useSparseImage) {
                        val usage = diskUsage
                        StatRow(
                            context.getString(R.string.disk_usage),
                            usage?.let {
                                context.getString(R.string.disk_usage_used_of_total, formatBytes(it.usedBytes), formatBytes(it.totalBytes))
                            } ?: context.getString(R.string.disk_usage_calculating),
                            usage?.let { if (it.totalBytes > 0L) it.usedBytes.toFloat() / it.totalBytes else 0f } ?: 0f
                        )
                    }
                }
            }

            item(key = "users_${container.name}") {
                Section(context.getString(R.string.available_users)) {
                    ContainerUsersCard(
                        containerName = container.name,
                        refreshTrigger = refreshTrigger,
                        snackbarHostState = snackbarHostState
                    )
                }
            }

            item(key = "terminal_${container.name}") {
                val sessionCount by remember {
                    derivedStateOf {
                        TerminalSessionService.globalSessionList.values.count { it.containerName == container.name }
                    }
                }
                DetailRow(
                    icon = Icons.Default.Terminal,
                    title = context.getString(R.string.terminal),
                    description = when (sessionCount) {
                        0 -> context.getString(R.string.terminal_card_desc)
                        1 -> context.getString(R.string.session_running_singular)
                        else -> context.getString(R.string.sessions_running_plural, sessionCount)
                    }
                ) {
                    DetailPill(
                        text = context.getString(if (sessionCount > 0) R.string.restore else R.string.open),
                        onClick = onNavigateToTerminal,
                        icon = if (sessionCount > 0) Icons.Default.Terminal else Icons.Default.ChevronRight
                    )
                }
            }

            item(key = "init_${container.name}") {
                val state = initSystemState
                DetailRow(
                    icon = Icons.Default.Tune,
                    title = when (state) {
                        is InitSystemCardState.Available -> context.getString(
                            when (state.initSystem) {
                                InitSystem.SYSTEMD -> R.string.systemd
                                InitSystem.PROCD -> R.string.openwrt
                                InitSystem.OPENRC -> R.string.openrc
                            }
                        )
                        else -> context.getString(R.string.init_system)
                    },
                    description = context.getString(R.string.init_services_desc)
                ) {
                    when (state) {
                        is InitSystemCardState.Checking -> LoadingIndicator(size = LoadingSize.Medium)
                        is InitSystemCardState.NotAvailable -> Text(
                            context.getString(R.string.not_available),
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        is InitSystemCardState.Available -> DetailPill(
                            text = context.getString(R.string.manage),
                            onClick = { onNavigateToServices(state.initSystem) }
                        )
                    }
                }
            }
        }
    }
}

/**
 * A section of details: the label above, then a surfaceContainer card holding the rows
 * 12 apart. The label sits 8 above its card and the extra 8 on top makes the 20 DESIGN.md
 * asks for over a section label, on top of the list's 12.
 */
@Composable
private fun Section(label: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        SectionLabel(label, Modifier.padding(start = 4.dp, top = 8.dp, bottom = 8.dp))
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = MaterialTheme.shapes.extraLarge,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = amoledCardBorder()
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp), content = content)
        }
    }
}

/** A one-card row that leads somewhere: icon, title over a line of description, and the pill. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun DetailRow(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    description: String,
    trailing: @Composable () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = amoledCardBorder()
    ) {
        ListItem(
            headlineContent = { Text(title, style = MaterialTheme.typography.titleMediumEmphasized) },
            supportingContent = { Text(description) },
            leadingContent = { Icon(icon, contentDescription = null) },
            trailingContent = trailing,
            colors = ListItemDefaults.colors(containerColor = androidx.compose.ui.graphics.Color.Transparent)
        )
    }
}

/**
 * Which init system is available in the container.
 */
enum class InitSystem { SYSTEMD, PROCD, OPENRC }

/**
 * Init system card state - sealed for type safety and stability
 */
private sealed class InitSystemCardState {
    data object Checking : InitSystemCardState()
    data class Available(val initSystem: InitSystem) : InitSystemCardState()
    data object NotAvailable : InitSystemCardState()
}

/**
 * Helper function to check if OS info actually changed.
 * Only updates UI when there are real changes (prevents unnecessary recompositions).
 */
private fun hasOSInfoChanged(old: ContainerOSInfoManager.OSInfo, new: ContainerOSInfoManager.OSInfo): Boolean {
    return old.prettyName != new.prettyName ||
           old.name != new.name ||
           old.version != new.version ||
           old.versionId != new.versionId ||
           old.id != new.id ||
           old.hostname != new.hostname ||
           old.ipAddress != new.ipAddress ||
           old.uptime != new.uptime ||
           old.cpuUsage != new.cpuUsage ||
           old.ramUsedKb != new.ramUsedKb ||
           old.ramPercent != new.ramPercent ||
           old.ramLimitKb != new.ramLimitKb
}


/**
 * Formats a byte count as a human-readable string (GiB with one decimal, or MiB below 1 GiB).
 */
private fun formatBytes(bytes: Long): String {
    val gb = bytes.toDouble() / (1024.0 * 1024.0 * 1024.0)
    if (gb >= 1.0) return String.format("%.1f GB", gb)
    val mb = bytes.toDouble() / (1024.0 * 1024.0)
    return String.format("%.0f MB", mb)
}
