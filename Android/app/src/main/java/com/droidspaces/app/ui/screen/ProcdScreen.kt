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
import com.droidspaces.app.util.ContainerProcdManager
import com.droidspaces.app.util.ContainerProcdManager.ServiceFilter
import com.droidspaces.app.util.ContainerProcdManager.ServiceInfo
import com.droidspaces.app.util.ContainerProcdManager.ServiceStatus
import kotlinx.coroutines.launch

private val JetBrainsMono = FontFamily(
    Font(R.font.jetbrains_mono_regular, FontWeight.Normal),
    Font(R.font.jetbrains_mono_bold, FontWeight.Bold)
)

private sealed class ProcdScreenState {
    data object Loading : ProcdScreenState()
    data object ProcdNotAvailable : ProcdScreenState()
    data class Ready(val services: List<ServiceInfo>) : ProcdScreenState()
}

private sealed class ProcdActionState {
    data object Idle : ProcdActionState()
    data class InProgress(val serviceName: String, val actionName: String) : ProcdActionState()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProcdScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(Unit) { ContainerProcdManager.initialize(context) }

    var screenState by remember { mutableStateOf<ProcdScreenState>(ProcdScreenState.Loading) }
    var actionState by remember { mutableStateOf<ProcdActionState>(ProcdActionState.Idle) }
    var selectedFilter by remember { mutableStateOf(ServiceFilter.RUNNING) }
    var logsDialogContent by remember { mutableStateOf<List<String>?>(null) }
    var searchQuery by remember { mutableStateOf("") }

