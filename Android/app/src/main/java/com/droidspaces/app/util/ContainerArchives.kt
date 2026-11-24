package com.droidspaces.app.util

import android.content.Context
import android.net.Uri
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Where a reset reads its rootfs archive from. */
sealed interface RootfsArchive {
    /** A path kept from the install or the last reset. */
    data class Saved(val path: String) : RootfsArchive

    /** A fresh pick, which may still need a cache copy for a pipe-backed provider. */
    data class Picked(val uri: Uri) : RootfsArchive
}

/**
 * Remembers the archive a container came from, so Reset can reuse it instead of asking
 * for the file every time. Only a path root can open is kept: a pipe-backed provider
 * never yields one, and a deleted download leaves the next reset to ask again.
 */
object ContainerArchives {
    private fun prefs(context: Context) = PreferencesManager.getInstance(context)

    /** Saves [source] under [containerName], or clears it when no path survived. */
    suspend fun remember(context: Context, containerName: String, source: RootfsArchive) {
        val path = when (source) {
            is RootfsArchive.Saved -> source.path
            is RootfsArchive.Picked -> FilePickerUtils.realPath(context, source.uri)
        }
        prefs(context).setContainerArchive(containerName, path)
    }

    /** The kept archive for [containerName] if root can still read it, else null. */
    suspend fun saved(context: Context, containerName: String): String? = withContext(Dispatchers.IO) {
        val path = prefs(context).getContainerArchive(containerName) ?: return@withContext null
        // A shell that cannot answer is not permission to reset from the path: ask instead.
        val readable = runCatching {
            Shell.cmd(
                "${Constants.BUSYBOX_BINARY_PATH} test -f -r ${ContainerCommandBuilder.quote(path)}"
            ).exec().isSuccess
        }.getOrDefault(false)
        path.takeIf { readable }
    }
}
