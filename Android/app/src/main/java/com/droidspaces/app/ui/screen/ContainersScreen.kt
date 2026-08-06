package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.DsTextFieldDefaults

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.foundation.combinedClickable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.material3.SnackbarHost
import com.droidspaces.app.ui.util.ProgressDialog
import com.droidspaces.app.ui.util.ErrorLogsDialog
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.util.showError
import com.droidspaces.app.ui.util.showSuccess
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarDuration
import kotlinx.coroutines.launch
import androidx.compose.runtime.rememberCoroutineScope
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.viewmodel.SystemStatsViewModel
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.FilePickerUtils
import com.droidspaces.app.ui.component.ContainerCard
import com.droidspaces.app.ui.component.ContainerCardActions
import com.droidspaces.app.ui.component.DialogFooterRow
import com.droidspaces.app.ui.component.TerminalDialog
import com.droidspaces.app.ui.component.EmptyState
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.RootUnavailableState
import com.droidspaces.app.ui.component.RootfsRepoSheet
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.ui.viewmodel.ContainerOperationsViewModel
import com.droidspaces.app.ui.viewmodel.UninstallState
import com.droidspaces.app.ui.viewmodel.SparseOperation
import androidx.compose.foundation.clickable
import androidx.compose.ui.draw.clip
import com.droidspaces.app.R
import androidx.compose.ui.window.Dialog

