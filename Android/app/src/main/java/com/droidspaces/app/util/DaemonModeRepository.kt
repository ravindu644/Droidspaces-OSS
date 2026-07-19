package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell

/**
 * Owns the root-shell I/O for the backend daemon-mode flag, which lives in a
 * root-protected file on disk ([Constants.DAEMON_MODE_FILE]).
 *
 * Extracted out of [PreferencesManager] so the preference store stays a plain
 * key-value store and does not execute root shell commands itself. The value
 * written ("1"/"0") and the path are constants, so no argument quoting applies.
 */
object DaemonModeRepository {

    /** Persist the daemon-mode flag to the root-protected file (non-blocking). */
    fun writeToDisk(enabled: Boolean) {
        val value = if (enabled) "1" else "0"
        val path = Constants.DAEMON_MODE_FILE
        Shell.cmd("echo '$value' > '$path'").submit()
    }

    /**
     * Read the daemon-mode flag from disk. Returns null when the file does not
     * exist or cannot be read (blocking; call from a background dispatcher).
     */
    fun readFromDisk(): Boolean? {
        val path = Constants.DAEMON_MODE_FILE
        val result = Shell.cmd("cat '$path' 2>/dev/null").exec()
        if (result.isSuccess && result.out.isNotEmpty()) {
            return result.out[0].trim() == "1"
        }
        return null
    }
}
