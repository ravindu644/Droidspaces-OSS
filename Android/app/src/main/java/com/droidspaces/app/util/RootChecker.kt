package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

enum class RootStatus {
    Checking,
    Granted,
    Denied
}

object RootChecker {
    /** Runs a command as root, which asks the root manager on first use. */
    suspend fun checkRootAccess(): RootStatus = withContext(Dispatchers.IO) {
        if (Shell.cmd("id").exec().isSuccess && Shell.isAppGrantedRoot() == true) RootStatus.Granted
        else RootStatus.Denied
    }

}

