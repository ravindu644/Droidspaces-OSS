package com.droidspaces.app.util

import android.content.Context
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

object BinaryInstaller {
    private const val INSTALL_PATH = Constants.INSTALL_PATH

    /**
     * Install the droidspaces and busybox binaries for this device's arch.
     */
    suspend fun install(context: Context): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val droidspacesBinaryName = "droidspaces-${DeviceArch.suffix(context)}"
            val busyboxBinaryName = "busybox-${DeviceArch.suffix(context)}"

            // Always install to the canonical path. The daemon's g_self_path fix
            // means this is safe even while the daemon is running - the mv is
            // atomic and the daemon automatically re-execs the new binary.
            val droidspacesTargetPath = Constants.DROIDSPACES_BINARY_PATH
            val busyboxTargetPath = Constants.BUSYBOX_BINARY_PATH

            // Step 2: Create directories
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
                val assetManager = context.assets
                val inputStream = assetManager.open("binaries/$assetName")

                // Write to a temp file in app cache first (we can write here without root)
                val tempFile = File("${context.cacheDir}/$assetName")
                FileOutputStream(tempFile).use { output ->
                    inputStream.copyTo(output)
                }
                inputStream.close()

                // The rename inside installFileDurably also dodges "text file busy" while the
                // daemon is running the old binary.
                val installResult = ContainerManager.installFileDurably(tempFile.absolutePath, targetPath, "755")
                tempFile.delete()
                if (!installResult.isSuccess) {
                    return Result.failure(
                        Exception("Failed to install $displayName: ${(installResult.out + installResult.err).joinToString()}")
                    )
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

            if (!Shell.cmd("test -x $droidspacesTargetPath").exec().isSuccess)
                return@withContext Result.failure(Exception("Droidspaces binary verification failed: file is not executable"))
            if (!Shell.cmd("test -x $busyboxTargetPath").exec().isSuccess)
                return@withContext Result.failure(Exception("Busybox binary verification failed: file is not executable"))

            Result.success(Unit)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /**
     * After a live binary swap, send SIGUSR2 to the running daemon
     * so it acknowledges the update (used for logging).
     */
    suspend fun signalDaemon(): Unit = withContext(Dispatchers.IO) {
        val pidResult = Shell.cmd("cat ${Constants.DAEMON_PID_FILE} 2>/dev/null").exec()
        if (pidResult.isSuccess && pidResult.out.isNotEmpty()) {
            val pid = pidResult.out[0].trim()
            if (pid.isNotEmpty()) {
                Shell.cmd("kill -USR2 $pid 2>/dev/null").exec()
            }
        }
    }

}

