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
import com.droidspaces.app.util.ContainerInstaller
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerDiskUsageManager
import com.droidspaces.app.util.ContainerUsersManager
import com.droidspaces.app.util.ContainerOperationExecutor
import com.droidspaces.app.util.FilePickerUtils
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.ViewModelLogger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File

/** Progress state for the stop/uninstall flow (drives the ProgressDialog). */
sealed class UninstallState {
    data object Idle : UninstallState()
    data class InProgress(val containerName: String, val message: String) : UninstallState()
}

/** A sparse-image operation awaiting a size from the user. */
enum class SparseOp { MIGRATE, RESIZE }

/**
 * Owns the container lifecycle/maintenance operations (start/stop/restart,
 * export, uninstall, sparse migrate/resize) that used to live as suspend
 * functions inside `ContainersScreen`. Root shell, asset deploy, command
 * building and content-URI streaming now live here; the composable observes the
 * state below and passes UI concerns (snackbar, list refresh, usage-cache clear)
 * back as callbacks so behavior is identical.
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

    /**
     * Stops [container] if it runs, behind the progress dialog, which stays up on
     * success for the caller's next step. False, with the dialog gone and the failure
     * logged, when the stop failed.
     */
    private suspend fun stopIfRunning(container: ContainerInfo, logger: ViewModelLogger): Boolean {
        if (!ContainerManager.checkContainerStatus(container.name).first) return true
        uninstallState = UninstallState.InProgress(container.name, string(R.string.stopping_container))
        val stopped = ContainerOperationExecutor.executeCommand(ContainerCommandBuilder.buildStopCommand(container), "stop", logger)
        if (!stopped) {
            uninstallState = UninstallState.Idle
            logger.e(string(R.string.failed_to_stop_container, container.name))
            return false
        }
        // `stop` returns when init has exited, but the monitor unmounts the rootfs after
        // that. The uninstall removes that rootfs and the sparse migrate moves it, so
        // give the teardown a second; the export only reads and loses nothing by waiting.
        delay(1000)
        return true
    }

    // ── Export ────────────────────────────────────────────────────────────────
    suspend fun executeExport(
        container: ContainerInfo,
        outputUri: Uri,
        includeEnv: Boolean,
        onError: (String) -> Unit,
    ) {
        runningOperationContainer = container.name
        val logs = logsFor(container.name)
        logs.clear()
        prefsManager.clearContainerLogs(container.name)

        val logger = ViewModelLogger { level, message -> logs.add(level to message) }

        var scriptFile: File? = null
        var tempArchive: File? = null
        try {
            if (!stopIfRunning(container, logger)) {
                onError(string(R.string.failed_to_stop_container, container.name))
                return
            }
            uninstallState = UninstallState.Idle

            logs.clear()
            showLogViewerFor = container.name
            logger.i(string(R.string.starting_export))

            val deployed = ContainerInstaller.deployAsset(appContext, "export_container.sh")
            scriptFile = deployed

            // Write straight into the picked document when its provider exposes a file.
            // Only a pipe-backed one (cloud storage) still goes through cacheDir.
            val output = FilePickerUtils.realPath(appContext, outputUri, "w")
                ?: File("${appContext.cacheDir}/${container.name}_export_tmp.tar.gz")
                    .also { it.delete(); tempArchive = it }.absolutePath

            val cmd = "${ContainerCommandBuilder.quote(deployed.absolutePath)} " +
                "${ContainerCommandBuilder.quote(container.name)} " +
                ContainerCommandBuilder.quote(output) +
                if (includeEnv) " --with-env" else ""
            val success = ContainerOperationExecutor.executeCommand(
                command = cmd,
                operation = "export",
                logger = logger,
                skipHeader = true,
                operationCompletedMessage = string(R.string.operation_completed_success)
            )

            val staged = tempArchive
            if (!success) {
                logger.e(string(R.string.export_container_failed, container.name))
                onError(string(R.string.export_container_failed, container.name))
            } else if (staged != null && staged.length() > 0) {
                logger.i("Writing archive to destination...")
                withContext(Dispatchers.IO) {
                    appContext.contentResolver.openOutputStream(outputUri)?.use { out ->
                        staged.inputStream().use { it.copyTo(out) }
                    }
                }
                logger.i("Done! Archive written successfully.")
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
        val logger = ViewModelLogger { _, message -> collectedLogs.add(message) }

        try {
            if (!stopIfRunning(container, logger)) {
                uninstallLogsDialog = collectedLogs
                onRefresh()
                return
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
                ContainerUsersManager.clearCache(container.name)
                ContainerDiskUsageManager.forget(container.rootfsPath)
                prefsManager.forgetContainer(container.name)
                containerLogs = containerLogs - container.name
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

        val logger = ViewModelLogger { level, message -> logs.add(level to message) }

        try {
            if (operation == "stop" || operation == "restart") {
                TerminalSessionService.stopContainerSessions(appContext, container.name)
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
                logger.e("")
                logger.e(string(R.string.operation_failed))
                onFailureSnackbar(string(R.string.failure_in_operation, operation, container.name))
                onRefresh()
                SystemInfoManager.refreshSELinuxStatus()
            } else {
                onRefresh()
                SystemInfoManager.refreshSELinuxStatus()
            }
        } catch (e: Exception) {
            logger.e("Error: ${e.message}")
            logger.e(e.stackTraceToString())
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
        container: ContainerInfo,
        op: SparseOp,
        sizeGb: Int,
        onRefresh: () -> Unit,
    ) {
        runningOperationContainer = container.name
        val logs = logsFor(container.name)
        logs.clear()
        prefsManager.clearContainerLogs(container.name)

        val logger = ViewModelLogger { level, message -> logs.add(level to message) }

        var scriptFile: File? = null
        try {
            if (!stopIfRunning(container, logger)) return
            uninstallState = UninstallState.Idle

            logs.clear()
            showLogViewerFor = container.name

            val deployedFile = ContainerInstaller.deployAsset(appContext, "sparsemgr.sh")
            scriptFile = deployedFile

            // container.config is the only record of where the rootfs actually is; a
            // container installed to a custom location cannot be derived from its name.
            val rootfsPath = ContainerManager.readRootfsPath(container.name)
            if (rootfsPath == null) {
                logger.e("Could not read rootfs_path from ${container.name}'s container.config")
                return
            }
            val rootfsParent = rootfsPath.substringBeforeLast('/')
            val qScript = ContainerCommandBuilder.quote(deployedFile.absolutePath)

            val cmd = when (op) {
                SparseOp.MIGRATE -> {
                    // sparsemgr derives <dir>/rootfs and <dir>/rootfs.img from -d, so it
                    // wants the rootfs's own parent, not the container directory. Refuse
                    // anything not laid out that way rather than pointing it at a path
                    // that does not exist.
                    if (rootfsPath.substringAfterLast('/') != "rootfs") {
                        logger.e("Rootfs at $rootfsPath is not a directory-mode rootfs; cannot migrate.")
                        return
                    }
                    logger.i(string(R.string.starting_migration))
                    "$qScript -d ${ContainerCommandBuilder.quote(rootfsParent)} migrate $sizeGb"
                }
                SparseOp.RESIZE -> {
                    if (rootfsPath.substringAfterLast('/') != "rootfs.img") {
                        logger.e("Rootfs at $rootfsPath is not a sparse image; cannot resize.")
                        return
                    }
                    logger.i(string(R.string.starting_resizing))
                    "$qScript -i ${ContainerCommandBuilder.quote(rootfsPath)} resize $sizeGb --yes"
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
                val updatedConfig = if (op == SparseOp.MIGRATE) {
                    container.copy(
                        useSparseImage = true,
                        sparseImageSizeGB = sizeGb,
                        // sparsemgr writes the image beside the rootfs it replaced.
                        rootfsPath = "$rootfsParent/rootfs.img"
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
