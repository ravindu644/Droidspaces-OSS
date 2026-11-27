package com.droidspaces.app.util

import android.content.Context
import android.util.Base64
import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.coroutines.resume

/** How a service stands, the union over the four init systems. */
enum class ServiceStatus { ENABLED_RUNNING, ENABLED_STOPPED, DISABLED_STOPPED, ABNORMAL, STATIC, MASKED, UNKNOWN }

data class ServiceInfo(
    val name: String,
    val description: String,
    val status: ServiceStatus,
    val isEnabled: Boolean,
    val isRunning: Boolean,
    val isMasked: Boolean = false,
    val isStatic: Boolean = false,
)

data class CommandResult(val exitCode: Int, val output: List<String>, val error: List<String>) {
    val isSuccess: Boolean get() = exitCode == 0
}

/**
 * What the four init-system managers share: a query script shipped as an asset
 * and streamed into the container base64-encoded, a bounded runner for the
 * management commands, the availability probe, and the list build from the
 * NAME|DESCRIPTION lines the scripts print. Every service name that reaches a
 * command passes [isSafeServiceName] first: it was discovered inside a rootfs
 * that may be hostile and lands inside the single-quoted `run '...'` payload of
 * a host-root shell command, where one quote would break out of the payload.
 */
abstract class ContainerInitManager(
    private val scriptAsset: String,
    private val probeCommand: String,
    private val timeoutMs: Long = 10_000L,
) {
    private val tag = javaClass.simpleName
    private var scriptBase64: String? = null

    companion object {
        // Letters, digits and the punctuation real unit names use (systemd templates
        // carry '@', some units ':'). No quotes, '$', ';', '|', '&', backticks or
        // whitespace, so a name that passes can be embedded literally.
        private val SAFE_SERVICE_NAME = Regex("^[A-Za-z0-9_.@:+-]+$")

        fun isSafeServiceName(name: String): Boolean = SAFE_SERVICE_NAME.matches(name)
    }

    fun initialize(context: Context) {
        if (scriptBase64 == null) {
            try {
                scriptBase64 = Base64.encodeToString(context.assets.open(scriptAsset).use { it.readBytes() }, Base64.NO_WRAP)
            } catch (e: Exception) {
                Log.e(tag, "Failed to load $scriptAsset from assets", e)
            }
        }
    }

    private fun inContainer(containerName: String, command: String) =
        "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run '$command'"

    /**
     * [command] inside the container, through the shared root shell, for as long as
     * it takes. A caller that stops waiting leaves the exec alone: libsu runs it on
     * its own worker and resumes us from there, and nothing interrupts that thread.
     * An interrupt mid-command returns early but leaves libsu's stream gobbler
     * reading, and every later command in the process then races it for stdout.
     */
    protected suspend fun exec(containerName: String, command: String): CommandResult =
        suspendCancellableCoroutine { cont ->
            Shell.cmd(inContainer(containerName, command)).submit(null) { cont.resume(CommandResult(it.code, it.out, it.err)) }
        }

    /** The query script's output for one flag; false when the script is missing or failed. */
    protected suspend fun runScript(containerName: String, flag: String): Pair<Boolean, List<String>> {
        val b64 = scriptBase64 ?: return Pair(false, emptyList())
        val result = exec(containerName, "echo $b64 | base64 -d | sh -s -- $flag")
        return Pair(result.isSuccess, result.output)
    }

    /** The script's own action on a service, for an init whose script owns the tool paths. */
    protected fun scriptAction(flag: String, name: String): String? =
        scriptBase64?.let { "echo $it | base64 -d | sh -s -- $flag $name" }

    /**
     * A management command, bounded by [timeoutMs] so a hung init tool cannot hang
     * the screen; the caller gets exit code 124 at the deadline while the command
     * itself finishes on its own.
     */
    suspend fun run(containerName: String, command: String): CommandResult =
        withTimeoutOrNull(timeoutMs) { exec(containerName, "$command 2>&1") } ?: run {
            // The first token names the tool; the rest may be a base64 blob logcat would truncate.
            Log.e(tag, "Command timed out: ${command.take(120)}")
            CommandResult(124, listOf("Command timed out after ${timeoutMs / 1000} seconds"), emptyList())
        }

    /** Every action's gate: a name the allow-list rejects never reaches a command. */
    protected suspend fun action(containerName: String, serviceName: String, command: (String) -> String?): CommandResult {
        if (!isSafeServiceName(serviceName))
            return CommandResult(2, emptyList(), listOf("Invalid service name: $serviceName"))
        val cmd = command(serviceName) ?: return CommandResult(1, emptyList(), listOf("$scriptAsset not loaded"))
        return run(containerName, cmd)
    }

    suspend fun isAvailable(containerName: String): Boolean = exec(containerName, probeCommand).isSuccess

    /** NAME|DESCRIPTION lines, or bare names, as a name-to-description map. */
    protected fun pairs(lines: List<String>): Map<String, String> = lines.mapNotNull { line ->
        val parts = line.split("|", limit = 2)
        val name = parts[0].trim()
        if (name.isBlank()) null else name to parts.getOrElse(1) { "" }.trim()
    }.toMap()

    /** One service with the status its flags imply, as every list build decided it. */
    protected fun service(
        name: String, description: String, enabled: Boolean, running: Boolean,
        masked: Boolean = false, static: Boolean = false,
    ) = ServiceInfo(
        name, description,
        when {
            masked -> ServiceStatus.MASKED
            static -> ServiceStatus.STATIC
            enabled -> if (running) ServiceStatus.ENABLED_RUNNING else ServiceStatus.ENABLED_STOPPED
            running -> ServiceStatus.ABNORMAL
            else -> ServiceStatus.DISABLED_STOPPED
        },
        enabled, running, masked, static,
    )
}
