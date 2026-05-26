package com.droidspaces.app.util

import android.os.Handler
import android.os.Looper
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume

/**
 * Executes container operations (start/stop/restart) with TRUE real-time output streaming.
 *
 * Uses [SuExec] instead of libsu Shell.cmd.
 *
 * Each line from the binary appears in the terminal immediately as it's printed.
 */
object ContainerOperationExecutor {
    private val errorPattern = Regex("""(?i)(error|failed|fail)""")
    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * Execute a container command with real-time output streaming.
     *
     * @param operationCompletedMessage Optional message to log after success.
     */
    suspend fun executeCommand(
        command: String,
        operation: String,
        logger: ContainerLogger,
        skipHeader: Boolean = false,
        operationCompletedMessage: String? = null
    ) = withContext(Dispatchers.IO) {
        try {
            if (!skipHeader) {
                logger.i("Starting $operation operation...")
                logger.i("Command: $command")
                logger.i("")
            }

            val lastLineWasEmpty = java.util.concurrent.atomic.AtomicBoolean(false)

            // Streaming callback using ShellCallback (replaces libsu CallbackList)
            val callback = object : ShellCallback {
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

            val result = SuExec.cmd("$command 2>&1").to(callback).exec()

            suspendCancellableCoroutine<Unit> { continuation ->
                mainHandler.post {
                    if (!lastLineWasEmpty.get()) {
                        logger.logImmediate(Log.INFO, "")
                    }
                    if (result.isSuccess) {
                        logger.logImmediate(Log.INFO, "Command executed (exit code: ${result.code})")
                        if (operationCompletedMessage != null) {
                            logger.logImmediate(Log.INFO, "")
                            logger.logImmediate(Log.INFO, operationCompletedMessage)
                        }
                    } else {
                        logger.logImmediate(Log.ERROR, "Command failed (exit code: ${result.code})")
                    }
                    continuation.resume(Unit)
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
        SuExec.cmd("$command 2>&1").exec().isSuccess
    }
}
