package com.droidspaces.app.ui.screen

import androidx.compose.foundation.background
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.FocusUtils
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.ContainerSystemdManager
import com.droidspaces.app.util.ContainerSystemdManager.ServiceFilter
import com.droidspaces.app.util.ContainerSystemdManager.ServiceInfo
import com.droidspaces.app.util.ContainerSystemdManager.ServiceStatus
import com.droidspaces.app.ui.util.ProgressDialog
import com.droidspaces.app.ui.util.ErrorLogsDialog
import com.droidspaces.app.ui.util.FullScreenLoading
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.util.showError
import com.droidspaces.app.ui.util.showSuccess
import kotlinx.coroutines.launch
import com.droidspaces.app.R

// Premium animation specs - now using centralized AnimationUtils

// Status colors - Material You inspired
private val ColorGreen = Color(0xFF4CAF50)
private val ColorYellow = Color(0xFFFFCA28)
private val ColorRed = Color(0xFFEF5350)
private val ColorStatic = Color(0xFF607D8B)
private val ColorAbnormal = Color(0xFFFF7043)
private val ColorWhite = Color(0xFFE0E0E0)

// UI States
private sealed class ScreenState {
    data object Loading : ScreenState()
    data object SystemdNotAvailable : ScreenState()
    data class Ready(val services: List<ServiceInfo>) : ScreenState()
}

private sealed class ActionState {
    data object Idle : ActionState()
    data class InProgress(val serviceName: String, val actionName: String) : ActionState()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SystemdScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    // Initialize script on first composition
    LaunchedEffect(Unit) {
        ContainerSystemdManager.initialize(context)
    }

    // Core state
    var screenState by remember { mutableStateOf<ScreenState>(ScreenState.Loading) }
    var actionState by remember { mutableStateOf<ActionState>(ActionState.Idle) }
    var selectedFilter by remember { mutableStateOf(ServiceFilter.RUNNING) }
    var logsDialogContent by remember { mutableStateOf<List<String>?>(null) }
    var searchQuery by remember { mutableStateOf("") }

