package com.droidspaces.app.util

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

enum class ContainerStatus {
    RUNNING,
    STOPPED,
    RESTARTING
}

data class BindMount(
    val src: String,
    val dest: String
)

data class PortForward(
    val hostPort: String,
    val containerPort: String? = null,
    val proto: String = "tcp"
)

data class ContainerInfo(
    val name: String,
    val hostname: String,
    val rootfsPath: String,
    val netMode: String = "nat",
    val disableIPv6: Boolean = false,
    val enableAndroidStorage: Boolean = false,
    val enableHwAccess: Boolean = false,
    val enableGpuMode: Boolean = false,
    val enableTermuxX11: Boolean = false,
    val selinuxPermissive: Boolean = false,
    val volatileMode: Boolean = false,
    val bindMounts: List<BindMount> = emptyList(),
    val dnsServers: String = "",
    val runAtBoot: Boolean = false,
    val status: ContainerStatus = ContainerStatus.STOPPED,
    val pid: Int? = null,
    val useSparseImage: Boolean = false,
    val sparseImageSizeGB: Int? = null,
    val envFileContent: String? = null,
    val upstreamInterfaces: List<String> = emptyList(),
    val portForwards: List<PortForward> = emptyList(),
    val rootless: Boolean = false,
    val staticNatIp: String = "",
    val privileged: String = "",
    val customInit: String = "",
    val forceCgroupv1: Boolean = false,
    val blockNestedNs: Boolean = false,
    val uuid: String = ""
) {
    val isRunning: Boolean
        get() = status == ContainerStatus.RUNNING

    fun toConfigContent(): String = buildString {
        appendLine("# Droidspaces Container Configuration")
        appendLine("# Generated automatically")
        appendLine()
        appendLine("name=$name")
        appendLine("hostname=$hostname")
        appendLine("rootfs_path=$rootfsPath")
        appendLine("net_mode=$netMode")
        appendLine("disable_ipv6=${if (disableIPv6) "1" else "0"}")
        appendLine("enable_android_storage=${if (enableAndroidStorage) "1" else "0"}")
        appendLine("enable_hw_access=${if (enableHwAccess) "1" else "0"}")
        appendLine("enable_gpu_mode=${if (enableGpuMode) "1" else "0"}")
        appendLine("enable_termux_x11=${if (enableTermuxX11) "1" else "0"}")
        appendLine("selinux_permissive=${if (selinuxPermissive) "1" else "0"}")
        appendLine("volatile_mode=${if (volatileMode) "1" else "0"}")
        appendLine("force_cgroupv1=${if (forceCgroupv1) "1" else "0"}")
        appendLine("block_nested_ns=${if (blockNestedNs) "1" else "0"}")
        if (bindMounts.isNotEmpty()) {
            appendLine("bind_mounts=${bindMounts.joinToString(",") { "${it.src}:${it.dest}" }}")
        }
        if (netMode == "nat" && upstreamInterfaces.isNotEmpty()) {
            appendLine("upstream_interfaces=${upstreamInterfaces.joinToString(",")}")
        }
        if (netMode == "nat" && portForwards.isNotEmpty()) {
            appendLine("port_forwards=${portForwards.joinToString(",") {
                val mapping = if (it.containerPort != null) "${it.hostPort}:${it.containerPort}" else it.hostPort
                "$mapping/${it.proto}"
            }}")
        }
        if (dnsServers.isNotEmpty()) {
            appendLine("dns_servers=$dnsServers")
        }
        appendLine("run_at_boot=${if (runAtBoot) "1" else "0"}")
        appendLine("rootless=${if (rootless) "1" else "0"}")
        if (netMode == "nat" && staticNatIp.isNotEmpty()) {
            appendLine("static_nat_ip=$staticNatIp")
        }
        appendLine("use_sparse_image=${if (useSparseImage) "1" else "0"}")
        if (sparseImageSizeGB != null) {
            appendLine("sparse_image_size_gb=$sparseImageSizeGB")
        }
        if (envFileContent != null) {
            appendLine("env_file=${WorkspacePaths.containerConfigPath(name, rootless)}")
        }
        if (privileged.isNotEmpty()) {
            appendLine("privileged=$privileged")
        }
        if (customInit.isNotEmpty()) {
            appendLine("custom_init=$customInit")
        }
        if (uuid.isNotEmpty()) {
            appendLine("uuid=$uuid")
        }
    }
}

