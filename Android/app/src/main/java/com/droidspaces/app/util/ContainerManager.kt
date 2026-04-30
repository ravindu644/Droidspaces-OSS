package com.droidspaces.app.util

import android.content.Context
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.delay
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
    val netMode: String = "host",
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
    val forceCgroupv1: Boolean = false,
    val blockNestedNs: Boolean = false,
    val staticNatIp: String = "",
    val privileged: String = "",
    val defaultUser: String = "root"
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
        appendLine("force_cgroupv1=${if (forceCgroupv1) "1" else "0"}")
        appendLine("block_nested_ns=${if (blockNestedNs) "1" else "0"}")
        if (netMode == "nat" && staticNatIp.isNotEmpty()) {
            appendLine("static_nat_ip=$staticNatIp")
        }
        appendLine("use_sparse_image=${if (useSparseImage) "1" else "0"}")
        if (sparseImageSizeGB != null) {
            appendLine("sparse_image_size_gb=$sparseImageSizeGB")
        }
        if (envFileContent != null) {
            appendLine("env_file=${Constants.CONTAINERS_BASE_PATH}/${ContainerManager.sanitizeContainerName(name)}/.env")
        }
        if (privileged.isNotEmpty()) {
            appendLine("privileged=$privileged")
        }
        if (defaultUser.isNotEmpty()) {
            appendLine("defaultUser=$defaultUser")
        }
    }
}

object ContainerManager {
    private const val CONTAINERS_BASE_PATH = Constants.CONTAINERS_BASE_PATH

    /**
     * Sanitize container name for use in directory paths.
     * Replaces spaces with dashes, but allows dots and other valid characters.
     * This ensures directory names are safe while preserving readable names.
     */
    fun sanitizeContainerName(name: String): String {
        return name.replace(" ", "-")
    }

    /**
     * Get the container directory path (parent directory).
     * Uses sanitized name to handle spaces.
     */
    fun getContainerDirectory(name: String): String {
        val sanitizedName = sanitizeContainerName(name)
        return "$CONTAINERS_BASE_PATH/$sanitizedName"
    }

    /**
     * Get the rootfs path for a container (LXC-style: /rootfs subdirectory).
     */
    fun getRootfsPath(name: String): String {
        return "${getContainerDirectory(name)}/rootfs"
    }

    /**
     * Get the sparse image path for a container.
     */
    fun getSparseImagePath(name: String): String {
        return "${getContainerDirectory(name)}/rootfs.img"
    }

    /**
     * List all installed containers by scanning the containers directory.
     * Returns a list of ContainerInfo objects.
     */
    suspend fun listContainers(): List<ContainerInfo> = withContext(Dispatchers.IO) {
        val containers = mutableListOf<ContainerInfo>()

        try {
            // List all directories in the containers base path (quoted for safety)
            val listResult = Shell.cmd("ls -d \"$CONTAINERS_BASE_PATH\"/*/ 2>/dev/null").exec()

            if (!listResult.isSuccess) {
                // Directory might not exist or be empty
                return@withContext emptyList()
            }

            // Parse each directory path
            listResult.out.forEach { line ->
                val trimmed = line.trim()
                if (trimmed.isEmpty() || !trimmed.startsWith(CONTAINERS_BASE_PATH)) {
                    return@forEach
                }

                // Extract sanitized container name from path: /data/local/Droidspaces/Containers/name/
                val sanitizedName = trimmed
                    .removeSuffix("/")
                    .substringAfterLast("/")

                if (sanitizedName.isEmpty()) {
                    return@forEach
                }

                // Try to load container config
                val configPath = "$CONTAINERS_BASE_PATH/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"
                // Use sanitizedName as default, but config file will have the real name
                val config = loadContainerConfig(configPath, sanitizedName)

                if (config != null) {
                    // Check if container is running (use the real name from config)
                    val runningInfo = checkContainerStatus(config.name)
                    val status = if (runningInfo.first) {
                        ContainerStatus.RUNNING
                    } else {
                        ContainerStatus.STOPPED
                    }
                    containers.add(config.copy(
                        status = status,
                        pid = runningInfo.second
                    ))
                }
            }
        } catch (e: Exception) {
            // Return empty list on error
        }

        containers
    }

