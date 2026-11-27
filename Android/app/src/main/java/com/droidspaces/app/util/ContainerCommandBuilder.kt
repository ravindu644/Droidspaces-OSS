package com.droidspaces.app.util

/**
 * Builds droidspaces commands dynamically based on container configuration.
 * Constructs start/stop/restart commands with all necessary flags.
 * All values are properly quoted to handle spaces and special characters.
 */
object ContainerCommandBuilder {
    private const val DROIDSPACES_BINARY_PATH = Constants.DROIDSPACES_BINARY_PATH

    /**
     * Quote a value for use in shell commands.
     * Escapes single quotes and wraps in single quotes for safety.
     */
    fun quote(value: String): String {
        // Replace single quotes with '\'' (end quote, escaped quote, start quote)
        return "'${value.replace("'", "'\\''")}'"
    }

    /**
     * rm -rf of a container path, exiting with rm's own code. A guest can leave immutable or
     * append-only files behind (NixOS marks /var/empty) and not even root can unlink those,
     * so a directory rootfs has the flags cleared first. A sparse image goes as one file.
     *
     * The guest wrote that tree, so this is the busybox we ship, whose chattr never follows
     * a symlink, and not whatever the ROM has. A trailing slash on [path] would defeat that.
     */
    fun buildDeleteCommand(path: String, useSparseImage: Boolean): String {
        val rm = "rm -rf ${quote(path)} 2>&1"
        if (useSparseImage) return rm
        val bb = Constants.BUSYBOX_BINARY_PATH
        // Nothing of ours stays mounted here, but through a mount the user left behind,
        // chattr would unprotect host files that rm cannot delete on its own.
        val mounted = "$bb grep -qF -e ${quote(" $path ")} -e ${quote(" $path/")} /proc/mounts"
        return "{ $mounted || $bb chattr -R -ia ${quote(path)}; } >/dev/null 2>&1; $rm"
    }

    /**
     * Get the absolute path to the container's configuration file.
     */
    fun getConfigPath(container: ContainerInfo): String {
        val sanitizedName = ContainerManager.sanitizeContainerName(container.name)
        return "${Constants.CONTAINERS_BASE_PATH}/$sanitizedName/${Constants.CONTAINER_CONFIG_FILE}"
    }

    /**
     * Build start command for a container based on its configuration.
     * Uses the central config file instead of passing all flags individually.
     */
    fun buildStartCommand(container: ContainerInfo): String =
        "$DROIDSPACES_BINARY_PATH --config=${quote(getConfigPath(container))} start"

    /**
     * Build stop command for a container.
     */
    fun buildStopCommand(container: ContainerInfo): String {
        return "$DROIDSPACES_BINARY_PATH --name=${quote(container.name)} stop"
    }

    /**
     * Build restart command for a container.
     */
    fun buildRestartCommand(container: ContainerInfo): String {
        return "$DROIDSPACES_BINARY_PATH --config=${quote(getConfigPath(container))} restart"
    }

    /** JSON status of every running container: OS, hostname, IP, uptime, CPU, RAM. */
    fun buildShowCommand(): String = "$DROIDSPACES_BINARY_PATH --json show"

    /** JSON of every kernel capability probe, one 0/1 per key. */
    fun buildCheckCommand(): String = "$DROIDSPACES_BINARY_PATH --json check"
}