object ContainerManager {
    /**
     * Sanitize container name for use in directory paths.
     */
    fun sanitizeContainerName(name: String): String {
        return name.replace(" ", "-")
    }

    /**
     * Get the container directory path (parent directory).
     */
    fun getContainerDirectory(name: String, rootless: Boolean = false): String {
        val sanitizedName = sanitizeContainerName(name)
        val base = WorkspacePaths.containersBase(rootless)
        return "$base/$sanitizedName"
    }

    /**
     * Get the rootfs path for a container (LXC-style: /rootfs subdirectory).
     */
    fun getRootfsPath(name: String, rootless: Boolean = false): String {
        return "${getContainerDirectory(name, rootless)}/rootfs"
    }

    /**
     * Get the sparse image path for a container.
     */
    fun getSparseImagePath(name: String, rootless: Boolean = false): String {
        return "${getContainerDirectory(name, rootless)}/rootfs.img"
    }

    // ── List containers ──────────────────────────────────────────────────

    /**
     * List all installed containers by scanning both root and rootless workspaces.
     */
    suspend fun listContainers(): List<ContainerInfo> = withContext(Dispatchers.IO) {
        val containers = mutableListOf<ContainerInfo>()
        // Scan both workspaces
        containers.addAll(scanContainerDir(WorkspacePaths.rootBase + "/Containers", rootless = false))
        containers.addAll(scanContainerDir(WorkspacePaths.rootlessContainers, rootless = true))
        containers
    }

    private suspend fun scanContainerDir(dir: String, rootless: Boolean): List<ContainerInfo> {
        val containers = mutableListOf<ContainerInfo>()
        try {
            if (rootless) {
                // Rootless: use File API (app has direct access)
                val containerDir = File(dir)
                if (!containerDir.isDirectory) return emptyList()
                containerDir.listFiles()?.forEach { subDir ->
                    if (subDir.isDirectory) {
                        val configFile = File(subDir, Constants.CONTAINER_CONFIG_FILE)
                        if (configFile.isFile) {
                            try {
                                val content = configFile.readText()
                                val config = parseConfig(content, subDir.name, rootless = true)
                                if (config != null) {
                                    val pid = ContainerRuntime.getContainerPid(config.name, rootless = true)
                                        .let { if (it > 0) it else null }
                                    containers.add(config.copy(
                                        status = if (pid != null) ContainerStatus.RUNNING else ContainerStatus.STOPPED,
                                        pid = pid
                                    ))
                                }
                            } catch (_: Exception) { }
                        }
                    }
                }
            } else {
                // Root: use SuExec to list (directory is root-owned)
                val listResult = SuExec.cmd("ls -d \"$dir\"/*/ 2>/dev/null").exec()
                if (!listResult.isSuccess) return emptyList()

                listResult.out.forEach { line ->
                    val trimmed = line.trim()
                    if (trimmed.isEmpty() || !trimmed.startsWith(dir)) return@forEach
                    val sanitizedName = trimmed.removeSuffix("/").substringAfterLast("/")
                    if (sanitizedName.isEmpty()) return@forEach

                    val configPath = "$dir/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"
                    val config = loadContainerConfigRoot(configPath, sanitizedName)
                    if (config != null) {
                        try {
                            val pid = ContainerRuntime.getContainerPid(config.name, rootless = false)
                                .let { if (it > 0) it else null }
                            containers.add(config.copy(
                                status = if (pid != null) ContainerStatus.RUNNING else ContainerStatus.STOPPED,
                                pid = pid
                            ))
                        } catch (_: Exception) {
                            containers.add(config)
                        }
                    }
                }
            }
        } catch (_: Exception) { }
        return containers
    }

    // ── Config loading ───────────────────────────────────────────────────

    /**
     * Load container configuration from a config file (root mode — uses SuExec).
     */
    private fun loadContainerConfigRoot(configPath: String, defaultName: String): ContainerInfo? {
        try {
            val readResult = SuExec.cmd("cat \"$configPath\" 2>/dev/null").exec()
            if (!readResult.isSuccess || readResult.out.isEmpty()) return null
            return parseConfig(readResult.out.joinToString("\n"), defaultName, rootless = false)
        } catch (_: Exception) { return null }
    }

