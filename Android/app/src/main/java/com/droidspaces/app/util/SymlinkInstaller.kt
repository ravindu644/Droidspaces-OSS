package com.droidspaces.app.util

import com.droidspaces.app.util.SuExec

object SymlinkInstaller {

    /** Returns true if the runner symlink exists at the module system/bin path. */
    private const val RUNNER_PATH = Constants.RUNNER_BINARY_PATH
    private const val MODULE_RUNNER_LINK = "${Constants.MODULE_SYSTEM_BIN_PATH}/${Constants.RUNNER_BINARY_NAME}"

    fun isSymlinkEnabled(): Boolean =
        SuExec.cmd("test -L '$MODULE_RUNNER_LINK'").exec().isSuccess

    /** Creates system/bin dir, symlink to runner, and sets permissions. Returns success. */
    fun enable(): Boolean {
        val binDir = Constants.MODULE_SYSTEM_BIN_PATH

        SuExec.cmd("mkdir -p '$binDir'").exec().takeIf { it.isSuccess } ?: return false
        SuExec.cmd("rm -f '$MODULE_RUNNER_LINK'").exec()
        SuExec.cmd("ln -sf '$RUNNER_PATH' '$MODULE_RUNNER_LINK'").exec().takeIf { it.isSuccess } ?: return false
        SuExec.cmd("chmod 755 '$MODULE_RUNNER_LINK'").exec()
        return true
    }

    /** Nukes the entire system/bin folder from the module. Returns success. */
    fun disable(): Boolean =
        SuExec.cmd("rm -rf '${Constants.MODULE_SYSTEM_BIN_PATH}'").exec().isSuccess
}
