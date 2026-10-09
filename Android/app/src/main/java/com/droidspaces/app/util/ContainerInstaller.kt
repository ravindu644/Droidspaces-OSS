package com.droidspaces.app.util

import android.content.Context
import android.net.Uri
import com.topjohnwu.superuser.Shell
import com.topjohnwu.superuser.io.SuFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

object ContainerInstaller {
    /* Every path below reaches a root shell, and the rootfs one is now user-chosen. */
    private fun quote(value: String) = ContainerCommandBuilder.quote(value)

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
        logger: ContainerLogger,
        preparedTarball: File? = null
    ): Result<Unit> = withContext(Dispatchers.IO) {
        // Use sanitized name for directory (spaces -> dashes)
        val sanitizedName = ContainerManager.sanitizeContainerName(config.name)
        val containerPath = ContainerManager.getContainerDirectory(config.name)
        // Honour the path the caller resolved. Re-deriving it from the name here is what
        // used to pin every container to CONTAINERS_BASE_PATH regardless of the
        // destination chosen in the wizard.
        val rootfsPath = config.rootfsPath
        val rootfsParent = rootfsPath.substringBeforeLast('/')
        val isExternal = rootfsParent != containerPath
        val configFilePath = "$containerPath/${Constants.CONTAINER_CONFIG_FILE}"
        var createdPaths = mutableListOf<String>()
        var tempTarball = preparedTarball

        try {
            // Reject control chars in single-line config values.
            ValidationUtils.validateConfigValues(config).errorMessage?.let {
                logger.e(it)
                return@withContext Result.failure(Exception(it))
            }

            // Step 1: Re-check the destination. The wizard already refused an unusable
            // one, but that decision can be stale by now and this is the only gate a
            // non-wizard caller passes through, so it fails closed.
            val target = StorageChecker.probe(rootfsParent)
            when {
                target.support == StorageSupport.UNUSABLE -> {
                    val why = target.reason ?: "'${target.fsType}' cannot hold a container."
                    logger.e(why)
                    return@withContext Result.failure(Exception(why))
                }
                target.support == StorageSupport.IMAGE_ONLY && !config.useSparseImage -> {
                    val why = target.reason
                        ?: "'${target.fsType}' cannot hold a rootfs directory. Use a sparse image."
                    logger.e(why)
                    return@withContext Result.failure(Exception(why))
                }
            }
            logger.i("Destination $rootfsParent is ${target.fsType}")

            // Step 2: Check storage space on the volume actually being written to
            logger.i("Checking available storage space...")
            val freeGB = target.freeGB
            if (freeGB != null) {
                logger.i("$rootfsParent has ${freeGB}GB free space")
                if (freeGB < Constants.MIN_STORAGE_GB) {
                    val why = "Not enough space at $rootfsParent: at least " +
                        "${Constants.MIN_STORAGE_GB}GB free is required."
                    logger.e(why)
                    return@withContext Result.failure(Exception(why))
                }
            } else {
                // Unknown is not the same as insufficient. Blocking here would make the
                // app unusable on a device where stat and df both fail, so warn instead
                // and let the extraction report the real error if space runs out.
                logger.w("Warning: Unable to determine free space. Proceeding anyway...")
            }

            // Step 3: Create container directory. This holds the config, .env and pidfile
            // and stays internal even when the rootfs lives on another volume.
            logger.i("Creating container directory: $containerPath")
            val mkdirResult = Shell.cmd("mkdir -p ${quote(containerPath)} 2>&1").exec()
            if (!mkdirResult.isSuccess) {
                val errorOutput = (mkdirResult.out + mkdirResult.err).joinToString("\n").trim()
                val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${mkdirResult.code})"
                throw Exception("Failed to create container directory: $errorMsg")
            }
            createdPaths.add(containerPath)

            if (isExternal) {
                logger.i("Creating storage directory: $rootfsParent")
                val mkdirExt = Shell.cmd("mkdir -p ${quote(rootfsParent)} 2>&1").exec()
                if (!mkdirExt.isSuccess) {
                    val errorOutput = (mkdirExt.out + mkdirExt.err).joinToString("\n").trim()
                    val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${mkdirExt.code})"
                    throw Exception("Failed to create storage directory: $errorMsg")
                }
                createdPaths.add(rootfsParent)
            }

            // Reuse the wizard's snapshot so the settings and payload come from the same archive.
            logger.i("Preparing installation archive...")
            val archive = tempTarball ?: File.createTempFile(
                "rootfs_", ".tar${getTarballExtension(context, tarballUri)}", context.cacheDir
            ).also {
                tempTarball = it
                copyTarball(context, tarballUri, it)
            }

            logger.i("Tarball ready: ${archive.absolutePath}")

            // Step 4.5: Verify the tarball is actually a Linux rootfs before we
            // extract anything, so users can't install arbitrary archives.
            validateRootfsTarball(context, archive, logger)

            // Step 5: Extract tarball (either to directory or sparse image)
            if (config.useSparseImage) {
                SparseImageInstaller.extract(
                    context = context,
                    tarball = archive,
                    imgPath = rootfsPath,
                    mountPoint = "${containerPath}/rootfs",
                    sizeGB = config.sparseImageSizeGB ?: 8,
                    logger = logger
                )
            } else {
                // Create rootfs subdirectory
                val mkdirRootfsResult = Shell.cmd("mkdir -p ${quote(rootfsPath)} 2>&1").exec()
                if (!mkdirRootfsResult.isSuccess) {
                    val errorOutput = (mkdirRootfsResult.out + mkdirRootfsResult.err).joinToString("\n").trim()
                    val errorMsg = if (errorOutput.isNotEmpty()) errorOutput else "Unknown error (exit code: ${mkdirRootfsResult.code})"
                    throw Exception("Failed to create rootfs directory: $errorMsg")
                }

                logger.i("Extracting tarball to $rootfsPath...")
                val isXz = archive.name.lowercase().endsWith(".xz")
                val extractCmd = if (isXz) {
                    "cd ${quote(rootfsPath)} && $BUSYBOX_PATH xzcat ${quote(archive.absolutePath)} | $BUSYBOX_PATH tar -xpf - 2>&1"
                } else {
                    "cd ${quote(rootfsPath)} && $BUSYBOX_PATH tar -xzpf ${quote(archive.absolutePath)} 2>&1"
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

            // Step 6: Write container config
            logger.i("Writing container configuration...")
            val configContent = config.toConfigContent()

            // Write config to temp file first (app can write to cache dir)
            // Use sanitizedName to avoid issues with spaces in filename
            val tempConfigFile = File("${context.cacheDir}/container_${sanitizedName}.config")
            tempConfigFile.writeText(configContent)

            // Copy temp config to final location using shell (root required)
            // Quote paths to handle any special characters
            val copyResult = Shell.cmd("cp ${quote(tempConfigFile.absolutePath)} ${quote(configFilePath)} 2>&1").exec()
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
            val chmodResult = Shell.cmd("chmod 644 ${quote(configFilePath)} 2>&1").exec()
            if (!chmodResult.isSuccess) {
                logger.w("Warning: Failed to set config file permissions")
            }

            // Clean up temp config file
            tempConfigFile.delete()

            logger.i("Container configuration saved")
            createdPaths.add(configFilePath)

            // Step 6.1: Write .env file if content exists
            if (!config.envFileContent.isNullOrBlank()) {
                logger.i("Writing environment variables (.env)...")
                val envFilePath = "$containerPath/.env"
                val tempEnvFile = File("${context.cacheDir}/.env_${sanitizedName}")

                try {
                    tempEnvFile.writeText(config.envFileContent + "\n")

                    val envCopyResult = Shell.cmd("cp ${quote(tempEnvFile.absolutePath)} ${quote(envFilePath)} 2>&1").exec()
                    if (!envCopyResult.isSuccess) {
                        val errorMsg = envCopyResult.err.joinToString("\n")
                        logger.w("Warning: Failed to copy .env file: $errorMsg")
                    } else {
                        Shell.cmd("chmod 644 ${quote(envFilePath)}").exec()
                        logger.i("Environment variables saved")
                        createdPaths.add(envFilePath)
                    }
                } catch (e: Exception) {
                    logger.w("Warning: Failed to write environment variables: ${e.message}")
                } finally {
                    tempEnvFile.delete()
                }
            }

            // Step 7: Verify installation
            logger.i("Verifying installation...")
            if (config.useSparseImage) {
                val imgExists = Shell.cmd("test -f ${quote(rootfsPath)} && echo 'exists' || echo 'not_found'").exec()
                if (!imgExists.isSuccess || !imgExists.out.any { it.contains("exists") }) {
                    throw Exception("Container sparse image not found after extraction")
                }
            } else {
            val rootfsExists = Shell.cmd("test -d ${quote(rootfsPath)} && echo 'exists' || echo 'not_found'").exec()
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
                tempTarball?.delete()
            } catch (e: Exception) {
                // Ignore cleanup errors
            }
        }
    }

    /**
     * Get the tarball extension (.xz or .gz) from the URI.
     * Uses FilePickerUtils.getFileName() to reliably get the filename even for recent files.
     */
    internal suspend fun getTarballExtension(context: Context, uri: Uri): String = withContext(Dispatchers.IO) {
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

    internal suspend fun copyTarball(context: Context, uri: Uri, destination: File) = withContext(Dispatchers.IO) {
        context.contentResolver.openInputStream(uri)?.use { input ->
            destination.outputStream().use { output ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    output.write(buffer, 0, count)
                }
            }
        } ?: throw Exception("Failed to open tarball input stream")
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
            // unverified rootfs, it is later run as root.
            logger.e("Failed to load rootfs validator: ${e.message}")
            throw Exception("Could not verify rootfs: validator unavailable (${e.message})")
        }

        try {
            // Make script executable
            val chmodResult = Shell.cmd("chmod 755 ${quote(scriptFile.absolutePath)} 2>&1").exec()
            if (!chmodResult.isSuccess) {
                // Fail CLOSED: never run a validator we could not make executable.
                logger.e("Failed to make rootfs validator executable")
                throw Exception("Could not verify rootfs: validator not executable")
            }

            val result = Shell.cmd(
                "BUSYBOX_PATH=$BUSYBOX_PATH ${quote(scriptFile.absolutePath)} ${quote(tarball.absolutePath)} 2>&1"
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
        val chmodResult = Shell.cmd("chmod 755 ${quote(postFixScriptFile.absolutePath)} 2>&1").exec()
        if (!chmodResult.isSuccess) {
            logger.w("Warning: Failed to make post-fix script executable")
            postFixScriptFile.delete()
            return
        }

        try {
            // Execute the script
            val result = Shell.cmd("BUSYBOX_PATH=$BUSYBOX_PATH ${quote(postFixScriptFile.absolutePath)} ${quote(rootfsPath)} 2>&1").exec()

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
