package com.droidspaces.app.util

import android.content.Context
import java.io.File

/**
 * Dual-mode workspace path manager.
 *
 * Root mode:   /data/local/Droidspaces/...  (shared, root-owned)
 * Rootless:    /data/data/<pkg>/files/droidspaces/...  (app-private)
 */
object WorkspacePaths {
    private var appContext: Context? = null

    fun init(context: Context) {
        appContext = context.applicationContext
    }

    private fun ctx(): Context = appContext
        ?: throw IllegalStateException("WorkspacePaths not initialized — call init(context) first")

    /** Root-mode base path: /data/local/Droidspaces */
    val rootBase: String get() = "/data/local/Droidspaces"

    /** Rootless-mode base path: /data/data/<pkg>/files/droidspaces */
    val rootlessBase: String get() = File(ctx().filesDir, "droidspaces").absolutePath

    /** Rootless containers dir: /data/data/.../files/droidspaces/Containers */
    val rootlessContainers: String get() = "$rootlessBase/Containers"

    /** Rootless binaries dir: /data/data/.../files/droidspaces/bin */
    val rootlessBin: String get() = "$rootlessBase/bin"

    /** Rootless busybox path */
    val rootlessBusybox: String get() = "$rootlessBin/busybox"

    /**
     * Return the containers base path for the given mode.
     */
    fun containersBase(rootless: Boolean): String =
        if (rootless) rootlessContainers else "${rootBase}/Containers"

    /**
     * Return the bin path for the given mode.
     */
    fun binPath(rootless: Boolean): String =
        if (rootless) rootlessBin else "${rootBase}/bin"

    /**
     * Return the busybox path for the given mode.
     */
    fun busyboxPath(rootless: Boolean): String =
        "${binPath(rootless)}/busybox"

    /**
     * Ensure the rootless workspace directories exist.
     */
    fun ensureRootlessDirs() {
        File(rootlessContainers).mkdirs()
        File(rootlessBin).mkdirs()
    }

    /** Rootless container config file path. */
    fun containerConfigPath(name: String, rootless: Boolean = true): String {
        val base = containersBase(rootless)
        return "$base/${ContainerManager.sanitizeContainerName(name)}/container.config"
    }

    /** Rootless container rootfs directory path. */
    fun containerRootfsPath(name: String, rootless: Boolean = true): String {
        val base = containersBase(rootless)
        return "$base/${ContainerManager.sanitizeContainerName(name)}/rootfs"
    }
}
