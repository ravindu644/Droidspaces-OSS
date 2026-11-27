package com.droidspaces.app.util

import android.content.Context
import android.content.SharedPreferences
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlin.properties.ReadWriteProperty
import kotlin.reflect.KProperty

/** The app's SharedPreferences, one slot per property. Writes use apply(). */
class PreferencesManager private constructor(context: Context) {
    val prefs: SharedPreferences = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /** A slot whose type follows its default; writing null to a String? slot removes the key. */
    @Suppress("UNCHECKED_CAST")
    private fun <T> pref(key: String, default: T) = object : ReadWriteProperty<Any?, T> {
        override fun getValue(thisRef: Any?, property: KProperty<*>): T = when (default) {
            is Boolean -> prefs.getBoolean(key, default) as T
            is Int -> prefs.getInt(key, default) as T
            is String -> (prefs.getString(key, default) ?: default) as T
            else -> prefs.getString(key, null) as T
        }

        override fun setValue(thisRef: Any?, property: KProperty<*>, value: T) {
            prefs.edit().apply {
                when (value) {
                    is Boolean -> putBoolean(key, value)
                    is Int -> putInt(key, value)
                    null -> remove(key)
                    else -> putString(key, value as String)
                }
            }.apply()
        }
    }

    var isSetupCompleted: Boolean by pref(KEY_SETUP_COMPLETED, false)

    var rootChecked: Boolean by pref(KEY_ROOT_CHECKED, false)

    var rootAvailable: Boolean by pref(KEY_ROOT_AVAILABLE, false)

    var cachedDroidspacesVersion: String? by pref(KEY_DROIDSPACES_VERSION, null)

    var cachedRunningCount: Int by pref(KEY_RUNNING_COUNT, 0)

    var cachedBackendStatus: String? by pref(KEY_BACKEND_STATUS, null)

    // Raw `check --json` JSON and the boot it was taken on, see HostCapabilities.
    var cachedHostCapabilities: String? by pref(KEY_HOST_CAPABILITIES, null)

    var cachedHostCapabilitiesBootId: String? by pref(KEY_HOST_CAPABILITIES_BOOT_ID, null)

    var cachedBackendMode: String? by pref(KEY_BACKEND_MODE, null)

    // Last known SELinux status. Seeds the home card on a fresh launch so it
    // never shows a loading row while the root shell spins up.
    var cachedSelinuxStatus: String? by pref(KEY_SELINUX_STATUS, null)

    // Theme preferences
    var followSystemTheme: Boolean by pref(KEY_FOLLOW_SYSTEM_THEME, true)

    var darkTheme: Boolean by pref(KEY_DARK_THEME, false)

    var amoledMode: Boolean by pref(KEY_AMOLED_MODE, false)

    var useDynamicColor: Boolean by pref(KEY_USE_DYNAMIC_COLOR, true)

    var themePalette: String by pref(KEY_THEME_PALETTE, "BROCCOLI")

    // Terminal-only dark mode - independent of the app-wide theme, so the rest
    // of the app can stay light while the terminal page renders dark.
    var terminalDarkTheme: Boolean by pref(KEY_TERMINAL_DARK_THEME, false)

    // Terminal text size in px, the unit Termux's TerminalView uses. Read once when
    // a session is created; pinch-to-zoom then adjusts that session in memory only.
    var terminalFontSizePx: Int by pref(KEY_TERMINAL_FONT_SIZE, 30)

    // Filename inside filesDir/fonts/; "" = the bundled JetBrains Mono. Like the
    // size above, sessions read it once at creation.
    var terminalFontFile: String by pref(KEY_TERMINAL_FONT_FILE, "")

    // Opt-in guard for the X on a terminal tab; off by default because it
    // turns a one-tap close into two.
    var terminalConfirmClose: Boolean by pref(KEY_TERMINAL_CONFIRM_CLOSE, false)

    // On by default so nothing changes on upgrade. Off hands a tap to whatever
    // runs in the terminal, which is what an editor wants, and the keyboard
    // comes from the button on the terminal instead.
    var terminalTapKeyboard: Boolean by pref(KEY_TERMINAL_TAP_KEYBOARD, true)

    // Which bottom corner that button sits in. False is the right, for the
    // right-handed majority.
    var terminalKeyboardLeft: Boolean by pref(KEY_TERMINAL_KEYBOARD_LEFT, false)