    /**
     * Load container configuration from an app-private file (rootless mode).
     */
    private fun loadContainerConfigRootless(name: String): ContainerInfo? {
        try {
            val configFile = File(WorkspacePaths.containerConfigPath(name, rootless = true))
            if (!configFile.isFile) return null
            return parseConfig(configFile.readText(), name, rootless = true)
        } catch (_: Exception) { return null }
    }

    /**
     * Parse container configuration from string content.
     */
    fun parseConfig(configContent: String, defaultName: String, rootless: Boolean = false): ContainerInfo? {
        try {
            val configMap = mutableMapOf<String, String>()

            configContent.lines().forEach { line ->
                val trimmed = line.trim()
                if (trimmed.isEmpty() || trimmed.startsWith("#")) return@forEach
                val parts = trimmed.split("=", limit = 2)
                if (parts.size == 2) {
                    configMap[parts[0].trim()] = parts[1].trim()
                }
            }

            val containerName = configMap["name"] ?: defaultName
            val useSparseImage = configMap["use_sparse_image"] == "1"
            val sparseImageSizeGB = configMap["sparse_image_size_gb"]?.toIntOrNull()
            val containerRootless = configMap["rootless"] == "1"

            val bindMounts = configMap["bind_mounts"]?.split(",")?.mapNotNull {
                val parts = it.split(":", limit = 2)
                if (parts.size == 2) BindMount(parts[0], parts[1]) else null
            } ?: emptyList()

            val upstreamInterfaces = configMap["upstream_interfaces"]?.split(",")?.map { it.trim() }?.filter { it.isNotEmpty() } ?: emptyList()

            val portForwards = configMap["port_forwards"]?.split(",")?.mapNotNull { pfStr ->
                try {
                    val parts = pfStr.trim().split("/")
                    val proto = if (parts.size > 1) parts[1].lowercase() else "tcp"
                    val portParts = parts[0].split(":")
                    if (portParts.size == 2) {
                        PortForward(portParts[0].trim(), portParts[1].trim(), proto)
                    } else if (portParts.size == 1 && portParts[0].isNotBlank()) {
                        PortForward(portParts[0].trim(), null, proto)
                    } else null
                } catch (_: Exception) { null }
            } ?: emptyList()

            return ContainerInfo(
                name = containerName,
                hostname = configMap["hostname"] ?: ValidationUtils.sanitizeHostname(containerName),
                rootfsPath = configMap["rootfs_path"] ?: if (useSparseImage) {
                    getSparseImagePath(containerName, containerRootless)
                } else {
                    getRootfsPath(containerName, containerRootless)
                },
                netMode = configMap["net_mode"] ?: "host",
                disableIPv6 = configMap["disable_ipv6"] == "1",
                enableAndroidStorage = configMap["enable_android_storage"] == "1",
                enableHwAccess = configMap["enable_hw_access"] == "1",
                enableGpuMode = configMap["enable_gpu_mode"] == "1",
                enableTermuxX11 = configMap["enable_termux_x11"] == "1",
                selinuxPermissive = configMap["selinux_permissive"] == "1",
                volatileMode = configMap["volatile_mode"] == "1",
                bindMounts = bindMounts,
                dnsServers = configMap["dns_servers"] ?: "",
                runAtBoot = configMap["run_at_boot"] == "1",
                status = ContainerStatus.STOPPED,
                useSparseImage = useSparseImage,
                sparseImageSizeGB = sparseImageSizeGB,
                envFileContent = loadEnvFileContent(containerName, containerRootless),
                upstreamInterfaces = upstreamInterfaces,
                portForwards = portForwards,
                rootless = containerRootless || rootless,
                staticNatIp = configMap["static_nat_ip"] ?: "",
                privileged = configMap["privileged"] ?: "",
                customInit = configMap["custom_init"] ?: "",
                forceCgroupv1 = configMap["force_cgroupv1"] == "1",
                blockNestedNs = configMap["block_nested_ns"] == "1",
                uuid = configMap["uuid"] ?: ""
            )
        } catch (_: Exception) { return null }
    }

