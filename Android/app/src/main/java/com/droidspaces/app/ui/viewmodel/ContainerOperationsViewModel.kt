package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.lifecycle.AndroidViewModel
import com.droidspaces.app.R
import com.droidspaces.app.service.TerminalSessionService
import com.droidspaces.app.util.ContainerCommandBuilder
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerOperationExecutor
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.ViewModelLogger
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File
import java.io.OutputStream

/** Progress state for the stop/uninstall flow (drives the ProgressDialog). */
sealed class UninstallState {
    data object Idle : UninstallState()
    data class InProgress(val containerName: String, val message: String) : UninstallState()
}

/** A pending sparse-image operation awaiting a size from the user. */
sealed class SparseOperation {
    data class Migrate(val container: ContainerInfo) : SparseOperation()
    data class Resize(val container: ContainerInfo) : SparseOperation()
}

/**
 * Owns the container lifecycle/maintenance operations (start/stop/restart,
 * export, uninstall, sparse migrate/resize) that used to live as suspend
 * functions inside `ContainersScreen`. Root shell, asset deploy, command
 * building and content-URI streaming now live here; the composable observes the
 * state below and passes UI concerns (snackbar, list refresh, usage-cache clear)
 * back as callbacks so behavior is identical. See FINDINGS_APP_DUCT_TAPES DT-6.
 */
class ContainerOperationsViewModel(app: Application) : AndroidViewModel(app) {

    private val appContext get() = getApplication<Application>()
    private val prefsManager get() = PreferencesManager.getInstance(appContext)

    /** Container whose operation is currently running (shows the terminal icon / blocks the log dialog). */
    var runningOperationContainer by mutableStateOf<String?>(null)
        private set

    /** Per-container streaming log buffers. */
    var containerLogs by mutableStateOf<Map<String, SnapshotStateList<Pair<Int, String>>>>(emptyMap())
        private set

    /** Container whose log viewer dialog is open (set from UI and from operations). */
    var showLogViewerFor by mutableStateOf<String?>(null)

    /** Last container whose operation failed (kept for parity with prior behavior). */
    var lastErrorContainer by mutableStateOf<String?>(null)
        private set

    /** Stop/uninstall progress. */
    var uninstallState by mutableStateOf<UninstallState>(UninstallState.Idle)
        private set

    /** Non-null when an uninstall failed and its logs should be shown (dismissed from UI). */
    var uninstallLogsDialog by mutableStateOf<List<String>?>(null)

    fun dismissUninstallLogs() { uninstallLogsDialog = null }

    /** Get-or-create the streaming log buffer for [name] (matches the previous inline pattern). */
    private fun logsFor(name: String): SnapshotStateList<Pair<Int, String>> {
        containerLogs[name]?.let { return it }
        val newLogs = mutableStateListOf<Pair<Int, String>>()
        containerLogs = containerLogs.toMutableMap().apply { put(name, newLogs) }
        return newLogs
    }

    /** Clear the in-memory + cached logs for [name] (log dialog "clear" action). */
    fun clearLogsBuffer(name: String) {
        val buffer = containerLogs[name] ?: mutableStateListOf<Pair<Int, String>>().also {
            containerLogs = containerLogs.toMutableMap().apply { put(name, it) }
        }
        buffer.clear()
        prefsManager.clearContainerLogs(name)
        containerLogs = containerLogs.toMutableMap()
    }

    private fun string(resId: Int, vararg args: Any): String =
        if (args.isEmpty()) appContext.getString(resId) else appContext.getString(resId, *args)

