package com.droidspaces.app.ui.screen

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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.ui.component.ContainerUsersCard
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerSystemdManager
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.droidspaces.app.util.AnimationUtils
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.service.TerminalSessionService
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.LifecycleOwner

/**
 * Premium Container Details Screen - Zero glitches, buttery smooth animations
 *
 * Key optimizations:
 * - Stable LazyColumn keys prevent recomposition glitches
 * - Fixed minimum heights prevent layout shifts during refresh
 * - Smooth 200ms animations with FastOutSlowIn for premium feel
 * - Pre-computed color states (no runtime calculations)
 * - Hardware-accelerated animations via graphicsLayer
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerDetailsScreen(
    container: ContainerInfo,
    onNavigateBack: () -> Unit,
    onNavigateToSystemd: () -> Unit = {},
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

    // Systemd state - stabilized to prevent mid-refresh changes
    var systemdState by remember { mutableStateOf<SystemdCardState>(SystemdCardState.Checking) }

    // Background systemd check - happens once per container load
    LaunchedEffect(container.name) {
        val isAvailable = ContainerSystemdManager.isSystemdAvailable(container.name)
        systemdState = if (isAvailable) {
            SystemdCardState.Available
        } else {
            SystemdCardState.NotAvailable
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
                    val newOSInfo = ContainerOSInfoManager.getOSInfo(container.name, useCache = false, appContext = context)
                    val currentInfo = osInfo
                    if (currentInfo == null || hasOSInfoChanged(currentInfo, newOSInfo)) {
                        osInfo = newOSInfo
                    }

                    // If container is not running, we don't need real-time updates for usage
                    if (!container.isRunning) break

                    // Refresh users on the first run, then leave it to manual/specific refreshes
                    if (refreshTrigger == 0) refreshTrigger++
                } catch (e: Exception) {
                    // Silently ignore refresh errors to keep the UI smooth
                }
                delay(2000)
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = container.name,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = LocalContext.current.getString(R.string.back)
                        )
                    }
                }
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // OS Information - Total Rewrite (Zero Shadow / Flat Design)
            item(key = "os_info_flat_grid_${container.name}") {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(20.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                    border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        // Header Row
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Info,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = context.getString(R.string.container_info),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }

                        osInfo?.let { info ->
                            Column(
                                modifier = Modifier.fillMaxWidth(),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                // Row 1: Distro and Hostname
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                                ) {
                                    val distroName = info.prettyName ?: info.name ?: "Linux"
                                    IdentityToken(
                                        modifier = Modifier.weight(1f),
                                        label = context.getString(R.string.distribution),
                                        value = distroName,
                                        icon = when {
                                            distroName.contains("Ubuntu", true) -> Icons.Default.Adjust
                                            distroName.contains("Debian", true) -> Icons.Default.Circle
                                            distroName.contains("Alpine", true) -> Icons.Default.Landscape
                                            else -> Icons.Default.Dashboard
                                        },
                                        containerColor = MaterialTheme.colorScheme.primary
                                    )
                                    IdentityToken(
                                        modifier = Modifier.weight(1f),
                                        label = context.getString(R.string.hostname),
                                        value = info.hostname ?: "localhost",
                                        icon = Icons.Default.Computer,
                                        containerColor = MaterialTheme.colorScheme.secondary
                                    )
                                }

                                // Row 2: Uptime and IP
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                                ) {
                                    IdentityToken(
                                        modifier = Modifier.weight(1f),
                                        label = context.getString(R.string.uptime),
                                        value = info.uptime ?: "0s",
                                        icon = Icons.Default.Timer,
                                        containerColor = MaterialTheme.colorScheme.tertiary
                                    )
                                    IdentityToken(
                                        modifier = Modifier.weight(1f),
                                        label = context.getString(R.string.ip_address),
                                        value = info.ipAddress ?: "127.0.0.1",
                                        icon = Icons.Default.Lan,
                                        containerColor = MaterialTheme.colorScheme.outline
                                    )
                                }

                                // Row 3: CPU and RAM Usage (Live Sync)
                                if (container.isRunning) {
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                    ) {
                                        IdentityToken(
                                            modifier = Modifier.weight(1f),
                                            label = context.getString(R.string.cpu_usage_label),
                                            value = info.cpuUsage?.let { context.getString(R.string.cpu_percent_label, it) } ?: "---",
                                            icon = Icons.Default.Speed,
                                            containerColor = MaterialTheme.colorScheme.primary
                                        )
                                        IdentityToken(
                                            modifier = Modifier.weight(1f),
                                            label = context.getString(R.string.ram_usage_label),
                                            value = info.ramUsageMb?.let { context.getString(R.string.ram_percent_label, it, info.ramPercent ?: 0.0) } ?: "---",
                                            icon = Icons.Default.Memory,
                                            containerColor = MaterialTheme.colorScheme.secondary
                                        )
                                    }
                                }
                            }
                        } ?: Box(
                            modifier = Modifier.fillMaxWidth().height(100.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = context.getString(R.string.unable_to_read_container_info),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            // Users Card - Stable key (don't include refreshTrigger to prevent recreation)
            item(key = "users_${container.name}") {
                ContainerUsersCard(
                    containerName = container.name,
                    refreshTrigger = refreshTrigger,
                    snackbarHostState = snackbarHostState
                )
            }

            item(key = "terminal_${container.name}") {
                TerminalCard(
                    containerName = container.name,
                    onOpenTerminal = onNavigateToTerminal
                )
            }

            item(key = "systemd_${container.name}") {
                PremiumSystemdCard(
                    state = systemdState,
                    onNavigateToSystemd = onNavigateToSystemd
                )
            }
        }
    }
}

/**
 * Systemd card state - sealed for type safety and stability
 */
