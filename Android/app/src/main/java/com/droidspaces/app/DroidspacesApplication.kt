package com.droidspaces.app

import android.app.Application
import android.system.Os
import com.topjohnwu.superuser.Shell
import com.droidspaces.app.util.SystemInfoManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

class DroidspacesApplication : Application() {
    // Use SupervisorJob to prevent child coroutine failures from cancelling the scope.
    // Private: only this class schedules onto it (avoids an app-wide unscoped launcher).
    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    override fun onCreate() {
        super.onCreate()

        // Initialize Shell early - critical for instant root checks
        // This is done synchronously to avoid any delay on first root access
        Shell.setDefaultBuilder(Shell.Builder.create().setFlags(Shell.FLAG_MOUNT_MASTER))

        // Set TMPDIR for native operations (if any)
        Os.setenv("TMPDIR", cacheDir.absolutePath, true)

        // Pre-load system info in parallel on boot (non-blocking)
        applicationScope.launch {
            SystemInfoManager.initialize(this@DroidspacesApplication)
        }
    }
}

