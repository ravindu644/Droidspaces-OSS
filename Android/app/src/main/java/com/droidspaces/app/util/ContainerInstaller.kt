package com.droidspaces.app.util

import android.content.Context
import android.net.Uri
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

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
        logger: ViewModelLogger
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
        var tempTarball: File? = null

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
                throw Exception("Failed to create container directory: ${mkdirResult.errorText()}")
            }
            createdPaths.add(containerPath)

            if (isExternal) {
                logger.i("Creating storage directory: $rootfsParent")
                val mkdirExt = Shell.cmd("mkdir -p ${quote(rootfsParent)} 2>&1").exec()
                if (!mkdirExt.isSuccess) {
                    throw Exception("Failed to create storage directory: ${mkdirExt.errorText()}")
                }
                createdPaths.add(rootfsParent)
            }

            // Step 4: Read the tarball where it lies. Only a provider that hands out a
            // pipe (cloud storage) still costs a full copy through cacheDir.
            val archive = FilePickerUtils.realPath(context, tarballUri) ?: run {
                logger.i("Copying tarball to temporary location...")
                val copy = File("${context.cacheDir}/container_${sanitizedName}.tar${getTarballExtension(context, tarballUri)}")
                tempTarball = copy
                context.contentResolver.openInputStream(tarballUri)?.use { input ->
                    FileOutputStream(copy).use { input.copyTo(it) }
                } ?: throw Exception("Failed to open tarball input stream")
                copy.absolutePath
            }
            logger.i("Tarball: $archive")

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
                    throw Exception("Failed to create rootfs directory: ${mkdirRootfsResult.errorText()}")
                }

                logger.i("Extracting tarball to $rootfsPath...")
                val extractResult = Shell.cmd(extractCommand(archive, rootfsPath)).exec()
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

            val copyResult = ContainerManager.installFileDurably(tempConfigFile.absolutePath, configFilePath)
            if (!copyResult.isSuccess) {
                val errorMsg = copyResult.errorText()
                logger.e("Failed to copy config: $errorMsg")
                logger.e("Source: ${tempConfigFile.absolutePath}")
                logger.e("Destination: $configFilePath")
                throw Exception("Failed to write container config: $errorMsg")
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

                    val envCopyResult = ContainerManager.installFileDurably(tempEnvFile.absolutePath, envFilePath)
                    if (!envCopyResult.isSuccess) {
                        val errorMsg = (envCopyResult.out + envCopyResult.err).joinToString("\n")
                        logger.w("Warning: Failed to copy .env file: $errorMsg")
                    } else {
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
            val test = if (config.useSparseImage) "-f" else "-d"
            if (!Shell.cmd("test $test ${quote(rootfsPath)}").exec().isSuccess)
                throw Exception("Container ${if (config.useSparseImage) "sparse image" else "rootfs directory"} not found after extraction")

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
            // Clean up temp tarball, never the user's own file
            try {
                tempTarball?.delete()
            } catch (e: Exception) {
                // Ignore cleanup errors
            }
        }
    }

    /** ".xz" for a .tar.xz, ".gz" for anything else the picker handed over. */
    private suspend fun getTarballExtension(context: Context, uri: Uri): String =
        if (FilePickerUtils.getFileName(context, uri)?.endsWith(".xz", ignoreCase = true) == true) ".xz" else ".gz"

    /**
     * Shell command that unpacks [tarball] into [destination]. An exported archive
     * carries its host config, and maybe its .env, as the first members. Those are for
     * the wizard, and extracting them would leave the old host's settings and secrets
     * lying in the guest's root.
     */
    internal fun extractCommand(tarball: String, destination: String): String {
        val skip = listOf(Constants.CONTAINER_CONFIG_FILE, RootfsConfig.ENV_MEMBER)
            .joinToString(" ") { "--exclude=$it --exclude=./$it" }
        val unpack = if (tarball.lowercase().endsWith(".xz")) {
            "$BUSYBOX_PATH xzcat ${quote(tarball)} | $BUSYBOX_PATH tar -xpf - $skip"
        } else {
            "$BUSYBOX_PATH tar -xzpf ${quote(tarball)} $skip"
        }
        return "cd ${quote(destination)} && $unpack 2>&1"
    }

    /** [name] from the assets copied into cacheDir and made executable; the caller deletes it. */
    internal fun deployAsset(context: Context, name: String): File {
        val file = File(context.cacheDir, name)
        context.assets.open(name).use { input -> file.outputStream().use { input.copyTo(it) } }
        // Fail closed: a script that could not be made executable is never run through a
        // fallback, the caller's error path is the place to say so.
        if (!Shell.cmd("chmod 755 ${quote(file.absolutePath)}").exec().isSuccess) {
            file.delete()
            error("Could not make $name executable")
        }
        return file
    }

    /**
     * Inspect the tarball (without extracting) to confirm it contains a Linux rootfs,
     * so users cannot install arbitrary archives (photo backups, source zips). Throws
     * on a confirmed-invalid archive, and on a validator that could not be deployed:
     * the rootfs is later run as root, so an unverified one is never installed.
     */
    private suspend fun validateRootfsTarball(context: Context, tarball: String, logger: ViewModelLogger) {
        logger.i("Inspecting tarball to verify it is a Linux rootfs...")
        val script = try {
            deployAsset(context, "validate_rootfs.sh")
        } catch (e: Exception) {
            logger.e("Failed to load rootfs validator: ${e.message}")
            throw Exception("Could not verify rootfs: validator unavailable (${e.message})")
        }
        try {
            val result = Shell.cmd("BUSYBOX_PATH=$BUSYBOX_PATH ${quote(script.absolutePath)} ${quote(tarball)} 2>&1").exec()
            if (!result.isSuccess) {
                val reason = result.out.map { it.trim() }.firstOrNull { it.isNotEmpty() }
                    ?: "selected file does not look like a Linux rootfs"
                logger.e(reason)
                throw Exception(reason)
            }
        } finally {
            script.delete()
        }
    }

    /** post_extract_fixes.sh over a freshly extracted rootfs; a failure is logged, never fatal. */
    internal suspend fun applyPostExtractionFixes(context: Context, rootfsPath: String, logger: ViewModelLogger) {
        logger.i("Applying post-extraction fixes...")
        val script = try {
            deployAsset(context, "post_extract_fixes.sh")
        } catch (e: Exception) {
            logger.w("Warning: Failed to load post_extract_fixes.sh: ${e.message}")
            return
        }
        try {
            val result = Shell.cmd("BUSYBOX_PATH=$BUSYBOX_PATH ${quote(script.absolutePath)} ${quote(rootfsPath)} 2>&1").exec()
            result.out.map { it.trim() }.filter { it.isNotEmpty() }.forEach { line ->
                when {
                    line.startsWith("[POST-FIX-WARN]") -> logger.w(line)
                    line.startsWith("[POST-FIX]") -> logger.i(line)
                    else -> logger.d(line)
                }
            }
            if (result.isSuccess) logger.i("Post-extraction fixes completed successfully")
            else logger.w("Warning: Post-extraction fixes failed, but continuing installation")
        } finally {
            script.delete()
        }
    }

    private suspend fun cleanup(paths: List<String>, logger: ViewModelLogger) {
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

