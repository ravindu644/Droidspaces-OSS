// SPDX-License-Identifier: GPL-3.0-or-later
package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell
import java.io.File

/** Reads the optional container.config using the existing container config parser. */
object RootfsConfig {
    fun read(tarball: File): ContainerInfo? {
        val bb = Constants.BUSYBOX_BINARY_PATH
        val decompress = if (tarball.name.endsWith(".xz")) "xzcat" else "zcat"
        val input = "$bb $decompress ${ContainerCommandBuilder.quote(tarball.absolutePath)}"
        // Ordinary rootfs archives should open the default wizard without a full scan.
        val first = Shell.cmd(
            "$input 2>/dev/null | $bb head -c 512 | " +
                "$bb tar -tvf - container.config ./container.config 2>/dev/null"
        ).exec().out
        val entry = first.firstNotNullOfOrNull {
            Regex("^-[rwxStTs-]{9}\\s+\\S+\\s+(\\d+)\\s+\\S+\\s+\\S+\\s+(\\./)?container\\.config$")
                .matchEntire(it)
        } ?: return null
        val size = entry.groupValues[1].toLongOrNull() ?: return null
        val member = entry.groupValues[2] + Constants.CONTAINER_CONFIG_FILE
        // Closing the prefix reader gives the decompressor SIGPIPE, which is expected.
        val source =
            "(set +o pipefail; $input 2>/dev/null | $bb head -c ${512 + ((size + 511) / 512) * 512})"
        val result = Shell.cmd(
            "(set -o pipefail; $source | $bb tar -xOf - ${ContainerCommandBuilder.quote(member)} 2>/dev/null)"
        ).exec()
        check(result.isSuccess) { "Could not read recommended configuration" }
        return parse(result.out.joinToString("\n"))
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
