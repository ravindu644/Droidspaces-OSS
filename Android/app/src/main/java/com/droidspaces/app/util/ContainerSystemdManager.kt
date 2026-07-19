package com.droidspaces.app.util

import android.content.Context
import android.util.Base64
import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/**
 * Feature-rich systemd manager for containers.
 * Uses base64-encoded script streaming for reliable command execution.
 */
object ContainerSystemdManager {
    private const val TAG = "ContainerSystemdManager"
    private const val COMMAND_TIMEOUT_MS = 10_000L // 10 seconds timeout

    // Base64-encoded chksystemd.sh script
    private var scriptBase64: String? = null

    /**
     * Service status with color coding:
     * - ENABLED_RUNNING: Green
     * - ENABLED_STOPPED: Yellow
     * - DISABLED_STOPPED: Red
     * - DISABLED_RUNNING: Orange
     * - MASKED: White
     */
    enum class ServiceStatus {
        ENABLED_RUNNING,
        ENABLED_STOPPED,
        DISABLED_STOPPED,
        STATIC,
        ABNORMAL,
        MASKED
    }

    data class ServiceInfo(
        val name: String,
        val description: String,
        val status: ServiceStatus,
        val isEnabled: Boolean,
        val isRunning: Boolean,
        val isMasked: Boolean,
        val isStatic: Boolean
    )

    enum class ServiceFilter {
        RUNNING,    // Enabled + Running (healthy running)
        ENABLED,    // Enabled but NOT running
        DISABLED,   // Disabled and NOT running
        ABNORMAL,   // Running but not enabled, static, or masked
        STATIC,     // Explicitly static services
        MASKED,
        ALL
    }

    /**
     * Command result with exit code for proper handling.
     */
    data class CommandResult(
        val exitCode: Int,
        val output: List<String>,
        val error: List<String>
    ) {
        val isSuccess: Boolean get() = exitCode == 0
        val hasLogs: Boolean get() = exitCode > 1
    }

    /**
     * Initialize the script from assets.
     */
    fun initialize(context: Context) {
        if (scriptBase64 == null) {
            try {
                val script = context.assets.open("chksystemd.sh").bufferedReader().readText()
                scriptBase64 = Base64.encodeToString(script.toByteArray(), Base64.NO_WRAP)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to load chksystemd.sh from assets", e)
            }
        }
    }

    /**
     * Run the chksystemd.sh script with given flag.
     */
    private suspend fun runScript(containerName: String, flag: String): Pair<Boolean, List<String>> =
        withContext(Dispatchers.IO) {
            val b64 = scriptBase64 ?: return@withContext Pair(false, emptyList())

            try {
                val cmd = "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'echo $b64 | base64 -d | sh -s -- $flag'"
                val result = Shell.cmd(cmd).exec()
                Pair(result.isSuccess, result.out)
            } catch (e: Exception) {
                Log.e(TAG, "Error running script", e)
                Pair(false, emptyList())
            }
        }

