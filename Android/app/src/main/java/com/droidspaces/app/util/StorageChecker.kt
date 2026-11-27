package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * What a filesystem can hold.
 *
 * A directory rootfs stores ownership, modes and symlinks directly, so it needs a
 * filesystem that implements the Linux permission model. A sparse image carries its own
 * ext4 inside a single file, so the filesystem underneath only has to store that file.
 */
enum class StorageSupport {
    /** Directory rootfs and sparse image both work. */
    FULL,

    /** Sparse image only. The filesystem cannot represent a rootfs directly. */
    IMAGE_ONLY,

    /** Neither works. */
    UNUSABLE,
}

/**
 * A probed storage destination. [reason] explains anything short of [StorageSupport.FULL]
 * and is written to be shown to the user unchanged.
 */
data class StorageTarget(
    val path: String,
    val fsType: String,
    val support: StorageSupport,
    val reason: String? = null,
    val freeGB: Int? = null,
)

object StorageChecker {
    private const val BUSYBOX_PATH = Constants.BUSYBOX_BINARY_PATH

    /** Filesystems that implement the Linux permission model. */
    private val POSIX_FS = setOf("ext2", "ext3", "ext4", "f2fs", "btrfs", "xfs")

    /**
     * FAT cannot hold a file larger than 4GiB, which caps a sparse image below any useful
     * container size, so it is refused outright rather than clamped.
     */
    private val FAT_FS = setOf("vfat", "msdos", "fat", "fat32", "fuseblk.vfat")

    /**
     * Kernel interfaces and RAM-backed mounts. None of them is somewhere a container can
     * live: they either vanish on reboot or are not storage at all.
     */
    private val PSEUDO_FS = setOf(
        "tmpfs", "ramfs", "rootfs", "devtmpfs", "devpts", "proc", "sysfs",
        "debugfs", "tracefs", "configfs", "securityfs", "selinuxfs", "pstore",
        "cgroup", "cgroup2", "bpf", "binder", "binfmt_misc", "functionfs",
        "fusectl", "mqueue", "hugetlbfs",
    )

    /**
     * Probe what [path] can hold.
     *
     * Reads /proc/mounts once and takes the longest mount point that prefixes the nearest
     * existing ancestor of [path]. One read answers both the filesystem type and the mount
     * options, and the longest-prefix match is what makes it correct for a subdirectory:
     * matching the path itself only works when the path is a mount point, which silently
     * reports every nested directory as neither noexec nor read-only.
     */
    suspend fun probe(path: String): StorageTarget = withContext(Dispatchers.IO) {
        val target = path.trim().trimEnd('/').ifEmpty { "/" }

        // Resolve symlinks and read the mount table in one root round-trip. The marker
        // keeps the resolved path apart from the mount lines even when it contains a space.
        val result = Shell.cmd(
            "cd ${ContainerCommandBuilder.quote(target)} 2>/dev/null && " +
                "printf 'resolved:%s\\n' \"\$(pwd -P)\"; cat /proc/mounts"
        ).exec()
        if (!result.isSuccess || result.out.isEmpty()) {
            return@withContext StorageTarget(
                target, "unknown", StorageSupport.UNUSABLE,
                "Could not read the mount table for this path."
            )
        }

        // The marker is only printed when the cd succeeded, so its absence means the
        // directory does not exist yet. Probe the nearest ancestor that does: a destination
        // is usually a new folder inside a volume the user just picked.
        val resolved = result.out.firstOrNull { it.startsWith("resolved:") }?.removePrefix("resolved:")
        val probePath = resolved?.ifEmpty { null } ?: nearestExistingAncestor(target, result.out)

        val mount = longestMountPrefix(probePath, result.out)
            ?: return@withContext StorageTarget(
                target, "unknown", StorageSupport.UNUSABLE,
                "No filesystem is mounted at this path."
            )

        val (fsType, options) = mount
        val free = getFreeSpaceGB(probePath)

        val (support, reason) = classify(fsType, options)
        StorageTarget(target, fsType, support, reason, free)
    }

