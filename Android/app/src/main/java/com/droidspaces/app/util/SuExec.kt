package com.droidspaces.app.util

import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * Lightweight replacement for libsu SuExec.cmd().
 *
 * Uses ProcessBuilder to run `su -c <command>` for root mode,
 * or plain `sh -c <command>` for non-root (rootless) mode.
 *
 * Mirrors the libsu API surface: .cmd().exec() / .to().exec() / .submit()
 */
data class ShellResult(
    val isSuccess: Boolean,
    val out: List<String>,
    val err: List<String>,
    val code: Int
)

interface ShellCallback {
    fun onAddElement(line: String?)
}

class ShellCommandBuilder(
    private val command: String,
    private val useRoot: Boolean = true
) {
    private var callback: ShellCallback? = null

    /** Register a streaming callback for stdout lines. */
    fun to(callback: ShellCallback): ShellCommandBuilder {
        this.callback = callback
        return this
    }

    /** Fire-and-forget execution (runs on a background thread). */
    fun submit() {
        Executors.newSingleThreadExecutor().submit { exec() }
    }

    /** Execute the command and wait for completion (default 30s timeout). */
    fun exec(timeoutMs: Long = 30000): ShellResult {
        val shell = if (useRoot) "su" else "sh"
        val pb = ProcessBuilder(shell, "-c", command)
        pb.redirectErrorStream(false)

        return try {
            val process = pb.start()

            val stdoutReader = BufferedReader(InputStreamReader(process.inputStream))
            val stdoutLines = mutableListOf<String>()
            var line: String?
            while (stdoutReader.readLine().also { line = it } != null) {
                stdoutLines.add(line!!)
                callback?.onAddElement(line)
            }

            val stderrReader = BufferedReader(InputStreamReader(process.errorStream))
            val stderrLines = mutableListOf<String>()
            while (stderrReader.readLine().also { line = it } != null) {
                stderrLines.add(line!!)
            }

            val finished = process.waitFor(timeoutMs, TimeUnit.MILLISECONDS)
            if (!finished) {
                process.destroyForcibly()
                return ShellResult(
                    isSuccess = false,
                    out = stdoutLines,
                    err = stderrLines + listOf("Command timed out after ${timeoutMs}ms"),
                    code = -1
                )
            }

            val exitCode = process.exitValue()
            ShellResult(
                isSuccess = exitCode == 0,
                out = stdoutLines,
                err = stderrLines,
                code = exitCode
            )
        } catch (e: Exception) {
            ShellResult(
                isSuccess = false,
                out = emptyList(),
                err = listOf(e.message ?: "Unknown error"),
                code = -1
            )
        }
    }
}

/**
 * Replacement for libsu `SuExec.cmd()` — root shell execution via ProcessBuilder.
 *
 * Usage (matching libsu):
 *   SuExec.cmd("some command").exec()
 *   SuExec.cmd("command").to(callback).exec()
 *   SuExec.cmd("command").submit()
 *
 * For rootless (non-root) commands:
 *   SuExec.nonRoot("command").exec()
 */
object SuExec {

    /** Execute command via `su -c`. Requires root. */
    fun cmd(command: String): ShellCommandBuilder = ShellCommandBuilder(command, useRoot = true)

    /** Execute command via `sh -c`. No root needed. */
    fun nonRoot(command: String): ShellCommandBuilder = ShellCommandBuilder(command, useRoot = false)

    /** Quick check: does a command succeed? */
    fun check(command: String): Boolean = cmd(command).exec().isSuccess

    /** Quick check: does a command succeed without root? */
    fun checkNonRoot(command: String): Boolean = nonRoot(command).exec().isSuccess
}