    // Track ongoing operations to prevent concurrent execution
    var fetchJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }
    var actionJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }

    // Fetch services - always shows loading first, cancels previous fetch
    fun fetchServices() {
        // Cancel previous fetch if still running
        fetchJob?.cancel()

        screenState = ScreenState.Loading
        fetchJob = scope.launch {
            try {
            val available = ContainerSystemdManager.isSystemdAvailable(containerName)
            if (!available) {
                screenState = ScreenState.SystemdNotAvailable
                return@launch
            }

            val services = ContainerSystemdManager.getAllServices(containerName)
            screenState = ScreenState.Ready(services)
            } catch (e: kotlinx.coroutines.CancellationException) {
                // Ignore cancellation
                throw e
            } catch (e: Exception) {
                // On error, keep current state or show error
                if (screenState is ScreenState.Loading) {
                    screenState = ScreenState.SystemdNotAvailable
                }
            }
        }
    }

    // Execute action with proper state management - prevents concurrent actions
    fun executeAction(
        serviceName: String,
        actionName: String,
        action: suspend () -> ContainerSystemdManager.CommandResult
    ) {
        // Cancel any ongoing action
        actionJob?.cancel()

        // Cancel any ongoing fetch
        fetchJob?.cancel()

        // Show action dialog immediately
        actionState = ActionState.InProgress(serviceName, actionName)

        actionJob = scope.launch {
            try {
            val result = action()

            // Action complete - dismiss dialog
            actionState = ActionState.Idle

            when {
                result.isSuccess -> {
                    // Immediately show loading screen
                    screenState = ScreenState.Loading

                    // Show snackbar in background (non-blocking)
                    scope.showSuccess(snackbarHostState, context.getString(R.string.action_successful, actionName, serviceName))

                        // Fetch fresh data (cancel any previous fetch first)
                        fetchJob?.cancel()
                        fetchJob = scope.launch {
                            try {
                    val services = ContainerSystemdManager.getAllServices(containerName)
                    screenState = ScreenState.Ready(services)
                            } catch (e: kotlinx.coroutines.CancellationException) {
                                throw e
                            } catch (e: Exception) {
                                // On error, keep loading state
                            }
                        }
                }
                else -> {
                    // Failed - show logs dialog if there's output, otherwise snackbar
                    val allLogs = result.output + result.error
                    if (allLogs.isNotEmpty()) {
                        logsDialogContent = allLogs
                    } else {
                        scope.showError(snackbarHostState, context.getString(R.string.failed_to_action, actionName, serviceName))
                }
                }
                }
            } catch (e: kotlinx.coroutines.CancellationException) {
                // Action was cancelled - reset state
                actionState = ActionState.Idle
                throw e
            } catch (e: Exception) {
                // On error, reset action state
                actionState = ActionState.Idle
                scope.showError(snackbarHostState, context.getString(R.string.error_unknown, e.message ?: context.getString(R.string.unknown)))
            }
        }
    }

    // Initial fetch
    LaunchedEffect(containerName) {
        fetchServices()
    }

    // Cleanup on dispose
    DisposableEffect(Unit) {
        onDispose {
            fetchJob?.cancel()
            actionJob?.cancel()
        }
    }

    // Computed values from state
    val allServices = (screenState as? ScreenState.Ready)?.services ?: emptyList()
    val filteredServices = remember(allServices, selectedFilter, searchQuery) {
        if (searchQuery.isBlank()) {
            // No search - apply filter normally
            ContainerSystemdManager.filterServices(allServices, selectedFilter)
        } else {
            // Search always searches from ALL services
            allServices.filter { it.name.contains(searchQuery, ignoreCase = true) }
        }
    }
    val serviceCounts = remember(allServices) {
        mapOf(
            ServiceFilter.RUNNING to allServices.count { it.isRunning && it.isEnabled && !it.isMasked },
            ServiceFilter.ENABLED to allServices.count { it.isEnabled && !it.isRunning && !it.isMasked },
            ServiceFilter.DISABLED to allServices.count { !it.isEnabled && !it.isRunning && !it.isMasked && !it.isStatic },
            ServiceFilter.ABNORMAL to allServices.count { it.isRunning && !it.isEnabled && !it.isStatic && !it.isMasked },
            ServiceFilter.STATIC to allServices.count { it.isStatic },
            ServiceFilter.MASKED to allServices.count { it.isMasked },
            ServiceFilter.ALL to allServices.size
        )
    }

    val clearFocus = rememberClearFocus()

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(context.getString(R.string.systemd_services))
                },
                navigationIcon = {
                    IconButton(
                        onClick = {
                            clearFocus()
                            onNavigateBack()
                        }
                    ) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                },
                actions = {
                    IconButton(
                        onClick = {
                            clearFocus()
                            fetchServices()
                        },
                        enabled = screenState !is ScreenState.Loading && actionState is ActionState.Idle
                    ) {
                        Icon(Icons.Default.Refresh, contentDescription = context.getString(R.string.refresh))
                    }
                }
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) }
    ) { padding ->
        when (screenState) {
            is ScreenState.Loading -> {
                FullScreenLoading(
                    message = context.getString(R.string.fetching_services),
                    modifier = Modifier.padding(padding)
                )
            }
            is ScreenState.SystemdNotAvailable -> {
                SystemdNotAvailable(modifier = Modifier.padding(padding))
            }
            is ScreenState.Ready -> {
                ClearFocusOnClickOutside(
                    modifier = Modifier.padding(padding)
                ) {
                    Column(
                        modifier = Modifier.fillMaxSize()
                ) {
                    //ColorLegend(
                    //    onTap = { clearFocus() }
                    //)

                    // Search bar
                    SearchBar(
                        query = searchQuery,
                        onQueryChange = { searchQuery = it }
                    )

                    FilterChipsRow(
                        selectedFilter = selectedFilter,
                        serviceCounts = serviceCounts,
                        onFilterSelected = {
                            selectedFilter = it
                            clearFocus()
                        }
                    )

                    if (filteredServices.isEmpty()) {
                        EmptyServicesState(
                            filter = selectedFilter,
                            modifier = Modifier
                                .weight(1f)
                                .clickable(
                                    indication = null,
                                    interactionSource = remember { MutableInteractionSource() }
                                ) {
                                    clearFocus()
                                }
                        )
                    } else {
                LazyColumn(
                            modifier = Modifier.weight(1f),
                            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                            items(filteredServices, key = { it.name }) { service ->
                                ServiceCard(
                                    service = service,
                                    containerName = containerName,
                                    clearFocus = clearFocus,
                                    onAction = { actionName, action ->
                                        clearFocus()
                                        executeAction(service.name, actionName, action)
                                    }
                                )
                            }
                            }
                        }
                    }
                }
            }
        }
    }

    // Action progress dialog
    (actionState as? ActionState.InProgress)?.let { state ->
        ProgressDialog(
            message = context.getString(R.string.actioning_service, state.actionName, state.serviceName)
        )
    }

    // Logs dialog
    logsDialogContent?.let { logs ->
        ErrorLogsDialog(
            logs = logs,
            onDismiss = { logsDialogContent = null }
        )
    }
                        }