    /**
     * Load container configuration from config file.
     */
    private fun loadContainerConfig(configPath: String, defaultName: String): ContainerInfo? {
        try {
            // Read config file using shell (quoted for safety)
            val readResult = Shell.cmd("cat \"$configPath\" 2>/dev/null").exec()

            if (!readResult.isSuccess || readResult.out.isEmpty()) {
                return null
            }

            val configContent = readResult.out.joinToString("\n")
            val configMap = mutableMapOf<String, String>()

            // Parse config file (key=value format)
            configContent.lines().forEach { line ->
                val trimmed = line.trim()
                // Skip comments and empty lines
                if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                    return@forEach
                }

                val parts = trimmed.split("=", limit = 2)
                if (parts.size == 2) {
                    configMap[parts[0].trim()] = parts[1].trim()
                }
            }

            // Build ContainerInfo from config
            val containerName = configMap["name"] ?: defaultName
            val useSparseImage = configMap["use_sparse_image"] == "1"
            val sparseImageSizeGB = configMap["sparse_image_size_gb"]?.toIntOrNull()

            // Parse bind mounts: src:dest,src2:dest2
            val bindMounts = configMap["bind_mounts"]?.split(",")?.mapNotNull {
                val parts = it.split(":", limit = 2)
                if (parts.size == 2) BindMount(parts[0], parts[1]) else null
            } ?: emptyList()

            // Parse upstream interfaces
            val upstreamInterfaces = configMap["upstream_interfaces"]?.split(",")?.map { it.trim() }?.filter { it.isNotEmpty() } ?: emptyList()

            // Parse port forwards: 8080:80/tcp, 9090:90/udp, 1000-2000/tcp (shorthand)
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
                } catch (e: Exception) { null }
            } ?: emptyList()

            return ContainerInfo(
                name = containerName,
                hostname = configMap["hostname"] ?: containerName,
                // Use the new rootfs path structure (LXC-style) or sparse image path
                rootfsPath = configMap["rootfs_path"] ?: if (useSparseImage) {
                    getSparseImagePath(containerName)
                } else {
                    getRootfsPath(containerName)
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
                envFileContent = loadEnvFileContent(containerName),
                upstreamInterfaces = upstreamInterfaces,
                portForwards = portForwards,
                forceCgroupv1 = configMap["force_cgroupv1"] == "1",
                blockNestedNs = configMap["block_nested_ns"] == "1",
                staticNatIp = configMap["static_nat_ip"] ?: "",
                privileged = configMap["privileged"] ?: "",
                defaultUser = configMap["defaultUser"] ?: "root",
            )
        } catch (e: Exception) {
            return null
        }
    }

    /**
     * Load .env file content for a container.
     */
    private fun loadEnvFileContent(containerName: String): String? {
        val envFilePath = "${getContainerDirectory(containerName)}/.env"
        val readResult = Shell.cmd("cat \"$envFilePath\" 2>/dev/null").exec()
        return if (readResult.isSuccess && readResult.out.isNotEmpty()) {
            readResult.out.joinToString("\n")
        } else {
            null
        }
    }

    /**
     * Check if a container is running and get its correct init PID.
     * Returns Pair<isRunning, pid>
     *
     * Uses 'droidspaces --name=X pid' which:
     *  - Reads the PID file directly (no pgrep guessing)
     *  - Checks kill(pid, 0) to confirm the process is alive
     *  - Prints just the PID number or "NONE"
     *  - NEVER calls cleanup_container_resources (safe to call post-start)
     */
    suspend fun checkContainerStatus(containerName: String): Pair<Boolean, Int?> = withContext(Dispatchers.IO) {
        try {
            val binary = Constants.DROIDSPACES_BINARY_PATH
            val quotedName = ContainerCommandBuilder.quote(containerName)
            val result = Shell.cmd("\"$binary\" --name=$quotedName pid 2>/dev/null").exec()

            val output = result.out.firstOrNull()?.trim() ?: "NONE"
            if (output == "NONE" || output.isEmpty()) {
                return@withContext Pair(false, null)
            }

            val pid = output.toIntOrNull()
            if (pid != null && pid > 0) {
                return@withContext Pair(true, pid)
            }
        } catch (e: Exception) {
            // Ignore errors - treat as stopped
        }

        Pair(false, null)
    }

    /**
     * Get container info by name.
     * Note: name should be the sanitized directory name (spaces replaced with dashes).
     */
    suspend fun getContainerInfo(name: String): ContainerInfo? = withContext(Dispatchers.IO) {
        val sanitizedName = sanitizeContainerName(name)
        val configPath = "$CONTAINERS_BASE_PATH/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"
        val config = loadContainerConfig(configPath, sanitizedName)

        if (config != null) {
            val runningInfo = checkContainerStatus(name)
            val status = if (runningInfo.first) {
                ContainerStatus.RUNNING
            } else {
                ContainerStatus.STOPPED
            }
            config.copy(
                status = status,
                pid = runningInfo.second
            )
        } else {
            null
        }
    }

    /**
     * List active upstream interfaces by scanning all routing tables.
     *
     * Uses `table all` instead of the default table so that CLAT/Qualcomm
     * devices are correctly detected - on these devices every interface has
     * its own per-interface routing table and nothing appears in the main
     * table, so `ip route show default` returns empty.
     */
    suspend fun listUpstreamInterfaces(): List<String> = withContext(Dispatchers.IO) {
        try {
            val busybox = Constants.BUSYBOX_BINARY_PATH
            val cmd = "ip route show table all | $busybox grep '^default' | $busybox awk '{for(i=1;i<=NF;i++) if(\$i==\"dev\") print \$(i+1)}' | $busybox grep -Ev '^(ds-|dummy)' | $busybox sort -u"
            val result = Shell.cmd(cmd).exec()
            if (result.isSuccess) {
                result.out.map { it.trim() }.filter { it.isNotEmpty() }
            } else {
                emptyList()
            }
        } catch (e: Exception) {
            emptyList()
        }
    }

    /**
     * Update container configuration.
     * Only updates the configurable options (hostname, flags), not name or rootfsPath.
     *
     * @param context Android context for temporary file creation
     * @param containerName Name of the container to update (will be sanitized)
     * @param newConfig New configuration values (only configurable fields are used)
     * @return Result.success on success, Result.failure on error
     */
    suspend fun updateContainerConfig(
        context: Context,
        containerName: String,
        newConfig: ContainerInfo
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val sanitizedName = sanitizeContainerName(containerName)
            val configPath = "$CONTAINERS_BASE_PATH/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"

            // Build new config content using the shared method
            val configContent = newConfig.toConfigContent()

            // Handle .env file
            val envFilePath = "${getContainerDirectory(containerName)}/.env"
            if (newConfig.envFileContent.isNullOrBlank()) {
                Shell.cmd("rm -f \"$envFilePath\"").exec()
            } else {
                val tempEnvFile = File("${context.cacheDir}/.env_${sanitizedName}")
                tempEnvFile.writeText(newConfig.envFileContent + "\n")
                Shell.cmd("cp \"${tempEnvFile.absolutePath}\" \"$envFilePath\"").exec()
                Shell.cmd("chmod 644 \"$envFilePath\"").exec()
                tempEnvFile.delete()
            }

            // Write config to temp file first (app can write to cache dir)
            // Use sanitizedName to avoid issues with spaces in filename
            val tempConfigFile = File("${context.cacheDir}/container_${sanitizedName}.config")
            tempConfigFile.writeText(configContent)

            // Copy temp config to final location using shell (root required)
            // Quote paths to handle spaces and special characters
            val copyResult = Shell.cmd("cp \"${tempConfigFile.absolutePath}\" \"$configPath\" 2>&1").exec()
            if (!copyResult.isSuccess) {
                // Check both stdout and stderr for error messages
                val errorOutput = (copyResult.out + copyResult.err).joinToString("\n").trim()
                val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${copyResult.code})"
                tempConfigFile.delete()
                return@withContext Result.failure(Exception("Failed to update container config: $errorMsg"))
            }

            // Set proper permissions
            val chmodResult = Shell.cmd("chmod 644 \"$configPath\" 2>&1").exec()
            if (!chmodResult.isSuccess) {
                // Non-fatal, but log warning
            }

            // Clean up temp config file
            tempConfigFile.delete()

            Result.success(Unit)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /**
     * Uninstall a container by stopping it (if running) and deleting its directory.
     * This function logs all operations using the provided logger callback.
     *
     * @param container Container to uninstall
     * @param logger Logger callback for logging operations
     * @return Result.success on success, Result.failure on error
     */
    suspend fun uninstallContainer(
        container: ContainerInfo,
        logger: ContainerLogger
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            logger.i("Starting uninstallation of container: ${container.name}")
            logger.i("")

            // Step 1: Check if container is running
            logger.i("Step 1: Checking container status...")
            val isRunning = checkContainerStatus(container.name).first

            if (isRunning) {
                logger.i("Container is currently running. Stopping it first...")
                logger.i("")

                // Stop the container using droidspaces command
                val stopCommand = ContainerCommandBuilder.buildStopCommand(container)
                logger.i("Executing: $stopCommand")

                val stopResult = Shell.cmd("$stopCommand 2>&1").exec()

                // Log stop command output
                if (stopResult.out.isNotEmpty()) {
                    stopResult.out.forEach { line ->
                        val trimmed = line.trim()
                        if (trimmed.isNotEmpty()) {
                            logger.i(trimmed)
                        }
                    }
                }
                if (stopResult.err.isNotEmpty()) {
                    stopResult.err.forEach { line ->
                        val trimmed = line.trim()
                        if (trimmed.isNotEmpty()) {
                            logger.e(trimmed)
                        }
                    }
                }

                if (!stopResult.isSuccess) {
                    logger.e("Failed to stop container (exit code: ${stopResult.code})")
                    logger.e("Uninstallation aborted.")
                    return@withContext Result.failure(Exception("Failed to stop container before uninstallation"))
                }

                logger.i("Container stopped successfully.")
                logger.i("")

                // Wait a moment for the container to fully stop
                kotlinx.coroutines.delay(500)
            } else {
                logger.i("Container is not running. Proceeding with deletion...")
                logger.i("")
            }

            // Step 2: Delete the container directory
            logger.i("Step 2: Deleting container directory...")
            // Delete the parent directory (which contains rootfs and config)
            val containerPath = getContainerDirectory(container.name)
            logger.i("Container path: $containerPath")

            // Use rm -rf to recursively delete the entire container directory
            val deleteCommand = "rm -rf \"$containerPath\" 2>&1"
            logger.i("Executing: $deleteCommand")

            val deleteResult = Shell.cmd(deleteCommand).exec()

            // Log delete command output
            if (deleteResult.out.isNotEmpty()) {
                deleteResult.out.forEach { line ->
                    val trimmed = line.trim()
                    if (trimmed.isNotEmpty()) {
                        logger.i(trimmed)
                    }
                }
            }
            if (deleteResult.err.isNotEmpty()) {
                deleteResult.err.forEach { line ->
                    val trimmed = line.trim()
                    if (trimmed.isNotEmpty()) {
                        logger.e(trimmed)
                    }
                }
            }

            if (!deleteResult.isSuccess) {
                logger.e("Failed to delete container directory (exit code: ${deleteResult.code})")
                return@withContext Result.failure(Exception("Failed to delete container directory"))
            }

            // Verify deletion
            logger.i("")
            logger.i("Verifying deletion...")
            val verifyResult = Shell.cmd("test -d \"$containerPath\" && echo 'exists' || echo 'deleted' 2>&1").exec()
            if (verifyResult.out.any { it.contains("exists") }) {
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
