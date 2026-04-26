package com.droidspaces.app.ui.screen

import androidx.compose.animation.*
import androidx.compose.animation.core.*
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
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
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
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    var refreshTrigger by remember { mutableIntStateOf(0) }

    // Pre-load cached OS info for instant display - zero delay
    // Uses persistent cache that survives app restarts
    var osInfo by remember {
        mutableStateOf(ContainerOSInfoManager.getCachedOSInfo(container.name, context))
    }

    // Systemd state - stabilized to prevent mid-refresh changes
    var systemdState by remember { mutableStateOf<SystemdCardState>(SystemdCardState.Checking) }

    // Background systemd check - happens once, never during refresh
    LaunchedEffect(container.name) {
        scope.launch {
            val isAvailable = ContainerSystemdManager.isSystemdAvailable(container.name)
            systemdState = if (isAvailable) {
                SystemdCardState.Available
            } else {
                SystemdCardState.NotAvailable
            }
        }
        }

    // Auto-refresh when entering the screen - refresh both container info and users
    // Only updates UI if data actually changed (prevents unnecessary recompositions)
    LaunchedEffect(container.name) {
        scope.launch {
            // Refresh container info in background (always fetch fresh data)
            val newOSInfo = ContainerOSInfoManager.getOSInfo(container.name, useCache = false, appContext = context)

            // Only update UI if data actually changed
            val currentInfo = osInfo
            if (currentInfo == null || hasOSInfoChanged(currentInfo, newOSInfo)) {
                osInfo = newOSInfo
            }

            // Trigger users refresh by incrementing refreshTrigger
            refreshTrigger++
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
                // OS Information Card - Stable key prevents recomposition glitches
                item(key = "os_info_${container.name}") {
                    ContainerInfoCard(
                        osInfo = osInfo
                    )
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
           old.uptime != new.uptime
}

/**
 * Premium Container Info Card - Smooth animations, instant display
 */
@Composable
private fun ContainerInfoCard(
    osInfo: ContainerOSInfoManager.OSInfo?,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    // Fade-in animation for smooth appearance
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        visible = true
    }

    val alpha by animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec = AnimationUtils.cardFadeSpec(),
        label = "card_fade"
    )

    ElevatedCard(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 200.dp) // Fixed minimum height prevents layout shifts
            .alpha(alpha)
            .graphicsLayer {
                // Hardware acceleration for premium animations
                this.alpha = alpha
            },
        shape = RoundedCornerShape(16.dp),
        elevation = CardDefaults.elevatedCardElevation(
            defaultElevation = 2.dp,
            pressedElevation = 4.dp
        )
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                Icon(
                    Icons.Default.Info,
                    contentDescription = null,
                    modifier = Modifier.size(22.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                            Text(
                                text = context.getString(R.string.container_info),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold
                            )
            }

                            // Use Crossfade for smooth transitions when osInfo updates
                            Crossfade(
                                targetState = osInfo,
                                animationSpec = AnimationUtils.mediumSpec(),
                                label = "os_info_transition"
                            ) { currentInfo ->
                                if (currentInfo != null) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    // Distribution
                                        (currentInfo.prettyName ?: currentInfo.name)?.let {
                        InfoRow(label = context.getString(R.string.distribution), value = it)
                                }

                                // Version
                                        currentInfo.version?.let {
                        InfoRow(label = context.getString(R.string.version), value = it)
                                }

                                // Container Uptime
                                        currentInfo.uptime?.let {
                        InfoRow(label = context.getString(R.string.uptime), value = it)
                                    }

                                    // Hostname
                                        currentInfo.hostname?.let {
                        InfoRow(label = context.getString(R.string.hostname), value = it)
                                    }

                                        // IP Address
                                        currentInfo.ipAddress?.let {
                                            InfoRow(label = context.getString(R.string.ip_address), value = it)
                                        }
                                }
                                } else {
                                    Text(
                                    text = context.getString(R.string.unable_to_read_container_info),
                style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                }
                            }
                            }
                        }
                    }

/**
 * Info row with optimized layout - no unnecessary recompositions
 */
@Composable
private fun InfoRow(
    label: String,
    value: String
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(0.4f)
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.weight(0.6f)
        )
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

    ElevatedCard(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 88.dp)
            .alpha(alpha)
            .graphicsLayer { this.alpha = alpha },
        shape = RoundedCornerShape(16.dp),
        elevation = CardDefaults.elevatedCardElevation(
            defaultElevation = 2.dp,
            pressedElevation = 4.dp
        ),
        colors = CardDefaults.elevatedCardColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer
        )
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
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                modifier = Modifier.weight(1f)
            ) {
                Icon(
                    imageVector = Icons.Default.Terminal,
                    contentDescription = null,
                    modifier = Modifier.size(22.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                Column {
                    Text(
                        text = context.getString(R.string.terminal),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    val description = if (sessionCount > 0) {
                        "$sessionCount ${if (sessionCount == 1) "session" else "sessions"} running · tap to restore"
                    } else {
                        context.getString(R.string.terminal_card_desc)
                    }
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.7f)
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
                    modifier = Modifier.size(18.dp)
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    if (sessionCount > 0) "Restore" else context.getString(R.string.open),
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

    ElevatedCard(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 88.dp) // Fixed minimum height prevents glitches
            .alpha(alpha)
            .graphicsLayer {
                this.alpha = alpha
            },
        shape = RoundedCornerShape(16.dp),
        elevation = CardDefaults.elevatedCardElevation(
            defaultElevation = 2.dp,
            pressedElevation = 4.dp
        ),
        colors = CardDefaults.elevatedCardColors(
            containerColor = when (state) {
                is SystemdCardState.Available -> MaterialTheme.colorScheme.primaryContainer
                else -> MaterialTheme.colorScheme.surfaceContainerLow
            }
        )
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
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                modifier = Modifier.weight(1f)
            ) {
                Icon(
                    imageVector = when (state) {
                        is SystemdCardState.Available -> Icons.Default.Settings
                        is SystemdCardState.NotAvailable -> Icons.Default.Block
                        is SystemdCardState.Checking -> Icons.Default.HourglassEmpty
                    },
                    contentDescription = null,
                    modifier = Modifier.size(22.dp),
                    tint = when (state) {
                        is SystemdCardState.Available -> MaterialTheme.colorScheme.primary
                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                    }
                )
                Text(
                    text = context.getString(R.string.systemd),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = when (state) {
                        is SystemdCardState.Available -> MaterialTheme.colorScheme.onPrimaryContainer
                        else -> MaterialTheme.colorScheme.onSurface
                    }
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
                            modifier = Modifier.widthIn(min = 140.dp), // Fixed width prevents shifts
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
                                disabledContainerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.5f),
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
                                modifier = Modifier.size(18.dp)
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