@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun ContainersScreen(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean = true,
    onNavigateToInstallation: (Uri) -> Unit = {},
    onNavigateToEditContainer: (String) -> Unit = {},
    onNavigateToContainerDetails: (String) -> Unit = {},
    containerViewModel: ContainerViewModel,
    expandedContainerName: String?,
    onExpandedContainerNameChange: (String?) -> Unit,
    emptyStateBottomInset: Dp = 0.dp
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val systemStatsViewModel: SystemStatsViewModel = viewModel()
    val prefsManager = PreferencesManager.getInstance(context)

    val snackbarHostState = remember { SnackbarHostState() }

    // Container lifecycle/maintenance operations + their state live in the ViewModel.
    val opsViewModel: ContainerOperationsViewModel = viewModel()

    // UI-only state (dialog triggers / pending pickers).
    var showUninstallConfirmation by remember { mutableStateOf<ContainerInfo?>(null) }
    var showStopConfirmationFor by remember { mutableStateOf<ContainerInfo?>(null) }
    var pendingSparseOperation by remember { mutableStateOf<SparseOperation?>(null) }
    var pendingExportContainer by remember { mutableStateOf<ContainerInfo?>(null) }
    var showRepoSheet by remember { mutableStateOf(false) }

    // File picker launcher - CreateDocument for saving the export archive
    val exportFileLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/gzip")
    ) { uri: Uri? ->
        val container = pendingExportContainer
        pendingExportContainer = null
        if (uri != null && container != null) {
            scope.launch {
                opsViewModel.executeExport(container, uri, onError = { msg -> scope.showError(snackbarHostState, msg) })
            }
        }
    }

    // File picker launcher - accept all files, validate internally
    // We don't filter in the picker (MIME types are unreliable for tar.xz/tar.gz)
    // FilePickerUtils handles proper filename extraction from any URI type (including recent files)
    // and validates that the file is a .tar.xz or .tar.gz file
    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            scope.launch {
                val (isValid, fileName) = FilePickerUtils.isValidTarball(context, uri)
                if (isValid && fileName != null) {
                    onNavigateToInstallation(uri)
                } else {
                    val errorMessage = if (fileName != null) {
                        context.getString(R.string.file_picker_error, fileName)
                    } else {
                        context.getString(R.string.file_picker_error_unknown)
                    }
                    scope.showError(snackbarHostState, errorMessage)
                }
            }
        }
    }

    // Get containers from ViewModel - single source of truth (KernelSU pattern)
    val containers = containerViewModel.containerList

    Box(
        modifier = Modifier.fillMaxSize()
    ) {
        // Show root unavailable state first, then backend unavailable, then content
        when {
            !isRootAvailable -> {
                RootUnavailableState(modifier = Modifier.padding(bottom = emptyStateBottomInset))
            }
            !isBackendAvailable -> {
                ErrorState(modifier = Modifier.padding(bottom = emptyStateBottomInset))
            }
            containers.isEmpty() -> {
                if (containerViewModel.isRefreshing) {
                    Box(
                        modifier = Modifier.fillMaxSize().padding(bottom = emptyStateBottomInset),
                        contentAlignment = Alignment.Center
                    ) {
                        LoadingIndicator(size = LoadingSize.Large)
                    }
                } else {
                    EmptyState(
                        icon = Icons.Default.Storage,
                        title = context.getString(R.string.no_containers_installed),
                        description = context.getString(R.string.install_container_description),
                        // Reserve the floating tab bar's space so the centered
                        // content sits in the visible region, not behind the bar.
                        modifier = Modifier.padding(bottom = emptyStateBottomInset)
                    )
                }
            }
            else -> {
                // Show container cards
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize()
                        .combinedClickable(
                            interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() },
                            indication = null,
                            onClick = { onExpandedContainerNameChange(null) }
                        )
                        .padding(horizontal = 16.dp),
                    contentPadding = PaddingValues(top = 8.dp, bottom = 120.dp), // Clear floating tab bar
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    items(containers, key = { it.name }) { container ->
                        // Console button is always visible - logs persist for each container
                        val isRunning = opsViewModel.runningOperationContainer == container.name

                        ContainerCard(
                            container = container,
                            isOperationRunning = isRunning,
                            isExpanded = expandedContainerName == container.name,
                            actions = ContainerCardActions(
                            onToggleExpand = {
                                onExpandedContainerNameChange(if (expandedContainerName == container.name) null else container.name)
                            },
                             onShowLogs = {
                                opsViewModel.showLogViewerFor = container.name
                            },
                            onStart = {
                                scope.launch {
                                    opsViewModel.executeOperation(
                                        container, "start",
                                        onRefresh = { containerViewModel.refresh() },
                                        onClearUsage = { systemStatsViewModel.clearContainerUsage(it) },
                                        onFailureSnackbar = { msg -> scope.launch { snackbarHostState.showSnackbar(msg, duration = SnackbarDuration.Long) } }
                                    )
                                }
                            },
                            onStop = {
                                 showStopConfirmationFor = container
                             },
                            onRestart = {
                                scope.launch {
                                    opsViewModel.executeOperation(
                                        container, "restart",
                                        onRefresh = { containerViewModel.refresh() },
                                        onClearUsage = { systemStatsViewModel.clearContainerUsage(it) },
                                        onFailureSnackbar = { msg -> scope.launch { snackbarHostState.showSnackbar(msg, duration = SnackbarDuration.Long) } }
                                    )
                                }
                            },
                            onEdit = {
                                onExpandedContainerNameChange(null)
                                onNavigateToEditContainer(container.name)
                            },
                            onEnter = {
                                onNavigateToContainerDetails(container.name)
                            },
                            onUninstall = {
                                onExpandedContainerNameChange(null)
                                showUninstallConfirmation = container
                            },
                            onMigrate = {
                                onExpandedContainerNameChange(null)
                                pendingSparseOperation = SparseOperation.Migrate(container)
                            },
                            onResize = {
                                onExpandedContainerNameChange(null)
                                pendingSparseOperation = SparseOperation.Resize(container)
                            },
                            onExport = {
                                onExpandedContainerNameChange(null)
                                // Generate filename: <name>_yyyyMMdd_HHmmss.tar.gz
                                val timestamp = java.text.SimpleDateFormat(
                                    "yyyyMMdd_HHmmss",
                                    java.util.Locale.US
                                ).format(java.util.Date())
                                val fileName = "${container.name}_${timestamp}.tar.gz"
                                pendingExportContainer = container
                                exportFileLauncher.launch(fileName)
                            }
                            )
                        )
                    }
                }
            }
        }

        // FAB LAYER (Above everything, below dialogs)
        if (isBackendAvailable && isRootAvailable) {
            Column(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .navigationBarsPadding()
                    .padding(end = 24.dp, bottom = 88.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                horizontalAlignment = Alignment.End
            ) {
                // Small secondary FAB: browse online repo (icon only)
                SmallFloatingActionButton(
                    onClick = { showRepoSheet = true },
                    shape = RoundedCornerShape(16.dp),
                    containerColor = MaterialTheme.colorScheme.secondaryContainer,
                    contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                    elevation = FloatingActionButtonDefaults.elevation(0.dp, 0.dp, 0.dp, 0.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.CloudDownload,
                        contentDescription = context.getString(R.string.repo_fab_label),
                        modifier = Modifier.size(20.dp)
                    )
                }

                // Primary FAB: install local file
                ExtendedFloatingActionButton(
                    onClick = { filePickerLauncher.launch("*/*") },
                    shape = RoundedCornerShape(20.dp),
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = MaterialTheme.colorScheme.onPrimary,
                    icon = {
                        Icon(
                            imageVector = Icons.Default.Add,
                            contentDescription = null,
                            modifier = Modifier.size(20.dp)
                        )
                    },
                    text = {
                        Text(
                            text = context.getString(R.string.install),
                            style = MaterialTheme.typography.labelLarge,
                            fontWeight = FontWeight.Bold
                        )
                    }
                )
            }
        }

        // Repo bottom sheet
        if (showRepoSheet) {
            val orientation = LocalConfiguration.current.orientation
            key(orientation) {
                RootfsRepoSheet(
                    onDismiss = { showRepoSheet = false },
                    onInstall = { uri -> onNavigateToInstallation(uri) }
                )
            }
        }

        // SNACKBAR LAYER (Highest Z-index in the root Box)
        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 100.dp)
        )

        // Log viewer dialog - console stays open, user must close manually
        opsViewModel.showLogViewerFor?.let { containerName ->
            // Load logs from memory first, fallback to cache if empty
            val memoryLogs = opsViewModel.containerLogs[containerName]?.toList() ?: emptyList()
            val cachedLogs = if (memoryLogs.isEmpty()) {
                prefsManager.loadContainerLogs(containerName)
            } else {
                emptyList()
            }
            val logs = memoryLogs.ifEmpty { cachedLogs }
            val isBlocking = opsViewModel.runningOperationContainer == containerName // Blocking when operation is running
            TerminalDialog(
                title = context.getString(R.string.logs_title, containerName),
                logs = logs,
                onDismiss = {
                    opsViewModel.showLogViewerFor = null
                    // Refresh container status when console is closed (KernelSU pattern)
                    containerViewModel.refresh()
                },
                onClear = {
                    opsViewModel.clearLogsBuffer(containerName)
                },
                isBlocking = isBlocking // Block dismissal when operation is running
            )
        }

        // Uninstall confirmation dialog
        showUninstallConfirmation?.let { container ->
            UninstallConfirmationDialog(
                containerName = container.name,
                onConfirm = {
                    showUninstallConfirmation = null
                    scope.launch {
                        opsViewModel.executeUninstall(
                            container,
                            onError = { msg -> scope.showError(snackbarHostState, msg) },
                            onSuccess = { msg -> scope.showSuccess(snackbarHostState, msg) },
                            onRefresh = { containerViewModel.refresh() }
                        )
                    }
                },
                onDismiss = {
                    showUninstallConfirmation = null
                }
            )
        }

        // Stop confirmation dialog
        showStopConfirmationFor?.let { container ->
            StopContainerConfirmationDialog(
                containerName = container.name,
                onConfirm = {
                    showStopConfirmationFor = null
                    scope.launch {
                        opsViewModel.executeOperation(
                            container, "stop",
                            onRefresh = { containerViewModel.refresh() },
                            onClearUsage = { systemStatsViewModel.clearContainerUsage(it) },
                            onFailureSnackbar = { msg -> scope.launch { snackbarHostState.showSnackbar(msg, duration = SnackbarDuration.Long) } }
                        )
                    }
                },
                onDismiss = {
                    showStopConfirmationFor = null
                }
            )
        }

        // Uninstall progress dialog
        (opsViewModel.uninstallState as? UninstallState.InProgress)?.let { state ->
            ProgressDialog(
                message = state.message
            )
        }

        // Uninstall logs dialog (only on failure)
        opsViewModel.uninstallLogsDialog?.let { logs ->
            ErrorLogsDialog(
                logs = logs,
                onDismiss = { opsViewModel.dismissUninstallLogs() }
            )
        }

        // Sparse operation size dialog
        pendingSparseOperation?.let { op ->
            val container = when (op) {
                is SparseOperation.Migrate -> op.container
                is SparseOperation.Resize -> op.container
            }

            SparseSizeDialog(
                title = context.getString(
                    if (op is SparseOperation.Migrate) R.string.migrate_dialog_title
                    else R.string.resize_dialog_title
                ),
                message = context.getString(
                    if (op is SparseOperation.Migrate) R.string.migrate_dialog_message
                    else R.string.resize_dialog_message,
                    container.name
                ),
                initialSize = container.sparseImageSizeGB ?: 8,
                onConfirm = { size ->
                    pendingSparseOperation = null
                    scope.launch {
                        opsViewModel.executeSparseOperation(op, size, onRefresh = { containerViewModel.refresh() })
                    }
                },
                onDismiss = { pendingSparseOperation = null }
            )
        }
    }
}