    /**
     * Load .env file content for a container.
     */
    private fun loadEnvFileContent(containerName: String, rootless: Boolean): String? {
        return try {
            if (rootless) {
                val envFile = File(WorkspacePaths.containerConfigPath(containerName, rootless = true))
                    .parentFile?.let { File(it, ".env") }
                if (envFile?.isFile == true) envFile.readText() else null
            } else {
                val envPath = "${getContainerDirectory(containerName)}/.env"
                val result = SuExec.cmd("cat \"$envPath\" 2>/dev/null").exec()
                if (result.isSuccess && result.out.isNotEmpty()) result.out.joinToString("\n") else null
            }
        } catch (_: Exception) { null }
    }

    // ── Container status ─────────────────────────────────────────────────

    /**
     * Check if a container is running and get its PID.
     */
    suspend fun checkContainerStatus(containerName: String): Pair<Boolean, Int?> = withContext(Dispatchers.IO) {
        try {
            // Determine mode: check both workspaces
            val rootlessConfig = loadContainerConfigRootless(containerName)
            val isRootless = rootlessConfig?.rootless ?: run {
                loadContainerConfigRoot(
                    "${WorkspacePaths.rootBase}/Containers/${sanitizeContainerName(containerName)}/${Constants.CONTAINER_CONFIG_FILE}",
                    containerName
                )?.rootless ?: false
            }

            val pid = ContainerRuntime.getContainerPid(containerName, isRootless)
            if (pid > 0) Pair(true, pid) else Pair(false, null)
        } catch (_: Exception) {
            Pair(false, null)
        }
    }

    /**
     * Get container info by name. Scans both workspaces.
     */
    suspend fun getContainerInfo(name: String): ContainerInfo? = withContext(Dispatchers.IO) {
        val sanitizedName = sanitizeContainerName(name)

        // Try rootless first (faster, no su overhead)
        val rootlessInfo = loadContainerConfigRootless(name)
        if (rootlessInfo != null) {
            val pid = ContainerRuntime.getContainerPid(name, rootless = true).let { if (it > 0) it else null }
            return@withContext rootlessInfo.copy(
                status = if (pid != null) ContainerStatus.RUNNING else ContainerStatus.STOPPED,
                pid = pid
            )
        }

        // Then try root
        val configPath = "${WorkspacePaths.rootBase}/Containers/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"
        val rootInfo = loadContainerConfigRoot(configPath, sanitizedName)
        if (rootInfo != null) {
            val pid = ContainerRuntime.getContainerPid(name, rootless = false).let { if (it > 0) it else null }
            return@withContext rootInfo.copy(
                status = if (pid != null) ContainerStatus.RUNNING else ContainerStatus.STOPPED,
                pid = pid
            )
        }

        null
    }

    // ── Upstream interfaces ──────────────────────────────────────────────

    /**
     * List active upstream interfaces.
     */
    suspend fun listUpstreamInterfaces(): List<String> = withContext(Dispatchers.IO) {
        try {
            val busybox = Constants.BUSYBOX_BINARY_PATH
            val cmd = "ip route show table all | $busybox grep '^default' | $busybox awk '{for(i=1;i<=NF;i++) if(\$i==\"dev\") print \$(i+1)}' | $busybox grep -Ev '^(ds-|dummy)' | $busybox sort -u"
            val result = SuExec.cmd(cmd).exec()
            if (result.isSuccess) {
                result.out.map { it.trim() }.filter { it.isNotEmpty() }
            } else {
                emptyList()
            }
        } catch (_: Exception) { emptyList() }
    }

    // ── Config update ────────────────────────────────────────────────────

