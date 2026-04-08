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
    fun buildStartCommand(container: ContainerInfo): String {
        val parts = mutableListOf<String>()

        // Binary path
        parts.add(DROIDSPACES_BINARY_PATH)

        // Config file path
        parts.add("--config=${quote(getConfigPath(container))}")

        if (container.allowUserNs) {
            parts.add("--allow-user-ns")
        }

        // Command
        parts.add("start")

        return parts.joinToString(" ")
    }

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

    /**
     * Build status command for a container.
     */
    fun buildStatusCommand(container: ContainerInfo): String {
        return "$DROIDSPACES_BINARY_PATH --name=${quote(container.name)} status"
    }

    /**
     * Build uptime command for a container.
     */
    fun buildUptimeCommand(containerName: String): String {
        return "$DROIDSPACES_BINARY_PATH --name=${quote(containerName)} uptime"
    }
}

