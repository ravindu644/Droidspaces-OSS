package com.droidspaces.app.util

import android.content.Context
import android.os.Build
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

sealed class InstallationStep {
    data class DetectingArchitecture(val arch: String) : InstallationStep()
    data class CreatingDirectories(val path: String) : InstallationStep()
    data class CopyingBinary(val binary: String) : InstallationStep()
    data class SettingPermissions(val path: String) : InstallationStep()
    data class Verifying(val path: String) : InstallationStep()
    object Success : InstallationStep()
    data class Error(val message: String) : InstallationStep()
}

object BinaryInstaller {
    private const val INSTALL_PATH = Constants.INSTALL_PATH
    private const val DROIDSPACES_BINARY_NAME = Constants.DROIDSPACES_BINARY_NAME
    private const val BUSYBOX_BINARY_NAME = Constants.BUSYBOX_BINARY_NAME

    /**
     * Map Android architecture to binary name suffix
     */
    private fun getArchitectureSuffix(): String = DeviceArch.suffix()

    /**
     * Get droidspaces binary name for architecture
     */
    private fun getDroidspacesBinaryName(): String {
        return "droidspaces-${getArchitectureSuffix()}"
    }

    /**
     * Get busybox binary name for architecture
     */
    private fun getBusyboxBinaryName(): String {
        return "busybox-${getArchitectureSuffix()}"
    }


    /**
     * Get human-readable architecture name
     */
    fun getArchitectureName(): String = DeviceArch.displayName()

    /**
     * Install droidspaces binary with progress updates
     */
    suspend fun install(
        context: Context,
        onProgress: (InstallationStep) -> Unit
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            // Step 1: Detect architecture
            val arch = getArchitectureName()
            onProgress(InstallationStep.DetectingArchitecture(arch))

            val droidspacesBinaryName = getDroidspacesBinaryName()
            val busyboxBinaryName = getBusyboxBinaryName()

            // Always install to the canonical path. The move is atomic, so an
            // already-running daemon can keep using its old inode until the app
            // restarts the daemon after the swap.
            val droidspacesTargetPath = Constants.DROIDSPACES_BINARY_PATH
            val busyboxTargetPath = Constants.BUSYBOX_BINARY_PATH

            // Step 2: Create directories
            onProgress(InstallationStep.CreatingDirectories(INSTALL_PATH))
            val mkdirResult = Shell.cmd("mkdir -p $INSTALL_PATH").exec()
            if (!mkdirResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to create directory: ${mkdirResult.err.joinToString()}")
                )
            }

            // Helper function to install a binary
            // Uses atomic move operation to avoid "text file busy" error when binary is running.
            // Strategy: Copy to temp file in same directory, then use mv (atomic on same filesystem).
            // This works even if the target binary is currently executing.
            fun installBinary(assetName: String, targetPath: String, displayName: String): Result<Unit> {
                onProgress(InstallationStep.CopyingBinary(displayName))
                val assetManager = context.assets
                val inputStream = assetManager.open("binaries/$assetName")

                // Write to a temp file in app cache first (we can write here without root)
                val tempFile = File("${context.cacheDir}/$assetName")
                FileOutputStream(tempFile).use { output ->
                    inputStream.copyTo(output)
                }
                inputStream.close()

                // Create temp file path in the same directory as target (same filesystem for atomic move)
                // Using .tmp suffix - the move is atomic so no race condition
                val tempTargetPath = "$targetPath.tmp"

                // Step 1: Copy from app cache to temp location in target directory (requires root)
                val copyResult = Shell.cmd("cp ${tempFile.absolutePath} $tempTargetPath 2>&1").exec()
                if (!copyResult.isSuccess) {
                    tempFile.delete()
                    return Result.failure(
                        Exception("Failed to copy $displayName to temp location: ${copyResult.err.joinToString()}")
                    )
                }
                tempFile.delete() // Clean up app cache temp file

                // Step 2: Set permissions on temp file (must be done before move)
                val chmodResult = Shell.cmd("chmod 755 $tempTargetPath 2>&1").exec()
                if (!chmodResult.isSuccess) {
                    Shell.cmd("rm -f $tempTargetPath 2>&1").exec() // Clean up temp file
                    return Result.failure(
                        Exception("Failed to set permissions for $displayName: ${chmodResult.err.joinToString()}")
                    )
                }

                // Step 3: Use atomic move (mv -f) to replace target file
                // mv is atomic on the same filesystem - it just renames the inode
                // This works even if the target binary is currently executing (no "text file busy" error)
                onProgress(InstallationStep.SettingPermissions(targetPath))
                val moveResult = Shell.cmd("mv -f $tempTargetPath $targetPath 2>&1").exec()
                if (!moveResult.isSuccess) {
                    Shell.cmd("rm -f $tempTargetPath 2>&1").exec() // Clean up temp file on failure
                    return Result.failure(
                        Exception("Failed to install $displayName: ${moveResult.err.joinToString()}")
                    )
                }

                // Step 4: Final permission check (mv preserves permissions, but ensure they're correct)
                val verifyChmodResult = Shell.cmd("chmod 755 $targetPath 2>&1").exec()
                if (!verifyChmodResult.isSuccess) {
                    // Non-fatal warning - permissions might already be correct
                    // Continue as the move succeeded
                }

                return Result.success(Unit)
            }

