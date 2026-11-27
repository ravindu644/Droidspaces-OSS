package com.droidspaces.app.util

import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope

/** runit: chkrunit.sh for the lists and for every action, since the script owns the
 * sv and active-directory discovery rather than trusting sv's compiled-in default. */
object ContainerRunitManager : ContainerInitManager("chkrunit.sh", "command -v sv") {

    suspend fun getAllServices(containerName: String): List<ServiceInfo> = coroutineScope {
        val (running, enabled, disabled) = listOf("--running", "--enabled", "--disabled")
            .map { async { runScript(containerName, it) } }.awaitAll()
            .map { (ok, lines) -> if (ok) pairs(lines) else emptyMap() }
        val all = mutableMapOf<String, ServiceInfo>()
        enabled.forEach { (name, enabledDesc) ->
            val desc = listOf(enabledDesc, running[name].orEmpty()).filter { it.isNotBlank() }.distinct().joinToString(" | ")
            all[name] = service(name, desc, enabled = true, running = name in running)
        }
        disabled.forEach { (name, desc) -> all[name] = service(name, desc, enabled = false, running = name in running) }
        running.forEach { (name, desc) -> all.getOrPut(name) { service(name, desc, enabled = false, running = true) } }
        all.values.sortedBy { it.name }
    }

    private suspend fun sv(containerName: String, serviceName: String, flag: String) =
        action(containerName, serviceName) { scriptAction(flag, it) }

    suspend fun startService(containerName: String, serviceName: String) = sv(containerName, serviceName, "--sv up")
    suspend fun stopService(containerName: String, serviceName: String) = sv(containerName, serviceName, "--sv down")
    suspend fun restartService(containerName: String, serviceName: String) = sv(containerName, serviceName, "--sv restart")
    suspend fun enableService(containerName: String, serviceName: String) = sv(containerName, serviceName, "--enable")
    suspend fun disableService(containerName: String, serviceName: String) = sv(containerName, serviceName, "--disable")
    suspend fun toggleDefaultState(containerName: String, serviceName: String) = sv(containerName, serviceName, "--toggle-boot")
}
