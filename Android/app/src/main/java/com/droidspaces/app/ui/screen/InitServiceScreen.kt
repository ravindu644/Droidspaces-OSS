package com.droidspaces.app.ui.screen

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.border
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
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.component.CardContentPadding
import com.droidspaces.app.ui.component.CardHeaderHeight
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.DsSnackbarHost
import com.droidspaces.app.ui.component.dsMenuBorder
import com.droidspaces.app.ui.component.DsMenuTheme
import com.droidspaces.app.ui.component.StatusPill
import com.droidspaces.app.ui.util.*
import com.droidspaces.app.util.AnimationUtils
import com.droidspaces.app.ui.viewmodel.InitScreenState
import com.droidspaces.app.ui.viewmodel.InitServiceViewModel
import com.droidspaces.app.util.CommandResult
import com.droidspaces.app.util.ServiceStatus
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import com.droidspaces.app.ui.component.DsSearchBar
import androidx.compose.material3.LocalContentColor

// One screen drives all four init systems: each maps its manager's ServiceInfo to a row
// whose actions are already bound to that manager.

/**
 * An overflow-menu action. [Command] fires a suspend action (restart / mask /
 * reload / …) through the shared executeAction pipeline (progress dialog +
 * snackbar + refetch). [Navigate] opens a dedicated screen (e.g. unit
 * inspection, override.conf editor) and does not touch the command pipeline.
 */
sealed class InitServiceMenuAction(val labelRes: Int, val icon: ImageVector) {
    class Command(
        labelRes: Int,
        icon: ImageVector,
        val run: suspend () -> CommandResult,
    ) : InitServiceMenuAction(labelRes, icon)

    class Navigate(
        labelRes: Int,
        icon: ImageVector,
        val onClick: () -> Unit,
    ) : InitServiceMenuAction(labelRes, icon)
}

/** One service row plus the actions applicable to it. */
data class InitServiceRow(
    val name: String,
    val description: String,
    val isRunning: Boolean,
    val isEnabled: Boolean,
    val isMasked: Boolean,
    val isStatic: Boolean,
    val status: ServiceStatus,
    val startStop: suspend () -> CommandResult,
    val enableDisable: suspend () -> CommandResult,
    /** Non-null only when the service is masked (systemd). */
    val unmask: (suspend () -> CommandResult)?,
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
    viewModel: InitServiceViewModel = viewModel(),
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    val screenState = viewModel.screenState
    val selectedFilterId = viewModel.selectedFilterId
    val searchQuery = viewModel.searchQuery
    var actionState by remember { mutableStateOf<InitActionState>(InitActionState.Idle) }
    var logsDialogContent by remember { mutableStateOf<List<String>?>(null) }

    var actionJob by remember { mutableStateOf<Job?>(null) }

    fun fetchServices() = viewModel.fetchServices(containerName, isAvailable, fetchRows)

    fun executeAction(serviceName: String, actionName: String, action: suspend () -> CommandResult) {
        actionJob?.cancel()
        viewModel.cancelFetch()
        actionState = InitActionState.InProgress(serviceName, actionName)
        actionJob = scope.launch {
            try {
                val result = action()
                actionState = InitActionState.Idle
                if (result.isSuccess) {
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

    LaunchedEffect(Unit) { viewModel.loadOnce(defaultFilterId, isAvailable, fetchRows, containerName) }

    val allRows = (screenState as? InitScreenState.Ready)?.rows ?: emptyList()
    val selectedFilter = filters.firstOrNull { it.id == selectedFilterId } ?: filters.first()
    val counts = remember(allRows) {
        filters.associate { chip -> chip.id to allRows.count(chip.predicate) }
    }

    val clearFocus = rememberClearFocus()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(context.getString(titleRes), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold) },
                navigationIcon = { IconButton(onClick = onNavigateBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, context.getString(R.string.back)) } },
                actions = { IconButton(onClick = { fetchServices() }, enabled = screenState !is InitScreenState.Loading && actionState is InitActionState.Idle) { Icon(Icons.Default.Refresh, context.getString(R.string.refresh)) } },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Transparent, scrolledContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.95f))
            )
        },
        snackbarHost = { DsSnackbarHost(snackbarHostState) }
    ) { padding ->
        Box(modifier = Modifier.padding(padding).fillMaxSize()) {
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
                        viewModel.selectedFilterId = filters[pagerState.currentPage].id
                    }
                    Column(modifier = Modifier.fillMaxSize()) {
                        DsSearchBar(searchQuery, { viewModel.searchQuery = it }, context.getString(R.string.search_services), Modifier.padding(horizontal = 16.dp, vertical = 8.dp), color = MaterialTheme.colorScheme.surfaceContainer)
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

    (actionState as? InitActionState.InProgress)?.let { state -> ProgressDialog(message = context.getString(R.string.actioning_service, state.actionName, state.serviceName)) }
    logsDialogContent?.let { logs -> ErrorLogsDialog(logs = logs, onDismiss = { logsDialogContent = null }) }
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

/**
 * The status hues. These are a deliberate traffic light rather than theme roles:
 * six of these states can sit in the legend row at once, and primary/tertiary
 * move with dynamic colour and the palette picker, so roles would collide.
 */
@Composable
fun statusColorFor(status: ServiceStatus): Color = when (status) {
    ServiceStatus.ENABLED_RUNNING -> Color(0xFF4CAF50)
    ServiceStatus.ENABLED_STOPPED -> Color(0xFFFFCA28)
    ServiceStatus.DISABLED_STOPPED -> Color(0xFFEF5350)
    ServiceStatus.ABNORMAL -> Color(0xFFFF7043)
    ServiceStatus.STATIC -> Color(0xFF607D8B)
    ServiceStatus.MASKED -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
    ServiceStatus.UNKNOWN -> Color(0xFF90A4AE)
}

private fun statusLabelRes(status: ServiceStatus): Int = when (status) {
    ServiceStatus.ENABLED_RUNNING -> R.string.running
    ServiceStatus.ENABLED_STOPPED -> R.string.enabled_legend
    ServiceStatus.DISABLED_STOPPED -> R.string.disabled_legend
    ServiceStatus.ABNORMAL -> R.string.abnormal_legend
    ServiceStatus.STATIC -> R.string.static_legend
    ServiceStatus.MASKED -> R.string.masked_legend
    ServiceStatus.UNKNOWN -> R.string.unknown_legend
}

/**
 * A 48dp action pill on a [container] fill. An [accent] tints the icon, label
 * and border; without one the pill is the neutral grey of a secondary action.
 */
@Composable
private fun PillButton(icon: ImageVector, label: String, accent: Color?, container: Color, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = modifier.height(48.dp),
        shape = RoundedCornerShape(16.dp),
        color = container,
        border = BorderStroke(1.dp, (accent ?: MaterialTheme.colorScheme.outlineVariant).copy(alpha = 0.2f))
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)
        ) {
            Icon(icon, null, Modifier.size(18.dp), tint = accent ?: LocalContentColor.current)
            Text(label, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = accent ?: Color.Unspecified)
        }
    }
}

