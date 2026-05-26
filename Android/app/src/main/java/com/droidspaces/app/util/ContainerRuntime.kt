package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.nativebridge.NativeBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Dispatch layer for container lifecycle operations.
 *
 * Rootless → pure JNI (no su needed).
 * Root     → uses the droidspaces-runner binary via su for privileged ops.
 *            Info queries (PID, version, mode) and runInContainer use JNI
 *            in-process regardless of mode.
 */

object ContainerRuntime {

    /** Rootless workspace — initialized from Application context. */
    private var workspace: String = ""

    /** Root-mode workspace (shared, root-owned). */
    private val rootWorkspace: String get() = "/data/local/Droidspaces"

    /** Initialize with app context (called once from Application.onCreate). */
    fun init(context: Context) {
        workspace = File(context.filesDir, "droidspaces").absolutePath
        WorkspacePaths.init(context)
    }

    // ── Container lifecycle ──────────────────────────────────────────────

    /**
     * Start a container.
     * Rootless → pure JNI. Root → runner via su.
     */
    suspend fun startContainer(
        container: ContainerInfo,
        logger: ContainerLogger? = null
    ): Boolean = withContext(Dispatchers.IO) {
        try {
            if (container.rootless) {
                startRootless(container, logger)
            } else {
                startRooted(container, logger)
            }
        } catch (e: Exception) {
            logger?.e("ContainerRuntime error: ${e.message}")
            false
        }
    }

    /**
     * Stop a container.
     * Rootless → JNI stopContainer. Root → runner via su.
     */
    suspend fun stopContainer(
        container: ContainerInfo,
        pid: Int = -1,
        logger: ContainerLogger? = null
    ): Boolean = withContext(Dispatchers.IO) {
        try {
            if (container.rootless) {
                val ret = NativeBridge.stopContainer(container.name, workspace, pid)
                ret == 0
            } else {
                val cmd = "${Constants.RUNNER_BINARY_PATH} stop '${container.name.replace("'", "'\\''")}' $pid"
                val result = SuExec.cmd("$cmd 2>&1").exec()
                if (!result.isSuccess) {
                    logger?.e("Stop failed (exit ${result.code}): ${result.err.joinToString(" ")}")
                }
                result.isSuccess
            }
        } catch (e: Exception) {
            logger?.e("ContainerRuntime error: ${e.message}")
            false
        }
    }

    /**
     * Get container PID.
     * Rootless → JNI getContainerPid. Root → runner via su.
     */
    suspend fun getContainerPid(
        name: String,
        rootless: Boolean
    ): Int = withContext(Dispatchers.IO) {
        try {
            if (rootless) {
                NativeBridge.getContainerPid(name, workspace)
            } else {
                val result = SuExec.cmd("${Constants.RUNNER_BINARY_PATH} pid '${name.replace("'", "'\\''")}' 2>/dev/null").exec()
                val output = result.out.firstOrNull()?.trim() ?: "NONE"
                if (output == "NONE" || output.isEmpty()) return@withContext -1
                output.toIntOrNull() ?: -1
            }
        } catch (e: Exception) {
            -1
        }
    }

    /** Run a command inside a container. Uses JNI with the correct workspace. */
    suspend fun runInContainer(
        name: String,
        rootless: Boolean,
        command: String
    ): String = withContext(Dispatchers.IO) {
        try {
            val ws = if (rootless) workspace else rootWorkspace
            NativeBridge.runInContainer(name, ws, command)
        } catch (e: Exception) {
            "ERROR: ${e.message}"
        }
    }

    /** Get container usage. Uses JNI with the correct workspace. */
    suspend fun getContainerUsage(
        name: String,
        rootless: Boolean
    ): String = withContext(Dispatchers.IO) {
        try {
            val ws = if (rootless) workspace else rootWorkspace
            NativeBridge.getContainerUsage(name, ws)
        } catch (e: Exception) {
            ""
        }
    }

    // ── Private helpers ──────────────────────────────────────────────────

    private suspend fun startRootless(
        container: ContainerInfo,
        logger: ContainerLogger?
    ): Boolean = withContext(Dispatchers.IO) {
        // Ensure workspace dirs exist
        WorkspacePaths.ensureRootlessDirs()

        // Write config to app-private path
        val configDir = File(WorkspacePaths.rootlessContainers, ContainerManager.sanitizeContainerName(container.name))
        configDir.mkdirs()
        val configFile = File(configDir, "container.config")
        configFile.writeText(container.toConfigContent())
        logger?.i("Config written to ${configFile.absolutePath}")

        // Call JNI — returns PID on success, -1 on failure
        val pid = NativeBridge.startContainer(
            configFile.absolutePath,
            workspace,
            rootless = true
        )

        if (pid > 0) {
            logger?.i("Container started (PID: $pid)")
            true
        } else {
            logger?.e("Container failed to start (JNI returned $pid)")
            false
        }
    }

    private suspend fun startRooted(
        container: ContainerInfo,
        logger: ContainerLogger?
    ): Boolean = withContext(Dispatchers.IO) {
        val sanitizedName = ContainerManager.sanitizeContainerName(container.name)
        val configDir = "${WorkspacePaths.rootBase}/Containers/$sanitizedName"
        val configPath = "$configDir/${Constants.CONTAINER_CONFIG_FILE}"

        // Write config via su (root-owned directory)
        val tempConfig = File.createTempFile("droidspaces-config-", ".tmp")
        try {
            tempConfig.writeText(container.toConfigContent())
            val mkdirResult = SuExec.cmd("mkdir -p \"$configDir\" 2>&1").exec()
            if (!mkdirResult.isSuccess) {
                logger?.e("Failed to create config directory: ${mkdirResult.err.joinToString()}")
                return@withContext false
            }
            val cpResult = SuExec.cmd("cp \"${tempConfig.absolutePath}\" \"$configPath\" 2>&1 && chmod 644 \"$configPath\" 2>&1").exec()
            if (!cpResult.isSuccess) {
                logger?.e("Failed to write config: ${cpResult.err.joinToString()}")
                return@withContext false
            }
        } finally {
            tempConfig.delete()
        }

        // Invoke runner via su
        val cmd = "${Constants.RUNNER_BINARY_PATH} start $configPath"
        val result = SuExec.cmd("$cmd 2>&1").exec()
        result.out.forEach { logger?.i(it) }
        result.err.forEach { logger?.e(it) }
        if (!result.isSuccess) {
            logger?.e("Start failed (exit ${result.code})")
        }
        result.isSuccess
    }
}