    // ── Export ────────────────────────────────────────────────────────────────
    suspend fun executeExport(
        container: ContainerInfo,
        outputUri: Uri,
        onError: (String) -> Unit,
    ) {
        runningOperationContainer = container.name
        val logs = logsFor(container.name)
        logs.clear()
        prefsManager.clearContainerLogs(container.name)

        val logger = ViewModelLogger { level, message -> logs.add(level to message) }.apply { verbose = true }

        var scriptFile: File? = null
        var tempArchive: File? = null
        try {
            val isRunning = ContainerManager.checkContainerStatus(container.name).first
            if (isRunning) {
                uninstallState = UninstallState.InProgress(container.name, string(R.string.stopping_container))
                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                val stopResult = ContainerOperationExecutor.executeCommand(stopCommand, "stop", logger)
                uninstallState = UninstallState.Idle
                if (!stopResult) {
                    logger.e(string(R.string.failed_to_stop_container, container.name))
                    onError(string(R.string.failed_to_stop_container, container.name))
                    return
                }
            }

            logs.clear()
            showLogViewerFor = container.name
            logger.i(string(R.string.starting_export))

            val deployed = File("${appContext.cacheDir}/export_container.sh")
            scriptFile = deployed
            appContext.assets.open("export_container.sh").use { input ->
                deployed.outputStream().use { out: OutputStream -> input.copyTo(out) }
            }
            Shell.cmd("chmod 755 \"${deployed.absolutePath}\"").exec()

            tempArchive = File("${appContext.cacheDir}/${container.name}_export_tmp.tar.gz")
            tempArchive.delete()

            val cmd = "\"${deployed.absolutePath}\" \"${container.name}\" \"${tempArchive.absolutePath}\""
            val success = ContainerOperationExecutor.executeCommand(
                command = cmd,
                operation = "export",
                logger = logger,
                skipHeader = true,
                operationCompletedMessage = string(R.string.operation_completed_success)
            )

            if (success && tempArchive.exists() && tempArchive.length() > 0) {
                logger.i("Writing archive to destination...")
                withContext(Dispatchers.IO) {
                    appContext.contentResolver.openOutputStream(outputUri)?.use { out ->
                        tempArchive.inputStream().use { it.copyTo(out) }
                    }
                }
                logger.i("Done! Archive written successfully.")
            } else if (!success) {
                logger.e(string(R.string.export_container_failed, container.name))
                onError(string(R.string.export_container_failed, container.name))
            }
        } catch (e: Exception) {
            logger.e("Export error: ${e.message}")
            logger.e(e.stackTraceToString())
            onError(string(R.string.export_container_failed, container.name))
        } finally {
            scriptFile?.delete()
            tempArchive?.delete()
            prefsManager.saveContainerLogs(container.name, logs.toList())
            delay(500)
            runningOperationContainer = null
        }
    }

    // ── Uninstall ───────────────────────────────────────────────────────────────
    suspend fun executeUninstall(
        container: ContainerInfo,
        onError: (String) -> Unit,
        onSuccess: (String) -> Unit,
        onRefresh: () -> Unit,
    ) {
        val collectedLogs = mutableListOf<String>()
        val logger = ViewModelLogger { _, message -> collectedLogs.add(message) }.apply { verbose = true }

        try {
            val isRunning = ContainerManager.checkContainerStatus(container.name).first
            if (isRunning) {
                uninstallState = UninstallState.InProgress(container.name, string(R.string.stopping_container))
                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                val stopResult = ContainerOperationExecutor.executeCommand(stopCommand, "stop", logger)
                if (!stopResult) {
                    uninstallState = UninstallState.Idle
                    if (collectedLogs.isNotEmpty()) uninstallLogsDialog = collectedLogs
                    else onError(string(R.string.failed_to_stop_container, container.name))
                    onRefresh()
                    return
                }
            }

            uninstallState = UninstallState.InProgress(container.name, string(R.string.uninstalling_container))
            val result = ContainerManager.uninstallContainer(container, logger)
            uninstallState = UninstallState.Idle

            if (result.isFailure) {
                if (collectedLogs.isNotEmpty()) uninstallLogsDialog = collectedLogs
                else onError(string(R.string.failed_to_uninstall_container, container.name))
                onRefresh()
            } else {
                ContainerOSInfoManager.clearCache(container.name, appContext)
                onSuccess(string(R.string.container_uninstalled_success, container.name))
                onRefresh()
            }
        } catch (e: Exception) {
            uninstallState = UninstallState.Idle
            collectedLogs.add("Exception: ${e.message}")
            collectedLogs.add(e.stackTraceToString())
            if (collectedLogs.isNotEmpty()) uninstallLogsDialog = collectedLogs
            else onError(string(R.string.failed_to_uninstall_container, container.name))
            onRefresh()
        }
    }

