package com.droidspaces.app.util

/**
 * OpenWrt's procd: chkprocd.sh for the list, /etc/init.d/<service> for the actions.
 * /etc/init.d alone is no proof, OpenRC and SysV have it too: /etc/openwrt_release
 * names the distro and the procd, ubusd, ubus or service binaries confirm the stack.
 */
object ContainerProcdManager : ContainerInitManager(
    "chkprocd.sh",
    "[ -f /etc/openwrt_release ] && { [ -x /sbin/procd ] || [ -x /sbin/ubusd ] || command -v ubus >/dev/null 2>&1 || command -v service >/dev/null 2>&1; }",
    timeoutMs = 15_000L,
) {
    private val safeActions = setOf("start", "stop", "restart", "reload", "enable", "disable", "status")

    /** The script prints NAME|DESCRIPTION|enabled/disabled|running/stopped/unknown; an init
     * script whose state cannot be read is UNKNOWN rather than guessed. */
    suspend fun getAllServices(containerName: String): List<ServiceInfo> {
        val (ok, lines) = runScript(containerName, "--all")
        if (!ok) return emptyList()
        return lines.mapNotNull { line ->
            val parts = line.split("|", limit = 4)
            if (parts.size < 4) return@mapNotNull null
            val name = parts[0].trim()
            if (!isSafeServiceName(name)) return@mapNotNull null
            val enabled = parts[2].trim() == "enabled"
            val running = parts[3].trim() == "running"
            if (parts[3].trim() == "unknown") ServiceInfo(name, parts[1].trim(), ServiceStatus.UNKNOWN, enabled, running)
            else service(name, parts[1].trim(), enabled, running)
        }.sortedBy { it.name }
    }

    private suspend fun initd(containerName: String, serviceName: String, act: String): CommandResult {
        if (act !in safeActions) return CommandResult(2, emptyList(), listOf("Unsupported OpenWrt service action: $act"))
        return action(containerName, serviceName) { "/etc/init.d/$it $act" }
    }

    suspend fun startService(containerName: String, serviceName: String) = initd(containerName, serviceName, "start")
    suspend fun stopService(containerName: String, serviceName: String) = initd(containerName, serviceName, "stop")
    suspend fun restartService(containerName: String, serviceName: String) = initd(containerName, serviceName, "restart")
    suspend fun reloadService(containerName: String, serviceName: String) = initd(containerName, serviceName, "reload")
    suspend fun enableService(containerName: String, serviceName: String) = initd(containerName, serviceName, "enable")
    suspend fun disableService(containerName: String, serviceName: String) = initd(containerName, serviceName, "disable")
}