    /**
     * Update container configuration.
     */
    suspend fun updateContainerConfig(
        context: Context,
        containerName: String,
        newConfig: ContainerInfo
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val sanitizedName = sanitizeContainerName(containerName)
            val rootless = newConfig.rootless
            val basePath = WorkspacePaths.containersBase(rootless)
            val configPath = "$basePath/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"

            // Preserve existing UUID
            val configToWrite = if (newConfig.uuid.isNotEmpty()) {
                newConfig
            } else {
                val existingContent = if (rootless) {
                    File(configPath).readText()
                } else {
                    SuExec.cmd("cat \"$configPath\" 2>/dev/null").exec().out.joinToString("\n")
                }
                val existingUuid = existingContent.lines()
                    .firstOrNull { it.startsWith("uuid=") }
                    ?.removePrefix("uuid=")?.trim() ?: ""
                newConfig.copy(uuid = existingUuid)
            }
            val configContent = configToWrite.toConfigContent()

            // Handle .env file
            val envFilePath = "$basePath/$sanitizedName/.env"
            if (newConfig.envFileContent.isNullOrBlank()) {
                if (rootless) File(envFilePath).delete() else SuExec.cmd("rm -f \"$envFilePath\"").exec()
            } else {
                if (rootless) {
                    File(envFilePath).writeText(newConfig.envFileContent + "\n")
                } else {
                    val tempEnvFile = File(context.cacheDir, ".env_$sanitizedName")
                    tempEnvFile.writeText(newConfig.envFileContent + "\n")
                    SuExec.cmd("cp \"${tempEnvFile.absolutePath}\" \"$envFilePath\"").exec()
                    SuExec.cmd("chmod 644 \"$envFilePath\"").exec()
                    tempEnvFile.delete()
                }
            }

            if (rootless) {
                File(configPath).writeText(configContent)
            } else {
                val tempConfigFile = File(context.cacheDir, "container_$sanitizedName.config")
                tempConfigFile.writeText(configContent)
                val copyResult = SuExec.cmd("cp \"${tempConfigFile.absolutePath}\" \"$configPath\" 2>&1").exec()
                if (!copyResult.isSuccess) {
                    val errorOutput = (copyResult.out + copyResult.err).joinToString("\n").trim()
                    tempConfigFile.delete()
                    return@withContext Result.failure(
                        Exception("Failed to update container config: ${errorOutput.ifEmpty { "exit ${copyResult.code}" }}")
                    )
                }
                SuExec.cmd("chmod 644 \"$configPath\" 2>&1").exec()
                tempConfigFile.delete()
            }

            Result.success(Unit)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ── Uninstall ────────────────────────────────────────────────────────

    /**
     * Uninstall a container by stopping it (if running) and deleting its directory.
     */
    suspend fun uninstallContainer(
        container: ContainerInfo,
        logger: ContainerLogger
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            logger.i("Starting uninstallation of container: ${container.name}")
            logger.i("")

            // Step 1: Check if running
            logger.i("Step 1: Checking container status...")
            val isRunning = checkContainerStatus(container.name).first

            if (isRunning) {
                logger.i("Container is currently running. Stopping it first...")
                logger.i("")
                val stopOk = ContainerRuntime.stopContainer(container, logger = logger)
                if (!stopOk) {
                    logger.e("Failed to stop container")
                    return@withContext Result.failure(Exception("Failed to stop container before uninstallation"))
                }
                logger.i("Container stopped successfully.")
                logger.i("")
                kotlinx.coroutines.delay(500)
            } else {
                logger.i("Container is not running. Proceeding with deletion...")
                logger.i("")
            }

            // Step 2: Delete directory
            logger.i("Step 2: Deleting container directory...")
            val containerPath = getContainerDirectory(container.name, container.rootless)
            logger.i("Container path: $containerPath")

            if (container.rootless) {
                File(containerPath).deleteRecursively()
            } else {
                val deleteResult = SuExec.cmd("rm -rf \"$containerPath\" 2>&1").exec()
                if (!deleteResult.isSuccess) {
                    logger.e("Failed to delete container directory (exit code: ${deleteResult.code})")
                    return@withContext Result.failure(Exception("Failed to delete container directory"))
                }
            }

            // Verify
            logger.i("")
            logger.i("Verifying deletion...")
            val exists = if (container.rootless) {
                File(containerPath).exists()
            } else {
                SuExec.cmd("test -d \"$containerPath\" && echo 'exists' || echo 'deleted' 2>&1").exec()
                    .out.any { it.contains("exists") }
            }
            if (exists) {
                logger.e("Warning: Container directory still exists after deletion attempt!")
                return@withContext Result.failure(Exception("Container directory still exists after deletion"))
            }

            logger.i("Container directory successfully deleted.")
            logger.i("")
            logger.i("Uninstallation completed successfully!")
            Result.success(Unit)
        } catch (e: Exception) {
            logger.e("Exception during uninstallation: ${e.message}")
            logger.e(e.stackTraceToString())
            Result.failure(e)
        }
    }
}
