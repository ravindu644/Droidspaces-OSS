package com.droidspaces.app.util

import android.content.Context
import android.util.Base64
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext

/**
 * OpenRC service manager for containers.
 * Mirrors ContainerSystemdManager - uses base64-encoded chkopenrc.sh for queries,
 * and rc-service / rc-update for management actions.
 */
object ContainerOpenRCManager {
    private const val TAG = "ContainerOpenRCManager"
    private const val COMMAND_TIMEOUT_MS = 10_000L

    private var scriptBase64: String? = null

    /**
     * OpenRC has no masked/static concepts - only running/enabled/disabled.
     */
    enum class ServiceStatus {
        ENABLED_RUNNING,   // Green
        ENABLED_STOPPED,   // Yellow
        DISABLED_STOPPED,  // Red
        ABNORMAL           // Orange - running but not enabled
    }

    data class ServiceInfo(
        val name: String,
        val description: String,
        val status: ServiceStatus,
        val isEnabled: Boolean,
        val isRunning: Boolean
    )

    enum class ServiceFilter {
        RUNNING,
        ENABLED,
        DISABLED,
        ABNORMAL,
        ALL
    }

    data class CommandResult(
        val exitCode: Int,
        val output: List<String>,
        val error: List<String>
    ) {
        val isSuccess: Boolean get() = exitCode == 0
    }

    fun initialize(context: Context) {
        if (scriptBase64 == null) {
            try {
                val script = context.assets.open("chkopenrc.sh").bufferedReader().readText()
                scriptBase64 = Base64.encodeToString(script.toByteArray(), Base64.NO_WRAP)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to load chkopenrc.sh from assets", e)
            }
        }
    }

    private suspend fun runScript(containerName: String, rootless: Boolean, flag: String): Pair<Boolean, List<String>> =
        withContext(Dispatchers.IO) {
            val b64 = scriptBase64 ?: return@withContext Pair(false, emptyList())
            try {
                val output = ContainerRuntime.runInContainer(containerName, rootless, "echo $b64 | base64 -d | sh -s -- $flag")
                if (output.startsWith("ERROR:")) return@withContext Pair(false, emptyList())
                Pair(true, output.lines().filter { it.isNotBlank() })
            } catch (e: Exception) {
                Log.e(TAG, "Error running script", e)
                Pair(false, emptyList())
            }
        }

    suspend fun executeRCCommand(
        containerName: String,
        command: String,
        rootless: Boolean = false
    ): CommandResult = withContext(Dispatchers.IO) {
        try {
            val output = ContainerRuntime.runInContainer(containerName, rootless, "$command 2>&1")
            if (output.startsWith("ERROR:")) {
                CommandResult(exitCode = 1, output = emptyList(), error = listOf(output))
            } else {
                val lines = output.lines().filter { it.isNotBlank() }
                CommandResult(exitCode = 0, output = lines, error = emptyList())
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error executing: $command", e)
            CommandResult(exitCode = 1, output = emptyList(), error = listOf(e.message ?: "Unknown error"))
        }
    }

    /**
     * Check if OpenRC is available in the container.
     */
    suspend fun isOpenRCAvailable(containerName: String, rootless: Boolean = false): Boolean = withContext(Dispatchers.IO) {
        try {
            val output = ContainerRuntime.runInContainer(containerName, rootless, "command -v rc-service")
            output.isNotBlank() && !output.startsWith("ERROR:")
        } catch (e: Exception) {
            false
        }
    }

    suspend fun getAllServices(containerName: String, rootless: Boolean = false): List<ServiceInfo> = coroutineScope {
        try {
            val runningDeferred = async { runScript(containerName, rootless, "--running") }
            val enabledDeferred = async { runScript(containerName, rootless, "--enabled") }
            val disabledDeferred = async { runScript(containerName, rootless, "--disabled") }

            val (runningSuccess, runningLines) = runningDeferred.await()
            val (enabledSuccess, enabledLines) = enabledDeferred.await()
            val (disabledSuccess, disabledLines) = disabledDeferred.await()

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

            val enabledSet = if (enabledSuccess) enabledLines.map { it.trim() }.filter { it.isNotBlank() }.toSet() else emptySet()
            val disabledSet = if (disabledSuccess) disabledLines.map { it.trim() }.filter { it.isNotBlank() }.toSet() else emptySet()

            val allServices = mutableMapOf<String, ServiceInfo>()

            enabledSet.forEach { name ->
                val isRunning = runningServices.containsKey(name)
                allServices[name] = ServiceInfo(
                    name = name,
                    description = runningServices[name] ?: "",
                    status = if (isRunning) ServiceStatus.ENABLED_RUNNING else ServiceStatus.ENABLED_STOPPED,
                    isEnabled = true,
                    isRunning = isRunning
                )
            }

            disabledSet.forEach { name ->
                val isRunning = runningServices.containsKey(name)
                allServices[name] = ServiceInfo(
                    name = name,
                    description = runningServices[name] ?: "",
                    status = if (isRunning) ServiceStatus.ABNORMAL else ServiceStatus.DISABLED_STOPPED,
                    isEnabled = false,
                    isRunning = isRunning
                )
            }

            // Any running services not in enabled/disabled lists
            runningServices.forEach { (name, desc) ->
                if (!allServices.containsKey(name)) {
                    allServices[name] = ServiceInfo(
                        name = name,
                        description = desc,
                        status = ServiceStatus.ABNORMAL,
                        isEnabled = false,
                        isRunning = true
                    )
                }
            }

            allServices.values.sortedBy { it.name }
        } catch (e: Exception) {
            Log.e(TAG, "Error fetching services", e)
            emptyList()
        }
    }

    fun filterServices(services: List<ServiceInfo>, filter: ServiceFilter): List<ServiceInfo> {
        return when (filter) {
            ServiceFilter.ALL -> services
            ServiceFilter.RUNNING -> services.filter { it.isRunning && it.isEnabled }
            ServiceFilter.ENABLED -> services.filter { it.isEnabled && !it.isRunning }
            ServiceFilter.DISABLED -> services.filter { !it.isEnabled && !it.isRunning }
            ServiceFilter.ABNORMAL -> services.filter { it.isRunning && !it.isEnabled }
        }.sortedWith(compareByDescending<ServiceInfo> { it.isRunning }.thenBy { it.name })
    }

    suspend fun startService(containerName: String, serviceName: String) =
        executeRCCommand(containerName, "rc-service $serviceName start")

    suspend fun stopService(containerName: String, serviceName: String) =
        executeRCCommand(containerName, "rc-service $serviceName stop")

    suspend fun restartService(containerName: String, serviceName: String) =
        executeRCCommand(containerName, "rc-service $serviceName restart")

    suspend fun enableService(containerName: String, serviceName: String) =
        executeRCCommand(containerName, "rc-update add $serviceName default")

    suspend fun disableService(containerName: String, serviceName: String) =
        executeRCCommand(containerName, "rc-update del $serviceName")
}
