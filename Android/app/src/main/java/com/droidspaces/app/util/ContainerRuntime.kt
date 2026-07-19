package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell

/**
 * Thin gateway over the root shell for container-runtime operations.
 *
 * The single place ViewModels reach the backend binary, so command construction
 * and quoting live here instead of being inlined in the UI/ViewModel layer. Any
 * dynamic or user-supplied argument passed through this object MUST be quoted
 * via [ContainerCommandBuilder.quote].
 */
object ContainerRuntime {

    /**
     * Run the backend `scan` to reconcile on-disk containers. Blocking; call
     * from a background dispatcher. The command is built from constants only
     * (no user input), so no argument quoting is required here.
     */
    fun scan(): Shell.Result {
        val command = "${Constants.getDroidspacesCommand()} scan"
        return Shell.cmd(command).exec()
    }
}