            // Step 3: Install droidspaces binary
            installBinary(droidspacesBinaryName, droidspacesTargetPath, "droidspaces")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Step 4: Install busybox binary
            installBinary(busyboxBinaryName, busyboxTargetPath, "busybox")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Step 5: Verification (scripts are handled by ModuleInstaller)

            onProgress(InstallationStep.Verifying("droidspaces and busybox"))
            val verifyDroidspaces = Shell.cmd("test -x $droidspacesTargetPath && echo 'verified' || echo 'verification_failed'").exec()
            val verifyBusybox = Shell.cmd("test -x $busyboxTargetPath && echo 'verified' || echo 'verification_failed'").exec()

            if (!verifyDroidspaces.isSuccess || !verifyDroidspaces.out.any { it.contains("verified") }) {
                return@withContext Result.failure(
                    Exception("Droidspaces binary verification failed: file is not executable")
                )
            }

            if (!verifyBusybox.isSuccess || !verifyBusybox.out.any { it.contains("verified") }) {
                return@withContext Result.failure(
                    Exception("Busybox binary verification failed: file is not executable")
                )
            }

            // Success
            onProgress(InstallationStep.Success)
            Result.success(Unit)

        } catch (e: Exception) {
            onProgress(InstallationStep.Error(e.message ?: "Unknown error"))
            Result.failure(e)
        }
    }

    /**
     * Restart the complete daemon tree after an atomic backend update.
     *
     * Re-executed CLI sessions already resolve the new canonical binary, but
     * the daemon's embedded socketd bridge stays mapped to the old inode. If a
     * release changes an internal request/config layout, mixing that old bridge
     * with the new CLI can reinterpret unrelated flags. Restarting the parent
     * also terminates its bridge through PR_SET_PDEATHSIG, so both processes
     * come back from the same binary.
     */
    suspend fun restartDaemon(): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val oldPid = Shell.cmd("cat ${Constants.DAEMON_PID_FILE} 2>/dev/null")
                .exec().out.firstOrNull()?.trim()?.toIntOrNull()

            if (oldPid != null && oldPid > 1) {
                Shell.cmd("kill -TERM $oldPid 2>/dev/null").exec()

                var alive = true
                for (attempt in 0 until 30) {
                    alive = Shell.cmd("kill -0 $oldPid 2>/dev/null").exec().isSuccess
                    if (!alive) break
                    Thread.sleep(100)
                }
                if (alive) {
                    Shell.cmd("kill -KILL $oldPid 2>/dev/null").exec()
                    Thread.sleep(100)
                }
            }

            // Preserve the daemon SELinux entrypoint label used by the module.
            Shell.cmd(
                "chcon u:object_r:droidspacesd_exec:s0 ${Constants.DROIDSPACES_BINARY_PATH} 2>/dev/null"
            ).exec()

            val launch = Shell.cmd("${Constants.DROIDSPACES_BINARY_PATH} daemon 2>&1").exec()
            if (!launch.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to restart Droidspaces daemon: ${launch.err.joinToString()}")
                )
            }

            repeat(30) {
                val pid = Shell.cmd("cat ${Constants.DAEMON_PID_FILE} 2>/dev/null")
                    .exec().out.firstOrNull()?.trim()?.toIntOrNull()
                if (pid != null && pid > 1 &&
                    Shell.cmd("kill -0 $pid 2>/dev/null").exec().isSuccess
                ) {
                    return@withContext Result.success(Unit)
                }
                Thread.sleep(100)
            }

            Result.failure(Exception("Droidspaces daemon did not become ready after update"))
        } catch (error: Exception) {
            Result.failure(error)
        }
    }

    /**
     * Check if binaries are already installed
     */
    suspend fun isInstalled(): Boolean = withContext(Dispatchers.IO) {
        val droidspacesResult = Shell.cmd("test -x $INSTALL_PATH/$DROIDSPACES_BINARY_NAME && echo 'installed' || echo 'not_installed'").exec()
        val busyboxResult = Shell.cmd("test -x $INSTALL_PATH/$BUSYBOX_BINARY_NAME && echo 'installed' || echo 'not_installed'").exec()
        val droidspacesOk = droidspacesResult.isSuccess && droidspacesResult.out.any { it.contains("installed") }
        val busyboxOk = busyboxResult.isSuccess && busyboxResult.out.any { it.contains("installed") }

        droidspacesOk && busyboxOk
    }
}