@Composable
private fun SparseSizeDialog(
    title: String,
    message: String,
    initialSize: Int,
    onConfirm: (Int) -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    var sizeText by remember { mutableStateOf(initialSize.toString()) }
    val size = sizeText.toIntOrNull()
    val isValid = size != null && size in 4..512
    val dialogShape = RoundedCornerShape(24.dp)

    Dialog(
        onDismissRequest = onDismiss,
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp)
                .imePadding(),
            shape = dialogShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
            tonalElevation = 0.dp
        ) {
            Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Text(title, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.titleLarge)
                Text(message, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)

                OutlinedTextField(
                    value = sizeText,
                    onValueChange = { if (it.isEmpty() || it.all { c -> c.isDigit() }) sizeText = it },
                    label = { Text(context.getString(R.string.size_gb)) },
                    placeholder = { Text(context.getString(R.string.size_range_4_512_hint)) },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    shape = RoundedCornerShape(16.dp),
                    colors = DsTextFieldDefaults.colors(),
                    isError = !isValid && sizeText.isNotEmpty(),
                    supportingText = {
                        if (!isValid && sizeText.isNotEmpty()) Text(context.getString(R.string.enter_size_between_4_512_gb))
                    },
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                        keyboardType = androidx.compose.ui.text.input.KeyboardType.Number
                    )
                )

                DialogFooterRow(
                    dismissLabel = context.getString(R.string.cancel),
                    confirmLabel = context.getString(R.string.continue_button),
                    onDismiss = onDismiss,
                    onConfirm = { size?.let { onConfirm(it) } },
                    confirmEnabled = isValid
                )
            }
        }
    }
}

