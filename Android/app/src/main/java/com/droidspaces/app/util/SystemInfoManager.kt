package com.droidspaces.app.util

import android.content.Context
import android.os.Build
import android.system.Os
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Host facts the status cards show. The static ones come from uname and Build; the
 * SELinux status, backend version and backend mode are fetched at app start and
 * cached in memory, with the last answer also kept in preferences so a card can
 * draw before the shell replies.
 */
object SystemInfoManager {
    private val uname = Os.uname()
    val kernelVersion: String get() = uname.release
    val architecture: String get() = uname.machine
    val androidVersion = "${Build.VERSION.RELEASE} (${Build.VERSION.SDK_INT})"

    @Volatile private var prefs: PreferencesManager? = null
    @Volatile private var version: String? = null
    @Volatile private var mode: String? = null

    /** What the UI draws first; [refreshSELinuxStatus] corrects it. */
    @Volatile var cachedSelinuxStatus: String? = null
        private set

    suspend fun initialize(context: Context) = withContext(Dispatchers.IO) {
        val p = PreferencesManager.getInstance(context).also { prefs = it }
        if (cachedSelinuxStatus == null) cachedSelinuxStatus = p.cachedSelinuxStatus
        HostCapabilities.load(context)
        coroutineScope {
            launch { refreshSELinuxStatus() }
            launch { getDroidspacesVersion(context, force = true) }
            launch { getBackendMode(context, force = true) }
        }
    }

    suspend fun refreshSELinuxStatus(): String = withContext(Dispatchers.IO) {
        SELinuxChecker.getSELinuxStatus().also {
            cachedSelinuxStatus = it
            prefs?.cachedSelinuxStatus = it
        }
    }

    /**
     * `droidspaces version`, cached unless [force]. A fresh answer is saved to
     * preferences; a failed fetch (the daemon mid-restart after an update) keeps the
     * last answer rather than blanking the card, [resetCache] is what clears it.
     */
    suspend fun getDroidspacesVersion(context: Context, force: Boolean = false): String? {
        if (!force) version?.let { return it }
        return DroidspacesChecker.getDroidspacesVersion()?.also {
            version = it
            PreferencesManager.getInstance(context).cachedDroidspacesVersion = it
        } ?: version
    }

    /** `droidspaces mode`, DAEMON or DIRECT, cached like the version. */
    suspend fun getBackendMode(context: Context, force: Boolean = false): String? {
        if (!force) mode?.let { return it }
        val result = withContext(Dispatchers.IO) { Shell.cmd("${Constants.getDroidspacesCommand()} mode 2>/dev/null").exec() }
        return result.out.firstOrNull()?.trim()?.takeIf { result.isSuccess && it.isNotEmpty() }?.uppercase()?.also {
            mode = it
            PreferencesManager.getInstance(context).cachedBackendMode = it
        } ?: mode
    }

    /** After a backend install or update, so the next read asks the new binary. */
    fun resetCache() {
        version = null
        mode = null
    }
}
