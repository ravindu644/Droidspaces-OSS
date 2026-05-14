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
import androidx.compose.foundation.combinedClickable
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
import com.topjohnwu.superuser.Shell
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
import com.droidspaces.app.ui.component.TerminalDialog
import com.droidspaces.app.ui.component.EmptyState
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.RootUnavailableState
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.OutputStream
import android.util.Log
import androidx.compose.foundation.clickable
import androidx.compose.ui.draw.clip
import com.droidspaces.app.R
import androidx.compose.ui.window.Dialog

// Uninstall state (similar to SystemdScreen ActionState)
private sealed class UninstallState {
    data object Idle : UninstallState()
    data class InProgress(val containerName: String, val message: String) : UninstallState()
}

// Sparse operation state
private sealed class SparseOperation {
    data class Migrate(val container: ContainerInfo) : SparseOperation()
    data class Resize(val container: ContainerInfo) : SparseOperation()
}

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
    onExpandedContainerNameChange: (String?) -> Unit
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

    // Uninstall/Operation state management
    var showUninstallConfirmation by remember { mutableStateOf<ContainerInfo?>(null) }
    var uninstallState by remember { mutableStateOf<UninstallState>(UninstallState.Idle) }
    var uninstallLogsDialog by remember { mutableStateOf<List<String>?>(null) }

    // Sparse operation state management
    var pendingSparseOperation by remember { mutableStateOf<SparseOperation?>(null) }

    // Export state - tracks which container is pending export (waiting for file picker)
    var pendingExportContainer by remember { mutableStateOf<ContainerInfo?>(null) }

    // Execute container export - writes archive to user-picked URI via temp file
    suspend fun executeExport(container: ContainerInfo, outputUri: Uri) {
        runningOperationContainer = container.name
        val logs = if (!containerLogs.containsKey(container.name)) {
            val newLogs = androidx.compose.runtime.mutableStateListOf<Pair<Int, String>>()
            containerLogs = containerLogs.toMutableMap().apply { put(container.name, newLogs) }
            newLogs
        } else {
            containerLogs[container.name]!!
        }
        logs.clear()
        prefsManager.clearContainerLogs(container.name)
        // Defer showLogViewerFor until after stopping logic

        val logger = ViewModelLogger { level, message ->
            logs.add(level to message)
        }.apply { verbose = true }

        var scriptFile: File? = null
        var tempArchive: File? = null
        try {
            // Check if container is running first
            val isRunning = ContainerManager.checkContainerStatus(container.name).first
            if (isRunning) {
                // Show progress dialog - stopping (using UninstallState for consistent UI)
                uninstallState = UninstallState.InProgress(container.name, context.getString(R.string.stopping_container))

                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                val stopResult = ContainerOperationExecutor.executeCommand(stopCommand, "stop", logger)

                // Dismiss progress dialog
                uninstallState = UninstallState.Idle

                if (!stopResult) {
                    logger.e(context.getString(R.string.failed_to_stop_container, container.name))
                    scope.showError(snackbarHostState, context.getString(R.string.failed_to_stop_container, container.name))
                    return
                }
            }

            // Clear stop-logs so terminal starts fresh for the actual export
            logs.clear()

            // Now show the log viewer for the actual operation
            showLogViewerFor = container.name
            logger.i(context.getString(R.string.starting_export))

            // Deploy export_container.sh from assets
            val deployed = File("${context.cacheDir}/export_container.sh")
            scriptFile = deployed
            context.assets.open("export_container.sh").use { input ->
                deployed.outputStream().use { out: java.io.OutputStream -> input.copyTo(out) }
            }
            Shell.cmd("chmod 755 \"${deployed.absolutePath}\"").exec()

            // Write archive to a temp file first, then copy to the URI
            tempArchive = File("${context.cacheDir}/${container.name}_export_tmp.tar.gz")
            tempArchive.delete()

            val cmd = "\"${deployed.absolutePath}\" \"${container.name}\" \"${tempArchive.absolutePath}\""
            val success = ContainerOperationExecutor.executeCommand(
                command = cmd,
                operation = "export",
                logger = logger,
                skipHeader = true,
                operationCompletedMessage = context.getString(R.string.operation_completed_success)
            )

            if (success && tempArchive.exists() && tempArchive.length() > 0) {
                // Copy temp file → user-picked URI
                logger.i("Writing archive to destination...")
                withContext(Dispatchers.IO) {
                    context.contentResolver.openOutputStream(outputUri)?.use { out ->
                        tempArchive.inputStream().use { it.copyTo(out) }
                    }
                }
                logger.i("Done! Archive written successfully.")
            } else if (!success) {
                logger.e(context.getString(R.string.export_container_failed, container.name))
                scope.showError(snackbarHostState, context.getString(R.string.export_container_failed, container.name))
            }
        } catch (e: Exception) {
            logger.e("Export error: ${e.message}")
            logger.e(e.stackTraceToString())
            scope.showError(snackbarHostState, context.getString(R.string.export_container_failed, container.name))
        } finally {
            scriptFile?.delete()
            tempArchive?.delete()
            prefsManager.saveContainerLogs(container.name, logs.toList())
            kotlinx.coroutines.delay(500)
            runningOperationContainer = null
        }
    }


    // File picker launcher - CreateDocument for saving the export archive
    val exportFileLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/gzip")
    ) { uri: Uri? ->
        val container = pendingExportContainer
        pendingExportContainer = null
        if (uri != null && container != null) {
            scope.launch {
                executeExport(container, uri)
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

            // Execute command using logger callback pattern (same as installation).
            // Pass operationCompletedMessage so the success line is logged inside executeCommand
            // in guaranteed order on Main.immediate - before this coroutine resumes.
            // This eliminates the race where logger.i() calls posted from here could
            // interleave with the exit-code line logged inside executeCommand.
            val success = ContainerOperationExecutor.executeCommand(
                command = command,
                operation = operation,
                logger = logger,
                operationCompletedMessage = context.getString(R.string.operation_completed_success)
            )

            if (!success) {
                lastErrorContainer = container.name
                logger.e("")
                logger.e(context.getString(R.string.operation_failed))

                // Operation failed - console stays open, user must close manually

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
                lastErrorContainer = null

                // Operation succeeded - console stays open, user must close manually

                // Refresh container status and SELinux immediately after successful operation (KernelSU pattern)
                containerViewModel.refresh()
                SystemInfoManager.refreshSELinuxStatus()
            }
        } catch (e: Exception) {
            logger.e("Error: ${e.message}")
            logger.e(e.stackTraceToString())
            lastErrorContainer = container.name

            // Operation failed with exception - console stays open, user must close manually

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

            // Add a generous delay before clearing status to allow TerminalConsole
            // to finish its final scroll animation for the "Success" message.
            kotlinx.coroutines.delay(500)

            // Clear running operation state (but keep console open)
            runningOperationContainer = null
        }
    }

    // Execute sparse image operation (migrate/resize)
    suspend fun executeSparseOperation(operation: SparseOperation, sizeGb: Int) {
        val container = when (operation) {
            is SparseOperation.Migrate -> operation.container
            is SparseOperation.Resize -> operation.container
        }

        // 1. Prepare terminal and logs
        runningOperationContainer = container.name
        val logs = if (!containerLogs.containsKey(container.name)) {
            val newLogs = androidx.compose.runtime.mutableStateListOf<Pair<Int, String>>()
            containerLogs = containerLogs.toMutableMap().apply { put(container.name, newLogs) }
            newLogs
        } else {
            containerLogs[container.name]!!
        }
        logs.clear()
        prefsManager.clearContainerLogs(container.name)
        // Defer showLogViewerFor until after stopping logic

        val logger = ViewModelLogger { level, message ->
            logs.add(level to message)
        }.apply { verbose = true }

        var scriptFile: File? = null
        try {
            // Check if container is running first
            val isRunning = ContainerManager.checkContainerStatus(container.name).first
            if (isRunning) {
                // Show progress dialog - stopping (using UninstallState for consistent UI)
                uninstallState = UninstallState.InProgress(container.name, context.getString(R.string.stopping_container))

                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                val stopResult = ContainerOperationExecutor.executeCommand(stopCommand, "stop", logger)

                // Dismiss progress dialog
                uninstallState = UninstallState.Idle

                if (!stopResult) {
                    logger.e(context.getString(R.string.failed_to_stop_container, container.name))
                    return
                }
                // Small delay to ensure cleanup
                kotlinx.coroutines.delay(1000)
            }

            // Clear stop-logs so terminal starts fresh for the actual operation
            logs.clear()

            // Now show the log viewer for the actual operation
            showLogViewerFor = container.name

            // 3. Deploy sparsemgr.sh from assets
            val deployedFile = File("${context.cacheDir}/sparsemgr.sh")
            scriptFile = deployedFile
            context.assets.open("sparsemgr.sh").use { input ->
                deployedFile.outputStream().use { output: OutputStream ->
                    input.copyTo(output)
                }
            }
            Shell.cmd("chmod 755 \"${deployedFile.absolutePath}\"").exec()

            // 4. Build and execute command
            val baseDir = ContainerManager.getContainerDirectory(container.name)
            val cmd = when (operation) {
                is SparseOperation.Migrate -> {
                    logger.i(context.getString(R.string.starting_migration))
                    "\"${deployedFile.absolutePath}\" -d \"$baseDir\" migrate $sizeGb"
                }
                is SparseOperation.Resize -> {
                    logger.i(context.getString(R.string.starting_resizing))
                    val imgPath = ContainerManager.getSparseImagePath(container.name)
                    "\"${deployedFile.absolutePath}\" -i \"$imgPath\" resize $sizeGb --yes"
                }
            }

            val success = ContainerOperationExecutor.executeCommand(
                command = cmd,
                operation = "sparse_op",
                logger = logger,
                skipHeader = true,
                operationCompletedMessage = context.getString(R.string.operation_completed_success)
            )

            if (success) {
                // 5. Update container config on success
                logger.i("Updating container configuration...")
                val updatedConfig = if (operation is SparseOperation.Migrate) {
                    container.copy(
                        useSparseImage = true,
                        sparseImageSizeGB = sizeGb,
                        rootfsPath = if (container.rootfsPath.endsWith(".img")) container.rootfsPath else "${container.rootfsPath}.img"
                    )
                } else {
                    container.copy(
                        sparseImageSizeGB = sizeGb
                    )
                }
                val configResult = withContext(Dispatchers.IO) {
                    ContainerManager.updateContainerConfig(context, container.name, updatedConfig)
                }

                if (configResult.isSuccess) {
                    logger.i("Configuration updated successfully")
                    containerViewModel.refresh()
                } else {
                    logger.w("Warning: Failed to update container.config: ${configResult.exceptionOrNull()?.message}")
                }
            } else {
                logger.e(context.getString(R.string.operation_failed))
            }

        } catch (e: Exception) {
            logger.e("Error during sparse operation: ${e.message}")
            logger.e(e.stackTraceToString())
        } finally {
            scriptFile?.delete()
            prefsManager.saveContainerLogs(container.name, logs.toList())
            // Add a generous delay before clearing status to allow TerminalConsole
            // to finish its final scroll animation for the "Update Config" message.
            kotlinx.coroutines.delay(500)
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
                        .combinedClickable(
                            interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() },
                            indication = null,
                            onClick = { onExpandedContainerNameChange(null) }
                        )
                        .verticalScroll(rememberScrollState())
                        .padding(horizontal = 16.dp)
                        .padding(top = 8.dp, bottom = 120.dp), // Clear floating tab bar
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    containers.forEach { container ->
                        // Console button is always visible - logs persist for each container
                        val isRunning = runningOperationContainer == container.name

                        ContainerCard(
                            container = container,
                            isOperationRunning = isRunning,
                            isExpanded = expandedContainerName == container.name,
                            onToggleExpand = {
                                onExpandedContainerNameChange(if (expandedContainerName == container.name) null else container.name)
                            },
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
                            onEnter = {
                                onNavigateToContainerDetails(container.name)
                            },
                            onUninstall = {
                                showUninstallConfirmation = container
                            },
                            onMigrate = {
                                pendingSparseOperation = SparseOperation.Migrate(container)
                            },
                            onResize = {
                                pendingSparseOperation = SparseOperation.Resize(container)
                            },
                            onExport = {
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
                    }
                }
            }
        }

        // FAB LAYER (Above everything, below dialogs)
        if (isBackendAvailable && isRootAvailable) {
            ExtendedFloatingActionButton(
                onClick = { filePickerLauncher.launch("*/*") },
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .navigationBarsPadding()
                    .padding(end = 24.dp, bottom = 88.dp), // 88dp clears the floating tab bar
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

        // SNACKBAR LAYER (Highest Z-index in the root Box)
        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 100.dp)
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
            TerminalDialog(
                title = context.getString(R.string.logs_title, containerName),
                logs = logs,
                onDismiss = {
                    showLogViewerFor = null
                    // Refresh container status when console is closed (KernelSU pattern)
                    containerViewModel.refresh()
                },
                onClear = {
                    // Force-clear memory list (initialize if null) to ensure UI reacts immediately
                    val buffer = containerLogs[containerName] ?: androidx.compose.runtime.mutableStateListOf<Pair<Int, String>>().also {
                        containerLogs = containerLogs.toMutableMap().apply { put(containerName, it) }
                    }
                    buffer.clear()

                    // Clear persistent cache synchronously
                    prefsManager.clearContainerLogs(containerName)

                    // Trigger map recomposition for safety
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
                        executeSparseOperation(op, size)
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
                    colors = OutlinedTextFieldDefaults.colors(
                        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLow,
                        focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                    ),
                    isError = !isValid && sizeText.isNotEmpty(),
                    supportingText = {
                        if (!isValid && sizeText.isNotEmpty()) Text(context.getString(R.string.enter_size_between_4_512_gb))
                    },
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                        keyboardType = androidx.compose.ui.text.input.KeyboardType.Number
                    )
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                        }
                    }
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(enabled = isValid, onClick = { size?.let { onConfirm(it) } }),
                        shape = RoundedCornerShape(14.dp),
                        color = if (isValid) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.continue_button),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = if (isValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                            )
                        }
                    }
                }
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
                    text = context.getString(R.string.uninstall_container_message, containerName),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                        }
                    }
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onConfirm),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.error,
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.uninstall),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onError
                            )
                        }
                    }
                }
            }
        }
    }
}
