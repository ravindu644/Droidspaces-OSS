package com.droidspaces.app.ui.screen

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.border
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.LockOpen
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SearchOff
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.*
import com.droidspaces.app.util.AnimationUtils
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

// ── UI-level model ───────────────────────────────────────────────────────────
// Manager-agnostic view of an init-system service, so a single screen can drive
// systemd, OpenRC and procd. Each screen maps its manager's ServiceInfo -> this.

/** Superset of the per-init-system statuses (color + label are derived from it). */
enum class InitServiceUiStatus {
    ENABLED_RUNNING, ENABLED_STOPPED, DISABLED_STOPPED, ABNORMAL, STATIC, MASKED, UNKNOWN
}

/** Manager-agnostic command result. */
data class InitCommandResult(val isSuccess: Boolean, val output: List<String>, val error: List<String>)

/** An overflow-menu action (restart / mask / reload / …). */
data class InitServiceMenuAction(
    val labelRes: Int,
    val icon: ImageVector,
    val run: suspend () -> InitCommandResult,
)

/** One service row plus the actions applicable to it. */
data class InitServiceRow(
    val name: String,
    val description: String,
    val isRunning: Boolean,
    val isEnabled: Boolean,
    val isMasked: Boolean,
    val isStatic: Boolean,
    val status: InitServiceUiStatus,
    val startStop: suspend () -> InitCommandResult,
    val enableDisable: suspend () -> InitCommandResult,
    /** Non-null only when the service is masked (systemd). */
    val unmask: (suspend () -> InitCommandResult)?,
    val menu: List<InitServiceMenuAction>,
)

/**
 * A filter chip. [predicate] is used both to filter the list and to compute the
 * chip's count, so counts and filtering can never drift. [dotColor] null = "All".
 */
data class InitServiceFilterChip(
    val id: String,
    val labelRes: Int,
    val dotColor: Color?,
    val emptyRes: Int,
    val predicate: (InitServiceRow) -> Boolean,
)

private val JetBrainsMono = FontFamily(
    Font(R.font.jetbrains_mono_regular, FontWeight.Normal),
    Font(R.font.jetbrains_mono_bold, FontWeight.Bold)
)

private sealed class InitScreenState {
    data object Loading : InitScreenState()
    data object NotAvailable : InitScreenState()
    data class Ready(val rows: List<InitServiceRow>) : InitScreenState()
}

private sealed class InitActionState {
    data object Idle : InitActionState()
    data class InProgress(val serviceName: String, val actionName: String) : InitActionState()
}