    /**
     * Execute a systemctl command and return result with exit code.
     * Times out after 10 seconds if command hangs.
     */
    suspend fun executeSystemctlCommand(
        containerName: String,
        command: String
    ): CommandResult = withContext(Dispatchers.IO) {
        val fullCmd = "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'systemctl $command 2>&1'"
        val executor = Executors.newSingleThreadExecutor()

        try {
            val future = executor.submit<Shell.Result> {
                Shell.cmd(fullCmd).exec()
            }

            try {
                val result = future.get(COMMAND_TIMEOUT_MS, TimeUnit.MILLISECONDS)
                CommandResult(
                    exitCode = result.code,
                    output = result.out,
                    error = result.err
                )
            } catch (e: TimeoutException) {
                future.cancel(true)
                Log.e(TAG, "Command timed out: systemctl $command")
                CommandResult(
                    exitCode = 124, // Standard timeout exit code
                    output = listOf("Command timed out after ${COMMAND_TIMEOUT_MS / 1000} seconds"),
                    error = emptyList()
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error executing systemctl $command", e)
            CommandResult(
                exitCode = 1,
                output = emptyList(),
                error = listOf(e.message ?: "Unknown error")
            )
        } finally {
            executor.shutdownNow()
        }
    }

    /**
     * Check if systemd is available.
     */
    suspend fun isSystemdAvailable(containerName: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'command -v systemctl'"
            ).exec()
            result.isSuccess && result.out.any { it.trim().isNotEmpty() }
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Get all services with complete status.
     */
    suspend fun getAllServices(containerName: String): List<ServiceInfo> = coroutineScope {
        try {
            // Fetch in parallel
            val runningDeferred = async { runScript(containerName, "--running") }
            val enabledDeferred = async { runScript(containerName, "--enabled") }
            val disabledDeferred = async { runScript(containerName, "--disabled") }
            val maskedDeferred = async { runScript(containerName, "--masked") }
            val staticDeferred = async { runScript(containerName, "--static") }

            val (runningSuccess, runningLines) = runningDeferred.await()
            val (enabledSuccess, enabledLines) = enabledDeferred.await()
            val (disabledSuccess, disabledLines) = disabledDeferred.await()
            val (maskedSuccess, maskedLines) = maskedDeferred.await()
            val (staticSuccess, staticLines) = staticDeferred.await()

            // Parse running services (NAME|DESCRIPTION)
            val runningServices = mutableMapOf<String, String>()
            if (runningSuccess) {
                runningLines.forEach { line ->
                    val parts = line.split("|", limit = 2)
                    if (parts.isNotEmpty() && parts[0].isNotBlank()) {
                        runningServices[parts[0].trim()] = parts.getOrElse(1) { "" }.trim()
                    }
                }
            }

            // Parse other services (NAME only)
            val enabledSet = if (enabledSuccess) enabledLines.map { it.trim() }.filter { it.isNotBlank() }.toSet() else emptySet()
            val disabledSet = if (disabledSuccess) disabledLines.map { it.trim() }.filter { it.isNotBlank() }.toSet() else emptySet()
            val maskedSet = if (maskedSuccess) maskedLines.map { it.trim() }.filter { it.isNotBlank() }.toSet() else emptySet()
            val staticSet = if (staticSuccess) staticLines.map { it.trim() }.filter { it.isNotBlank() }.toSet() else emptySet()

            // Build unified list
            val allServices = mutableMapOf<String, ServiceInfo>()

            // Enabled services
            enabledSet.forEach { name ->
                val isRunning = runningServices.containsKey(name)
                allServices[name] = ServiceInfo(
                    name = name,
                    description = runningServices[name] ?: "",
                    status = if (isRunning) ServiceStatus.ENABLED_RUNNING else ServiceStatus.ENABLED_STOPPED,
                    isEnabled = true,
                    isRunning = isRunning,
                    isMasked = false,
                    isStatic = false
                )
            }

            // Static services
            staticSet.forEach { name ->
                val isRunning = runningServices.containsKey(name)
                allServices[name] = ServiceInfo(
                    name = name,
                    description = runningServices[name] ?: "",
                    status = ServiceStatus.STATIC,
                    isEnabled = false,
                    isRunning = isRunning,
                    isMasked = false,
                    isStatic = true
                )
            }

            // Disabled services
            disabledSet.forEach { name ->
                val isRunning = runningServices.containsKey(name)
                allServices[name] = ServiceInfo(
                    name = name,
                    description = runningServices[name] ?: "",
                    status = if (isRunning) ServiceStatus.ABNORMAL else ServiceStatus.DISABLED_STOPPED,
                    isEnabled = false,
                    isRunning = isRunning,
                    isMasked = false,
                    isStatic = false
                )
            }

            // Masked services
            maskedSet.forEach { name ->
                allServices[name] = ServiceInfo(
                    name = name,
                    description = "",
                    status = ServiceStatus.MASKED,
                    isEnabled = false,
                    isRunning = false,
                    isMasked = true,
                    isStatic = false
                )
            }

            // Remaining running services (categorized as Abnormal if not missed above)
            runningServices.forEach { (name, desc) ->
                if (!allServices.containsKey(name)) {
                    allServices[name] = ServiceInfo(
                        name = name,
                        description = desc,
                        status = ServiceStatus.ABNORMAL,
                        isEnabled = false,
                        isRunning = true,
                        isMasked = false,
                        isStatic = false
                    )
                }
            }

            allServices.values.sortedBy { it.name }
        } catch (e: Exception) {
            Log.e(TAG, "Error fetching services", e)
            emptyList()
        }
    }

    /**
     * Filter services - each category is mutually exclusive (except ALL).
     */
    fun filterServices(services: List<ServiceInfo>, filter: ServiceFilter): List<ServiceInfo> {
        return when (filter) {
            ServiceFilter.ALL -> services
            ServiceFilter.RUNNING -> services.filter { it.isRunning && it.isEnabled && !it.isMasked }  // Healthy running
            ServiceFilter.STATIC -> services.filter { it.isStatic }
            ServiceFilter.ABNORMAL -> services.filter { it.isRunning && !it.isEnabled && !it.isStatic && !it.isMasked } // Running but not enabled/static
            ServiceFilter.ENABLED -> services.filter { it.isEnabled && !it.isRunning && !it.isMasked } // Enabled but stopped
            ServiceFilter.DISABLED -> services.filter { !it.isEnabled && !it.isRunning && !it.isMasked && !it.isStatic } // Disabled and stopped
            ServiceFilter.MASKED -> services.filter { it.isMasked }
        }.sortedWith(compareByDescending<ServiceInfo> { it.isRunning }.thenBy { it.name })
    }

    // Systemctl commands. Every service name is validated against the shared
    // allow-list (ServiceManagerBase) before it is interpolated into the
    // host-root `run 'systemctl ...'` payload — see FINDINGS_APP_VULN V1.
    private suspend fun runSystemctl(containerName: String, action: String, serviceName: String): CommandResult {
        if (!ServiceManagerBase.isSafeServiceName(serviceName)) {
            return CommandResult(exitCode = 2, output = emptyList(), error = listOf("Invalid unit name: $serviceName"))
        }
        return executeSystemctlCommand(containerName, "$action $serviceName")
    }

    suspend fun startService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "start --no-block", serviceName)

    suspend fun stopService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "stop", serviceName)

    suspend fun restartService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "restart", serviceName)

    suspend fun enableService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "enable", serviceName)

    suspend fun disableService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "disable", serviceName)

    suspend fun maskService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "mask", serviceName)

    suspend fun unmaskService(containerName: String, serviceName: String) =
        runSystemctl(containerName, "unmask", serviceName)
}
