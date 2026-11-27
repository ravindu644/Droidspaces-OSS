package com.droidspaces.app.util

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Streams a container operation's log lines to the UI. The suspend forms hop to
 * the main thread because the list they feed is Compose state; [logImmediate]
 * is for callers already there, libsu's CallbackList.onAddElement among them.
 */
class ViewModelLogger(private val onLog: (Int, String) -> Unit) {
    fun logImmediate(level: Int, msg: String) = onLog(level, msg)

    suspend fun log(level: Int, msg: String) {
        Log.println(level, "ContainerLogger", msg)
        withContext(Dispatchers.Main.immediate) { onLog(level, msg) }
    }

    suspend fun d(msg: String) = log(Log.DEBUG, msg)
    suspend fun i(msg: String) = log(Log.INFO, msg)
    suspend fun w(msg: String) = log(Log.WARN, msg)
    suspend fun e(msg: String) = log(Log.ERROR, msg)
}