@Composable
private fun InitServiceCard(
    row: InitServiceRow,
    onAction: (String, suspend () -> CommandResult) -> Unit
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
                modifier = Modifier.fillMaxWidth().padding(CardContentPadding).height(CardHeaderHeight),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = row.name,
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontFamily = JetBrainsMono
                    ),
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                StatusPill(
                    label = context.getString(statusLabelRes(row.status)).uppercase(),
                    color = statusColor
                )
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
                    // The wrapper sits tighter than the 16.dp buttons inside it on purpose.
                    // Matching the two radii flattens the nested look, do not "fix" it.
                    shape = RoundedCornerShape(12.dp),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                ) {
                    Row(modifier = Modifier.fillMaxWidth().padding(4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        val unmask = row.unmask
                        if (row.isMasked && unmask != null) {
                            val label = context.getString(R.string.unmask)
                            PillButton(Icons.Default.LockOpen, label, MaterialTheme.colorScheme.primary, MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f), Modifier.weight(1f)) { onAction(label, unmask) }
                        } else {
                            val label = context.getString(if (row.isRunning) R.string.stop else R.string.start)
                            PillButton(
                                if (row.isRunning) Icons.Default.Stop else Icons.Default.PlayArrow, label,
                                if (row.isRunning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                                (if (row.isRunning) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.primaryContainer).copy(alpha = 0.4f),
                                Modifier.weight(1f)
                            ) { onAction(label, row.startStop) }
                            if (!row.isStatic) {
                                val toggle = context.getString(if (row.isEnabled) R.string.disable_service else R.string.enable_service)
                                PillButton(if (row.isEnabled) Icons.Default.Block else Icons.Default.CheckCircle, toggle, null, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f), Modifier.weight(1f)) { onAction(toggle, row.enableDisable) }
                            }
                            if (row.menu.isNotEmpty()) {
                                Box {
                                    Surface(onClick = { showMenu = true }, modifier = Modifier.size(48.dp), shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f), border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))) {
                                        Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.MoreVert, null, tint = MaterialTheme.colorScheme.onSurfaceVariant) }
                                    }
                                    DsMenuTheme {
                                        DropdownMenu(
                                            expanded = showMenu,
                                            onDismissRequest = { showMenu = false },
                                            modifier = Modifier.dsMenuBorder()
                                        ) {
                                            row.menu.forEach { item ->
                                                DropdownMenuItem(
                                                    text = { Text(context.getString(item.labelRes)) },
                                                    leadingIcon = { Icon(item.icon, null) },
                                                    onClick = {
                                                        showMenu = false
                                                        when (item) {
                                                            is InitServiceMenuAction.Command -> onAction(context.getString(item.labelRes), item.run)
                                                            is InitServiceMenuAction.Navigate -> item.onClick()
                                                        }
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