private sealed class SystemdCardState {
    data object Checking : SystemdCardState()
    data object Available : SystemdCardState()
    data object NotAvailable : SystemdCardState()
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
           old.ramUsageMb != new.ramUsageMb ||
           old.ramPercent != new.ramPercent
}


/**
 * Info row with optimized layout - no unnecessary recompositions
 */
@Composable
private fun IdentityToken(
    label: String,
    value: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    containerColor: androidx.compose.ui.graphics.Color,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier
            .heightIn(min = 96.dp)
            .fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = containerColor.copy(alpha = 0.08f),
        border = androidx.compose.foundation.BorderStroke(1.dp, containerColor.copy(alpha = 0.15f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            // Header Row (Top-Center)
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 4.dp) // Consistent inner top margin
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(14.dp),
                    tint = containerColor
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(
                    text = label.uppercase(),
                    style = MaterialTheme.typography.labelSmall,
                    color = containerColor,
                    fontWeight = FontWeight.Black,
                    letterSpacing = 1.sp,
                    maxLines = 1
                )
            }

            // Value Text (Middle-Center)
            Text(
                text = value,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                maxLines = 2,
                overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
            )
        }
    }
}

/**
 * Terminal Card - opens a real interactive shell inside the container.
 */
@Composable
private fun TerminalCard(
    containerName: String,
    onOpenTerminal: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    val sessionCount by remember {
        derivedStateOf {
            TerminalSessionService.globalSessionList.values.count { it.containerName == containerName }
        }
    }

    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { visible = true }

    val alpha by animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec = AnimationUtils.cardFadeSpec(),
        label = "terminal_card_fade"
    )

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 88.dp)
            .alpha(alpha)
            .graphicsLayer { this.alpha = alpha },
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.weight(1f)
            ) {
                Icon(
                    imageVector = Icons.Default.Terminal,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                Column {
                    Text(
                        text = context.getString(R.string.terminal),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    val description = if (sessionCount > 0) {
                        if (sessionCount == 1) context.getString(R.string.session_running_singular)
                        else context.getString(R.string.sessions_running_plural, sessionCount)
                    } else {
                        context.getString(R.string.terminal_card_desc)
                    }
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }
            }

            Button(
                onClick = onOpenTerminal,
                modifier = Modifier.widthIn(min = 140.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = MaterialTheme.colorScheme.onPrimary
                )
            ) {
                Icon(
                    if (sessionCount > 0) Icons.Default.Terminal else Icons.Default.ChevronRight,
                    contentDescription = null,
                    modifier = Modifier.size(16.dp)
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    if (sessionCount > 0) context.getString(R.string.restore) else context.getString(R.string.open),
                    fontWeight = FontWeight.SemiBold
                )
            }
        }
    }
}

/**
 * PREMIUM SYSTEMD CARD - Zero glitches, buttery smooth
 *
 * Key features:
 * - Fixed height prevents layout shifts during pull-to-refresh
 * - CrossFade for smooth state transitions (200ms)
 * - Pre-computed button widths prevent text changes from causing jumps
 * - Hardware-accelerated animations
 */
@Composable
private fun PremiumSystemdCard(
    state: SystemdCardState,
    onNavigateToSystemd: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    // Fade-in animation
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        visible = true
    }

    val alpha by animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec = AnimationUtils.cardFadeSpec(),
        label = "systemd_fade"
    )

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 88.dp)
            .alpha(alpha)
            .graphicsLayer {
                this.alpha = alpha
            },
        shape = RoundedCornerShape(20.dp),
        color = when (state) {
            is SystemdCardState.Available -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
            else -> MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = 0.5f)
        },
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Icon + Title
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.weight(1f)
            ) {
                Icon(
                    imageVector = when (state) {
                        is SystemdCardState.Available -> Icons.Default.Settings
                        is SystemdCardState.NotAvailable -> Icons.Default.Block
                        is SystemdCardState.Checking -> Icons.Default.HourglassEmpty
                    },
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    tint = when (state) {
                        is SystemdCardState.Available -> MaterialTheme.colorScheme.primary
                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                    }
                )
                Text(
                    text = context.getString(R.string.systemd),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface
                )
            }

            // Button with CrossFade for smooth transitions - NO GLITCHES
            Crossfade(
                targetState = state,
                animationSpec = AnimationUtils.mediumSpec(),
                label = "systemd_button_transition"
            ) { currentState ->
                when (currentState) {
                    is SystemdCardState.Checking -> {
                        FilledTonalButton(
                            onClick = {},
                            enabled = false,
                            modifier = Modifier.widthIn(min = 140.dp),
                            colors = ButtonDefaults.filledTonalButtonColors(
                                disabledContainerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
                                disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        ) {
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                LoadingIndicator(
                                    size = LoadingSize.Small,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(context.getString(R.string.checking))
                            }
                        }
                    }
                    is SystemdCardState.NotAvailable -> {
                        FilledTonalButton(
                            onClick = {},
                            enabled = false,
                            modifier = Modifier.widthIn(min = 140.dp),
                            colors = ButtonDefaults.filledTonalButtonColors(
                                disabledContainerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.3f),
                                disabledContentColor = MaterialTheme.colorScheme.onErrorContainer
                            )
                        ) {
                            Text(context.getString(R.string.not_available))
                        }
                    }
                    is SystemdCardState.Available -> {
                        Button(
                            onClick = onNavigateToSystemd,
                            modifier = Modifier.widthIn(min = 140.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.primary,
                                contentColor = MaterialTheme.colorScheme.onPrimary
                            )
                        ) {
                            Icon(
                                Icons.Default.ChevronRight,
                                contentDescription = null,
                                modifier = Modifier.size(16.dp)
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                context.getString(R.string.manage),
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                    }
                }
            }
        }
    }
}

