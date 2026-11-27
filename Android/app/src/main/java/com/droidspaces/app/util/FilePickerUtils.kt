package com.droidspaces.app.util

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.system.Os
import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Utility functions for file picker operations.
 */
object FilePickerUtils {
    private const val TAG = "FilePickerUtils"

    /** The display name a SAF provider answers for [uri]; failing that, a path segment
     * that looks like a file name, never an opaque document id. */
    suspend fun getFileName(context: Context, uri: Uri): String? = withContext(Dispatchers.IO) {
        runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                val col = it.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (col >= 0 && it.moveToFirst()) it.getString(col) else null
            }
        }.getOrNull()?.takeIf { it.isNotEmpty() }
            ?: uri.lastPathSegment?.substringAfterLast(":")?.takeIf { "." in it }
    }

    /**
     * The file a provider actually opened for [uri], as a path the root shell can read
     * or write in place, so a multi-GB tarball never has to be staged in cacheDir.
     * Parsing document IDs is a guess per provider, the kernel already knows the answer.
     *
     * Null when the provider hands out a pipe (Drive and other cloud providers) or root
     * sees a different file at that path. Callers keep their copy for those.
     */
    suspend fun realPath(context: Context, uri: Uri, mode: String = "r"): String? = withContext(Dispatchers.IO) {
        runCatching {
            context.contentResolver.openFileDescriptor(uri, mode)?.use { pfd ->
                val path = Os.readlink("/proc/self/fd/${pfd.fd}")
                val st = Os.fstat(pfd.fileDescriptor)
                // Checked from root's side, root is the one that opens it later.
                val seen = Shell.cmd(
                    "${Constants.BUSYBOX_BINARY_PATH} stat -c '%d %i' ${ContainerCommandBuilder.quote(path)}"
                ).exec().out.singleOrNull()
                path.takeIf { it.startsWith("/") && seen == "${st.st_dev} ${st.st_ino}" }
            }
        }.onFailure { Log.w(TAG, "No direct path for $uri", it) }.getOrNull()
    }

    /**
     * Check if a URI points to a valid tar.xz or tar.gz file.
     *
     * @param context The context to use for ContentResolver
     * @param uri The URI to check
     * @return Pair of (isValid, fileName) where isValid is true if the file is a tar.xz or tar.gz
     */
    suspend fun isValidTarball(context: Context, uri: Uri): Pair<Boolean, String?> {
        val fileName = getFileName(context, uri)
        if (fileName == null) {
            return Pair(false, null)
        }

        val fileNameLower = fileName.lowercase()
        val isValid = fileNameLower.endsWith(".tar.xz") || fileNameLower.endsWith(".tar.gz")

        return Pair(isValid, fileName)
    }
}

