// SPDX-License-Identifier: GPL-3.0-or-later
package com.droidspaces.app.util

import android.content.Context
import android.net.Uri
import com.topjohnwu.superuser.Shell
import java.io.File

/**
 * Reads the optional container.config, and the container.env an export may carry beside
 * it, using the existing container config parser.
 */
object RootfsConfig {
    // Export writes the config, then the optional env, as the first members, so both
    // decode from the first few KB. Peeking a prefix keeps the wizard instant and works for pipe-backed providers.
    // ponytail: a config placed past the first 64 KiB is ignored, exports never do that
    private const val PEEK_BYTES = 64 * 1024

    /** Export's name for the .env member, so it cannot clash with a guest's own /.env. */
    const val ENV_MEMBER = "container.env"

    fun read(context: Context, uri: Uri): ContainerInfo? {
        val head = ByteArray(PEEK_BYTES)
        var size = 0
        context.contentResolver.openInputStream(uri)?.use { input ->
            while (size < head.size) {
                val n = input.read(head, size, head.size - size)
                if (n < 0) break
                size += n
            }
        } ?: throw Exception("Failed to open tarball input stream")

        // Sniff the format instead of trusting the file name.
        val xz = size > 5 && head[0] == 0xFD.toByte() && String(head, 1, 4) == "7zXZ"
        val bb = Constants.BUSYBOX_BINARY_PATH
        val peek = File.createTempFile("rootfs_peek", null, context.cacheDir)
        try {
            peek.writeBytes(head.copyOf(size))
            // The prefix ends mid-stream, so both tools exit non-zero after printing
            // the member. Its output is the answer, the exit code is not.
            fun member(name: String) = Shell.cmd(
                "$bb ${if (xz) "xzcat" else "zcat"} ${ContainerCommandBuilder.quote(peek.absolutePath)} 2>/dev/null | " +
                    "$bb tar -xOf - $name ./$name 2>/dev/null"
            ).exec().out.joinToString("\n")
            val config = member(Constants.CONTAINER_CONFIG_FILE).ifEmpty { return null }
            return parse(config)?.copy(envFileContent = member(ENV_MEMBER).ifBlank { null })
        } finally {
            peek.delete()
        }
    }

    internal fun parse(content: String): ContainerInfo? {
        val values = ContainerManager.parseConfigValues(content)
        // These defaults belong to the installation wizard, not an existing container.
        val defaults = "use_sparse_image=1\nsparse_image_size_gb=8\n"
        return ContainerManager.parseConfig(defaults + content, "Container", loadEnvironment = false)?.copy(
            name = values["name"].orEmpty(),
            hostname = values["hostname"].orEmpty()
        )
    }
}
