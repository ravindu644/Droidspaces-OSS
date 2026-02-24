package com.droidspaces.app.util

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Logger interface for container operations (inspired by LSPatch Logger pattern).
 * Allows real-time log streaming to UI with zero overhead.
 */
abstract class ContainerLogger {
    abstract suspend fun d(msg: String)
    abstract suspend fun i(msg: String)
    abstract suspend fun e(msg: String)
    abstract suspend fun w(msg: String)

    /**
     * Non-suspend, synchronous log — safe to call directly from the main thread
     * (e.g. inside libsu's CallbackList.onAddElement which already runs on main).
     * Avoids the MainScope().launch antipattern which creates untracked detached coroutines.
     */
    abstract fun logImmediate(level: Int, msg: String)

    var verbose: Boolean = false
}

/**
 * ViewModel logger implementation that captures logs for UI display.
 * Optimized for real-time streaming - ensures all log updates are on Main thread
 * for instant UI responsiveness. Uses suspend functions to allow efficient
 * thread switching without creating coroutines for each message.
 */
class ViewModelLogger(
    private val onLog: (Int, String) -> Unit  // Changed to non-suspend: called on main thread directly
) : ContainerLogger() {

    override fun logImmediate(level: Int, msg: String) {
        // Already on main thread (called from libsu's onAddElement), invoke directly
        onLog(level, msg)
    }

    override suspend fun d(msg: String) {
        if (verbose) {
            Log.d("ContainerLogger", msg)
            withContext(Dispatchers.Main.immediate) {
                onLog(Log.DEBUG, msg)
            }
        }
    }

    override suspend fun i(msg: String) {
        Log.i("ContainerLogger", msg)
        withContext(Dispatchers.Main.immediate) {
            onLog(Log.INFO, msg)
        }
    }

    override suspend fun e(msg: String) {
        Log.e("ContainerLogger", msg)
        withContext(Dispatchers.Main.immediate) {
            onLog(Log.ERROR, msg)
        }
    }

    override suspend fun w(msg: String) {
        Log.w("ContainerLogger", msg)
        withContext(Dispatchers.Main.immediate) {
            onLog(Log.WARN, msg)
        }
    }
}
