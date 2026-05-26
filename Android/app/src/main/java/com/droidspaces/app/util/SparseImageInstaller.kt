package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.util.SuExec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

/**
 * Native Kotlin implementation of the Sparse Image Installer.
 * Optimized for stability on Samsung and other devices with strict kernel policies.
 * Uses libsu directly to manage the lifecycle: Truncate -> Format -> Mount -> Extract -> Unmount.
 */
object SparseImageInstaller {
    private const val TAG = "SparseImageInstaller"

    /**
     * Extracts a tarball into a sparse image file.
     *
     * @param context App context
     * @param tarball The source tarball file in app cache
     * @param imgPath The target path for the rootfs.img
     * @param mountPoint The temporary directory where the image will be mounted
     * @param sizeGB The desired size of the sparse image in GB
     * @param logger Logger for real-time progress updates
     */
    suspend fun extract(
        context: Context,
        tarball: File,
        imgPath: String,
        mountPoint: String,
        sizeGB: Int,
        logger: ContainerLogger
    ) = withContext(Dispatchers.IO) {
        try {
            // 1. Create Sparse Image
            logger.i("[SPARSE] Creating sparse image: ${sizeGB}GB")
            val truncateCmd = "truncate -s ${sizeGB}G \"$imgPath\" || ${Constants.BUSYBOX_BINARY_PATH} truncate -s ${sizeGB}G \"$imgPath\""
            runRootCommand(truncateCmd, logger) ?: throw Exception("Failed to create sparse image file")

            // Wait for filesystem to settle
            SuExec.cmd("${Constants.BUSYBOX_BINARY_PATH} sync").exec()
            delay(1000)

            // 2. Format as ext4
            logger.i("[SPARSE] Formatting sparse image as ext4...")
            val mkfsCmd = "mkfs.ext4 -F -E lazy_itable_init=0,lazy_journal_init=0 -L \"droidspaces-rootfs\" \"$imgPath\" || mke2fs -t ext4 -F -E lazy_itable_init=0,lazy_journal_init=0 -L \"droidspaces-rootfs\" \"$imgPath\""
            runRootCommand(mkfsCmd, logger) ?: throw Exception("Failed to format sparse image as ext4")

            // 2b. Reclaim reserved blocks (tune2fs -m 0)
            logger.i("[SPARSE] Reclaiming reserved disk space (tune2fs -m 0)...")
            runRootCommand("tune2fs -m 0 \"$imgPath\"", logger)

            // 2c. Verify with e2fsck
            logger.i("[SPARSE] Verifying filesystem integrity (e2fsck)...")
            val checkResult = SuExec.cmd("e2fsck -fy \"$imgPath\"").exec()
            // e2fsck exit codes: 0 (No Errors), 1 (Corrected), 2 (Reboot suggested - safe for us), 4 (Uncorrected)
            if (checkResult.code >= 4) {
                logger.e("[SPARSE] e2fsck failed with exit code ${checkResult.code}")
                throw Exception("Filesystem verification failed (e2fsck error ${checkResult.code})")
            }
            logger.i("[SPARSE] Filesystem integrity verified (code ${checkResult.code})")

            // 2d. Settle Delay (Samsung/Kernel 3.18 stability)
            logger.i("[SPARSE] Waiting for filesystem to settle (2.5s)...")
            SuExec.cmd("${Constants.BUSYBOX_BINARY_PATH} sync").exec()
            delay(2500)

            // 3. Create Mount Point
            logger.i("[SPARSE] Creating mount point: $mountPoint")
            runRootCommand("mkdir -p \"$mountPoint\"", logger) ?: throw Exception("Failed to create mount point")

            // 3b. Apply correct SELinux context to the image file
            logger.i("[SPARSE] Applying SELinux context (vold_data_file)...")
            runRootCommand("chcon u:object_r:vold_data_file:s0 \"$imgPath\"", logger) ?: throw Exception("Failed to apply SELinux context to image")

            // 4. Mount Image (Minimal options for Max compatibility)
            logger.i("[SPARSE] Mounting sparse image (Minimal loop,rw)...")
            val mountOptions = "loop,rw,nodelalloc,noatime,nodiratime,init_itable=0"
            val mountCmd = "${Constants.BUSYBOX_BINARY_PATH} mount -t ext4 -o $mountOptions \"$imgPath\" \"$mountPoint\" || " +
                          "mount -t ext4 -o $mountOptions \"$imgPath\" \"$mountPoint\""

            runRootCommand(mountCmd, logger) ?: throw Exception("Failed to mount sparse image. Your kernel might not support loop mounts here.")

            try {
                // 5. Extract Tarball
                logger.i("[SPARSE] Extracting tarball to mount point...")
                val isXz = tarball.name.lowercase().endsWith(".xz")
                val extractCmd = if (isXz) {
                    "cd \"$mountPoint\" && ${Constants.BUSYBOX_BINARY_PATH} xzcat \"${tarball.absolutePath}\" | ${Constants.BUSYBOX_BINARY_PATH} tar -xpf - 2>&1"
                } else {
                    "cd \"$mountPoint\" && ${Constants.BUSYBOX_BINARY_PATH} tar -xzpf \"${tarball.absolutePath}\" 2>&1"
                }

                // For extraction, we stream the output to the logger's debug level
                val extractResult = SuExec.cmd(extractCmd).exec()
                if (!extractResult.isSuccess) {
                    throw Exception("Tarball extraction failed: ${extractResult.err.joinToString("\n")}")
                }
                logger.i("[SPARSE] Extraction completed successfully")

                // 6. Apply Post-Extraction Fixes (using script as requested)
                applyScriptFixes(context, mountPoint, logger)

            } finally {
                // 7. Unmount (Always attempt)
                logger.i("[SPARSE] Unmounting sparse image...")
                SuExec.cmd("${Constants.BUSYBOX_BINARY_PATH} sync").exec()
                delay(1000)

                val umountCmd = "${Constants.BUSYBOX_BINARY_PATH} umount -l \"$mountPoint\" || umount -l \"$mountPoint\""
                SuExec.cmd(umountCmd).exec()

                // Cleanup mount point directory
                SuExec.cmd("rmdir \"$mountPoint\"").exec()
            }

        } catch (e: Exception) {
            logger.e("[SPARSE] Error: ${e.message}")
            // Cleanup incomplete image on failure
            SuExec.cmd("rm -f \"$imgPath\"").exec()
            throw e
        }
    }