    /** Mirrored into the root-owned flag file the backend's boot scripts read. */
    var isDaemonModeEnabled: Boolean
        get() = prefs.getBoolean(KEY_DAEMON_MODE_ENABLED, false)
        set(value) {
            prefs.edit().putBoolean(KEY_DAEMON_MODE_ENABLED, value).apply()
            Shell.cmd("echo '${if (value) 1 else 0}' > '${Constants.DAEMON_MODE_FILE}'").submit()
        }

    var isSymlinkEnabled: Boolean by pref(KEY_SYMLINK_ENABLED, false)

    /**
     * Reactive stream of the daemon-mode preference, emits the current value and
     * every later change, [syncDaemonModeFromDisk]'s included, so the UI can collect it with
     * collectAsStateWithLifecycle() instead of a hand-rolled change listener.
     */
    val daemonModeFlow: Flow<Boolean> = booleanPrefFlow(KEY_DAEMON_MODE_ENABLED, false)

    /** Reactive stream of the symlink-enabled preference. */
    val symlinkEnabledFlow: Flow<Boolean> = booleanPrefFlow(KEY_SYMLINK_ENABLED, false)

    // 32-bit OS on a 64-bit kernel: install the 64-bit backend and list 64-bit
    // rootfs images. Read by DeviceArch, which every arch lookup goes through.
    var treatAs64Bit: Boolean by pref(KEY_TREAT_AS_64BIT, false)

    val treatAs64BitFlow: Flow<Boolean> = booleanPrefFlow(KEY_TREAT_AS_64BIT, false)

    // One GET to the GitHub releases API per app open. Default on, since the
    // app only ships through GitHub Releases and nobody polls that page by hand.
    var checkAppUpdates: Boolean by pref(KEY_CHECK_APP_UPDATES, true)

