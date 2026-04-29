package com.droidspaces.app.ui.screen

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.*
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.*
import com.droidspaces.app.util.AnimationUtils
import com.droidspaces.app.util.ContainerSystemdManager
import com.droidspaces.app.util.ContainerSystemdManager.ServiceFilter
import com.droidspaces.app.util.ContainerSystemdManager.ServiceInfo
import com.droidspaces.app.util.ContainerSystemdManager.ServiceStatus
import kotlinx.coroutines.launch

// Premium Font Stack
private val JetBrainsMono = FontFamily(
    Font(R.font.jetbrains_mono_regular, FontWeight.Normal),
    Font(R.font.jetbrains_mono_bold, FontWeight.Bold)
)

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

    LaunchedEffect(Unit) { ContainerSystemdManager.initialize(context) }

    var screenState by remember { mutableStateOf<ScreenState>(ScreenState.Loading) }
    var actionState by remember { mutableStateOf<ActionState>(ActionState.Idle) }
    var selectedFilter by remember { mutableStateOf(ServiceFilter.RUNNING) }
    var logsDialogContent by remember { mutableStateOf<List<String>?>(null) }
    var searchQuery by remember { mutableStateOf("") }

    var fetchJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }
    var actionJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }

    fun fetchServices() {
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
            } catch (e: Exception) {
                if (e is kotlinx.coroutines.CancellationException) throw e
                screenState = ScreenState.SystemdNotAvailable
            }
        }
    }

    fun executeAction(serviceName: String, actionName: String, action: suspend () -> ContainerSystemdManager.CommandResult) {
        actionJob?.cancel()
        fetchJob?.cancel()
        actionState = ActionState.InProgress(serviceName, actionName)
        actionJob = scope.launch {
            try {
                val result = action()
                actionState = ActionState.Idle
                if (result.isSuccess) {
                    screenState = ScreenState.Loading
                    scope.showSuccess(snackbarHostState, context.getString(R.string.action_successful, actionName, serviceName))
                    fetchServices()
                } else {
                    val allLogs = result.output + result.error
                    if (allLogs.isNotEmpty()) logsDialogContent = allLogs
                    else scope.showError(snackbarHostState, context.getString(R.string.failed_to_action, actionName, serviceName))
                }
            } catch (e: Exception) {
                actionState = ActionState.Idle
                if (e is kotlinx.coroutines.CancellationException) throw e
                scope.showError(snackbarHostState, context.getString(R.string.error_unknown, e.message ?: context.getString(R.string.unknown)))
            }
        }
    }

    LaunchedEffect(containerName) { fetchServices() }

    val allServices = (screenState as? ScreenState.Ready)?.services ?: emptyList()
    val filteredServices = remember(allServices, selectedFilter, searchQuery) {
        if (searchQuery.isBlank()) ContainerSystemdManager.filterServices(allServices, selectedFilter)
        else allServices.filter { it.name.contains(searchQuery, ignoreCase = true) }
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

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Scaffold(
            topBar = {
                CenterAlignedTopAppBar(
                    title = { Text(context.getString(R.string.systemd_services), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold) },
                    navigationIcon = { IconButton(onClick = onNavigateBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, context.getString(R.string.back)) } },
                    actions = { IconButton(onClick = { fetchServices() }, enabled = screenState !is ScreenState.Loading && actionState is ActionState.Idle) { Icon(Icons.Default.Refresh, context.getString(R.string.refresh)) } },
                    colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = Color.Transparent, scrolledContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.95f))
                )
            },
            snackbarHost = { SnackbarHost(snackbarHostState) },
            containerColor = Color.Transparent
        ) { padding ->
            ClearFocusOnClickOutside(modifier = Modifier.padding(padding).fillMaxSize()) {
                Box(modifier = Modifier.fillMaxSize()) {
                    when (screenState) {
                        is ScreenState.Loading -> FullScreenLoading(message = context.getString(R.string.fetching_services))
                        is ScreenState.SystemdNotAvailable -> SystemdNotAvailable()
                        is ScreenState.Ready -> {
                            Column(modifier = Modifier.fillMaxSize()) {
                                SearchBar(query = searchQuery, onQueryChange = { searchQuery = it })
                                FilterChipsRow(selectedFilter = selectedFilter, serviceCounts = serviceCounts, onFilterSelected = { selectedFilter = it; clearFocus() })
                                if (filteredServices.isEmpty()) {
                                    EmptyServicesState(filter = selectedFilter, modifier = Modifier.weight(1f))
                                } else {
                                    LazyColumn(modifier = Modifier.weight(1f), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                                        items(filteredServices, key = { it.name }) { service ->
                                            ServiceCard(service = service, containerName = containerName, onAction = { name, act -> executeAction(service.name, name, act) })
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    (actionState as? ActionState.InProgress)?.let { state -> ProgressDialog(message = context.getString(R.string.actioning_service, state.actionName, state.serviceName)) }
    logsDialogContent?.let { logs -> ErrorLogsDialog(logs = logs, onDismiss = { logsDialogContent = null }) }
}

@Composable
private fun SearchBar(query: String, onQueryChange: (String) -> Unit) {
    val context = LocalContext.current
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    val borderColor by animateColorAsState(
        targetValue = if (isFocused) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f),
        animationSpec = AnimationUtils.fastSpec()
    )

    Surface(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, borderColor)
    ) {
        TextField(
            value = query,
            onValueChange = onQueryChange,
            modifier = Modifier.fillMaxWidth(),
            interactionSource = interactionSource,
            placeholder = { Text(context.getString(R.string.search_services), color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)) },
            leadingIcon = { Icon(Icons.Default.Search, null, tint = if (isFocused) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)) },
            trailingIcon = { if (query.isNotEmpty()) { IconButton(onClick = { onQueryChange("") }) { Icon(Icons.Default.Clear, null) } } },
            colors = TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent,
                unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent,
                cursorColor = MaterialTheme.colorScheme.primary
            ),
            singleLine = true,
            textStyle = MaterialTheme.typography.bodyLarge,
            keyboardOptions = FocusUtils.searchKeyboardOptions,
            keyboardActions = FocusUtils.clearFocusKeyboardActions()
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
    val filters = listOf(
        Triple(ServiceFilter.RUNNING, R.string.running, Color(0xFF4CAF50)),
        Triple(ServiceFilter.ENABLED, R.string.enabled_legend, Color(0xFFFFCA28)),
        Triple(ServiceFilter.DISABLED, R.string.disabled_legend, Color(0xFFEF5350)),
        Triple(ServiceFilter.ABNORMAL, R.string.abnormal_legend, Color(0xFFFF7043)),
        Triple(ServiceFilter.STATIC, R.string.static_legend, Color(0xFF607D8B)),
        Triple(ServiceFilter.MASKED, R.string.masked_legend, MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)),
        Triple(ServiceFilter.ALL, R.string.all_legend, null)
    )

    Row(
        modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        filters.forEach { (filter, labelRes, dotColor) ->
            val count = serviceCounts[filter] ?: 0
            val isSelected = selectedFilter == filter
            
            FilterChip(
                selected = isSelected,
                onClick = { onFilterSelected(filter) },
                label = {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        if (dotColor != null) {
                            Surface(modifier = Modifier.size(6.dp), shape = CircleShape, color = dotColor) {}
                        }
                        Text("${context.getString(labelRes)} ($count)", style = MaterialTheme.typography.labelLarge)
                    }
                },
                shape = RoundedCornerShape(12.dp),
                colors = FilterChipDefaults.filterChipColors(selectedContainerColor = MaterialTheme.colorScheme.primaryContainer),
                border = FilterChipDefaults.filterChipBorder(selected = isSelected, enabled = true, 
                    borderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f), 
                    selectedBorderColor = MaterialTheme.colorScheme.primary)
            )
        }
    }
}

@Composable
private fun ServiceCard(
    service: ServiceInfo,
    containerName: String,
    onAction: (String, suspend () -> ContainerSystemdManager.CommandResult) -> Unit
) {
    val context = LocalContext.current
    var showMenu by remember { mutableStateOf(false) }

    val statusColor = when (service.status) {
        ServiceStatus.ENABLED_RUNNING -> Color(0xFF4CAF50)
        ServiceStatus.ENABLED_STOPPED -> Color(0xFFFFCA28)
        ServiceStatus.DISABLED_STOPPED -> Color(0xFFEF5350)
        ServiceStatus.STATIC -> Color(0xFF607D8B)
        ServiceStatus.ABNORMAL -> Color(0xFFFF7043)
        ServiceStatus.MASKED -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
    }

    Surface(
        modifier = Modifier.fillMaxWidth().animateContentSize(AnimationUtils.mediumSpec()),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
    ) {
        Column {
            // Premium Header Row (Symmetric) - Fixed Height 32dp + Padding
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp).height(32.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = service.name,
                    style = MaterialTheme.typography.titleMedium.copy(fontFamily = JetBrainsMono),
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )

                Surface(
                    color = statusColor.copy(alpha = 0.1f),
                    shape = RoundedCornerShape(8.dp),
                    border = BorderStroke(1.dp, statusColor.copy(alpha = 0.2f))
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Surface(modifier = Modifier.size(6.dp), shape = CircleShape, color = statusColor) {}
                        Text(
                            text = when (service.status) {
                                ServiceStatus.ENABLED_RUNNING -> context.getString(R.string.running)
                                ServiceStatus.ENABLED_STOPPED -> context.getString(R.string.enabled_legend)
                                ServiceStatus.STATIC -> context.getString(R.string.static_legend)
                                ServiceStatus.ABNORMAL -> context.getString(R.string.abnormal_legend)
                                ServiceStatus.DISABLED_STOPPED -> context.getString(R.string.disabled_legend)
                                ServiceStatus.MASKED -> context.getString(R.string.masked_legend)
                            }.uppercase(),
                            style = MaterialTheme.typography.labelSmall,
                            fontWeight = FontWeight.Black,
                            letterSpacing = 0.5.sp,
                            color = statusColor
                        )
                    }
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                if (service.description.isNotEmpty()) {
                    Text(
                        text = service.description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                    )
                }

                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    shape = RoundedCornerShape(12.dp),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                ) {
                    Row(modifier = Modifier.fillMaxWidth().padding(4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        if (service.isMasked) {
                            Surface(onClick = { onAction(context.getString(R.string.unmask)) { ContainerSystemdManager.unmaskService(containerName, service.name) } }, modifier = Modifier.weight(1f).height(48.dp), shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f), border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.2f))) {
                                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) { Icon(Icons.Default.LockOpen, null, Modifier.size(18.dp), tint = MaterialTheme.colorScheme.primary); Text(context.getString(R.string.unmask), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary) }
                                }
                            }
                        } else {
                            val btnColor = if (service.isRunning) MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f) else MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f)
                            val accentColor = if (service.isRunning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                            Surface(onClick = { if (service.isRunning) onAction(context.getString(R.string.stop)) { ContainerSystemdManager.stopService(containerName, service.name) } else onAction(context.getString(R.string.start)) { ContainerSystemdManager.startService(containerName, service.name) } }, modifier = Modifier.weight(1f).height(48.dp), shape = RoundedCornerShape(16.dp), color = btnColor, border = BorderStroke(1.dp, accentColor.copy(alpha = 0.2f))) {
                                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) { Icon(if (service.isRunning) Icons.Default.Stop else Icons.Default.PlayArrow, null, Modifier.size(18.dp), tint = accentColor); Text(if (service.isRunning) context.getString(R.string.stop) else context.getString(R.string.start), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = accentColor) } }
                            }
                            if (!service.isStatic) {
                                Surface(onClick = { if (service.isEnabled) onAction(context.getString(R.string.disable_service)) { ContainerSystemdManager.disableService(containerName, service.name) } else onAction(context.getString(R.string.enable_service)) { ContainerSystemdManager.enableService(containerName, service.name) } }, modifier = Modifier.weight(1f).height(48.dp), shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f), border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))) {
                                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) { Icon(if (service.isEnabled) Icons.Default.Block else Icons.Default.CheckCircle, null, Modifier.size(18.dp)); Text(if (service.isEnabled) context.getString(R.string.disable_service) else context.getString(R.string.enable_service), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold) } }
                                }
                            }
                            Box {
                                Surface(onClick = { showMenu = true }, modifier = Modifier.size(48.dp), shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f), border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))) { Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.MoreVert, null, tint = MaterialTheme.colorScheme.onSurfaceVariant) } }
                                DropdownMenu(expanded = showMenu, onDismissRequest = { showMenu = false }) {
                                    if (service.isRunning) { DropdownMenuItem(text = { Text(context.getString(R.string.restart_service)) }, leadingIcon = { Icon(Icons.Default.Refresh, null) }, onClick = { showMenu = false; onAction(context.getString(R.string.restart_service)) { ContainerSystemdManager.restartService(containerName, service.name) } }) }
                                    DropdownMenuItem(text = { Text(context.getString(R.string.mask_service)) }, leadingIcon = { Icon(Icons.Default.Lock, null) }, onClick = { showMenu = false; onAction(context.getString(R.string.mask_service)) { ContainerSystemdManager.maskService(containerName, service.name) } })
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
private fun SystemdNotAvailable() {
    val context = LocalContext.current
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(24.dp), modifier = Modifier.padding(32.dp)) {
            Surface(shape = CircleShape, color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.2f), modifier = Modifier.size(120.dp), border = BorderStroke(2.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.2f))) { Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.Warning, null, modifier = Modifier.size(56.dp), tint = MaterialTheme.colorScheme.error) } }
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(context.getString(R.string.systemd_not_available), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
                Text(context.getString(R.string.systemd_not_available_desc), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            }
        }
    }
}

@Composable
private fun EmptyServicesState(filter: ServiceFilter, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Icon(Icons.Default.SearchOff, null, modifier = Modifier.size(64.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f))
            Text(text = when (filter) {
                ServiceFilter.RUNNING -> context.getString(R.string.no_running_services)
                ServiceFilter.ENABLED -> context.getString(R.string.no_enabled_services)
                ServiceFilter.DISABLED -> context.getString(R.string.no_disabled_services)
                ServiceFilter.STATIC -> context.getString(R.string.no_static_services)
                ServiceFilter.ABNORMAL -> context.getString(R.string.no_abnormal_services)
                ServiceFilter.MASKED -> context.getString(R.string.no_masked_services)
                ServiceFilter.ALL -> context.getString(R.string.no_services_found)
            }, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
        }
    }
}
