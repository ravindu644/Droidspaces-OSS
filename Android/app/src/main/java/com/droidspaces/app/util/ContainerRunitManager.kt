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

object ContainerRunitManager {
    private const val TAG = "ContainerRunitManager"
    private const val COMMAND_TIMEOUT_MS = 10_000L

    private var scriptBase64: String? = null

    enum class ServiceStatus {
        ENABLED_RUNNING,
        ENABLED_STOPPED,
        DISABLED_STOPPED,
        ABNORMAL
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
                val script = context.assets.open("chkrunit.sh").bufferedReader().readText()
                scriptBase64 = Base64.encodeToString(script.toByteArray(), Base64.NO_WRAP)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to load chkrunit.sh from assets", e)
            }
        }
    }

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

    suspend fun executeRunitCommand(
        containerName: String,
        command: String
    ): CommandResult = withContext(Dispatchers.IO) {
        val fullCmd = "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run '$command 2>&1'"
        val executor = Executors.newSingleThreadExecutor()
        try {
            val future = executor.submit<Shell.Result> { Shell.cmd(fullCmd).exec() }
            try {
                val result = future.get(COMMAND_TIMEOUT_MS, TimeUnit.MILLISECONDS)
                CommandResult(exitCode = result.code, output = result.out, error = result.err)
            } catch (e: TimeoutException) {
                future.cancel(true)
                Log.e(TAG, "Command timed out: $command")
                CommandResult(
                    exitCode = 124,
                    output = listOf("Command timed out after ${COMMAND_TIMEOUT_MS / 1000} seconds"),
                    error = emptyList()
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error executing: $command", e)
            CommandResult(exitCode = 1, output = emptyList(), error = listOf(e.message ?: "Unknown error"))
        } finally {
            executor.shutdownNow()
        }
    }

    suspend fun isRunitAvailable(containerName: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'command -v sv'"
            ).exec()
            result.isSuccess && result.out.any { it.trim().isNotEmpty() }
        } catch (e: Exception) {
            false
        }
    }

    suspend fun getAllServices(containerName: String): List<ServiceInfo> = coroutineScope {
        try {
            val runningDeferred = async { runScript(containerName, "--running") }
            val enabledDeferred = async { runScript(containerName, "--enabled") }
            val disabledDeferred = async { runScript(containerName, "--disabled") }

            val (runningSuccess, runningLines) = runningDeferred.await()
            val (enabledSuccess, enabledLines) = enabledDeferred.await()
            val (disabledSuccess, disabledLines) = disabledDeferred.await()

            val runningServices = mutableMapOf<String, String>()
            if (runningSuccess) {
                runningLines.forEach { line ->
                    val parts = line.split("|", limit = 2)
                    if (parts.isNotEmpty() && parts[0].isNotBlank()) {
                        runningServices[parts[0].trim()] = parts.getOrElse(1) { "" }.trim()
                    }
                }
            }

            val enabledSet = mutableMapOf<String, String>()
            if (enabledSuccess) {
                enabledLines.forEach { line ->
                    val parts = line.split("|", limit = 2)
                    if (parts.isNotEmpty() && parts[0].isNotBlank()) {
                        enabledSet[parts[0].trim()] = parts.getOrElse(1) { "" }.trim()
                    }
                }
            }

            val disabledSet = mutableMapOf<String, String>()
            if (disabledSuccess) {
                disabledLines.forEach { line ->
                    val parts = line.split("|", limit = 2)
                    if (parts.isNotEmpty() && parts[0].isNotBlank()) {
                        disabledSet[parts[0].trim()] = parts.getOrElse(1) { "" }.trim()
                    }
                }
            }

            val allServices = mutableMapOf<String, ServiceInfo>()

            enabledSet.forEach { (name, enabledDesc) ->
                val isRunning = runningServices.containsKey(name)
                val runningDesc = runningServices[name].orEmpty()
                val desc = listOf(enabledDesc, runningDesc)
                    .filter { it.isNotBlank() }
                    .distinct()
                    .joinToString(" | ")
                allServices[name] = ServiceInfo(
                    name = name,
                    description = desc,
                    status = if (isRunning) ServiceStatus.ENABLED_RUNNING else ServiceStatus.ENABLED_STOPPED,
                    isEnabled = true,
                    isRunning = isRunning
                )
            }

            disabledSet.forEach { (name, desc) ->
                val isRunning = runningServices.containsKey(name)
                allServices[name] = ServiceInfo(
                    name = name,
                    description = desc,
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

    private suspend fun runSV(containerName: String, serviceName: String, buildCommand: (String) -> String): CommandResult {
        if (!ServiceManagerBase.isSafeServiceName(serviceName)) {
            return CommandResult(exitCode = 2, output = emptyList(), error = listOf("Invalid service name: $serviceName"))
        }
        return executeRunitCommand(containerName, buildCommand(serviceName))
    }

    suspend fun startService(containerName: String, serviceName: String) =
        runSV(containerName, serviceName) { "sv up $it" }

    suspend fun stopService(containerName: String, serviceName: String) =
        runSV(containerName, serviceName) { "sv down $it" }

    suspend fun restartService(containerName: String, serviceName: String) =
        runSV(containerName, serviceName) { "sv restart $it" }

    suspend fun enableService(containerName: String, serviceName: String) =
        runSV(containerName, serviceName) { service ->
            """
            ACTIVE_DIR=""
            for dir in /etc/service /service /var/service; do
                if [ -d "${'$'}dir" ]; then
                    ACTIVE_DIR="${'$'}dir"
                    break
                fi
            done

            SV_DIR=""
            for dir in /etc/sv /etc/runit/sv /usr/local/etc/sv; do
                if [ -d "${'$'}dir" ]; then
                    SV_DIR="${'$'}dir"
                    break
                fi
            done

            ln -s "${'$'}SV_DIR/$service" "${'$'}ACTIVE_DIR/$service"
            """.trimIndent()
        }

    suspend fun disableService(containerName: String, serviceName: String) =
        runSV(containerName, serviceName) { service ->
            """
            ACTIVE_DIR=""
            for dir in /etc/service /service /var/service; do
                if [ -d "${'$'}dir" ]; then
                    ACTIVE_DIR="${'$'}dir"
                    break
                fi
            done

            sv down $service
            rm "${'$'}ACTIVE_DIR/$service"
            """.trimIndent()
        }

    suspend fun toggleDefaultState(containerName: String, serviceName: String) =
        runSV(containerName, serviceName) { service ->
            """
            SV_DIR=""
            for dir in /etc/sv /etc/runit/sv /usr/local/etc/sv; do
                if [ -d "${'$'}dir" ]; then
                    SV_DIR="${'$'}dir"
                    break
                fi
            done

            if [ -f ${'$'}SV_DIR/$service/down ]; then
                rm ${'$'}SV_DIR/$service/down
            else
                touch ${'$'}SV_DIR/$service/down
            fi
            """.trimIndent()
        }
}
