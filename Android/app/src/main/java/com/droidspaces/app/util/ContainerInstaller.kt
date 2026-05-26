package com.droidspaces.app.util

import android.content.Context
import android.net.Uri
import com.droidspaces.app.util.SuExec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

object ContainerInstaller {
    private const val BUSYBOX_PATH = Constants.BUSYBOX_BINARY_PATH

    /**
     * Extract tarball and install container.
     * Routes to root or rootless paths based on [config.rootless].
     */
    suspend fun installContainer(
        context: Context,
        tarballUri: Uri,
        config: ContainerInfo,
        logger: ContainerLogger
    ): Result<Unit> = withContext(Dispatchers.IO) {
        val sanitizedName = ContainerManager.sanitizeContainerName(config.name)
        val rootless = config.rootless

        // Paths differ by mode
        val containerPath = if (rootless) {
            "${WorkspacePaths.rootlessContainers}/$sanitizedName"
        } else {
            ContainerManager.getContainerDirectory(config.name)
        }
        val rootfsPath = if (config.useSparseImage) {
            "$containerPath/rootfs.img"
        } else {
            "$containerPath/rootfs"
        }
        val configFilePath = "$containerPath/${Constants.CONTAINER_CONFIG_FILE}"
        var createdPaths = mutableListOf<String>()

        try {
            // Step 1: Check storage space
            logger.i("Checking available storage space...")
            val freeGB = StorageChecker.getFreeSpaceGB()
            if (freeGB != null) {
                logger.i("/data partition has ${freeGB}GB free space")
                val requiredGB = if (config.useSparseImage) {
                    (config.sparseImageSizeGB ?: 8) + Constants.MIN_STORAGE_GB
                } else {
                    Constants.MIN_STORAGE_GB
                }
                if (freeGB < requiredGB) {
                    logger.w("Warning: Less than ${requiredGB}GB available. Installation may fail.")
                }
            } else {
                logger.w("Warning: Unable to determine free space. Proceeding anyway...")
            }

            // Step 2: Create container directory
            logger.i("Creating container directory: $containerPath")
            if (rootless) {
                File(containerPath).mkdirs()
            } else {
                val mkdirResult = SuExec.cmd("mkdir -p \"$containerPath\" 2>&1").exec()
                if (!mkdirResult.isSuccess) {
                    val errorOutput = (mkdirResult.out + mkdirResult.err).joinToString("\n").trim()
                    val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${mkdirResult.code})"
                    throw Exception("Failed to create container directory: $errorMsg")
                }
            }
            createdPaths.add(containerPath)

            // Step 3: Copy tarball to temp location
            logger.i("Copying tarball to temporary location...")
            val tarballExtension = getTarballExtension(context, tarballUri)
            val tempTarball = File("${context.cacheDir}/container_${sanitizedName}.tar$tarballExtension")
            context.contentResolver.openInputStream(tarballUri)?.use { inputStream ->
                FileOutputStream(tempTarball).use { outputStream ->
                    inputStream.copyTo(outputStream)
                }
            } ?: throw Exception("Failed to open tarball input stream")
            logger.i("Tarball copied: ${tempTarball.absolutePath}")

            // Step 4: Extract tarball
            if (config.useSparseImage) {
                if (rootless) {
                    throw Exception("Sparse images are not supported in rootless mode (requires loop device)")
                }
                SparseImageInstaller.extract(
                    context = context,
                    tarball = tempTarball,
                    imgPath = rootfsPath,
                    mountPoint = "${containerPath}/rootfs",
                    sizeGB = config.sparseImageSizeGB ?: 8,
                    logger = logger
                )
            } else {
                if (rootless) {
                    extractRootless(context, tempTarball, rootfsPath, logger)
                } else {
                    extractRooted(tempTarball, rootfsPath, logger)
                }
            }

            // Step 5: Write container config
            logger.i("Writing container configuration...")
            if (rootless) {
                File(configFilePath).writeText(config.toConfigContent())
            } else {
                writeConfigRooted(context, config, configFilePath, sanitizedName, logger)
            }
            createdPaths.add(configFilePath)

            // .env file
            if (!config.envFileContent.isNullOrBlank()) {
                val envFilePath = "$containerPath/.env"
                if (rootless) {
                    File(envFilePath).writeText(config.envFileContent + "\n")
                } else {
                    writeEnvRooted(context, config, envFilePath, sanitizedName, logger)
                }
                createdPaths.add(envFilePath)
            }

            // Step 6: Verify installation
            logger.i("Verifying installation...")
            if (rootless) {
                val rootfsDir = File(rootfsPath)
                if (!rootfsDir.isDirectory || rootfsDir.listFiles().isNullOrEmpty()) {
                    throw Exception("Container rootfs directory is empty or missing after extraction")
                }
            } else {
                if (config.useSparseImage) {
                    val imgExists = SuExec.cmd("test -f \"$rootfsPath\" && echo 'exists' || echo 'not_found'").exec()
                    if (!imgExists.isSuccess || !imgExists.out.any { it.contains("exists") }) {
                        throw Exception("Container sparse image not found after extraction")
                    }
                } else {
                    val rootfsExists = SuExec.cmd("test -d \"$rootfsPath\" && echo 'exists' || echo 'not_found'").exec()
                    if (!rootfsExists.isSuccess || !rootfsExists.out.any { it.contains("exists") }) {
                        throw Exception("Container rootfs directory not found after extraction")
                    }
                }
            }

            // Apply post-extraction fixes
            applyPostExtractionFixes(context, rootfsPath, rootless, logger)

            logger.i("Container installed successfully!")
            Result.success(Unit)
        } catch (e: Exception) {
            logger.e("Installation failed: ${e.message}")
            logger.e(e.stackTraceToString())
            logger.i("Cleaning up created files...")
            cleanupPaths(createdPaths, rootless, logger)
            Result.failure(e)
        } finally {
            try {
                File("${context.cacheDir}/container_${sanitizedName}.tar.xz").delete()
                File("${context.cacheDir}/container_${sanitizedName}.tar.gz").delete()
            } catch (_: Exception) { }
        }
    }

    // ── Rootless helpers (no su, app-private paths) ──────────────────────

    private suspend fun extractRootless(
        context: Context,
        tarball: File,
        rootfsPath: String,
        logger: ContainerLogger
    ) {
        val rootfsDir = File(rootfsPath)
        rootfsDir.mkdirs()

        // Ensure app-private busybox is available
        val appBusybox = ensureAppBusybox(context)

        logger.i("Extracting tarball to $rootfsPath (rootless)...")
        val isXz = tarball.name.lowercase().endsWith(".xz")
        // Pipe decompression into tar -xpf -
        // Use shell for pipe: sh -c "xzcat file | tar -xpf -"
        val pipeCmd = if (isXz) {
            "$appBusybox xzcat '${tarball.absolutePath}' | $appBusybox tar -xpf -"
        } else {
            "$appBusybox gzip -dc '${tarball.absolutePath}' | $appBusybox tar -xpf -"
        }

        val process = ProcessBuilder("sh", "-c", pipeCmd)
            .directory(File(rootfsPath))
            .redirectErrorStream(true)
            .start()
        val exitCode = process.waitFor()

        if (exitCode != 0) {
            val err = process.inputStream.bufferedReader().readText()
            throw Exception("Extraction failed (exit $exitCode): $err")
        }
        logger.i("Tarball extracted successfully")
    }

    /**
     * Extract busybox from APK assets to app-private workspace if not already present.
     */
    private fun ensureAppBusybox(context: Context): String {
        val binDir = File(WorkspacePaths.rootlessBin)
        binDir.mkdirs()
        val busyboxFile = File(binDir, "busybox")

        if (busyboxFile.isFile && busyboxFile.canExecute()) {
            return busyboxFile.absolutePath
        }

        // Copy from assets
        val archSuffix = getArchSuffix()
        val assetName = "binaries/busybox-$archSuffix"
        context.assets.open(assetName).use { input ->
            busyboxFile.outputStream().use { output ->
                input.copyTo(output)
            }
        }
        busyboxFile.setExecutable(true)
        return busyboxFile.absolutePath
    }

    private fun getArchSuffix(): String {
        val arch = android.os.Build.SUPPORTED_ABIS[0]
        return when {
            arch.contains("arm64") || arch.contains("aarch64") -> "aarch64"
            arch.contains("armeabi") || arch.contains("arm") -> "armhf"
            arch.contains("x86_64") -> "x86_64"
            arch.contains("x86") -> "x86"
            else -> "aarch64"
        }
    }

    // ── Root helpers (su required) ───────────────────────────────────────

    private suspend fun extractRooted(
        tarball: File,
        rootfsPath: String,
        logger: ContainerLogger
    ) {
        val mkdirRootfsResult = SuExec.cmd("mkdir -p \"$rootfsPath\" 2>&1").exec()
        if (!mkdirRootfsResult.isSuccess) {
            val errorMsg = (mkdirRootfsResult.out + mkdirRootfsResult.err).joinToString("\n").trim()
            throw Exception("Failed to create rootfs directory: $errorMsg")
        }

        logger.i("Extracting tarball to $rootfsPath...")
        val isXz = tarball.name.lowercase().endsWith(".xz")
        val extractCmd = if (isXz) {
            "cd \"$rootfsPath\" && $BUSYBOX_PATH xzcat \"${tarball.absolutePath}\" | $BUSYBOX_PATH tar -xpf - 2>&1"
        } else {
            "cd \"$rootfsPath\" && $BUSYBOX_PATH tar -xzpf \"${tarball.absolutePath}\" 2>&1"
        }

        val extractResult = SuExec.cmd(extractCmd).exec()
        if (!extractResult.isSuccess) {
            throw Exception("Failed to extract tarball: ${extractResult.err.joinToString("\n")}")
        }
        logger.i("Tarball extracted successfully")
    }

    private suspend fun writeConfigRooted(
        context: Context,
        config: ContainerInfo,
        configFilePath: String,
        sanitizedName: String,
        logger: ContainerLogger
    ) {
        val tempConfigFile = File("${context.cacheDir}/container_$sanitizedName.config")
        tempConfigFile.writeText(config.toConfigContent())

        val copyResult = SuExec.cmd("cp \"${tempConfigFile.absolutePath}\" \"$configFilePath\" 2>&1").exec()
        if (!copyResult.isSuccess) {
            val errorOutput = (copyResult.out + copyResult.err).joinToString("\n").trim()
            tempConfigFile.delete()
            throw Exception("Failed to write container config: ${errorOutput.ifEmpty { "exit ${copyResult.code}" }}")
        }
        SuExec.cmd("chmod 644 \"$configFilePath\" 2>&1").exec()
        tempConfigFile.delete()
    }

    private suspend fun writeEnvRooted(
        context: Context,
        config: ContainerInfo,
        envFilePath: String,
        sanitizedName: String,
        logger: ContainerLogger
    ) {
        val tempEnvFile = File("${context.cacheDir}/.env_$sanitizedName")
        try {
            tempEnvFile.writeText(config.envFileContent!! + "\n")
            val envCopyResult = SuExec.cmd("cp \"${tempEnvFile.absolutePath}\" \"$envFilePath\" 2>&1").exec()
            if (!envCopyResult.isSuccess) {
                logger.w("Warning: Failed to copy .env file: ${envCopyResult.err.joinToString()}")
            } else {
                SuExec.cmd("chmod 644 \"$envFilePath\"").exec()
                logger.i("Environment variables saved")
            }
        } catch (e: Exception) {
            logger.w("Warning: Failed to write environment variables: ${e.message}")
        } finally {
            tempEnvFile.delete()
        }
    }

    // ── Post-extraction fixes ────────────────────────────────────────────

    private suspend fun applyPostExtractionFixes(
        context: Context,
        rootfsPath: String,
        rootless: Boolean,
        logger: ContainerLogger
    ) {
        logger.i("Applying post-extraction fixes...")

        val postFixScriptFile = File("${context.cacheDir}/post_extract_fixes.sh")
        try {
            context.assets.open("post_extract_fixes.sh").use { input ->
                FileOutputStream(postFixScriptFile).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: Exception) {
            logger.w("Warning: Failed to load post_extract_fixes.sh from assets: ${e.message}")
            return
        }

        postFixScriptFile.setExecutable(true)

        try {
            val result = if (rootless) {
                val appBusybox = ensureAppBusybox(context)
                val process = ProcessBuilder(
                    "sh", "-c",
                    "BUSYBOX_PATH='$appBusybox' '${postFixScriptFile.absolutePath}' '$rootfsPath' 2>&1"
                ).redirectErrorStream(true).start()
                val output = process.inputStream.bufferedReader().readText()
                val exitCode = process.waitFor()
                ShellResult(exitCode == 0, output.lines(), emptyList(), exitCode)
            } else {
                SuExec.cmd("BUSYBOX_PATH=$BUSYBOX_PATH \"${postFixScriptFile.absolutePath}\" \"$rootfsPath\" 2>&1").exec()
            }

            result.out.forEach { line ->
                val trimmed = line.trim()
                if (trimmed.isNotEmpty()) {
                    when {
                        trimmed.startsWith("[POST-FIX-WARN]") -> logger.w(trimmed)
                        trimmed.startsWith("[POST-FIX]") -> logger.i(trimmed)
                        else -> logger.d(trimmed)
                    }
                }
            }

            if (!result.isSuccess) {
                logger.w("Warning: Post-extraction fixes failed, but continuing installation")
            } else {
                logger.i("Post-extraction fixes completed successfully")
            }
        } finally {
            postFixScriptFile.delete()
        }
    }

    // ── Cleanup ──────────────────────────────────────────────────────────

    private suspend fun cleanupPaths(paths: List<String>, rootless: Boolean, logger: ContainerLogger) {
        paths.reversed().forEach { path ->
            try {
                if (rootless) {
                    File(path).deleteRecursively()
                } else {
                    val result = SuExec.cmd("rm -rf $path 2>&1").exec()
                    if (!result.isSuccess) {
                        logger.w("Failed to clean up: $path")
                    }
                }
                logger.d("Cleaned up: $path")
            } catch (e: Exception) {
                logger.w("Error cleaning up $path: ${e.message}")
            }
        }
    }

    // ── Tarball extension detection ──────────────────────────────────────

    private suspend fun getTarballExtension(context: Context, uri: Uri): String = withContext(Dispatchers.IO) {
        val fileName = FilePickerUtils.getFileName(context, uri)
        if (fileName != null) {
            val nameLower = fileName.lowercase()
            return@withContext when {
                nameLower.endsWith(".tar.xz") -> ".xz"
                nameLower.endsWith(".tar.gz") -> ".gz"
                else -> ".gz"
            }
        }
        val uriString = uri.toString().lowercase()
        when {
            uriString.endsWith(".tar.xz") -> ".xz"
            uriString.endsWith(".tar.gz") -> ".gz"
            else -> ".gz"
        }
    }
}
