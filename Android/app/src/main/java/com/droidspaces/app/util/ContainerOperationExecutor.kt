package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Executes container operations (start/stop/restart) with TRUE real-time output streaming.
 *
 * Uses libsu's CallbackList API to receive each line of output as the binary produces it.
 * Each line is immediately dispatched to the logger via logImmediate() — no coroutines,
 * no fire-and-forget detached scopes, no race conditions.
 *
 * Flow: Binary stdout → CallbackList.onAddElement() [main thread] → logImmediate() → TerminalConsole
 */
object ContainerOperationExecutor {
    // Pre-compiled error pattern for efficient matching
    private val errorPattern = Regex("""(?i)(error|failed|fail)""")

    /**
     * Execute a container command with real-time output streaming.
     *
     * Each line from the binary appears in the terminal immediately as it's printed.
     * The coroutine blocks on exec() until the process exits, but output streams in real-time
     * via CallbackList's onAddElement() callback — which libsu calls on the main thread.
     *
     * We call logger.logImmediate() synchronously inside onAddElement instead of spinning up
     * MainScope().launch coroutines, which were detached, untracked, and caused the terminal
     * to cut off before all lines were flushed to the UI.
     */
    suspend fun executeCommand(
        command: String,
        operation: String, // "start", "stop", "restart", "check"
        logger: ContainerLogger,
        skipHeader: Boolean = false
    ) = withContext(Dispatchers.IO) {
        try {
            if (!skipHeader) {
                logger.i("Starting $operation operation...")
                logger.i("Command: $command")
                logger.i("")
            }

            val lastLineWasEmpty = java.util.concurrent.atomic.AtomicBoolean(false)

            // onAddElement is called by libsu on the main thread as each line arrives.
            // We call logImmediate() directly — synchronous, no coroutines, no races.
            val callbackList = object : CallbackList<String>() {
                override fun onAddElement(s: String?) {
                    s ?: return
                    val trimmed = s.trim()
                    if (trimmed.isEmpty()) {
                        logger.logImmediate(Log.INFO, "")
                        lastLineWasEmpty.set(true)
                    } else {
                        if (errorPattern.containsMatchIn(trimmed)) {
                            logger.logImmediate(Log.ERROR, s)
                        } else {
                            logger.logImmediate(Log.INFO, s)
                        }
                        lastLineWasEmpty.set(false)
                    }
                }
            }

            // exec() blocks on IO until the process exits.
            // All output has already been delivered synchronously via onAddElement by this point.
            val result = Shell.cmd("$command 2>&1").to(callbackList).exec()

            // No delay needed — onAddElement is synchronous, all lines are already in the UI.

            // Log result status
            withContext(Dispatchers.Main.immediate) {
                if (!lastLineWasEmpty.get()) {
                    logger.logImmediate(Log.INFO, "")
                }
                if (result.isSuccess) {
                    logger.logImmediate(Log.INFO, "Command executed (exit code: ${result.code})")
                } else {
                    logger.logImmediate(Log.ERROR, "Command failed (exit code: ${result.code})")
                }
            }

            result.isSuccess
        } catch (e: Exception) {
            logger.e("Exception executing command: ${e.message}")
            logger.e(e.stackTraceToString())
            false
        }
    }

    /**
     * Check if a command execution was successful.
     */
    suspend fun checkCommandSuccess(command: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("$command 2>&1").exec()
            result.isSuccess
        } catch (e: Exception) {
            false
        }
    }
}