/*
@Composable
private fun ColorLegend(
    onTap: () -> Unit = {}
) {
    val context = LocalContext.current
    Surface(
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(
                indication = null,
                interactionSource = remember { MutableInteractionSource() }
            ) {
                onTap()
            }
    ) {
        Row(
            modifier = Modifier
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(20.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            LegendItem(ColorGreen, context.getString(R.string.running))
            LegendItem(ColorYellow, context.getString(R.string.enabled_legend))
            LegendItem(ColorRed, context.getString(R.string.disabled_legend))
            LegendItem(ColorAbnormal, context.getString(R.string.abnormal_legend))
            LegendItem(ColorStatic, context.getString(R.string.static_legend))
            LegendItem(ColorWhite, context.getString(R.string.masked_legend))
        }
    }
}
*/

@Composable
private fun SearchBar(
    query: String,
    onQueryChange: (String) -> Unit
) {
    val context = LocalContext.current
    OutlinedTextField(
        value = query,
        onValueChange = onQueryChange,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp),
        placeholder = {
            Text(
                context.getString(R.string.search_services),
                style = MaterialTheme.typography.bodyLarge
            )
        },
        leadingIcon = {
            Icon(
                Icons.Default.Search,
                contentDescription = context.getString(R.string.search),
                tint = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
        },
        trailingIcon = {
            if (query.isNotEmpty()) {
                IconButton(onClick = { onQueryChange("") }) {
                    Icon(
                        Icons.Default.Clear,
                        contentDescription = context.getString(R.string.clear),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        },
        singleLine = true,
        shape = RoundedCornerShape(16.dp),
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLow,
            focusedBorderColor = MaterialTheme.colorScheme.primary,
            unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.5f)
        ),
        textStyle = MaterialTheme.typography.bodyLarge,
        keyboardOptions = FocusUtils.searchKeyboardOptions,
        keyboardActions = FocusUtils.clearFocusKeyboardActions()
    )
}

@Composable
private fun LegendItem(color: Color, label: String) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .clip(CircleShape)
                .background(color)
        )
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun FilterChipsRow(
    selectedFilter: ServiceFilter,
    serviceCounts: Map<ServiceFilter, Int>,
    onFilterSelected: (ServiceFilter) -> Unit
) {
    val context = LocalContext.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        FilterChipItem(context.getString(R.string.running), serviceCounts[ServiceFilter.RUNNING] ?: 0, selectedFilter == ServiceFilter.RUNNING, ColorGreen) { onFilterSelected(ServiceFilter.RUNNING) }
        FilterChipItem(context.getString(R.string.enabled_legend), serviceCounts[ServiceFilter.ENABLED] ?: 0, selectedFilter == ServiceFilter.ENABLED, ColorYellow) { onFilterSelected(ServiceFilter.ENABLED) }
        FilterChipItem(context.getString(R.string.disabled_legend), serviceCounts[ServiceFilter.DISABLED] ?: 0, selectedFilter == ServiceFilter.DISABLED, ColorRed) { onFilterSelected(ServiceFilter.DISABLED) }
        FilterChipItem(context.getString(R.string.abnormal_legend), serviceCounts[ServiceFilter.ABNORMAL] ?: 0, selectedFilter == ServiceFilter.ABNORMAL, ColorAbnormal) { onFilterSelected(ServiceFilter.ABNORMAL) }
        FilterChipItem(context.getString(R.string.static_legend), serviceCounts[ServiceFilter.STATIC] ?: 0, selectedFilter == ServiceFilter.STATIC, ColorStatic) { onFilterSelected(ServiceFilter.STATIC) }
        FilterChipItem(context.getString(R.string.masked_legend), serviceCounts[ServiceFilter.MASKED] ?: 0, selectedFilter == ServiceFilter.MASKED, ColorWhite) { onFilterSelected(ServiceFilter.MASKED) }
        FilterChipItem(context.getString(R.string.all_legend), serviceCounts[ServiceFilter.ALL] ?: 0, selectedFilter == ServiceFilter.ALL, null) { onFilterSelected(ServiceFilter.ALL) }
    }
}

