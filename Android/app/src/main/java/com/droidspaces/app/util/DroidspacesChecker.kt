package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.nativebridge.NativeBridge
import com.droidspaces.app.util.SuExec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

sealed class DroidspacesBackendStatus {
    object Checking : DroidspacesBackendStatus()
    object Available : DroidspacesBackendStatus()
    object UpdateAvailable : DroidspacesBackendStatus()
    object NotInstalled : DroidspacesBackendStatus()
    object Corrupted : DroidspacesBackendStatus()
    object ModuleMissing : DroidspacesBackendStatus()
}

object DroidspacesChecker {
    private const val BUSYBOX_BINARY_PATH = Constants.BUSYBOX_BINARY_PATH
    private const val MAGISKPOLICY_BINARY_PATH = Constants.MAGISKPOLICY_BINARY_PATH
    private const val RUNNER_BINARY_PATH = Constants.RUNNER_BINARY_PATH
    private const val RUNNER_LIB_PATH = "${Constants.RUNNER_LIB_PATH}/libdroidspaces.so"
    private const val MAGISK_MODULE_PATH = "/data/adb/modules/droidspaces"
    private const val MODULE_PROP_PATH = "$MAGISK_MODULE_PATH/module.prop"

    /**
     * Check if Magisk module is installed.
     */
    private suspend fun checkModuleInstalled(): Boolean = withContext(Dispatchers.IO) {
        try {
            val dirCheck = SuExec.cmd("test -d '$MAGISK_MODULE_PATH'").exec()
            if (!dirCheck.isSuccess) return@withContext false
            val propCheck = SuExec.cmd("test -f '$MODULE_PROP_PATH'").exec()
            propCheck.isSuccess
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Check if the backend is available: module + busybox + magiskpolicy + SELinux policy.
     * Version is fetched via JNI.
     */
    suspend fun checkBackendStatus(): DroidspacesBackendStatus = withContext(Dispatchers.IO) {
        try {
            if (!checkModuleInstalled()) {
                return@withContext DroidspacesBackendStatus.ModuleMissing
            }

            fun checkBinary(binaryPath: String): Boolean {
                val existsCheck = SuExec.cmd("test -f '$binaryPath'").exec()
                if (!existsCheck.isSuccess) return false
                val execCheck = SuExec.cmd("test -x '$binaryPath'").exec()
                return execCheck.isSuccess
            }

            val busyboxOk = checkBinary(BUSYBOX_BINARY_PATH)
            val magiskpolicyOk = checkBinary(MAGISKPOLICY_BINARY_PATH)
            val runnerOk = checkBinary(RUNNER_BINARY_PATH)
            val runnerLibOk = SuExec.cmd("test -f '$RUNNER_LIB_PATH'").exec().isSuccess
            val teOk = SuExec.cmd("test -f ${Constants.DROIDSPACES_TE_PATH}").exec().isSuccess

            when {
                busyboxOk && magiskpolicyOk && runnerOk && runnerLibOk && teOk -> DroidspacesBackendStatus.Available
                !busyboxOk || !magiskpolicyOk || !teOk -> DroidspacesBackendStatus.NotInstalled
                else -> DroidspacesBackendStatus.Corrupted
            }
        } catch (e: Exception) {
            DroidspacesBackendStatus.NotInstalled
        }
    }

    /**
     * Get droidspaces version via JNI (no external binary needed).
     * Returns version string or null if unavailable.
     */
    suspend fun getDroidspacesVersion(): String? = withContext(Dispatchers.IO) {
        try {
            val version = NativeBridge.getVersion()
            version.takeIf { it.isNotBlank() }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Check if backend update is available.
     * Always returns false — library is bundled with the APK (NDK-managed).
     */
    suspend fun checkUpdateAvailable(context: Context): Boolean = withContext(Dispatchers.IO) {
        false
    }

    /**
     * Quick synchronous check (for cached state).
     */
    fun quickCheck(): DroidspacesBackendStatus? {
        return null
    }
}

