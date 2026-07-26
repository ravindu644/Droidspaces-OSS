package com.droidspaces.app.util

import android.content.Context
import android.net.Uri
import com.topjohnwu.superuser.Shell
import com.topjohnwu.superuser.io.SuFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

object ContainerInstaller {
    private const val CONTAINERS_BASE_PATH = Constants.CONTAINERS_BASE_PATH
    private const val BUSYBOX_PATH = Constants.BUSYBOX_BINARY_PATH

    /**
     * Extract tarball and install container.
     * Returns Result.success on success, Result.failure on error.
     * On failure, automatically cleans up created files.
     */
    suspend fun installContainer(
        context: Context,
        tarballUri: Uri,
        config: ContainerInfo,
        logger: ContainerLogger
    ): Result<Unit> = withContext(Dispatchers.IO) {
        // Use sanitized name for directory (spaces -> dashes)
        val sanitizedName = ContainerManager.sanitizeContainerName(config.name)
        val containerPath = ContainerManager.getContainerDirectory(config.name)
        val rootfsPath = if (config.useSparseImage) {
            ContainerManager.getSparseImagePath(config.name)
        } else {
            ContainerManager.getRootfsPath(config.name)
        }
        val configFilePath = "$containerPath/${Constants.CONTAINER_CONFIG_FILE}"
        var createdPaths = mutableListOf<String>()

        try {
            // Reject control chars in single-line config values (VULN V11).
            ValidationUtils.validateConfigValues(config).errorMessage?.let {
                logger.e(it)
                return@withContext Result.failure(Exception(it))
            }

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
            val mkdirResult = Shell.cmd("mkdir -p \"$containerPath\" 2>&1").exec()
            if (!mkdirResult.isSuccess) {
                val errorOutput = (mkdirResult.out + mkdirResult.err).joinToString("\n").trim()
                val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${mkdirResult.code})"
                throw Exception("Failed to create container directory: $errorMsg")
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

            // Step 3.5: Verify the tarball is actually a Linux rootfs before we
            // extract anything, so users can't install arbitrary archives.
            validateRootfsTarball(context, tempTarball, logger)

            // Step 4: Extract tarball (either to directory or sparse image)
            if (config.useSparseImage) {
                SparseImageInstaller.extract(
                    context = context,
                    tarball = tempTarball,
                    imgPath = rootfsPath,
                    mountPoint = "${containerPath}/rootfs",
                    sizeGB = config.sparseImageSizeGB ?: 8,
                    logger = logger
                )
            } else {
                // Create rootfs subdirectory
                val mkdirRootfsResult = Shell.cmd("mkdir -p \"$rootfsPath\" 2>&1").exec()
                if (!mkdirRootfsResult.isSuccess) {
                    val errorOutput = (mkdirRootfsResult.out + mkdirRootfsResult.err).joinToString("\n").trim()
                    val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${mkdirRootfsResult.code})"
                    throw Exception("Failed to create rootfs directory: $errorMsg")
                }

                logger.i("Extracting tarball to $rootfsPath...")
                val isXz = tempTarball.name.lowercase().endsWith(".xz")
                val extractCmd = if (isXz) {
                    "cd \"$rootfsPath\" && $BUSYBOX_PATH xzcat \"${tempTarball.absolutePath}\" | $BUSYBOX_PATH tar -xpf - 2>&1"
                } else {
                    "cd \"$rootfsPath\" && $BUSYBOX_PATH tar -xzpf \"${tempTarball.absolutePath}\" 2>&1"
                }

                val extractResult = Shell.cmd(extractCmd).exec()
                if (!extractResult.isSuccess) {
                    val errorMsg = extractResult.err.joinToString("\n")
                    logger.e("Extraction failed: $errorMsg")
                    throw Exception("Failed to extract tarball: $errorMsg")
                }

                logger.i("Tarball extracted successfully")

                // Apply post-extraction fixes
                applyPostExtractionFixes(context, rootfsPath, logger)
            }

            // Step 5: Write container config
            logger.i("Writing container configuration...")
            val configContent = config.toConfigContent()

            // Write config to temp file first (app can write to cache dir)
            // Use sanitizedName to avoid issues with spaces in filename
            val tempConfigFile = File("${context.cacheDir}/container_${sanitizedName}.config")
            tempConfigFile.writeText(configContent)

            // Copy temp config to final location using shell (root required)
            // Quote paths to handle any special characters
            val copyResult = Shell.cmd("cp \"${tempConfigFile.absolutePath}\" \"$configFilePath\" 2>&1").exec()
            if (!copyResult.isSuccess) {
                // Check both stdout and stderr for error messages
                val errorOutput = (copyResult.out + copyResult.err).joinToString("\n").trim()
                val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${copyResult.code})"
                logger.e("Failed to copy config: $errorMsg")
                logger.e("Source: ${tempConfigFile.absolutePath}")
                logger.e("Destination: $configFilePath")
                throw Exception("Failed to write container config: $errorMsg")
            }