/**
 * Generic init-system service management screen. Behaviour is identical across
 * systemd / OpenRC / procd; the differences (title, availability probe, service
 * fetch/mapping, and the filter set) are supplied by the caller.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun InitServiceScreen(
    containerName: String,
    titleRes: Int,
    onNavigateBack: () -> Unit,
    isAvailable: suspend (String) -> Boolean,
    fetchRows: suspend (String) -> List<InitServiceRow>,
    filters: List<InitServiceFilterChip>,
    defaultFilterId: String,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    var screenState by remember { mutableStateOf<InitScreenState>(InitScreenState.Loading) }
    var actionState by remember { mutableStateOf<InitActionState>(InitActionState.Idle) }
    var selectedFilterId by remember { mutableStateOf(defaultFilterId) }
    var logsDialogContent by remember { mutableStateOf<List<String>?>(null) }
    var searchQuery by remember { mutableStateOf("") }

    var fetchJob by remember { mutableStateOf<Job?>(null) }
    var actionJob by remember { mutableStateOf<Job?>(null) }

    fun fetchServices() {
        fetchJob?.cancel()
        screenState = InitScreenState.Loading
        fetchJob = scope.launch {
            try {
                if (!isAvailable(containerName)) {
                    screenState = InitScreenState.NotAvailable
                    return@launch
                }
                screenState = InitScreenState.Ready(fetchRows(containerName))
            } catch (e: Exception) {
                if (e is kotlinx.coroutines.CancellationException) throw e
                screenState = InitScreenState.NotAvailable
            }
        }
    }

    fun executeAction(serviceName: String, actionName: String, action: suspend () -> InitCommandResult) {
        actionJob?.cancel()
        fetchJob?.cancel()
        actionState = InitActionState.InProgress(serviceName, actionName)
        actionJob = scope.launch {
            try {
                val result = action()
                actionState = InitActionState.Idle
                if (result.isSuccess) {
                    screenState = InitScreenState.Loading
                    scope.showSuccess(snackbarHostState, context.getString(R.string.action_successful, actionName, serviceName))
                    fetchServices()
                } else {
                    val allLogs = result.output + result.error
                    if (allLogs.isNotEmpty()) logsDialogContent = allLogs
                    else scope.showError(snackbarHostState, context.getString(R.string.failed_to_action, actionName, serviceName))
                }
            } catch (e: Exception) {
                actionState = InitActionState.Idle
                if (e is kotlinx.coroutines.CancellationException) throw e
                scope.showError(snackbarHostState, context.getString(R.string.error_unknown, e.message ?: context.getString(R.string.unknown)))
            }
        }
    }

    LaunchedEffect(containerName) { fetchServices() }

    val allRows = (screenState as? InitScreenState.Ready)?.rows ?: emptyList()
    val selectedFilter = filters.firstOrNull { it.id == selectedFilterId } ?: filters.first()
    val counts = remember(allRows) {
        filters.associate { chip -> chip.id to allRows.count(chip.predicate) }
    }

    val clearFocus = rememberClearFocus()

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Scaffold(
            topBar = {
                CenterAlignedTopAppBar(
                    title = { Text(context.getString(titleRes), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold) },
                    navigationIcon = { IconButton(onClick = onNavigateBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, context.getString(R.string.back)) } },
                    actions = { IconButton(onClick = { fetchServices() }, enabled = screenState !is InitScreenState.Loading && actionState is InitActionState.Idle) { Icon(Icons.Default.Refresh, context.getString(R.string.refresh)) } },
                    colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = Color.Transparent, scrolledContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.95f))
                )
            },
            snackbarHost = { SnackbarHost(snackbarHostState) },
            containerColor = Color.Transparent
        ) { padding ->
            ClearFocusOnClickOutside(modifier = Modifier.padding(padding).fillMaxSize()) {
                Box(modifier = Modifier.fillMaxSize()) {
                    when (screenState) {
                        is InitScreenState.Loading -> FullScreenLoading(message = context.getString(R.string.fetching_services))
                        is InitScreenState.NotAvailable -> InitServiceNotAvailable()
                        is InitScreenState.Ready -> {
                            val pagerState = rememberPagerState(
                                initialPage = filters.indexOfFirst { it.id == selectedFilterId }.coerceAtLeast(0),
                                pageCount = { filters.size }
                            )
                            // Swiping between filter pages keeps the chip row selection in sync.
                            LaunchedEffect(pagerState.currentPage) {
                                selectedFilterId = filters[pagerState.currentPage].id
                            }
                            Column(modifier = Modifier.fillMaxSize()) {
                                InitServiceSearchBar(query = searchQuery, onQueryChange = { searchQuery = it })
                                InitServiceFilterChipsRow(
                                    filters = filters,
                                    counts = counts,
                                    selectedFilterId = selectedFilterId,
                                    onFilterSelected = { id ->
                                        clearFocus()
                                        val idx = filters.indexOfFirst { it.id == id }
                                        if (idx >= 0) scope.launch {
                                            // Animate only for adjacent hops; jump directly for distant
                                            // ones so we don't flip through every page in between.
                                            if (kotlin.math.abs(idx - pagerState.currentPage) <= 1) {
                                                pagerState.animateScrollToPage(idx)
                                            } else {
                                                pagerState.scrollToPage(idx)
                                            }
                                        }
                                    }
                                )
                                if (searchQuery.isNotBlank()) {
                                    // Search overrides the filter pages: one results list across all services.
                                    val results = allRows.filter { it.name.contains(searchQuery, ignoreCase = true) }
                                    if (results.isEmpty()) {
                                        InitServiceEmptyState(emptyRes = selectedFilter.emptyRes, modifier = Modifier.weight(1f))
                                    } else {
                                        LazyColumn(modifier = Modifier.weight(1f), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                                            items(results, key = { it.name }) { row ->
                                                InitServiceCard(row = row, onAction = { actionName, act -> executeAction(row.name, actionName, act) })
                                            }
                                        }
                                    }
                                } else {
                                    HorizontalPager(state = pagerState, modifier = Modifier.weight(1f)) { page ->
                                        val pageFilter = filters[page]
                                        val rows = allRows.filter(pageFilter.predicate)
                                            .sortedWith(compareByDescending<InitServiceRow> { it.isRunning }.thenBy { it.name })
                                        if (rows.isEmpty()) {
                                            InitServiceEmptyState(emptyRes = pageFilter.emptyRes, modifier = Modifier.fillMaxSize())
                                        } else {
                                            LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                                                items(rows, key = { it.name }) { row ->
                                                    InitServiceCard(row = row, onAction = { actionName, act -> executeAction(row.name, actionName, act) })
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
        }
    }

    (actionState as? InitActionState.InProgress)?.let { state -> ProgressDialog(message = context.getString(R.string.actioning_service, state.actionName, state.serviceName)) }
    logsDialogContent?.let { logs -> ErrorLogsDialog(logs = logs, onDismiss = { logsDialogContent = null }) }
}

@Composable
private fun InitServiceSearchBar(query: String, onQueryChange: (String) -> Unit) {
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
private fun InitServiceFilterChipsRow(
    filters: List<InitServiceFilterChip>,
    counts: Map<String, Int>,
    selectedFilterId: String,
    onFilterSelected: (String) -> Unit
) {
    val context = LocalContext.current
    val listState = rememberLazyListState()
    // Keep the highlighted chip on-screen when the filter changes (swipe or tap).
    LaunchedEffect(selectedFilterId) {
        val idx = filters.indexOfFirst { it.id == selectedFilterId }
        if (idx >= 0) listState.animateScrollToItem(idx)
    }
    LazyRow(
        state = listState,
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        items(filters) { chip ->
            val count = counts[chip.id] ?: 0
            val isSelected = selectedFilterId == chip.id
            FilterChip(
                selected = isSelected,
                onClick = { onFilterSelected(chip.id) },
                label = {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        if (chip.dotColor != null) {
                            Surface(modifier = Modifier.size(6.dp), shape = CircleShape, color = chip.dotColor) {}
                        }
                        Text("${context.getString(chip.labelRes)} ($count)", style = MaterialTheme.typography.labelLarge)
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
private fun statusColorFor(status: InitServiceUiStatus): Color = when (status) {
    InitServiceUiStatus.ENABLED_RUNNING -> Color(0xFF4CAF50)
    InitServiceUiStatus.ENABLED_STOPPED -> Color(0xFFFFCA28)
    InitServiceUiStatus.DISABLED_STOPPED -> Color(0xFFEF5350)
    InitServiceUiStatus.ABNORMAL -> Color(0xFFFF7043)
    InitServiceUiStatus.STATIC -> Color(0xFF607D8B)
    InitServiceUiStatus.MASKED -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
    InitServiceUiStatus.UNKNOWN -> Color(0xFF90A4AE)
}

private fun statusLabelRes(status: InitServiceUiStatus): Int = when (status) {
    InitServiceUiStatus.ENABLED_RUNNING -> R.string.running
    InitServiceUiStatus.ENABLED_STOPPED -> R.string.enabled_legend
    InitServiceUiStatus.DISABLED_STOPPED -> R.string.disabled_legend
    InitServiceUiStatus.ABNORMAL -> R.string.abnormal_legend
    InitServiceUiStatus.STATIC -> R.string.static_legend
    InitServiceUiStatus.MASKED -> R.string.masked_legend
    InitServiceUiStatus.UNKNOWN -> R.string.unknown_legend
}

@Composable
private fun InitServiceCard(
    row: InitServiceRow,
    onAction: (String, suspend () -> InitCommandResult) -> Unit
) {
    val context = LocalContext.current
    var showMenu by remember { mutableStateOf(false) }
    val statusColor = statusColorFor(row.status)

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
                    text = row.name,
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontFamily = JetBrainsMono,
                        fontSize = if (row.name.length > 25) 13.sp else 16.sp
                    ),
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
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
                            text = context.getString(statusLabelRes(row.status)).uppercase(),
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
                if (row.description.isNotEmpty()) {
                    Text(
                        text = row.description,
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
                        val unmask = row.unmask
                        if (row.isMasked && unmask != null) {
                            Surface(
                                onClick = { onAction(context.getString(R.string.unmask), unmask) },
                                modifier = Modifier.weight(1f).height(48.dp),
                                shape = RoundedCornerShape(16.dp),
                                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f),
                                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.2f))
                            ) {
                                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        Icon(Icons.Default.LockOpen, null, Modifier.size(18.dp), tint = MaterialTheme.colorScheme.primary)
                                        Text(context.getString(R.string.unmask), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary)
                                    }
                                }
                            }
                        } else {
                            val btnColor = if (row.isRunning) MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f) else MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f)
                            val accentColor = if (row.isRunning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                            Surface(
                                onClick = { onAction(if (row.isRunning) context.getString(R.string.stop) else context.getString(R.string.start), row.startStop) },
                                modifier = Modifier.weight(1f).height(48.dp),
                                shape = RoundedCornerShape(16.dp),
                                color = btnColor,
                                border = BorderStroke(1.dp, accentColor.copy(alpha = 0.2f))
                            ) {
                                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        Icon(if (row.isRunning) Icons.Default.Stop else Icons.Default.PlayArrow, null, Modifier.size(18.dp), tint = accentColor)
                                        Text(if (row.isRunning) context.getString(R.string.stop) else context.getString(R.string.start), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = accentColor)
                                    }
                                }
                            }
                            if (!row.isStatic) {
                                Surface(
                                    onClick = { onAction(if (row.isEnabled) context.getString(R.string.disable_service) else context.getString(R.string.enable_service), row.enableDisable) },
                                    modifier = Modifier.weight(1f).height(48.dp),
                                    shape = RoundedCornerShape(16.dp),
                                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f),
                                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                                ) {
                                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                            Icon(if (row.isEnabled) Icons.Default.Block else Icons.Default.CheckCircle, null, Modifier.size(18.dp))
                                            Text(if (row.isEnabled) context.getString(R.string.disable_service) else context.getString(R.string.enable_service), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
                                        }
                                    }
                                }
                            }
                            if (row.menu.isNotEmpty()) {
                                Box {
                                    Surface(onClick = { showMenu = true }, modifier = Modifier.size(48.dp), shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f), border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))) {
                                        Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.MoreVert, null, tint = MaterialTheme.colorScheme.onSurfaceVariant) }
                                    }
                                    // Dark-mode dropdown theming: force an opaque surface + rounded corners.
                                    val isDark = MaterialTheme.colorScheme.background.luminance() < 0.5f
                                    val dropdownColor = if (isDark) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer
                                    MaterialTheme(
                                        colorScheme = MaterialTheme.colorScheme.copy(
                                            surface = dropdownColor,
                                            surfaceContainer = dropdownColor,
                                            surfaceTint = Color.Transparent
                                        ),
                                        shapes = MaterialTheme.shapes.copy(extraSmall = RoundedCornerShape(20.dp))
                                    ) {
                                        DropdownMenu(
                                            expanded = showMenu,
                                            onDismissRequest = { showMenu = false },
                                            modifier = Modifier.border(width = 1.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f), shape = RoundedCornerShape(20.dp))
                                        ) {
                                            row.menu.forEach { item ->
                                                DropdownMenuItem(
                                                    text = { Text(context.getString(item.labelRes)) },
                                                    leadingIcon = { Icon(item.icon, null) },
                                                    onClick = { showMenu = false; onAction(context.getString(item.labelRes), item.run) }
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
        }
    }
}

@Composable
private fun InitServiceNotAvailable() {
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
private fun InitServiceEmptyState(emptyRes: Int, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Icon(Icons.Default.SearchOff, null, modifier = Modifier.size(64.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f))
            Text(text = context.getString(emptyRes), style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
        }
    }
}
