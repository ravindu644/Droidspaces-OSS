package com.droidspaces.app

import android.app.Application
import android.system.Os
import com.droidspaces.app.util.ContainerRuntime
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.WorkspacePaths
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

class DroidspacesApplication : Application() {
    val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    override fun onCreate() {
        super.onCreate()

        // Initialize dual-mode runtime (rootless JNI + root SuExec)
        ContainerRuntime.init(this)
        WorkspacePaths.init(this)

        // Set TMPDIR for native operations
        Os.setenv("TMPDIR", cacheDir.absolutePath, true)

        // Pre-load system info in parallel on boot
        applicationScope.launch {
            SystemInfoManager.initialize(this@DroidspacesApplication)
        }
    }
}
