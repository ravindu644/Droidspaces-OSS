package com.droidspaces.app.util

import android.content.Context
import android.content.res.AssetManager
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

object ModuleInstaller {
    private const val MODULE_PATH = "/data/adb/modules/droidspaces"

    /** The boot-module assets, staged in cacheDir, installed over a fresh module directory. */
    suspend fun install(context: Context): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            // The module carries the system/bin symlink, so replacing it removes the link.
            val wasSymlinkEnabled = SymlinkInstaller.isSymlinkEnabled()
            val staging = File(context.cacheDir, "boot-module-temp")
            try {
                staging.deleteRecursively()
                copyAssetDir(context.assets, "boot-module", staging)
                val result = Shell.cmd(
                    "{ rm -rf '$MODULE_PATH' && mkdir -p '$MODULE_PATH' && " +
                        "cp -arf ${ContainerCommandBuilder.quote(staging.absolutePath)}/* '$MODULE_PATH/' && " +
                        "chmod 755 '$MODULE_PATH'/*.sh && chmod 644 '$MODULE_PATH'/*.prop '$MODULE_PATH/sepolicy.rule' && " +
                        "test -f '$MODULE_PATH/module.prop' -a -f '$MODULE_PATH/sepolicy.rule'; } 2>&1"
                ).exec()
                check(result.isSuccess) { "Module install failed: ${result.errorText()}" }
            } finally {
                staging.deleteRecursively()
            }
            if (wasSymlinkEnabled) SymlinkInstaller.enable()
        }
    }

    /** An asset file, or an asset directory tree, copied to [dest]. */
    private fun copyAssetDir(assets: AssetManager, path: String, dest: File) {
        val children = assets.list(path).orEmpty()
        if (children.isEmpty()) {
            assets.open(path).use { input -> dest.outputStream().use { input.copyTo(it) } }
            return
        }
        dest.mkdirs()
        children.forEach { copyAssetDir(assets, "$path/$it", File(dest, it)) }
    }
}
