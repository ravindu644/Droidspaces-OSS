package com.droidspaces.app.util

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

enum class RootStatus {
    Checking,
    Granted,
    Denied
}

object RootChecker {
    /**
     * Check if root access is available via su.
     */
    suspend fun checkRootAccess(): RootStatus = withContext(Dispatchers.IO) {
        return@withContext try {
            val result = SuExec.cmd("id").exec()
            if (result.isSuccess) RootStatus.Granted else RootStatus.Denied
        } catch (e: Exception) {
            RootStatus.Denied
        }
    }

    fun checkRootAccessSync(): RootStatus {
        return try {
            val result = SuExec.cmd("id").exec()
            if (result.isSuccess) RootStatus.Granted else RootStatus.Denied
        } catch (e: Exception) {
            RootStatus.Denied
        }
    }
}