            // Set proper permissions
            val chmodResult = Shell.cmd("chmod 644 \"$configFilePath\" 2>&1").exec()
            if (!chmodResult.isSuccess) {
                logger.w("Warning: Failed to set config file permissions")
            }

            // Clean up temp config file
            tempConfigFile.delete()

            logger.i("Container configuration saved")
            createdPaths.add(configFilePath)

            // Step 5.1: Write .env file if content exists
            if (!config.envFileContent.isNullOrBlank()) {
                logger.i("Writing environment variables (.env)...")
                val envFilePath = "$containerPath/.env"
                val tempEnvFile = File("${context.cacheDir}/.env_${sanitizedName}")

                try {
                    tempEnvFile.writeText(config.envFileContent + "\n")

                    val envCopyResult = Shell.cmd("cp \"${tempEnvFile.absolutePath}\" \"$envFilePath\" 2>&1").exec()
                    if (!envCopyResult.isSuccess) {
                        val errorMsg = envCopyResult.err.joinToString("\n")
                        logger.w("Warning: Failed to copy .env file: $errorMsg")
                    } else {
                        Shell.cmd("chmod 644 \"$envFilePath\"").exec()
                        logger.i("Environment variables saved")
                        createdPaths.add(envFilePath)
                    }
                } catch (e: Exception) {
                    logger.w("Warning: Failed to write environment variables: ${e.message}")
                } finally {
                    tempEnvFile.delete()
                }
            }

            // Step 6: Verify installation
            logger.i("Verifying installation...")
            if (config.useSparseImage) {
                val imgExists = Shell.cmd("test -f \"$rootfsPath\" && echo 'exists' || echo 'not_found'").exec()
                if (!imgExists.isSuccess || !imgExists.out.any { it.contains("exists") }) {
                    throw Exception("Container sparse image not found after extraction")
                }
            } else {
            val rootfsExists = Shell.cmd("test -d \"$rootfsPath\" && echo 'exists' || echo 'not_found'").exec()
            if (!rootfsExists.isSuccess || !rootfsExists.out.any { it.contains("exists") }) {
                throw Exception("Container rootfs directory not found after extraction")
                }
            }

