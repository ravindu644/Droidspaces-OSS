package com.droidspaces.app.util

import android.content.Context
import android.util.Base64
import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/**
 * OpenWrt/procd service manager for containers.
 *
 * Uses a base64-encoded chkprocd.sh helper for service discovery and
 * /etc/init.d/<service> for procd-compatible service actions.
 */
object ContainerProcdManager {
    private const val TAG = "ContainerProcdManager"
    private const val COMMAND_TIMEOUT_MS = 15_000L

    private val safeServiceName = Regex("^[A-Za-z0-9_.+@-]+$")
    private val safeActions = setOf("start", "stop", "restart", "reload", "enable", "disable", "status")

    private var scriptBase64: String? = null

    /**
     * procd does not expose systemd's masked/static concepts. Runtime state can
     * also be unavailable for some init scripts, so UNKNOWN is explicit.
     */
    enum class ServiceStatus {
        ENABLED_RUNNING,
        ENABLED_STOPPED,
        DISABLED_STOPPED,
        ABNORMAL,
        UNKNOWN
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
        UNKNOWN,
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
                val script = context.assets.open("chkprocd.sh").bufferedReader().readText()
                scriptBase64 = Base64.encodeToString(script.toByteArray(), Base64.NO_WRAP)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to load chkprocd.sh from assets", e)
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
                Log.e(TAG, "Error running procd query script", e)
                Pair(false, emptyList())
            }
        }

    /**
     * Check if the container looks like an OpenWrt/procd system.
     *
     * Do not use /etc/init.d alone: OpenRC and SysV-style systems also expose
     * that directory. /etc/openwrt_release is the distro-level discriminator;
     * procd/ubus/service binaries confirm that the OpenWrt service stack exists.
     */
    suspend fun isProcdAvailable(containerName: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val cmd = "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run '[ -f /etc/openwrt_release ] && { [ -x /sbin/procd ] || [ -x /sbin/ubusd ] || command -v ubus >/dev/null 2>&1 || command -v service >/dev/null 2>&1; }'"
            Shell.cmd(cmd).exec().isSuccess
        } catch (e: Exception) {
            Log.e(TAG, "Error checking procd availability", e)
            false
        }
    }

    suspend fun getAllServices(containerName: String): List<ServiceInfo> = withContext(Dispatchers.IO) {
        try {
            val (success, lines) = runScript(containerName, "--all")
            if (!success) return@withContext emptyList()

            lines.mapNotNull { line ->
                val parts = line.split("|", limit = 4)
                if (parts.size < 4) return@mapNotNull null

                val name = parts[0].trim()
                if (!isSafeServiceName(name)) return@mapNotNull null

                val description = parts[1].trim()
                val enabled = parts[2].trim() == "enabled"
                val runningState = parts[3].trim()
                val running = runningState == "running"

                val status = when {
                    runningState == "unknown" -> ServiceStatus.UNKNOWN
                    enabled && running -> ServiceStatus.ENABLED_RUNNING
                    enabled && !running -> ServiceStatus.ENABLED_STOPPED
                    !enabled && running -> ServiceStatus.ABNORMAL
                    else -> ServiceStatus.DISABLED_STOPPED
                }

                ServiceInfo(
                    name = name,
                    description = description,
                    status = status,
                    isEnabled = enabled,
                    isRunning = running
                )
            }.sortedBy { it.name }
        } catch (e: Exception) {
            Log.e(TAG, "Error fetching procd services", e)
            emptyList()
        }
    }

    fun filterServices(services: List<ServiceInfo>, filter: ServiceFilter): List<ServiceInfo> {
        return when (filter) {
            ServiceFilter.ALL -> services
            ServiceFilter.RUNNING -> services.filter { it.isRunning && it.isEnabled }
            ServiceFilter.ENABLED -> services.filter { it.isEnabled && !it.isRunning && it.status != ServiceStatus.UNKNOWN }
            ServiceFilter.DISABLED -> services.filter { !it.isEnabled && !it.isRunning && it.status != ServiceStatus.UNKNOWN }
            ServiceFilter.ABNORMAL -> services.filter { it.isRunning && !it.isEnabled }
            ServiceFilter.UNKNOWN -> services.filter { it.status == ServiceStatus.UNKNOWN }
        }.sortedWith(compareByDescending<ServiceInfo> { it.isRunning }.thenBy { it.name })
    }

    private fun isSafeServiceName(serviceName: String): Boolean = safeServiceName.matches(serviceName)

    private fun validateServiceAction(serviceName: String, action: String): CommandResult? {
        if (!isSafeServiceName(serviceName)) {
            return CommandResult(
                exitCode = 2,
                output = emptyList(),
                error = listOf("Invalid OpenWrt service name: $serviceName")
            )
        }
        if (action !in safeActions) {
            return CommandResult(
                exitCode = 2,
                output = emptyList(),
                error = listOf("Unsupported OpenWrt service action: $action")
            )
        }
        return null
    }

    private suspend fun executeProcdCommand(
        containerName: String,
        serviceName: String,
        action: String
    ): CommandResult = withContext(Dispatchers.IO) {
        validateServiceAction(serviceName, action)?.let { return@withContext it }

        val command = "/etc/init.d/$serviceName $action"
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

    suspend fun startService(containerName: String, serviceName: String) =
        executeProcdCommand(containerName, serviceName, "start")

    suspend fun stopService(containerName: String, serviceName: String) =
        executeProcdCommand(containerName, serviceName, "stop")

    suspend fun restartService(containerName: String, serviceName: String) =
        executeProcdCommand(containerName, serviceName, "restart")

    suspend fun reloadService(containerName: String, serviceName: String) =
        executeProcdCommand(containerName, serviceName, "reload")

    suspend fun enableService(containerName: String, serviceName: String) =
        executeProcdCommand(containerName, serviceName, "enable")

    suspend fun disableService(containerName: String, serviceName: String) =
        executeProcdCommand(containerName, serviceName, "disable")
}