    // ── Start / Stop / Restart ────────────────────────────────────────────────
    suspend fun executeOperation(
        container: ContainerInfo,
        operation: String,
        onRefresh: () -> Unit,
        onClearUsage: (String) -> Unit,
        onFailureSnackbar: (String) -> Unit,
    ) {
        runningOperationContainer = container.name
        val logs = logsFor(container.name)
        logs.clear()
        prefsManager.clearContainerLogs(container.name)
        showLogViewerFor = container.name

        val logger = ViewModelLogger { level, message -> logs.add(level to message) }.apply { verbose = true }

        try {
            if (operation == "stop" || operation == "restart") {
                appContext.startService(
                    Intent(appContext, TerminalSessionService::class.java).apply {
                        action = TerminalSessionService.ACTION_STOP_CONTAINER_SESSIONS
                        putExtra(TerminalSessionService.EXTRA_CONTAINER_NAME, container.name)
                    }
                )
                onClearUsage(container.name)
            }

            val command = when (operation) {
                "start" -> ContainerCommandBuilder.buildStartCommand(container)
                "stop" -> ContainerCommandBuilder.buildStopCommand(container)
                "restart" -> ContainerCommandBuilder.buildRestartCommand(container)
                else -> {
                    runningOperationContainer = null
                    return
                }
            }

            val success = ContainerOperationExecutor.executeCommand(
                command = command,
                operation = operation,
                logger = logger,
                operationCompletedMessage = string(R.string.operation_completed_success)
            )

            if (!success) {
                lastErrorContainer = container.name
                logger.e("")
                logger.e(string(R.string.operation_failed))
                onFailureSnackbar(string(R.string.failure_in_operation, operation, container.name))
                onRefresh()
                SystemInfoManager.refreshSELinuxStatus()
            } else {
                lastErrorContainer = null
                onRefresh()
                SystemInfoManager.refreshSELinuxStatus()
            }
        } catch (e: Exception) {
            logger.e("Error: ${e.message}")
            logger.e(e.stackTraceToString())
            lastErrorContainer = container.name
            onFailureSnackbar(string(R.string.failure_in_operation, operation, container.name))
            onRefresh()
        } finally {
            prefsManager.saveContainerLogs(container.name, logs.toList())
            delay(500)
            runningOperationContainer = null
        }
    }

    // ── Sparse migrate / resize ─────────────────────────────────────────────────
    suspend fun executeSparseOperation(
        operation: SparseOperation,
        sizeGb: Int,
        onRefresh: () -> Unit,
    ) {
        val container = when (operation) {
            is SparseOperation.Migrate -> operation.container
            is SparseOperation.Resize -> operation.container
        }

        runningOperationContainer = container.name
        val logs = logsFor(container.name)
        logs.clear()
        prefsManager.clearContainerLogs(container.name)

        val logger = ViewModelLogger { level, message -> logs.add(level to message) }.apply { verbose = true }

        var scriptFile: File? = null
        try {
            val isRunning = ContainerManager.checkContainerStatus(container.name).first
            if (isRunning) {
                uninstallState = UninstallState.InProgress(container.name, string(R.string.stopping_container))
                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                val stopResult = ContainerOperationExecutor.executeCommand(stopCommand, "stop", logger)
                uninstallState = UninstallState.Idle
                if (!stopResult) {
                    logger.e(string(R.string.failed_to_stop_container, container.name))
                    return
                }
                delay(1000)
            }

            logs.clear()
            showLogViewerFor = container.name

            val deployedFile = File("${appContext.cacheDir}/sparsemgr.sh")
            scriptFile = deployedFile
            appContext.assets.open("sparsemgr.sh").use { input ->
                deployedFile.outputStream().use { output: OutputStream -> input.copyTo(output) }
            }
            Shell.cmd("chmod 755 \"${deployedFile.absolutePath}\"").exec()

            val baseDir = ContainerManager.getContainerDirectory(container.name)
            val cmd = when (operation) {
                is SparseOperation.Migrate -> {
                    logger.i(string(R.string.starting_migration))
                    "\"${deployedFile.absolutePath}\" -d \"$baseDir\" migrate $sizeGb"
                }
                is SparseOperation.Resize -> {
                    logger.i(string(R.string.starting_resizing))
                    val imgPath = ContainerManager.getSparseImagePath(container.name)
                    "\"${deployedFile.absolutePath}\" -i \"$imgPath\" resize $sizeGb --yes"
                }
            }

            val success = ContainerOperationExecutor.executeCommand(
                command = cmd,
                operation = "sparse_op",
                logger = logger,
                skipHeader = true,
                operationCompletedMessage = string(R.string.operation_completed_success)
            )

            if (success) {
                logger.i("Updating container configuration...")
                val updatedConfig = if (operation is SparseOperation.Migrate) {
                    container.copy(
                        useSparseImage = true,
                        sparseImageSizeGB = sizeGb,
                        rootfsPath = if (container.rootfsPath.endsWith(".img")) container.rootfsPath else "${container.rootfsPath}.img"
                    )
                } else {
                    container.copy(sparseImageSizeGB = sizeGb)
                }
                val configResult = withContext(Dispatchers.IO) {
                    ContainerManager.updateContainerConfig(appContext, container.name, updatedConfig)
                }
                if (configResult.isSuccess) {
                    logger.i("Configuration updated successfully")
                    onRefresh()
                } else {
                    logger.w("Warning: Failed to update container.config: ${configResult.exceptionOrNull()?.message}")
                }
            } else {
                logger.e(string(R.string.operation_failed))
            }
        } catch (e: Exception) {
            logger.e("Error during sparse operation: ${e.message}")
            logger.e(e.stackTraceToString())
        } finally {
            scriptFile?.delete()
            prefsManager.saveContainerLogs(container.name, logs.toList())
            delay(500)
            runningOperationContainer = null
        }
    }
}
