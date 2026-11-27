package com.droidspaces.app.util

import android.content.Context
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * Native Kotlin implementation of the Sparse Image Installer.
 * Optimized for stability on Samsung and other devices with strict kernel policies.
 * Uses libsu directly to manage the lifecycle: Truncate -> Format -> Mount -> Extract -> Unmount.
 */
object SparseImageInstaller {
    private const val TAG = "SparseImageInstaller"

    /* imgPath is chosen by the user now, so it must never reach the shell unquoted. */
    private fun quote(value: String) = ContainerCommandBuilder.quote(value)

    /**
     * Extracts a tarball into a sparse image file.
     *
     * @param context App context
     * @param tarball Path of the source tarball, readable by root
     * @param imgPath The target path for the rootfs.img
     * @param mountPoint The temporary directory where the image will be mounted
     * @param sizeGB The desired size of the sparse image in GB
     * @param logger Logger for real-time progress updates
     */
    suspend fun extract(
        context: Context,
        tarball: String,
        imgPath: String,
        mountPoint: String,
        sizeGB: Int,
        logger: ViewModelLogger
    ) = withContext(Dispatchers.IO) {
        try {
            // 1. Create Sparse Image
            logger.i("[SPARSE] Creating sparse image: ${sizeGB}GB")
            // BusyBox first, as with the mount below: both are a plain ftruncate and
            // give a byte-identical sparse file, so prefer the one that is the same
            // binary on every device.
            val truncateCmd = "${Constants.BUSYBOX_BINARY_PATH} truncate -s ${sizeGB}G ${quote(imgPath)} || " +
                "truncate -s ${sizeGB}G ${quote(imgPath)}"
            runRootCommand(truncateCmd, logger) ?: throw Exception("Failed to create sparse image file")

            // Wait for filesystem to settle
            Shell.cmd("${Constants.BUSYBOX_BINARY_PATH} sync").exec()
            delay(1000)

            // 2. Format as ext4
            logger.i("[SPARSE] Formatting sparse image as ext4...")
            val mkfsCmd = "mkfs.ext4 -F -E lazy_itable_init=0,lazy_journal_init=0 -L droidspaces-rootfs ${quote(imgPath)} || mke2fs -t ext4 -F -E lazy_itable_init=0,lazy_journal_init=0 -L droidspaces-rootfs ${quote(imgPath)}"
            runRootCommand(mkfsCmd, logger) ?: throw Exception("Failed to format sparse image as ext4")

            // 2b. Reclaim reserved blocks (tune2fs -m 0)
            logger.i("[SPARSE] Reclaiming reserved disk space (tune2fs -m 0)...")
            runRootCommand("tune2fs -m 0 ${quote(imgPath)}", logger)

            // 2c. Verify with e2fsck
            logger.i("[SPARSE] Verifying filesystem integrity (e2fsck)...")
            val checkResult = Shell.cmd("e2fsck -fy ${quote(imgPath)}").exec()
            // e2fsck exit codes: 0 (No Errors), 1 (Corrected), 2 (Reboot suggested - safe for us), 4 (Uncorrected)
            if (checkResult.code >= 4) {
                logger.e("[SPARSE] e2fsck failed with exit code ${checkResult.code}")
                throw Exception("Filesystem verification failed (e2fsck error ${checkResult.code})")
            }
            logger.i("[SPARSE] Filesystem integrity verified (code ${checkResult.code})")

            // 2d. Settle Delay (Samsung/Kernel 3.18 stability)
            logger.i("[SPARSE] Waiting for filesystem to settle (2.5s)...")
            Shell.cmd("${Constants.BUSYBOX_BINARY_PATH} sync").exec()
            delay(2500)

            // 3. Create Mount Point
            logger.i("[SPARSE] Creating mount point: $mountPoint")
            runRootCommand("mkdir -p ${quote(mountPoint)}", logger) ?: throw Exception("Failed to create mount point")

            // 3b. Apply correct SELinux context to the image file
            logger.i("[SPARSE] Applying SELinux context (vold_data_file)...")
            runRootCommand("chcon u:object_r:vold_data_file:s0 ${quote(imgPath)}", logger) ?: throw Exception("Failed to apply SELinux context to image")

            // 4. Mount Image (Minimal options for Max compatibility)
            logger.i("[SPARSE] Mounting sparse image (Minimal loop,rw)...")
            val mountOptions = "loop,rw,nodelalloc,noatime,nodiratime,init_itable=0"
            val mountCmd = "${Constants.BUSYBOX_BINARY_PATH} mount -t ext4 -o $mountOptions ${quote(imgPath)} ${quote(mountPoint)} || " +
                          "mount -t ext4 -o $mountOptions ${quote(imgPath)} ${quote(mountPoint)}"

            runRootCommand(mountCmd, logger) ?: throw Exception("Failed to mount sparse image. Your kernel might not support loop mounts here.")

            try {
                // 5. Extract Tarball
                logger.i("[SPARSE] Extracting tarball to mount point...")
                val extractCmd = ContainerInstaller.extractCommand(tarball, mountPoint)

                // For extraction, we stream the output to the logger's debug level
                val extractResult = Shell.cmd(extractCmd).exec()
                if (!extractResult.isSuccess) {
                    throw Exception("Tarball extraction failed: ${extractResult.err.joinToString("\n")}")
                }
                logger.i("[SPARSE] Extraction completed successfully")

                // 6. Apply Post-Extraction Fixes
                ContainerInstaller.applyPostExtractionFixes(context, mountPoint, logger)

            } finally {
                // 7. Unmount (Always attempt)
                logger.i("[SPARSE] Unmounting sparse image...")
                Shell.cmd("${Constants.BUSYBOX_BINARY_PATH} sync").exec()
                delay(1000)

                val umountCmd = "${Constants.BUSYBOX_BINARY_PATH} umount -l ${quote(mountPoint)} || umount -l ${quote(mountPoint)}"
                Shell.cmd(umountCmd).exec()

                // Cleanup mount point directory
                Shell.cmd("rmdir ${quote(mountPoint)}").exec()
            }

        } catch (e: Exception) {
            logger.e("[SPARSE] Error: ${e.message}")
            // Cleanup incomplete image on failure
            Shell.cmd("rm -f ${quote(imgPath)}").exec()
            throw e
        }
    }

    /**
     * Runs a root command and logs output. Returns result if successful, null otherwise.
     */
    private suspend fun runRootCommand(cmd: String, logger: ViewModelLogger): Shell.Result? {
        val result = Shell.cmd(cmd).exec()
        result.out.forEach { line -> if (line.isNotBlank()) logger.d(line) }
        result.err.forEach { line -> if (line.isNotBlank()) logger.w(line) }
        return if (result.isSuccess) result else null
    }
}