    private fun booleanPrefFlow(key: String, default: Boolean): Flow<Boolean> = callbackFlow {
        trySend(prefs.getBoolean(key, default))
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, changedKey ->
            if (changedKey == key || changedKey == null) trySend(prefs.getBoolean(key, default))
        }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        awaitClose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }.distinctUntilChanged()

    /** Takes the flag file's value when it exists and differs. The pref write alone,
     * so the setter's own disk write does not recurse. */
    fun syncDaemonModeFromDisk() {
        val out = Shell.cmd("cat '${Constants.DAEMON_MODE_FILE}' 2>/dev/null").exec().out
        val enabled = out.firstOrNull()?.trim()?.let { it == "1" } ?: return
        if (isDaemonModeEnabled != enabled)
            prefs.edit().putBoolean(KEY_DAEMON_MODE_ENABLED, enabled).apply()
    }

    /** Syncs symlink pref from actual filesystem state. */
    fun syncSymlinkFromDisk() {
        val actual = SymlinkInstaller.isSymlinkEnabled()
        if (isSymlinkEnabled != actual) {
            prefs.edit().putBoolean(KEY_SYMLINK_ENABLED, actual).apply()
        }
    }

    /**
     * Store container logs in cache (only last action).
     * Format: "level:message\nlevel:message\n..."
     */
    fun saveContainerLogs(containerName: String, logs: List<Pair<Int, String>>) {
        val logText = logs.joinToString("\n") { "${it.first}:${it.second}" }
        prefs.edit().putString("${KEY_CONTAINER_LOG_PREFIX}$containerName", logText).apply()
    }

    /**
     * Load cached container logs.
     * Returns empty list if no logs cached.
     */
    fun loadContainerLogs(containerName: String): List<Pair<Int, String>> {
        val logText = prefs.getString("${KEY_CONTAINER_LOG_PREFIX}$containerName", null) ?: return emptyList()
        return logText.split("\n").mapNotNull { line ->
            val parts = line.split(":", limit = 2)
            if (parts.size == 2) {
                parts[0].toIntOrNull()?.let { level -> level to parts[1] }
            } else null
        }
    }

    /**
     * Clear cached container logs.
     */
    fun clearContainerLogs(containerName: String) {
        prefs.edit().remove("${KEY_CONTAINER_LOG_PREFIX}$containerName").commit()
    }

    // Preselected in the terminal's user picker, per container
    fun getLastTerminalUser(containerName: String): String? =
        prefs.getString("terminal_last_user_$containerName", null)

    fun setLastTerminalUser(containerName: String, user: String) {
        prefs.edit().putString("terminal_last_user_$containerName", user).apply()
    }

    // Uninstall drops everything kept under the name, so a new container that reuses it
    // starts clean. A new per-container key belongs here too.
    fun forgetContainer(containerName: String) {
        prefs.edit()
            .remove("${KEY_CONTAINER_LOG_PREFIX}$containerName")
            .remove("${KEY_CONTAINER_OS_INFO_PREFIX}$containerName")
            .remove("terminal_last_user_$containerName")
            .commit()
    }

    private val osInfoKeys = listOf("prettyName", "name", "version", "versionId", "id", "hostname", "ipAddress", "uptime")

    /** commit() not apply(): called from IO and the sync write survives a force-close. */
    fun saveContainerOSInfo(containerName: String, osInfo: ContainerOSInfoManager.OSInfo) {
        val values = listOf(osInfo.prettyName, osInfo.name, osInfo.version, osInfo.versionId, osInfo.id,
            osInfo.hostname, osInfo.ipAddress, osInfo.uptime)
        val obj = org.json.JSONObject()
        osInfoKeys.zip(values).forEach { (k, v) -> if (v != null) obj.put(k, v) }
        prefs.edit().putString("${KEY_CONTAINER_OS_INFO_PREFIX}$containerName", obj.toString()).commit()
    }

    /** Null when nothing is cached or it is not JSON, which an older pipe-delimited entry is not. */
    fun loadContainerOSInfo(containerName: String): ContainerOSInfoManager.OSInfo? {
        val data = prefs.getString("${KEY_CONTAINER_OS_INFO_PREFIX}$containerName", null) ?: return null
        if (!data.startsWith("{")) { // the pipe-delimited entry of older builds, until its next save
            val v = data.split("|").map { it.ifEmpty { null } }
            if (v.size < 7) return null
            return ContainerOSInfoManager.OSInfo(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v.getOrNull(7))
        }
        return try {
            val obj = org.json.JSONObject(data)
            val v = osInfoKeys.map { k -> obj.optString(k, "").takeIf { it.isNotEmpty() } }
            ContainerOSInfoManager.OSInfo(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        } catch (e: Exception) {
            null
        }
    }

    // Custom rootfs repository subscriptions
    // Stored as a JSON array string: [{"name":"...","url":"..."},...]

    fun getCustomRepos(): List<Pair<String, String>> {
        val raw = prefs.getString(KEY_CUSTOM_REPOS, null) ?: return emptyList()
        return try {
            val arr = org.json.JSONArray(raw)
            buildList {
                for (i in 0 until arr.length()) {
                    val obj = arr.getJSONObject(i)
                    add(obj.optString("name", "") to obj.optString("url", ""))
                }
            }.filter { it.first.isNotBlank() && it.second.isNotBlank() }
        } catch (e: Exception) {
            emptyList()
        }
    }

    fun addCustomRepo(name: String, url: String) {
        // Only HTTPS repos, the rootfs manifest/payload must not be MITM-able (V13).
        if (!url.startsWith("https://", ignoreCase = true)) return
        val current = getCustomRepos().toMutableList()
        if (current.any { it.second == url }) return
        current.add(name to url)
        saveCustomRepos(current)
    }

    fun removeCustomRepo(url: String) {
        val current = getCustomRepos().filter { it.second != url }
        saveCustomRepos(current)
    }

    private fun saveCustomRepos(repos: List<Pair<String, String>>) {
        val arr = org.json.JSONArray()
        repos.forEach { (name, url) ->
            arr.put(org.json.JSONObject().apply {
                put("name", name)
                put("url", url)
            })
        }
        prefs.edit().putString(KEY_CUSTOM_REPOS, arr.toString()).apply()
    }

    /**
     * Clear cached container OS info.
     */
    fun clearContainerOSInfo(containerName: String) {
        prefs.edit().remove("${KEY_CONTAINER_OS_INFO_PREFIX}$containerName").commit()
    }

    /**
     * Save the entire container list to cache.
     * Stores names in one key and each config in its own key.
     */
    fun saveCachedContainers(containers: List<ContainerInfo>) {
        val editor = prefs.edit()
        
        // 1. Clear old configs to avoid stale data
        val oldNamesText = prefs.getString(KEY_CACHED_CONTAINER_NAMES, null)
        if (!oldNamesText.isNullOrEmpty()) {
            oldNamesText.split(",").forEach { name ->
                editor.remove("${KEY_CACHED_CONTAINER_CONFIG_PREFIX}$name")
            }
        }

        // 2. Save new names
        val names = containers.joinToString(",") { it.name }
        editor.putString(KEY_CACHED_CONTAINER_NAMES, names)
        
        // 3. Save each container's config content
        containers.forEach { container ->
            editor.putString("${KEY_CACHED_CONTAINER_CONFIG_PREFIX}${container.name}", container.toConfigContent())
        }
        editor.apply()
    }

    /**
     * Load the entire container list from cache.
     * Note: status will always be STOPPED and pid will be null.
     */
    val cachedContainers: List<ContainerInfo>
        get() {
            val namesText = prefs.getString(KEY_CACHED_CONTAINER_NAMES, null) ?: return emptyList()
            if (namesText.isEmpty()) return emptyList()
            
            val names = namesText.split(",")
            return names.mapNotNull { name ->
                val configContent = prefs.getString("${KEY_CACHED_CONTAINER_CONFIG_PREFIX}$name", null)
                if (configContent != null) {
                    // Display only until the first fetch, so skip the root read of each .env.
                    ContainerManager.parseConfig(configContent, name, loadEnvironment = false)
                } else null
            }
        }

    companion object {
        private const val PREFS_NAME = "droidspaces_prefs"
        private const val KEY_SETUP_COMPLETED = "setup_completed"
        private const val KEY_ROOT_CHECKED = "root_checked"
        private const val KEY_ROOT_AVAILABLE = "root_available"
        private const val KEY_DROIDSPACES_VERSION = "droidspaces_version"
        private const val KEY_RUNNING_COUNT = "running_count"
        private const val KEY_BACKEND_STATUS = "backend_status"
        private const val KEY_BACKEND_MODE = "backend_mode"
        private const val KEY_SELINUX_STATUS = "selinux_status"
        private const val KEY_HOST_CAPABILITIES = "host_capabilities"
        private const val KEY_HOST_CAPABILITIES_BOOT_ID = "host_capabilities_boot_id"
        private const val KEY_FOLLOW_SYSTEM_THEME = "follow_system_theme"
        private const val KEY_DARK_THEME = "dark_theme"
        private const val KEY_AMOLED_MODE = "amoled_mode"
        private const val KEY_USE_DYNAMIC_COLOR = "use_dynamic_color"
        private const val KEY_TERMINAL_DARK_THEME = "terminal_dark_theme"
        private const val KEY_TERMINAL_FONT_SIZE = "terminal_font_size_px"
        private const val KEY_TERMINAL_FONT_FILE = "terminal_font_file"
        private const val KEY_TERMINAL_CONFIRM_CLOSE = "terminal_confirm_close"
        private const val KEY_TERMINAL_TAP_KEYBOARD = "terminal_tap_keyboard"
        private const val KEY_TERMINAL_KEYBOARD_LEFT = "terminal_keyboard_button_left"
        private const val KEY_THEME_PALETTE = "theme_palette"
        private const val KEY_DAEMON_MODE_ENABLED = "daemon_mode_enabled"
        private const val KEY_SYMLINK_ENABLED = "symlink_enabled"
        private const val KEY_TREAT_AS_64BIT = "treat_as_64bit"
        private const val KEY_CHECK_APP_UPDATES = "check_app_updates"
        private const val KEY_CONTAINER_LOG_PREFIX = "container_log_"
        private const val KEY_CONTAINER_OS_INFO_PREFIX = "container_os_info_"
        private const val KEY_CACHED_CONTAINER_NAMES = "cached_container_names"
        private const val KEY_CACHED_CONTAINER_CONFIG_PREFIX = "cached_container_config_"
        private const val KEY_CUSTOM_REPOS = "custom_rootfs_repos"

        // Double-checked locking pattern for thread-safe singleton
        // @Volatile ensures visibility across threads without full synchronization
        @Volatile
        private var INSTANCE: PreferencesManager? = null

        // Optimized singleton access - double-checked locking pattern
        // After first initialization, INSTANCE is non-null, avoiding synchronization
        @JvmStatic
        fun getInstance(context: Context): PreferencesManager {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: PreferencesManager(context.applicationContext).also {
                    INSTANCE = it
                }
            }
        }
    }
}

