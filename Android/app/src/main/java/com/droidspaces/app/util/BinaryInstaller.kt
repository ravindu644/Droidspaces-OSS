package com.droidspaces.app.util

import android.content.Context
import android.os.Build
import com.droidspaces.app.util.SuExec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipFile

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
    private const val RUNNER_LIB_PATH = Constants.RUNNER_LIB_PATH
    private const val BUSYBOX_BINARY_NAME = Constants.BUSYBOX_BINARY_NAME
    private const val MAGISKPOLICY_BINARY_NAME = Constants.MAGISKPOLICY_BINARY_NAME
    private const val RUNNER_BINARY_NAME = Constants.RUNNER_BINARY_NAME

    /**
     * Map Android architecture to binary name suffix
     */
    private fun getArchitectureSuffix(): String {
        val arch = Build.SUPPORTED_ABIS[0]
        return when {
            arch.contains("arm64") || arch.contains("aarch64") -> "aarch64"
            arch.contains("armeabi") || arch.contains("arm") -> "armhf"
            arch.contains("x86_64") -> "x86_64"
            arch.contains("x86") -> "x86"
            else -> "aarch64"
        }
    }

    /**
     * Map Android ABI to APK lib directory subfolder
     */
    private fun getAbiFolder(): String {
        val arch = Build.SUPPORTED_ABIS[0]
        return when {
            arch.contains("arm64") || arch.contains("aarch64") -> "arm64-v8a"
            arch.contains("armeabi") || arch.contains("arm") -> "armeabi-v7a"
            arch.contains("x86_64") -> "x86_64"
            arch.contains("x86") -> "x86"
            else -> "arm64-v8a"
        }
    }

    private fun getBusyboxBinaryName(): String = "busybox-${getArchitectureSuffix()}"
    private fun getMagiskpolicyBinaryName(): String = "magiskpolicy-${getArchitectureSuffix()}"
    private fun getRunnerBinaryName(): String = "runner-${getArchitectureSuffix()}"

    /**
     * Get human-readable architecture name
     */
    fun getArchitectureName(): String {
        val arch = Build.SUPPORTED_ABIS[0]
        return when {
            arch.contains("arm64") || arch.contains("aarch64") -> "ARM64 (aarch64)"
            arch.contains("armeabi") || arch.contains("arm") -> "ARM (armhf)"
            arch.contains("x86_64") -> "x86_64"
            arch.contains("x86") -> "x86"
            else -> arch
        }
    }

    /**
     * Install binaries: busybox, magiskpolicy, droidspaces-runner, libdroidspaces.so
     */
    suspend fun install(
        context: Context,
        onProgress: (InstallationStep) -> Unit
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val arch = getArchitectureName()
            onProgress(InstallationStep.DetectingArchitecture(arch))

            val installPaths = listOf(INSTALL_PATH, RUNNER_LIB_PATH)

            // Step 2: Create directories
            onProgress(InstallationStep.CreatingDirectories(INSTALL_PATH))
            val mkdirResult = SuExec.cmd("mkdir -p ${installPaths.joinToString(" ")}").exec()
            if (!mkdirResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to create directories: ${mkdirResult.err.joinToString()}")
                )
            }

            // Helper function to install a binary from assets
            // Uses atomic move to avoid "text file busy" when replacing running binaries.
            fun installBinary(assetName: String, targetPath: String, displayName: String): Result<Unit> {
                onProgress(InstallationStep.CopyingBinary(displayName))
                val assetManager = context.assets
                val inputStream = assetManager.open("binaries/$assetName")

                val tempFile = File("${context.cacheDir}/$assetName")
                FileOutputStream(tempFile).use { output ->
                    inputStream.copyTo(output)
                }
                inputStream.close()

                val tempTargetPath = "$targetPath.tmp"

                val copyResult = SuExec.cmd("cp ${tempFile.absolutePath} $tempTargetPath 2>&1").exec()
                if (!copyResult.isSuccess) {
                    tempFile.delete()
                    return Result.failure(
                        Exception("Failed to copy $displayName to temp location: ${copyResult.err.joinToString()}")
                    )
                }
                tempFile.delete()

                val chmodResult = SuExec.cmd("chmod 755 $tempTargetPath 2>&1").exec()
                if (!chmodResult.isSuccess) {
                    SuExec.cmd("rm -f $tempTargetPath 2>&1").exec()
                    return Result.failure(
                        Exception("Failed to set permissions for $displayName: ${chmodResult.err.joinToString()}")
                    )
                }

                onProgress(InstallationStep.SettingPermissions(targetPath))
                val moveResult = SuExec.cmd("mv -f $tempTargetPath $targetPath 2>&1").exec()
                if (!moveResult.isSuccess) {
                    SuExec.cmd("rm -f $tempTargetPath 2>&1").exec()
                    return Result.failure(
                        Exception("Failed to install $displayName: ${moveResult.err.joinToString()}")
                    )
                }

                SuExec.cmd("chmod 755 $targetPath 2>&1").exec() // non-fatal

                return Result.success(Unit)
            }

            // Install busybox
            installBinary(getBusyboxBinaryName(), Constants.BUSYBOX_BINARY_PATH, "busybox")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Install magiskpolicy
            installBinary(getMagiskpolicyBinaryName(), Constants.MAGISKPOLICY_BINARY_PATH, "magiskpolicy")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Install droidspaces-runner
            installBinary(getRunnerBinaryName(), Constants.RUNNER_BINARY_PATH, "droidspaces-runner")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Install libdroidspaces.so for the runner (dlopen runtime dependency)
            onProgress(InstallationStep.CopyingBinary("libdroidspaces.so"))
            val libInstallResult = installNativeLib(context)
            if (!libInstallResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to install libdroidspaces.so: ${libInstallResult.exceptionOrNull()?.message}")
                )
            }

            // Verification
            val verifyPaths = listOf(
                Constants.BUSYBOX_BINARY_PATH to "busybox",
                Constants.MAGISKPOLICY_BINARY_PATH to "magiskpolicy",
                Constants.RUNNER_BINARY_PATH to "droidspaces-runner",
                "$RUNNER_LIB_PATH/libdroidspaces.so" to "libdroidspaces.so",
            )
            for ((path, name) in verifyPaths) {
                onProgress(InstallationStep.Verifying(name))
                val verify = SuExec.cmd("test -x '$path' && echo 'verified' || echo 'verification_failed'").exec()
                if (!verify.isSuccess || !verify.out.any { it.contains("verified") }) {
                    return@withContext Result.failure(
                        Exception("$name verification failed at $path")
                    )
                }
            }

            onProgress(InstallationStep.Success)
            Result.success(Unit)

        } catch (e: Exception) {
            onProgress(InstallationStep.Error(e.message ?: "Unknown error"))
            Result.failure(e)
        }
    }

    /**
     * Extract libdroidspaces.so from the APK and install it to /data/local/Droidspaces/lib/.
     *
     * The APK is a ZIP file; the native .so is inside at lib/<abi>/libdroidspaces.so.
     * We extract it to a temp file, then copy via su to the target location.
     */
    private fun installNativeLib(context: Context): Result<Unit> {
        try {
            val apkPath = context.applicationInfo.sourceDir
            val abiFolder = getAbiFolder()
            val entryPath = "lib/$abiFolder/libdroidspaces.so"

            val zipFile = ZipFile(apkPath)
            val entry = zipFile.getEntry(entryPath)
                ?: zipFile.getEntry("lib/${Build.SUPPORTED_ABIS[0]}/libdroidspaces.so")
                ?: return Result.failure(Exception("libdroidspaces.so not found in APK at $entryPath"))

            val tempLib = File("${context.cacheDir}/libdroidspaces.so")
            zipFile.getInputStream(entry).use { input ->
                FileOutputStream(tempLib).use { output ->
                    input.copyTo(output)
                }
            }
            zipFile.close()

            val targetPath = "$RUNNER_LIB_PATH/libdroidspaces.so"
            val tempTargetPath = "$targetPath.tmp"

            val copyResult = SuExec.cmd("cp ${tempLib.absolutePath} $tempTargetPath 2>&1").exec()
            if (!copyResult.isSuccess) {
                tempLib.delete()
                return Result.failure(Exception("Failed to copy libdroidspaces.so: ${copyResult.err.joinToString()}"))
            }
            tempLib.delete()

            val chmodResult = SuExec.cmd("chmod 755 $tempTargetPath 2>&1").exec()
            if (!chmodResult.isSuccess) {
                SuExec.cmd("rm -f $tempTargetPath 2>&1").exec()
                return Result.failure(Exception("Failed to set permissions on libdroidspaces.so"))
            }

            val moveResult = SuExec.cmd("mv -f $tempTargetPath $targetPath 2>&1").exec()
            if (!moveResult.isSuccess) {
                SuExec.cmd("rm -f $tempTargetPath 2>&1").exec()
                return Result.failure(Exception("Failed to install libdroidspaces.so: ${moveResult.err.joinToString()}"))
            }

            return Result.success(Unit)
        } catch (e: Exception) {
            return Result.failure(e)
        }
    }

    /**
     * Check if all binaries are already installed.
     */
    suspend fun isInstalled(): Boolean = withContext(Dispatchers.IO) {
        val checks = listOf(
            "$INSTALL_PATH/$BUSYBOX_BINARY_NAME" to "busybox",
            "$INSTALL_PATH/$MAGISKPOLICY_BINARY_NAME" to "magiskpolicy",
            "$INSTALL_PATH/$RUNNER_BINARY_NAME" to "droidspaces-runner",
            "$RUNNER_LIB_PATH/libdroidspaces.so" to "libdroidspaces.so",
        )
        checks.all { (path, _) ->
            val result = SuExec.cmd("test -x '$path' && echo 'installed' || echo 'not_installed'").exec()
            result.isSuccess && result.out.any { it.contains("installed") }
        }
    }
}
