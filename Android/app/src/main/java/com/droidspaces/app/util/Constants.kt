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
    const val MAGISKPOLICY_BINARY_NAME = "magiskpolicy"
    const val DROIDSPACES_BINARY_PATH = "$INSTALL_PATH/$DROIDSPACES_BINARY_NAME"
    const val BUSYBOX_BINARY_PATH = "$INSTALL_PATH/$BUSYBOX_BINARY_NAME"
    const val MAGISKPOLICY_BINARY_PATH = "$INSTALL_PATH/$MAGISKPOLICY_BINARY_NAME"
    const val MAGISK_MODULE_PATH = "/data/adb/modules/droidspaces"
    const val DROIDSPACES_TE_PATH = MAGISK_MODULE_PATH + "/etc/droidspaces.te"

    // Container paths
    const val CONTAINERS_BASE_PATH = "/data/local/Droidspaces/Containers"
    const val MODULE_SYSTEM_BIN_PATH = "$MAGISK_MODULE_PATH/system/bin"
    const val SYSTEM_BIN_SYMLINK_PATH = "$MODULE_SYSTEM_BIN_PATH/$DROIDSPACES_BINARY_NAME"
    const val KEY_SYMLINK_ENABLED = "symlink_enabled"

    const val DAEMON_MODE_FILE = "/data/local/Droidspaces/.daemon_mode"
    const val DAEMON_PID_FILE = "/data/local/Droidspaces/droidspacesd.pid"
    const val CONTAINER_CONFIG_FILE = "container.config"

    // Preferences keys
    const val PREFS_NAME = "droidspaces_prefs"
    const val KEY_SETUP_COMPLETED = "setup_completed"
    const val KEY_ROOT_CHECKED = "root_checked"
    const val KEY_ROOT_SKIPPED = "root_skipped"
    const val KEY_ROOT_AVAILABLE = "root_available"
    const val KEY_ROOT_PROVIDER_VERSION = "root_provider_version"
    const val KEY_DROIDSPACES_VERSION = "droidspaces_version"
    const val KEY_CONTAINER_COUNT = "container_count"
    const val KEY_RUNNING_COUNT = "running_count"
    const val KEY_BACKEND_STATUS = "backend_status"
    const val KEY_FOLLOW_SYSTEM_THEME = "follow_system_theme"
    const val KEY_DARK_THEME = "dark_theme"
    const val KEY_AMOLED_MODE = "amoled_mode"
    const val KEY_USE_DYNAMIC_COLOR = "use_dynamic_color"
    const val KEY_THEME_PALETTE = "theme_palette"
    const val KEY_APP_LOCALE = "app_locale"
    const val KEY_BACKEND_MODE = "backend_mode"
    const val KEY_DAEMON_MODE_ENABLED = "daemon_mode_enabled"

    // Container log cache prefix
    const val KEY_CONTAINER_LOG_PREFIX = "container_log_"
    const val KEY_CONTAINER_OS_INFO_PREFIX = "container_os_info_"
    const val KEY_CONTAINER_USERS_PREFIX = "container_users_"
    const val KEY_CACHED_CONTAINER_NAMES = "cached_container_names"
    const val KEY_CACHED_CONTAINER_CONFIG_PREFIX = "cached_container_config_"

    // Minimum storage requirements
    const val MIN_STORAGE_GB = 4


    // NAT Networking constants
    const val NAT_IP_PREFIX = "172.28"
    const val NAT_OCTET_MIN = 1
    const val NAT_OCTET_MAX = 254

    // Maximum DNS servers
    const val MAX_DNS_SERVERS = 8

    // Pull-to-refresh animation delay (ms) - smooth refresh indicator animation
    const val PULL_TO_REFRESH_ANIMATION_DELAY = 200L

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
