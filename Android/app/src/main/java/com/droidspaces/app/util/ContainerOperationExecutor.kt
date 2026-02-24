package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.delay

/**
 * Executes container operations (start/stop/restart) with TRUE real-time output streaming.
 *
 * Uses libsu's CallbackList API to receive each line of output as the binary produces it.
 * Each line is immediately dispatched to the logger callback, which updates the UI via
 * Dispatchers.Main.immediate for instant rendering in the TerminalConsole.
 *
 * Flow: Binary stdout → CallbackList.onAddElement() → ViewModelLogger → TerminalConsole
 */
object ContainerOperationExecutor {
    // Pre-compiled error pattern for efficient matching
    private val errorPattern = Regex("""(?i)(error|failed|fail)""")

    /**
     * Execute a container command with real-time output streaming.
     *
     * Each line from the binary appears in the pop-up terminal immediately as it's printed.
     * The coroutine blocks on exec() until the process exits, but output streams in real-time
     * via CallbackList's onAddElement() callback.
     */
    suspend fun executeCommand(
        command: String,
        operation: String, // "start", "stop", "restart", "check"
        logger: ContainerLogger,
        skipHeader: Boolean = false // Skip "Starting..." messages for operations like "check"
    ) = withContext(Dispatchers.IO) {
        try {
            if (!skipHeader) {
                logger.i("Starting ${operation} operation...")
                logger.i("Command: $command")
                logger.i("")
            }

            // Track if last line was empty (for final status formatting)
            // Use atomic reference since it's accessed from different threads (onAddElement vs main thread)
            val lastLineWasEmpty = java.util.concurrent.atomic.AtomicBoolean(false)

            // Create a CallbackList that streams each output line to the logger in real-time.
            // onAddElement() is called by libsu on the main thread (default executor) as each
            // line arrives from the process, BEFORE exec() returns.
            val callbackList = object : CallbackList<String>() {
                override fun onAddElement(s: String?) {
                    s ?: return
                    // Launch a coroutine on Main to call suspend logger functions.
                    // This is fire-and-forget since onAddElement is synchronous.
                    MainScope().launch {
                        val trimmed = s.trim()
                        if (trimmed.isEmpty()) {
                            logger.i("")
                            lastLineWasEmpty.set(true)
                        } else {
                            if (errorPattern.containsMatchIn(trimmed)) {
                                logger.e(s) // Log original line to preserve ANSI formatting
                            } else {
                                logger.i(s) // Log original line to preserve ANSI formatting
                            }
                            lastLineWasEmpty.set(false)
                        }
                    }
                }
            }

            // Execute command with real-time streaming via CallbackList.
            // 2>&1 merges stderr into stdout for unified output handling.
            // exec() blocks until the process exits, but callbackList fires per-line.
            val result = Shell.cmd("$command 2>&1").to(callbackList).exec()

            // Small delay to ensure all onAddElement coroutines have completed
            // This prevents race condition where final status logging doesn't see
            // the correct lastLineWasEmpty state
            kotlinx.coroutines.delay(100)

            // Log result status - only add newline if last output line wasn't empty
            if (!lastLineWasEmpty.get()) {
                logger.i("")
            }
            if (result.isSuccess) {
                logger.i("Command executed (exit code: ${result.code})")
            } else {
                logger.e("Command failed (exit code: ${result.code})")
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