@Composable
private fun UninstallConfirmationDialog(
    containerName: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val dialogShape = RoundedCornerShape(24.dp)
    var confirmText by remember { mutableStateOf("") }
    val isConfirmed = confirmText == containerName

    Dialog(
        onDismissRequest = onDismiss,
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp),
            shape = dialogShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.3f)),
            tonalElevation = 0.dp
        ) {
            Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(
                        Icons.Default.Warning, contentDescription = null,
                        tint = MaterialTheme.colorScheme.error, modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = context.getString(R.string.uninstall_container_title),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                }
                Text(
                    text = buildAnnotatedString {
                        val template = context.getString(R.string.uninstall_container_message)
                        val parts = template.split("%1\$s")
                        append(parts.getOrElse(0) { "" })
                        withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append(containerName) }
                        append(parts.getOrElse(1) { "" })
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = context.getString(R.string.type_container_name_to_confirm),
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.Bold
                    )
                    OutlinedTextField(
                        value = confirmText,
                        onValueChange = { confirmText = it },
                        modifier = Modifier.fillMaxWidth(),
                        placeholder = { Text(containerName) },
                        singleLine = true,
                        isError = confirmText.isNotEmpty() && !isConfirmed,
                        shape = RoundedCornerShape(14.dp),
                        colors = OutlinedTextFieldDefaults.colors(
                            unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                            focusedBorderColor = MaterialTheme.colorScheme.error.copy(alpha = 0.8f),
                            unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
                            focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
                        )
                    )
                }
                DialogFooterRow(
                    dismissLabel = context.getString(R.string.cancel),
                    confirmLabel = context.getString(R.string.uninstall),
                    onDismiss = onDismiss,
                    onConfirm = onConfirm,
                    confirmEnabled = isConfirmed,
                    confirmColor = MaterialTheme.colorScheme.error,
                    confirmContentColor = MaterialTheme.colorScheme.onError
                )
            }
        }
    }
}

@Composable
private fun StopContainerConfirmationDialog(
    containerName: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val dialogShape = RoundedCornerShape(24.dp)

    Dialog(
        onDismissRequest = onDismiss,
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp),
            shape = dialogShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f)),
            tonalElevation = 0.dp
        ) {
            Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(
                        imageVector = androidx.compose.material.icons.Icons.Default.Warning,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = "Stop Container?",
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                }
                Text(
                    text = "Are you sure you want to stop the container \"$containerName\"?",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                DialogFooterRow(
                    dismissLabel = context.getString(android.R.string.no),
                    confirmLabel = context.getString(android.R.string.yes),
                    onDismiss = onDismiss,
                    onConfirm = onConfirm,
                    confirmEnabled = true,
                    confirmColor = MaterialTheme.colorScheme.error,
                    confirmContentColor = MaterialTheme.colorScheme.onError
                )
            }
        }
    }
}
