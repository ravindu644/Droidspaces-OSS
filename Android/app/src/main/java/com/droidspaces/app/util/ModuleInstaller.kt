package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.util.SuExec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

sealed class ModuleInstallationStep {
    data class RemovingOldModule(val path: String) : ModuleInstallationStep()
    data class ExtractingAssets(val path: String) : ModuleInstallationStep()
    data class CopyingModule(val path: String) : ModuleInstallationStep()
    data class SettingPermissions(val path: String) : ModuleInstallationStep()
    data class Verifying(val path: String) : ModuleInstallationStep()
    object Success : ModuleInstallationStep()
    data class Error(val message: String) : ModuleInstallationStep()
}

object ModuleInstaller {
    private const val MAGISK_MODULE_PATH = "/data/adb/modules/droidspaces"
    private const val MODULE_PROP_PATH = "$MAGISK_MODULE_PATH/module.prop"

    /**
     * Install Magisk module from assets to /data/adb/modules/droidspaces
     * Simple approach: remove old, extract assets to temp, copy everything
     */
    suspend fun install(
        context: Context,
        onProgress: (ModuleInstallationStep) -> Unit
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val tempDir = File(context.cacheDir, "boot-module-temp")
            val assetManager = context.assets

            // Pre-check: Is the system binary symlink enabled?
            val wasSymlinkEnabled = SymlinkInstaller.isSymlinkEnabled()

            // Step 1: Remove old module directory
            onProgress(ModuleInstallationStep.RemovingOldModule(MAGISK_MODULE_PATH))
            SuExec.cmd("rm -rf '$MAGISK_MODULE_PATH' 2>&1").exec()

            // Step 2: Extract all assets from boot-module to temp directory
            onProgress(ModuleInstallationStep.ExtractingAssets(tempDir.absolutePath))
            tempDir.deleteRecursively()
            tempDir.mkdirs()

            try {
                // Manually handle the structure to be safe and simple
                val assetFiles = assetManager.list("boot-module") ?: emptyArray()
                for (fileName in assetFiles) {
                    val assetPath = "boot-module/$fileName"
                    val subAssets = assetManager.list(assetPath) ?: emptyArray()

                    if (subAssets.isEmpty()) {
                        // File
                        val tempFile = File(tempDir, fileName)
                        assetManager.open(assetPath).use { input ->
                            tempFile.outputStream().use { output ->
                                input.copyTo(output)
                            }
                        }
                    } else {
                        // Directory (e.g., etc)
                        val subDir = File(tempDir, fileName)
                        subDir.mkdirs()
                        for (subFileName in subAssets) {
                            val subAssetPath = "$assetPath/$subFileName"
                            val tempFile = File(subDir, subFileName)
                            assetManager.open(subAssetPath).use { input ->
                                tempFile.outputStream().use { output ->
                                    input.copyTo(output)
                                }
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                tempDir.deleteRecursively()
                return@withContext Result.failure(
                    Exception("Failed to extract assets: ${e.message}")
                )
            }

            // Step 3: Create module directory and copy everything
            onProgress(ModuleInstallationStep.CopyingModule(MAGISK_MODULE_PATH))
            val mkdirResult = SuExec.cmd("mkdir -p '$MAGISK_MODULE_PATH' 2>&1").exec()
            if (!mkdirResult.isSuccess) {
                tempDir.deleteRecursively()
                return@withContext Result.failure(
                    Exception("Failed to create module directory: ${mkdirResult.err.joinToString()}")
                )
            }

            val copyResult = SuExec.cmd("cp -arf '${tempDir.absolutePath}'/* '$MAGISK_MODULE_PATH/' 2>&1").exec()
            tempDir.deleteRecursively()
            if (!copyResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to copy module files: ${copyResult.err.joinToString()}")
                )
            }

            // Step 4: Set permissions
            onProgress(ModuleInstallationStep.SettingPermissions(MAGISK_MODULE_PATH))
            val chmodScriptsResult = SuExec.cmd("chmod 755 '$MAGISK_MODULE_PATH'/*.sh 2>&1 && chmod 644 '$MAGISK_MODULE_PATH'/*.prop 2>&1 && mkdir -p '$MAGISK_MODULE_PATH/etc' && chmod 644 '$MAGISK_MODULE_PATH'/etc/*.te 2>&1").exec()
            if (!chmodScriptsResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to set permissions: ${chmodScriptsResult.err.joinToString()}")
                )
            }

            // Step 5: Verify installation
            onProgress(ModuleInstallationStep.Verifying(MAGISK_MODULE_PATH))
            val verifyDirResult = SuExec.cmd("test -d '$MAGISK_MODULE_PATH' 2>&1").exec()
            val verifyPropResult = SuExec.cmd("test -f '$MODULE_PROP_PATH' 2>&1").exec()

            if (!verifyDirResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Module directory verification failed")
                )
            }

            if (!verifyPropResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("module.prop verification failed")
                )
            }

            val verifyTeResult = SuExec.cmd("test -f '$MAGISK_MODULE_PATH/etc/droidspaces.te' 2>&1").exec()
            if (!verifyTeResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("droidspaces.te verification failed")
                )
            }

            // Restore symlink if it was previously enabled
            if (wasSymlinkEnabled) {
                SymlinkInstaller.enable()
            }

            onProgress(ModuleInstallationStep.Success)
            Result.success(Unit)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }
}
