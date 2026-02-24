package com.droidspaces.app.ui.screen

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
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
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerCommandBuilder
import com.droidspaces.app.util.ContainerOperationExecutor
import com.droidspaces.app.util.ContainerLogger
import com.droidspaces.app.util.ViewModelLogger
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.FilePickerUtils
import com.droidspaces.app.ui.component.ContainerCard
import com.droidspaces.app.ui.component.ContainerLogViewer
import com.droidspaces.app.ui.component.EmptyState
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.RootUnavailableState
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.util.Log
import com.droidspaces.app.R

// Uninstall state (similar to SystemdScreen ActionState)
private sealed class UninstallState {
    data object Idle : UninstallState()
    data class InProgress(val containerName: String, val message: String) : UninstallState()
}

@Composable
fun ContainersScreen(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean = true,
    onNavigateToInstallation: (Uri) -> Unit = {},
    onNavigateToEditContainer: (String) -> Unit = {},
    containerViewModel: ContainerViewModel
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val prefsManager = PreferencesManager.getInstance(context)

    val snackbarHostState = remember { SnackbarHostState() }

    // Track running operations and logs per container
    // Use mutableStateListOf directly like installation screen for optimal performance
    var runningOperationContainer by remember { mutableStateOf<String?>(null) }
    var containerLogs by remember { mutableStateOf<Map<String, androidx.compose.runtime.snapshots.SnapshotStateList<Pair<Int, String>>>>(emptyMap()) }
    var showLogViewerFor by remember { mutableStateOf<String?>(null) }
    var lastErrorContainer by remember { mutableStateOf<String?>(null) }

    // Uninstall state management (similar to SystemdScreen pattern)
    var showUninstallConfirmation by remember { mutableStateOf<ContainerInfo?>(null) }
    var uninstallState by remember { mutableStateOf<UninstallState>(UninstallState.Idle) }
    var uninstallLogsDialog by remember { mutableStateOf<List<String>?>(null) }

    // File picker launcher - accept all files, validate internally
    // We don't filter in the picker (MIME types are unreliable for tar.xz/tar.gz)
    // FilePickerUtils handles proper filename extraction from any URI type (including recent files)
    // and validates that the file is a .tar.xz or .tar.gz file
    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            // Use FilePickerUtils to properly get filename from any URI type (including recent files)
            // and validate file extension
            scope.launch {
                val (isValid, fileName) = FilePickerUtils.isValidTarball(context, uri)
                if (isValid && fileName != null) {
                    onNavigateToInstallation(uri)
                } else {
                    // Show error snackbar with actual filename or helpful message
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

    // Execute container uninstallation - using same pattern as SystemdScreen
    suspend fun executeUninstall(container: ContainerInfo) {
        // Collect logs during uninstallation
        val collectedLogs = mutableListOf<String>()

        // Create logger to collect logs
        val logger = ViewModelLogger { _, message ->
            // Collect logs for potential error dialog
            collectedLogs.add(message)
        }.apply {
            verbose = true
        }

        try {
            // Check if container is running first
            val isRunning = ContainerManager.checkContainerStatus(container.name).first

            if (isRunning) {
                // Show progress dialog - stopping
                uninstallState = UninstallState.InProgress(container.name, context.getString(R.string.stopping_container))

                // Stop the container first
                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                val stopResult = ContainerOperationExecutor.executeCommand(stopCommand, "stop", logger)

                if (!stopResult) {
                    // Failed to stop - dismiss progress and show error
                    uninstallState = UninstallState.Idle
                    if (collectedLogs.isNotEmpty()) {
                        uninstallLogsDialog = collectedLogs
                    } else {
                        scope.showError(snackbarHostState, context.getString(R.string.failed_to_stop_container, container.name))
                    }
                    containerViewModel.refresh()
                    return
                }
            }

            // Show progress dialog - uninstalling
            uninstallState = UninstallState.InProgress(container.name, context.getString(R.string.uninstalling_container))

            // Execute uninstallation using ContainerManager
            val result = ContainerManager.uninstallContainer(container, logger)

            // Dismiss progress dialog
            uninstallState = UninstallState.Idle

            if (result.isFailure) {
                // Failed - show logs dialog if there are logs, otherwise snackbar
                if (collectedLogs.isNotEmpty()) {
                    uninstallLogsDialog = collectedLogs
                } else {
                    scope.showError(snackbarHostState, context.getString(R.string.failed_to_uninstall_container, container.name))
                }

                // Refresh container status immediately on failure
                containerViewModel.refresh()
            } else {
                // Success - show snackbar
                scope.showSuccess(snackbarHostState, context.getString(R.string.container_uninstalled_success, container.name))

                // Refresh container status immediately after successful uninstallation
                containerViewModel.refresh()
            }
        } catch (e: Exception) {
            // Dismiss progress dialog
            uninstallState = UninstallState.Idle

            // Add exception to logs
            collectedLogs.add("Exception: ${e.message}")
            collectedLogs.add(e.stackTraceToString())

            // Show logs dialog
            if (collectedLogs.isNotEmpty()) {
                uninstallLogsDialog = collectedLogs
            } else {
                scope.showError(snackbarHostState, context.getString(R.string.failed_to_uninstall_container, container.name))
            }

            // Refresh container status immediately even on exception
            containerViewModel.refresh()
        }
    }

    // Execute container operation (start/stop/restart) - using same pattern as installation
    suspend fun executeOperation(container: ContainerInfo, operation: String) {
        // Show terminal icon immediately - prepare console first
        runningOperationContainer = container.name

        // Initialize logs list if needed - use mutableStateListOf for optimal performance
        val logs = if (!containerLogs.containsKey(container.name)) {
            val newLogs = androidx.compose.runtime.mutableStateListOf<Pair<Int, String>>()
            containerLogs = containerLogs.toMutableMap().apply {
                put(container.name, newLogs)
            }
            newLogs
        } else {
            containerLogs[container.name]!!
        }

        // Auto-clear previous logs when starting new action (only store 1 action)
        logs.clear()
        prefsManager.clearContainerLogs(container.name)

        // Immediately show blocking log viewer
        showLogViewerFor = container.name

        // Create logger - directly add to mutableStateListOf (no map recreation needed!)
        // This matches installation screen pattern exactly for smooth performance
        // Logger uses suspend functions and ensures Main thread for UI updates
        val logger = ViewModelLogger { level, message ->
            // Direct add to SnapshotStateList - Compose will handle efficient recomposition
            // No need to recreate map or trigger manual recomposition
            // This callback is already on Main thread (ensured by ViewModelLogger)
            logs.add(level to message)
        }.apply {
            verbose = true
        }

        try {
            // Build command
            val command = when (operation) {
                "start" -> ContainerCommandBuilder.buildStartCommand(container)
                "stop" -> ContainerCommandBuilder.buildStopCommand(container)
                "restart" -> ContainerCommandBuilder.buildRestartCommand(container)
                else -> {
                    runningOperationContainer = null
                    return
                }
            }

            // Execute command using logger callback pattern (same as installation)
            val success = ContainerOperationExecutor.executeCommand(command, operation, logger)

            // If the command execution itself succeeded, consider the operation successful
            // The status check is just for additional verification but shouldn't cause failure
            if (!success) {
                lastErrorContainer = container.name
                logger.e("")
                logger.e(context.getString(R.string.operation_failed))

                // Operation failed - console stays open, user must close manually
                // Don't set showLogViewerFor = null

                // Show snackbar
                scope.launch {
                    snackbarHostState.showSnackbar(
                        message = context.getString(R.string.failure_in_operation, operation, container.name),
                        duration = SnackbarDuration.Long
                    )
                }

                // Refresh container status and SELinux immediately on failure (KernelSU pattern)
                containerViewModel.refresh()
                SystemInfoManager.refreshSELinuxStatus()
            } else {
                // Command execution succeeded - check if we need to add status verification note
                val operationSuccess = if (operation == "start" || operation == "restart") {
                    ContainerOperationExecutor.checkCommandSuccess(
                        ContainerCommandBuilder.buildStatusCommand(container)
                    )
                } else {
                    // For stop, check if container is no longer running
                    !ContainerOperationExecutor.checkCommandSuccess(
                        ContainerCommandBuilder.buildStatusCommand(container)
                    )
                }

                if (!operationSuccess) {
                    // Command succeeded but status check failed - add a note but don't treat as failure
                    logger.w("")
                    logger.w("Note: Command executed successfully, but container status verification failed")
                    logger.w("This may be normal during startup/shutdown - check container status manually")
                }

                logger.i("")
                logger.i(context.getString(R.string.operation_completed_success))
                lastErrorContainer = null

                // Operation succeeded - console stays open, user must close manually
                // Don't set showLogViewerFor = null

                // Refresh container status and SELinux immediately after successful operation (KernelSU pattern)
                containerViewModel.refresh()
                SystemInfoManager.refreshSELinuxStatus()
            }
        } catch (e: Exception) {
            logger.e("Error: ${e.message}")
            logger.e(e.stackTraceToString())
            lastErrorContainer = container.name

            // Operation failed with exception - console stays open, user must close manually
            // Don't set showLogViewerFor = null

            // Show snackbar
            scope.launch {
                snackbarHostState.showSnackbar(
                    message = context.getString(R.string.failure_in_operation, operation, container.name),
                    duration = SnackbarDuration.Long
                )
            }

            // Refresh container status immediately even on exception (KernelSU pattern)
            containerViewModel.refresh()
        } finally {
            // Save logs to cache when operation completes (only last action)
            prefsManager.saveContainerLogs(container.name, logs.toList())

            // Clear running operation state (but keep console open)
            runningOperationContainer = null
        }
    }

    Box(
        modifier = Modifier.fillMaxSize()
    ) {
        // Show root unavailable state first, then backend unavailable, then content
        when {
            !isRootAvailable -> {
                RootUnavailableState()
            }
            !isBackendAvailable -> {
                ErrorState()
            }
            containers.isEmpty() -> {
                EmptyState(
                    icon = Icons.Default.Storage,
                    title = context.getString(R.string.no_containers_installed),
                    description = context.getString(R.string.install_container_description)
                )
            }
            else -> {
                // Show container cards
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    containers.forEach { container ->
                        // Console button is always visible - logs persist for each container
                        val isRunning = runningOperationContainer == container.name

                        ContainerCard(
                            container = container,
                            isOperationRunning = isRunning, // Only used for disabling buttons during operations
                            onShowLogs = {
                                showLogViewerFor = container.name
                            },
                            onStart = {
                                scope.launch {
                                    executeOperation(container, "start")
                                }
                            },
                            onStop = {
                                scope.launch {
                                    executeOperation(container, "stop")
                                }
                            },
                            onRestart = {
                                scope.launch {
                                    executeOperation(container, "restart")
                                }
                            },
                            onEdit = {
                                onNavigateToEditContainer(container.name)
                            },
                            onUninstall = {
                                showUninstallConfirmation = container
                            }
                        )
                    }
                }
            }
        }

        // Floating Action Button - Install Container (bottom right, only if backend available)
        if (isBackendAvailable && isRootAvailable) {
            FloatingActionButton(
                onClick = {
                    // Launch file picker - accept all files, validation happens in callback
                    filePickerLauncher.launch("*/*")
                },
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(16.dp),
                shape = RoundedCornerShape(16.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.Add,
                    contentDescription = context.getString(R.string.install_container)
                )
            }
        }

        // Snackbar for error messages
        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier.align(Alignment.BottomCenter)
        )

        // Log viewer dialog - console stays open, user must close manually
        showLogViewerFor?.let { containerName ->
            // Load logs from memory first, fallback to cache if empty
            val memoryLogs = containerLogs[containerName]?.toList() ?: emptyList()
            val cachedLogs = if (memoryLogs.isEmpty()) {
                prefsManager.loadContainerLogs(containerName)
            } else {
                emptyList()
            }
            val logs = memoryLogs.ifEmpty { cachedLogs }
            val isBlocking = runningOperationContainer == containerName // Blocking when operation is running
            ContainerLogViewer(
                containerName = containerName,
                logs = logs,
                onDismiss = {
                    showLogViewerFor = null
                    // Refresh container status when console is closed (KernelSU pattern)
                    containerViewModel.refresh()
                },
                onClear = {
                    // Clear logs from memory and cache
                    containerLogs[containerName]?.clear()
                    prefsManager.clearContainerLogs(containerName)
                    // Force recomposition by updating the map
                    containerLogs = containerLogs.toMutableMap()
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
                        executeUninstall(container)
                    }
                },
                onDismiss = {
                    showUninstallConfirmation = null
                }
            )
        }

        // Uninstall progress dialog
        (uninstallState as? UninstallState.InProgress)?.let { state ->
            ProgressDialog(
                message = state.message
            )
        }

        // Uninstall logs dialog (only on failure)
        uninstallLogsDialog?.let { logs ->
            ErrorLogsDialog(
                logs = logs,
                onDismiss = { uninstallLogsDialog = null }
            )
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
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = {
            Icon(
                Icons.Default.Warning,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.error
            )
        },
        title = {
            Text(
                text = context.getString(R.string.uninstall_container_title),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
        },
        text = {
            Text(
                text = context.getString(R.string.uninstall_container_message, containerName),
                style = MaterialTheme.typography.bodyMedium
            )
        },
        confirmButton = {
            TextButton(
                onClick = onConfirm,
                colors = ButtonDefaults.textButtonColors(
                    contentColor = MaterialTheme.colorScheme.error
                )
            ) {
                Text(context.getString(R.string.uninstall), fontWeight = FontWeight.Bold)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(context.getString(R.string.cancel))
            }
        },
        shape = RoundedCornerShape(28.dp)
    )
}