    /**
     * Runs a root command and logs output. Returns result if successful, null otherwise.
     */
    private suspend fun runRootCommand(cmd: String, logger: ContainerLogger): ShellResult? {
        val result = SuExec.cmd(cmd).exec()
        result.out.forEach { line -> if (line.isNotBlank()) logger.d(line) }
        result.err.forEach { line -> if (line.isNotBlank()) logger.w(line) }
        return if (result.isSuccess) result else null
    }

    /**
     * Runs the post_extract_fixes.sh script on the given rootfs path.
     */
    private suspend fun applyScriptFixes(context: Context, rootfs: String, logger: ContainerLogger) {
        logger.i("[POST-FIX] Running post-extraction fixes script...")
        val postFixScriptFile = File("${context.cacheDir}/post_extract_fixes.sh")
        try {
            context.assets.open("post_extract_fixes.sh").use { inputStream ->
                FileOutputStream(postFixScriptFile).use { outputStream ->
                    inputStream.copyTo(outputStream)
                }
            }
            SuExec.cmd("chmod 755 \"${postFixScriptFile.absolutePath}\"").exec()

            val result = SuExec.cmd("BUSYBOX_PATH=${Constants.BUSYBOX_BINARY_PATH} \"${postFixScriptFile.absolutePath}\" \"$rootfs\" 2>&1").exec()

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
                logger.w("Warning: Post-extraction fixes failed, but continuing.")
            } else {
                logger.i("[POST-FIX] Fixes applied successfully")
            }
        } catch (e: Exception) {
            logger.w("Warning: Failed to run post-fix script: ${e.message}")
        } finally {
            postFixScriptFile.delete()
        }
    }
}