            logger.i("Container installed successfully!")
            Result.success(Unit)
        } catch (e: Exception) {
            logger.e("Installation failed: ${e.message}")
            logger.e(e.stackTraceToString())

            // Cleanup on failure
            logger.i("Cleaning up created files...")
            cleanup(createdPaths, logger)

            Result.failure(e)
        } finally {
            // Clean up temp tarball
            try {
                File("${context.cacheDir}/container_${sanitizedName}.tar.xz").delete()
                File("${context.cacheDir}/container_${sanitizedName}.tar.gz").delete()
            } catch (e: Exception) {
                // Ignore cleanup errors
            }
        }
    }

    /**
     * Get the tarball extension (.xz or .gz) from the URI.
     * Uses FilePickerUtils.getFileName() to reliably get the filename even for recent files.
     */
    private suspend fun getTarballExtension(context: Context, uri: Uri): String = withContext(Dispatchers.IO) {
        // First, try to get the filename using FilePickerUtils (handles content URIs)
        val fileName = FilePickerUtils.getFileName(context, uri)

        if (fileName != null) {
            val fileNameLower = fileName.lowercase()
            return@withContext when {
                fileNameLower.endsWith(".tar.xz") -> ".xz"
                fileNameLower.endsWith(".tar.gz") -> ".gz"
                else -> {
                    // Fallback: default to .gz if we can't determine
                    ".gz"
                }
            }
        }

        // Fallback: Check URI string directly (for file:// URIs)
        val uriString = uri.toString().lowercase()
        when {
            uriString.endsWith(".tar.xz") -> ".xz"
            uriString.endsWith(".tar.gz") -> ".gz"
            else -> ".gz" // Default to .gz if we can't determine
        }
    }



    /**
     * Inspect the tarball (without extracting) to confirm it contains a Linux
     * rootfs, so users can't install arbitrary archives (photo backups, source
     * zips, etc.). Throws on a confirmed-invalid archive; a validator that fails
     * to load is treated as non-fatal (warn and continue).
     */
    private suspend fun validateRootfsTarball(
        context: Context,
        tarball: File,
        logger: ContainerLogger
    ) {
        logger.i("Inspecting tarball to verify it is a Linux rootfs...")

        // Copy validator script from assets
        val scriptFile = File("${context.cacheDir}/validate_rootfs.sh")
        try {
            context.assets.open("validate_rootfs.sh").use { inputStream ->
                FileOutputStream(scriptFile).use { outputStream ->
                    inputStream.copyTo(outputStream)
                }
            }
        } catch (e: Exception) {
            // Fail CLOSED: if the validator itself can't be loaded, do not install an
            // unverified rootfs — it is later run as root. See FINDINGS_APP_VULN V12.
            logger.e("Failed to load rootfs validator: ${e.message}")
            throw Exception("Could not verify rootfs: validator unavailable (${e.message})")
        }

        try {
            // Make script executable
            val chmodResult = Shell.cmd("chmod 755 \"${scriptFile.absolutePath}\" 2>&1").exec()
            if (!chmodResult.isSuccess) {
                // Fail CLOSED — see FINDINGS_APP_VULN V12.
                logger.e("Failed to make rootfs validator executable")
                throw Exception("Could not verify rootfs: validator not executable")
            }

            val result = Shell.cmd(
                "BUSYBOX_PATH=$BUSYBOX_PATH \"${scriptFile.absolutePath}\" \"${tarball.absolutePath}\" 2>&1"
            ).exec()

            if (!result.isSuccess) {
                val reason = result.out
                    .map { it.trim() }
                    .firstOrNull { it.isNotEmpty() }
                    ?: "selected file does not look like a Linux rootfs"
                logger.e(reason)
                throw Exception(reason)
            }
        } finally {
            try {
                scriptFile.delete()
            } catch (e: Exception) {
                logger.w("Warning: Failed to clean up validator script: ${e.message}")
            }
        }
    }

    /**
     * Apply post-extraction fixes to the rootfs (both sparse and directory modes).
     */
    private suspend fun applyPostExtractionFixes(
        context: Context,
        rootfsPath: String,
        logger: ContainerLogger
    ) {
        logger.i("Applying post-extraction fixes...")

        // Copy post-extraction fix script from assets
        val postFixScriptFile = File("${context.cacheDir}/post_extract_fixes.sh")
        try {
            context.assets.open("post_extract_fixes.sh").use { inputStream ->
                FileOutputStream(postFixScriptFile).use { outputStream ->
                    inputStream.copyTo(outputStream)
                }
            }
        } catch (e: Exception) {
            logger.w("Warning: Failed to load post_extract_fixes.sh from assets: ${e.message}")
            return
        }

        // Make script executable
        val chmodResult = Shell.cmd("chmod 755 \"${postFixScriptFile.absolutePath}\" 2>&1").exec()
        if (!chmodResult.isSuccess) {
            logger.w("Warning: Failed to make post-fix script executable")
            postFixScriptFile.delete()
            return
        }

        try {
            // Execute the script
            val result = Shell.cmd("BUSYBOX_PATH=$BUSYBOX_PATH \"${postFixScriptFile.absolutePath}\" \"$rootfsPath\" 2>&1").exec()

            // Log all output from the script
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

            // Log errors
            result.err.forEach { line ->
                val trimmed = line.trim()
                if (trimmed.isNotEmpty()) {
                    logger.w(trimmed)
                }
            }

            if (!result.isSuccess) {
                logger.w("Warning: Post-extraction fixes failed, but continuing installation")
            } else {
                logger.i("Post-extraction fixes completed successfully")
            }
        } finally {
            // Clean up script file
            try {
                postFixScriptFile.delete()
            } catch (e: Exception) {
                logger.w("Warning: Failed to clean up post-fix script file: ${e.message}")
            }
        }
    }

    private suspend fun cleanup(paths: List<String>, logger: ContainerLogger) {
        paths.reversed().forEach { path ->
            try {
                val result = Shell.cmd("rm -rf ${ContainerCommandBuilder.quote(path)} 2>&1").exec()
                if (result.isSuccess) {
                    logger.d("Cleaned up: $path")
                } else {
                    logger.w("Failed to clean up: $path")
                }
            } catch (e: Exception) {
                logger.w("Error cleaning up $path: ${e.message}")
            }
        }
    }
}