    var fetchJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }
    var actionJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }

    fun fetchServices() {
        fetchJob?.cancel()
        screenState = ProcdScreenState.Loading
        fetchJob = scope.launch {
            try {
                val available = ContainerProcdManager.isProcdAvailable(containerName)
                if (!available) {
                    screenState = ProcdScreenState.ProcdNotAvailable
                    return@launch
                }
                val services = ContainerProcdManager.getAllServices(containerName)
                screenState = ProcdScreenState.Ready(services)
            } catch (e: Exception) {
                if (e is kotlinx.coroutines.CancellationException) throw e
                screenState = ProcdScreenState.ProcdNotAvailable
            }
        }
    }

    fun executeAction(serviceName: String, actionName: String, action: suspend () -> ContainerProcdManager.CommandResult) {
        actionJob?.cancel()
        fetchJob?.cancel()
        actionState = ProcdActionState.InProgress(serviceName, actionName)
        actionJob = scope.launch {
            try {
                val result = action()
                actionState = ProcdActionState.Idle
                if (result.isSuccess) {
                    screenState = ProcdScreenState.Loading
                    scope.showSuccess(snackbarHostState, context.getString(R.string.action_successful, actionName, serviceName))
                    fetchServices()
                } else {
                    val allLogs = result.output + result.error
                    if (allLogs.isNotEmpty()) logsDialogContent = allLogs
                    else scope.showError(snackbarHostState, context.getString(R.string.failed_to_action, actionName, serviceName))
                }
            } catch (e: Exception) {
                actionState = ProcdActionState.Idle
                if (e is kotlinx.coroutines.CancellationException) throw e
                scope.showError(snackbarHostState, context.getString(R.string.error_unknown, e.message ?: context.getString(R.string.unknown)))
            }
        }
    }

    LaunchedEffect(containerName) { fetchServices() }

    val allServices = (screenState as? ProcdScreenState.Ready)?.services ?: emptyList()
    val filteredServices = remember(allServices, selectedFilter, searchQuery) {
        if (searchQuery.isBlank()) ContainerProcdManager.filterServices(allServices, selectedFilter)
        else allServices.filter { it.name.contains(searchQuery, ignoreCase = true) }
    }

    val serviceCounts = remember(allServices) {
        mapOf(
            ServiceFilter.RUNNING to allServices.count { it.isRunning && it.isEnabled },
            ServiceFilter.ENABLED to allServices.count { it.isEnabled && !it.isRunning && it.status != ServiceStatus.UNKNOWN },
            ServiceFilter.DISABLED to allServices.count { !it.isEnabled && !it.isRunning && it.status != ServiceStatus.UNKNOWN },
            ServiceFilter.ABNORMAL to allServices.count { it.isRunning && !it.isEnabled },
            ServiceFilter.UNKNOWN to allServices.count { it.status == ServiceStatus.UNKNOWN },
            ServiceFilter.ALL to allServices.size
        )
    }

    val clearFocus = rememberClearFocus()

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Scaffold(
            topBar = {
                CenterAlignedTopAppBar(
                    title = { Text(context.getString(R.string.openwrt_services), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold) },
                    navigationIcon = { IconButton(onClick = onNavigateBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, context.getString(R.string.back)) } },
                    actions = { IconButton(onClick = { fetchServices() }, enabled = screenState !is ProcdScreenState.Loading && actionState is ProcdActionState.Idle) { Icon(Icons.Default.Refresh, context.getString(R.string.refresh)) } },
                    colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = Color.Transparent, scrolledContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.95f))
                )
            },
            snackbarHost = { SnackbarHost(snackbarHostState) },
            containerColor = Color.Transparent
        ) { padding ->
            ClearFocusOnClickOutside(modifier = Modifier.padding(padding).fillMaxSize()) {
                Box(modifier = Modifier.fillMaxSize()) {
                    when (screenState) {
                        is ProcdScreenState.Loading -> FullScreenLoading(message = context.getString(R.string.fetching_services))
                        is ProcdScreenState.ProcdNotAvailable -> ProcdNotAvailable()
                        is ProcdScreenState.Ready -> {
                            Column(modifier = Modifier.fillMaxSize()) {
                                ProcdSearchBar(query = searchQuery, onQueryChange = { searchQuery = it })
                                ProcdFilterChipsRow(selectedFilter = selectedFilter, serviceCounts = serviceCounts, onFilterSelected = { selectedFilter = it; clearFocus() })
                                if (filteredServices.isEmpty()) {
                                    ProcdEmptyServicesState(filter = selectedFilter, modifier = Modifier.weight(1f))
                                } else {
                                    LazyColumn(modifier = Modifier.weight(1f), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                                        items(filteredServices) { service ->
                                            ProcdServiceCard(service = service, containerName = containerName, onAction = { name, act -> executeAction(service.name, name, act) })
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

    (actionState as? ProcdActionState.InProgress)?.let { state -> ProgressDialog(message = context.getString(R.string.actioning_service, state.actionName, state.serviceName)) }
    logsDialogContent?.let { logs -> ErrorLogsDialog(logs = logs, onDismiss = { logsDialogContent = null }) }
}

@Composable
private fun ProcdSearchBar(query: String, onQueryChange: (String) -> Unit) {
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
private fun ProcdFilterChipsRow(
    selectedFilter: ServiceFilter,
    serviceCounts: Map<ServiceFilter, Int>,
    onFilterSelected: (ServiceFilter) -> Unit
) {
    val context = LocalContext.current
    // procd has no masked/static concepts; UNKNOWN covers scripts without reliable status.
    val filters = listOf(
        Triple(ServiceFilter.RUNNING, R.string.running, Color(0xFF4CAF50)),
        Triple(ServiceFilter.ENABLED, R.string.enabled_legend, Color(0xFFFFCA28)),
        Triple(ServiceFilter.DISABLED, R.string.disabled_legend, Color(0xFFEF5350)),
        Triple(ServiceFilter.ABNORMAL, R.string.abnormal_legend, Color(0xFFFF7043)),
        Triple(ServiceFilter.UNKNOWN, R.string.unknown_legend, Color(0xFF90A4AE)),
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
private fun ProcdServiceCard(
    service: ServiceInfo,
    containerName: String,
    onAction: (String, suspend () -> ContainerProcdManager.CommandResult) -> Unit
) {
    val context = LocalContext.current
    var showMenu by remember { mutableStateOf(false) }

    val statusColor = when (service.status) {
        ServiceStatus.ENABLED_RUNNING -> Color(0xFF4CAF50)
        ServiceStatus.ENABLED_STOPPED -> Color(0xFFFFCA28)
        ServiceStatus.DISABLED_STOPPED -> Color(0xFFEF5350)
        ServiceStatus.ABNORMAL -> Color(0xFFFF7043)
        ServiceStatus.UNKNOWN -> Color(0xFF90A4AE)
    }

    Surface(
        modifier = Modifier.fillMaxWidth().animateContentSize(AnimationUtils.mediumSpec()),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp).height(32.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = service.name,
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontFamily = JetBrainsMono,
                        fontSize = if (service.name.length > 25) 13.sp else 16.sp
                    ),
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
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
                                ServiceStatus.DISABLED_STOPPED -> context.getString(R.string.disabled_legend)
                                ServiceStatus.ABNORMAL -> context.getString(R.string.abnormal_legend)
                                ServiceStatus.UNKNOWN -> context.getString(R.string.unknown_legend)
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
                        val btnColor = if (service.isRunning) MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f) else MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f)
                        val accentColor = if (service.isRunning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary

                        // Start / Stop
                        Surface(
                            onClick = {
                                if (service.isRunning) onAction(context.getString(R.string.stop)) { ContainerProcdManager.stopService(containerName, service.name) }
                                else onAction(context.getString(R.string.start)) { ContainerProcdManager.startService(containerName, service.name) }
                            },
                            modifier = Modifier.weight(1f).height(48.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = btnColor,
                            border = BorderStroke(1.dp, accentColor.copy(alpha = 0.2f))
                        ) {
                            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    Icon(if (service.isRunning) Icons.Default.Stop else Icons.Default.PlayArrow, null, Modifier.size(18.dp), tint = accentColor)
                                    Text(if (service.isRunning) context.getString(R.string.stop) else context.getString(R.string.start), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = accentColor)
                                }
                            }
                        }

                        // Enable / Disable
                        Surface(
                            onClick = {
                                if (service.isEnabled) onAction(context.getString(R.string.disable_service)) { ContainerProcdManager.disableService(containerName, service.name) }
                                else onAction(context.getString(R.string.enable_service)) { ContainerProcdManager.enableService(containerName, service.name) }
                            },
                            modifier = Modifier.weight(1f).height(48.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                        ) {
                            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    Icon(if (service.isEnabled) Icons.Default.Block else Icons.Default.CheckCircle, null, Modifier.size(18.dp))
                                    Text(if (service.isEnabled) context.getString(R.string.disable_service) else context.getString(R.string.enable_service), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
                                }
                            }
                        }

                        // More (Restart / Reload)
                        Box {
                            Surface(
                                onClick = { showMenu = true },
                                modifier = Modifier.size(48.dp),
                                shape = RoundedCornerShape(16.dp),
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f),
                                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                            ) {
                                Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.MoreVert, null, tint = MaterialTheme.colorScheme.onSurfaceVariant) }
                            }
                            DropdownMenu(expanded = showMenu, onDismissRequest = { showMenu = false }) {
                                DropdownMenuItem(
                                    text = { Text(context.getString(R.string.restart_service)) },
                                    leadingIcon = { Icon(Icons.Default.Refresh, null) },
                                    onClick = { showMenu = false; onAction(context.getString(R.string.restart_service)) { ContainerProcdManager.restartService(containerName, service.name) } }
                                )
                                DropdownMenuItem(
                                    text = { Text(context.getString(R.string.reload_service)) },
                                    leadingIcon = { Icon(Icons.Default.Refresh, null) },
                                    onClick = { showMenu = false; onAction(context.getString(R.string.reload_service)) { ContainerProcdManager.reloadService(containerName, service.name) } }
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ProcdNotAvailable() {
    val context = LocalContext.current
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(24.dp), modifier = Modifier.padding(32.dp)) {
            Surface(shape = CircleShape, color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.2f), modifier = Modifier.size(120.dp), border = BorderStroke(2.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.2f))) {
                Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.Warning, null, modifier = Modifier.size(56.dp), tint = MaterialTheme.colorScheme.error) }
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(context.getString(R.string.init_system_not_available), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
                Text(context.getString(R.string.init_system_not_available_desc), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            }
        }
    }
}

@Composable
private fun ProcdEmptyServicesState(filter: ServiceFilter, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Icon(Icons.Default.SearchOff, null, modifier = Modifier.size(64.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f))
            Text(
                text = when (filter) {
                    ServiceFilter.RUNNING -> context.getString(R.string.no_running_services)
                    ServiceFilter.ENABLED -> context.getString(R.string.no_enabled_services)
                    ServiceFilter.DISABLED -> context.getString(R.string.no_disabled_services)
                    ServiceFilter.ABNORMAL -> context.getString(R.string.no_abnormal_services)
                    ServiceFilter.UNKNOWN -> context.getString(R.string.no_unknown_services)
                    ServiceFilter.ALL -> context.getString(R.string.no_services_found)
                },
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
        }
    }
}