@Composable
private fun FilterChipItem(
    label: String,
    count: Int,
    selected: Boolean,
    dotColor: Color?,
    onClick: () -> Unit
) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        label = {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (dotColor != null) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(dotColor)
                    )
                }
                Text(
                    "$label ($count)",
                    style = MaterialTheme.typography.labelLarge
            )
        }
        },
        shape = RoundedCornerShape(12.dp)
    )
}

@Composable
private fun ServiceCard(
    service: ServiceInfo,
    containerName: String,
    clearFocus: () -> Unit,
    onAction: (String, suspend () -> ContainerSystemdManager.CommandResult) -> Unit
) {
    val context = LocalContext.current
    var showMenu by remember { mutableStateOf(false) }

    val statusDotColor = when (service.status) {
        ServiceStatus.ENABLED_RUNNING -> ColorGreen
        ServiceStatus.ENABLED_STOPPED -> ColorYellow
        ServiceStatus.DISABLED_STOPPED -> ColorRed
        ServiceStatus.STATIC -> ColorStatic
        ServiceStatus.ABNORMAL -> ColorAbnormal
        ServiceStatus.MASKED -> ColorWhite
    }

    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        elevation = CardDefaults.elevatedCardElevation(defaultElevation = 2.dp),
        colors = CardDefaults.elevatedCardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerLow
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Header row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(14.dp),
                    modifier = Modifier.weight(1f)
                ) {
                    // Status dot with glow effect
                    Box(
                        modifier = Modifier
                            .size(12.dp)
                            .clip(CircleShape)
                            .background(statusDotColor)
                    )
                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text(
                            text = service.name,
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        if (service.description.isNotEmpty()) {
                            Text(
                                text = service.description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        }
                    }
                }

                // Status badge
                Surface(
                    shape = RoundedCornerShape(10.dp),
                    color = when (service.status) {
                        ServiceStatus.ENABLED_RUNNING -> MaterialTheme.colorScheme.primaryContainer
                        ServiceStatus.ENABLED_STOPPED -> MaterialTheme.colorScheme.tertiaryContainer
                        ServiceStatus.STATIC -> MaterialTheme.colorScheme.secondaryContainer
                        ServiceStatus.ABNORMAL -> MaterialTheme.colorScheme.errorContainer
                        ServiceStatus.DISABLED_STOPPED -> MaterialTheme.colorScheme.surfaceContainerHighest
                        ServiceStatus.MASKED -> MaterialTheme.colorScheme.surfaceContainerHighest
                    }
                ) {
                    Text(
                        text = when (service.status) {
                            ServiceStatus.ENABLED_RUNNING -> context.getString(R.string.running)
                            ServiceStatus.ENABLED_STOPPED -> context.getString(R.string.enabled_legend)
                            ServiceStatus.STATIC -> context.getString(R.string.static_legend)
                            ServiceStatus.ABNORMAL -> context.getString(R.string.abnormal_legend)
                            ServiceStatus.DISABLED_STOPPED -> context.getString(R.string.disabled_legend)
                            ServiceStatus.MASKED -> context.getString(R.string.masked_legend)
                        },
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.Medium,
                        color = when (service.status) {
                            ServiceStatus.ENABLED_RUNNING -> MaterialTheme.colorScheme.onPrimaryContainer
                            ServiceStatus.ENABLED_STOPPED -> MaterialTheme.colorScheme.onTertiaryContainer
                            ServiceStatus.STATIC -> MaterialTheme.colorScheme.onSecondaryContainer
                            ServiceStatus.ABNORMAL -> MaterialTheme.colorScheme.onErrorContainer
                            else -> MaterialTheme.colorScheme.onSurfaceVariant
                        }
                    )
                }
            }

            HorizontalDivider(
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)
            )

            // Action buttons row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                    if (service.isMasked) {
                    FilledTonalButton(
                        onClick = {
                            clearFocus()
                            onAction(context.getString(R.string.unmask)) { ContainerSystemdManager.unmaskService(containerName, service.name) }
                        },
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Icon(Icons.Default.LockOpen, null, Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(context.getString(R.string.unmask), style = MaterialTheme.typography.labelLarge)
                    }
                } else {
                    // Start/Stop button
                    if (service.isRunning) {
                        Button(
                            onClick = {
                                clearFocus()
                                onAction(context.getString(R.string.stop)) { ContainerSystemdManager.stopService(containerName, service.name) }
                            },
                            modifier = Modifier.weight(1f),
                            shape = RoundedCornerShape(12.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.error
                            )
                        ) {
                            Icon(Icons.Default.Stop, null, Modifier.size(18.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(context.getString(R.string.stop), style = MaterialTheme.typography.labelLarge)
                        }
                    } else {
                        Button(
                            onClick = {
                                clearFocus()
                                onAction(context.getString(R.string.start)) { ContainerSystemdManager.startService(containerName, service.name) }
                            },
                            modifier = Modifier.weight(1f),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Icon(Icons.Default.PlayArrow, null, Modifier.size(18.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(context.getString(R.string.start), style = MaterialTheme.typography.labelLarge)
                        }
                    }

                    // Enable/Disable button - Hidden for static services
                    if (!service.isStatic) {
                        if (service.isEnabled) {
                            OutlinedButton(
                                onClick = {
                                    clearFocus()
                                    onAction(context.getString(R.string.disable_service)) { ContainerSystemdManager.disableService(containerName, service.name) }
                                },
                                modifier = Modifier.weight(1f),
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Icon(Icons.Default.Block, null, Modifier.size(18.dp))
                                Spacer(Modifier.width(6.dp))
                                Text(context.getString(R.string.disable_service), style = MaterialTheme.typography.labelLarge)
                            }
                        } else {
                            OutlinedButton(
                                onClick = {
                                    clearFocus()
                                    onAction(context.getString(R.string.enable_service)) { ContainerSystemdManager.enableService(containerName, service.name) }
                                },
                                modifier = Modifier.weight(1f),
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Icon(Icons.Default.CheckCircle, null, Modifier.size(18.dp))
                                Spacer(Modifier.width(6.dp))
                                Text(context.getString(R.string.enable_service), style = MaterialTheme.typography.labelLarge)
                            }
                        }
                    }

                    // 3-dot menu for all non-masked services
                    if (!service.isMasked) {
                        Box {
                            IconButton(
                                onClick = {
                                    clearFocus()
                                    showMenu = true
                                }
                            ) {
                                Icon(
                                    Icons.Default.MoreVert,
                                    contentDescription = context.getString(R.string.more_options),
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            DropdownMenu(
                                expanded = showMenu,
                                onDismissRequest = { showMenu = false }
                            ) {
                                // Restart option for running services
                                if (service.isRunning) {
                                    DropdownMenuItem(
                                        text = {
                                            Text(
                                                context.getString(R.string.restart_service),
                                                style = MaterialTheme.typography.bodyLarge
                                            )
                                        },
                                        onClick = {
                                            clearFocus()
                                            showMenu = false
                                            onAction(context.getString(R.string.restart_service)) { ContainerSystemdManager.restartService(containerName, service.name) }
                                        },
                                        leadingIcon = {
                                            Icon(
                                                Icons.Default.Refresh,
                                                contentDescription = null,
                                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    )
                                }

                                // Mask option for all non-masked services
                                if (!service.isMasked) {
                                    DropdownMenuItem(
                                        text = {
                                            Text(
                                                context.getString(R.string.mask_service),
                                                style = MaterialTheme.typography.bodyLarge
                                            )
                                        },
                                        onClick = {
                                            clearFocus()
                                            showMenu = false
                                            onAction(context.getString(R.string.mask_service)) { ContainerSystemdManager.maskService(containerName, service.name) }
                                        },
                                        leadingIcon = {
                                            Icon(
                                                Icons.Default.Lock,
                                                contentDescription = null,
                                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}


@Composable
private fun SystemdNotAvailable(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
            modifier = Modifier.padding(32.dp)
        ) {
            Surface(
                shape = CircleShape,
                color = MaterialTheme.colorScheme.errorContainer,
                modifier = Modifier.size(80.dp)
            ) {
                Box(contentAlignment = Alignment.Center, modifier = Modifier.fillMaxSize()) {
                    Icon(
                        Icons.Default.Warning,
                        contentDescription = null,
                        modifier = Modifier.size(40.dp),
                        tint = MaterialTheme.colorScheme.onErrorContainer
                    )
                }
            }
            Text(
                text = context.getString(R.string.systemd_not_available),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center
            )
            Text(
                text = context.getString(R.string.systemd_not_available_message),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Composable
private fun EmptyServicesState(filter: ServiceFilter, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp),
            modifier = Modifier.padding(32.dp)
        ) {
            Surface(
                shape = CircleShape,
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                modifier = Modifier.size(72.dp)
            ) {
                Box(contentAlignment = Alignment.Center, modifier = Modifier.fillMaxSize()) {
                    Icon(
                        Icons.AutoMirrored.Filled.List,
                        contentDescription = null,
                        modifier = Modifier.size(36.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
}
            }
            Text(
                text = context.getString(R.string.no_services_found),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Medium,
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center
            )
            Text(
                text = when (filter) {
                    ServiceFilter.ALL -> context.getString(R.string.no_systemd_services)
                    ServiceFilter.RUNNING -> context.getString(R.string.no_running_services)
                    ServiceFilter.ENABLED -> context.getString(R.string.no_enabled_services)
                    ServiceFilter.DISABLED -> context.getString(R.string.no_disabled_services)
                    ServiceFilter.STATIC -> context.getString(R.string.no_static_services)
                    ServiceFilter.ABNORMAL -> context.getString(R.string.no_abnormal_services)
                    ServiceFilter.MASKED -> context.getString(R.string.no_masked_services)
                },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
        }
    }
}