    // What the filesystem is beats how it happens to be mounted. sysfs is usually mounted
    // read-only, and telling someone to remount it invites them to try; naming it as a
    // kernel interface tells them the actual problem.
    private fun classify(fsType: String, options: Set<String>): Pair<StorageSupport, String?> = when {
        // Called out separately from the rest of PSEUDO_FS because it is the one users
        // actually land on: root's /storage is an empty tmpfs, so browsing there looks
        // like a real destination right up until the container disappears on reboot.
        fsType == "tmpfs" ->
            StorageSupport.UNUSABLE to
                "This is an in-memory tmpfs, not a real volume, and anything written " +
                "here is lost on reboot."

        fsType in PSEUDO_FS ->
            StorageSupport.UNUSABLE to
                "'$fsType' is a kernel interface, not storage. Pick a real volume."

        "ro" in options ->
            StorageSupport.UNUSABLE to "This volume is mounted read-only."

        fsType in FAT_FS ->
            StorageSupport.UNUSABLE to
                "FAT32 cannot store a file larger than 4GB. Reformat the volume as " +
                "ext4 or exFAT to use it."

        fsType !in POSIX_FS ->
            StorageSupport.IMAGE_ONLY to
                "'$fsType' does not support Linux ownership and permissions, so a rootfs " +
                "directory cannot live here. A sparse image works, because it carries its " +
                "own ext4 inside one file."

        // A directory rootfs is pivot_root'ed into, so its binaries execute straight off
        // this mount. An image is mounted separately and does not inherit the flag.
        // nodev is deliberately not checked: setup_dev() in src/dev/dev.c mounts <rootfs>/dev
        // as its own devtmpfs, so device nodes never land on this filesystem, and Android
        // mounts /data itself nodev.
        "noexec" in options ->
            StorageSupport.IMAGE_ONLY to
                "This volume is mounted noexec, so binaries in a rootfs directory cannot " +
                "run from it. A sparse image works, because it is mounted separately."

        else -> StorageSupport.FULL to null
    }

    /**
     * Walk up until a component is a known mount point. Used when the target directory
     * does not exist yet, so `pwd -P` could not resolve it.
     */
    private fun nearestExistingAncestor(path: String, mounts: List<String>): String {
        var candidate = path
        while (candidate.count { it == '/' } > 1) {
            candidate = candidate.substringBeforeLast('/')
            if (longestMountPrefix(candidate, mounts) != null) return candidate
        }
        return "/"
    }

    /**
     * The filesystem type and mount options of the longest mount point prefixing [path].
     */
    private fun longestMountPrefix(path: String, mounts: List<String>): Pair<String, Set<String>>? {
        var bestLen = -1
        var best: Pair<String, Set<String>>? = null

        for (line in mounts) {
            val f = line.split(' ')
            if (f.size < 4) continue
            // The kernel escapes spaces, tabs, newlines and backslashes in octal.
            val mountPoint = f[1]
                .replace("\\040", " ").replace("\\011", "\t")
                .replace("\\012", "\n").replace("\\134", "\\")

            val matches = path == mountPoint ||
                (mountPoint == "/" && path.startsWith("/")) ||
                path.startsWith("$mountPoint/")
            if (!matches || mountPoint.length <= bestLen) continue

            bestLen = mountPoint.length
            // Split rather than substring-match, so errors=remount-ro is not read as "ro".
            best = f[2] to f[3].split(',').toSet()
        }
        return best
    }

    /**
     * Check available space at [path]'s mount point.
     * Returns free space in GB, or null if unable to determine.
     *
     * Performance: ~10-50ms (runs in background)
     */
    private suspend fun getFreeSpaceGB(path: String): Int? = withContext(Dispatchers.IO) {
        val quoted = ContainerCommandBuilder.quote(path)

        // Try using stat first (more accurate)
        val statResult = Shell.cmd("stat -f -c '%a %S' $quoted 2>/dev/null").exec()
        if (statResult.isSuccess && statResult.out.isNotEmpty()) {
            val parts = statResult.out[0].trim().split(" ")
            if (parts.size == 2) {
                val availBlocks = parts[0].toLongOrNull()
                val blockSize = parts[1].toLongOrNull()
                if (availBlocks != null && blockSize != null && blockSize > 0) {
                    val freeGB = (availBlocks * blockSize / 1024 / 1024 / 1024).toInt()
                    return@withContext freeGB
                }
            }
        }

        // Fallback: use busybox df
        val dfResult = Shell.cmd("$BUSYBOX_PATH df -k $quoted 2>/dev/null | $BUSYBOX_PATH tail -n1 | $BUSYBOX_PATH awk '{print \$4}'").exec()
        if (dfResult.isSuccess && dfResult.out.isNotEmpty()) {
            val freeKB = dfResult.out[0].trim().toLongOrNull()
            if (freeKB != null && freeKB > 0) {
                val freeGB = (freeKB / 1024 / 1024).toInt()
                return@withContext freeGB
            }
        }

        null
    }

}
