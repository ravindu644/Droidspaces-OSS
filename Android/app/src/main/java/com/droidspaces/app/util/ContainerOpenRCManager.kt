package com.droidspaces.app.util

import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope

/** OpenRC: chkopenrc.sh for the lists, rc-service and rc-update for the actions. */
object ContainerOpenRCManager : ContainerInitManager("chkopenrc.sh", "command -v rc-service") {

    suspend fun getAllServices(containerName: String): List<ServiceInfo> = coroutineScope {
        val (running, enabled, disabled) = listOf("--running", "--enabled", "--disabled")
            .map { async { runScript(containerName, it) } }.awaitAll()
            .map { (ok, lines) -> if (ok) pairs(lines) else emptyMap() }
        val all = mutableMapOf<String, ServiceInfo>()
        enabled.keys.forEach { all[it] = service(it, running[it] ?: "", enabled = true, running = it in running) }
        disabled.keys.forEach { all[it] = service(it, running[it] ?: "", enabled = false, running = it in running) }
        running.forEach { (name, desc) -> all.getOrPut(name) { service(name, desc, enabled = false, running = true) } }
        all.values.sortedBy { it.name }
    }

    suspend fun startService(containerName: String, serviceName: String) = action(containerName, serviceName) { "rc-service $it start" }
    suspend fun stopService(containerName: String, serviceName: String) = action(containerName, serviceName) { "rc-service $it stop" }
    suspend fun restartService(containerName: String, serviceName: String) = action(containerName, serviceName) { "rc-service $it restart" }
    suspend fun enableService(containerName: String, serviceName: String) = action(containerName, serviceName) { "rc-update add $it default" }
    suspend fun disableService(containerName: String, serviceName: String) = action(containerName, serviceName) { "rc-update del $it" }
}
