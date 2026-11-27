package com.droidspaces.app.util

/**
 * Centralized constants for the entire application.
 * Single source of truth for all paths, keys, and configuration values.
 */
object Constants {
    // Installation paths
    const val INSTALL_PATH = "/data/local/Droidspaces/bin"
    const val DROIDSPACES_BINARY_NAME = "droidspaces"
    const val BUSYBOX_BINARY_NAME = "busybox"
    const val DROIDSPACES_BINARY_PATH = "$INSTALL_PATH/$DROIDSPACES_BINARY_NAME"
    const val BUSYBOX_BINARY_PATH = "$INSTALL_PATH/$BUSYBOX_BINARY_NAME"
    const val MAGISK_MODULE_PATH = "/data/adb/modules/droidspaces"

    // Container paths
    const val CONTAINERS_BASE_PATH = "/data/local/Droidspaces/Containers"
    const val MODULE_SYSTEM_BIN_PATH = "$MAGISK_MODULE_PATH/system/bin"
    const val SYSTEM_BIN_SYMLINK_PATH = "$MODULE_SYSTEM_BIN_PATH/$DROIDSPACES_BINARY_NAME"

    const val DAEMON_MODE_FILE = "/data/local/Droidspaces/.daemon_mode"
    const val DAEMON_PID_FILE = "/data/local/Droidspaces/droidspacesd.pid"
    const val CONTAINER_CONFIG_FILE = "container.config"
    // Default network mode, the single source of truth for both the ContainerInfo
    // model default and the config parser default (they used to disagree).
    const val DEFAULT_NET_MODE = "nat"

    const val KERNEL_CONFIG_DOC_URL = "https://github.com/ravindu644/Droidspaces-OSS/blob/main/Documentation/Kernel-Configuration.md"

    // Minimum storage requirements
    const val MIN_STORAGE_GB = 4

    // NAT Networking constants
    const val NAT_IP_PREFIX = "172.28"
    const val NAT_OCTET_MIN = 1
    const val NAT_OCTET_MAX = 254

    /**
     * Check if droidspaces binary is available in system PATH
     * Returns the command to use (either "droidspaces" if in PATH, or full path otherwise)
     */
    fun getDroidspacesCommand(): String {
        val result = com.topjohnwu.superuser.Shell.cmd("command -v droidspaces 2>&1").exec()
        return if (result.isSuccess && result.out.isNotEmpty() && result.out[0].isNotBlank()) {
            // droidspaces is in PATH, use just the command name
            "droidspaces"
        } else {
            // droidspaces is not in PATH, use full path
            DROIDSPACES_BINARY_PATH
        }
    }
}
