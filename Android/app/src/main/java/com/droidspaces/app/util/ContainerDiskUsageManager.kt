package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Reports the on-disk footprint of a sparse-image container's rootfs.img.
 *
 * For a sparse file the apparent size (e.g. 32 GB) is much larger than the blocks
 * actually allocated on the host (e.g. 5.6 GB). A single `stat -c '%s %b'` gives both:
 *   - %s  = apparent size in bytes        -> total
 *   - %b  = allocated 512-byte blocks      -> used = %b * 512
 *
 * This is the storage the image currently occupies on the device. Note it reflects
 * host-allocated blocks, which only grow: deleting files inside the guest will not
 * shrink it unless the guest issues fstrim/discard. Works whether the container is
 * running or not - no mount required.
 *
 * Always uses our bundled busybox (pinned version) rather than the system `stat`/toybox,
 * so the `%s %b` output is reproducible across devices regardless of the OEM userspace.
 */
object ContainerDiskUsageManager {

    /** Allocated 512-byte blocks are always 512 bytes regardless of filesystem block size. */
    private const val STAT_BLOCK_SIZE = 512L

    data class DiskUsage(val usedBytes: Long, val totalBytes: Long)

    /** Last measured value per image path - lets a re-opened screen paint instantly. */
    private val lastKnown = HashMap<String, DiskUsage>()

    /** Last value measured for [imgPath] in this process, or null if never measured. */
    fun getCached(imgPath: String): DiskUsage? = synchronized(lastKnown) { lastKnown[imgPath] }

    /**
     * @param imgPath path to the rootfs.img (use [ContainerInfo.rootfsPath] for a sparse container).
     * @return used/total in bytes, or null if the image could not be stat'd.
     */
    suspend fun getUsage(imgPath: String): DiskUsage? = withContext(Dispatchers.IO) {
        val cmd = "${Constants.BUSYBOX_BINARY_PATH} stat -c '%s %b' ${ContainerCommandBuilder.quote(imgPath)} 2>/dev/null"
        val result = Shell.cmd(cmd).exec()
        if (!result.isSuccess) return@withContext null

        val parts = result.out.firstOrNull()?.trim()?.split(Regex("\\s+")) ?: return@withContext null
        val total = parts.getOrNull(0)?.toLongOrNull() ?: return@withContext null
        val blocks = parts.getOrNull(1)?.toLongOrNull() ?: return@withContext null
        if (total <= 0L) return@withContext null

        DiskUsage(usedBytes = blocks * STAT_BLOCK_SIZE, totalBytes = total).also {
            synchronized(lastKnown) { lastKnown[imgPath] = it }
        }
    }
}
