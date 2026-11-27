package com.droidspaces.app.util

import android.util.Base64
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope

/** systemd: chksystemd.sh for the lists, systemctl for everything else. */
object ContainerSystemdManager : ContainerInitManager("chksystemd.sh", "command -v systemctl") {

    /** Parsed `systemctl show` properties, raw `systemctl status` text, and the dependency tree. */
    data class UnitInspection(
        val properties: Map<String, String>,
        val statusText: List<String>,
        val dependencies: List<String>
    )

    suspend fun getAllServices(containerName: String): List<ServiceInfo> = coroutineScope {
        val (running, enabled, disabled, masked, static) = listOf("--running", "--enabled", "--disabled", "--masked", "--static")
            .map { async { runScript(containerName, it) } }.awaitAll()
            .map { (ok, lines) -> if (ok) pairs(lines) else emptyMap() }
        val all = mutableMapOf<String, ServiceInfo>()
        enabled.keys.forEach { all[it] = service(it, running[it] ?: "", enabled = true, running = it in running) }
        static.keys.forEach { all[it] = service(it, running[it] ?: "", enabled = false, running = it in running, static = true) }
        disabled.keys.forEach { all[it] = service(it, running[it] ?: "", enabled = false, running = it in running) }
        masked.keys.forEach { all[it] = service(it, "", enabled = false, running = false, masked = true) }
        running.forEach { (name, desc) -> all.getOrPut(name) { service(name, desc, enabled = false, running = true) } }
        all.values.sortedBy { it.name }
    }

    private suspend fun systemctl(containerName: String, action: String, unit: String) =
        action(containerName, unit) { "systemctl $action $it" }

    suspend fun startService(containerName: String, serviceName: String) = systemctl(containerName, "start --no-block", serviceName)
    suspend fun stopService(containerName: String, serviceName: String) = systemctl(containerName, "stop", serviceName)
    suspend fun restartService(containerName: String, serviceName: String) = systemctl(containerName, "restart", serviceName)
    suspend fun enableService(containerName: String, serviceName: String) = systemctl(containerName, "enable", serviceName)
    suspend fun disableService(containerName: String, serviceName: String) = systemctl(containerName, "disable", serviceName)
    suspend fun maskService(containerName: String, serviceName: String) = systemctl(containerName, "mask", serviceName)
    suspend fun unmaskService(containerName: String, serviceName: String) = systemctl(containerName, "unmask", serviceName)

    /** Any systemctl invocation, bounded like an action; the unit name is the caller's to validate. */
    suspend fun executeSystemctlCommand(containerName: String, command: String): CommandResult =
        run(containerName, "systemctl $command")

    /** Tail of the unit's journal. Empty if the unit name is unsafe or journalctl is missing. */
    suspend fun dumpJournal(containerName: String, unitName: String, lines: Int): List<String> =
        if (isSafeServiceName(unitName)) exec(containerName, "journalctl -u $unitName --no-pager -n $lines 2>/dev/null").output
        else emptyList()

    /** Key properties, the raw status text and the dependency tree of one unit; null for an unsafe name. */
    suspend fun inspectUnit(containerName: String, unitName: String): UnitInspection? {
        if (!isSafeServiceName(unitName)) return null
        val props = executeSystemctlCommand(
            containerName,
            "show $unitName --no-pager -p " +
                "Description,LoadState,ActiveState,SubState,UnitFileState,FragmentPath," +
                "DropInPaths,MainPID,ExecMainStartTimestamp,Restart,MemoryCurrent,CPUUsageNSec"
        )
        val status = executeSystemctlCommand(containerName, "status $unitName --no-pager -l")
        val deps = executeSystemctlCommand(containerName, "list-dependencies $unitName --no-pager --plain")
        return UnitInspection(
            properties = props.output.mapNotNull { line ->
                val idx = line.indexOf('=')
                if (idx > 0) line.substring(0, idx) to line.substring(idx + 1) else null
            }.toMap(),
            statusText = status.output,
            dependencies = deps.output.map { it.trim() }.filter { it.isNotBlank() }
        )
    }

    // Override (drop-in) management, what `systemctl edit <unit>` does:
    // /etc/systemd/system/<unit>.d/override.conf inside the container.
    private fun overrideDirPath(unitName: String) = "/etc/systemd/system/$unitName.d"
    private fun overridePath(unitName: String) = "${overrideDirPath(unitName)}/override.conf"

    /** The unit's override.conf, or null when the name is unsafe or the file is missing. */
    suspend fun getOverrideConf(containerName: String, unitName: String): String? {
        if (!isSafeServiceName(unitName)) return null
        val path = overridePath(unitName)
        val result = exec(containerName, "[ -f $path ] && cat $path")
        return if (result.isSuccess && result.output.isNotEmpty()) result.output.joinToString("\n") else null
    }

    /** Writes override.conf and reloads systemd. The content is base64-streamed, like the
     * query script, so user-typed quotes, `$` or backticks never leave the single-quoted payload.
     * Unbounded on purpose: daemon-reload on a big unit set outlives the action timeout, and
     * a deadline here would report a write that already happened as failed. */
    suspend fun setOverrideConf(containerName: String, unitName: String, content: String): CommandResult {
        if (!isSafeServiceName(unitName))
            return CommandResult(2, emptyList(), listOf("Invalid unit name: $unitName"))
        val b64 = Base64.encodeToString(content.toByteArray(), Base64.NO_WRAP)
        return exec(containerName, "mkdir -p ${overrideDirPath(unitName)} && echo $b64 | base64 -d > ${overridePath(unitName)} && systemctl daemon-reload 2>&1")
    }

    /** Removes override.conf and its drop-in dir if that is now empty, then reloads systemd. */
    suspend fun deleteOverrideConf(containerName: String, unitName: String): CommandResult {
        if (!isSafeServiceName(unitName))
            return CommandResult(2, emptyList(), listOf("Invalid unit name: $unitName"))
        return exec(containerName, "rm -f ${overridePath(unitName)} && rmdir --ignore-fail-on-non-empty ${overrideDirPath(unitName)} 2>/dev/null; systemctl daemon-reload 2>&1")
    }
}
