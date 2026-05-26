package com.droidspaces.app.ui.terminal

import com.droidspaces.app.util.Constants
import com.termux.terminal.TerminalEmulator
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient

/**
 * Creates a [TerminalSession] that opens a root shell inside a Droidspaces container.
 *
 * ── How TerminalSession passes args to execvp ────────────────────────────────
 *
 * termux.c JNI creates:  execvp(shellPath, argv)
 * where argv IS the Java args array - shellPath is NOT prepended as argv[0].
 *
 * Therefore args[0] MUST be the shell path (argv[0] convention), otherwise:
 *   args = ["-c", "cmd"]  →  execvp(sh, ["-c", "cmd"])
 *   shell sees argv[0]="-c" (its own name), treats "cmd" as a script FILE path
 *   → error: "c: cmd: No such file or directory"   ← exactly what we saw
 *
 * Correct:
 *   args = ["/system/bin/sh", "-c", "cmd"]  →  execvp(sh, ["sh", "-c", "cmd"])
 *   shell sees argv[0]="sh", argv[1]="-c", argv[2]="cmd"  ← works correctly
 *
 * ── Working command (confirmed on device) ───────────────────────────────────
 *
 *   sh -c "su -c 'droidspaces -n "Name" enter user'"
 *
 * Mapped to TerminalSession:
 *   shell = "/system/bin/sh"
 *   args  = ["/system/bin/sh", "-c", "su -c 'droidspaces -n \"Name\" enter user'"]
 */
object DroidspacesTerminalSession {

    fun create(
        sessionClient: TerminalSessionClient,
        containerName: String?,
        containerUser: String? = null,
    ): TerminalSession {
        return if (containerName != null) {
            // Escape any literal " in the container name - it sits inside "..." in the su payload.
            val escapedName = containerName.replace("\"", "\\\"")
            val user = containerUser ?: "root"

            // Equivalent to: sh -c "su -c '/data/local/Droidspaces/bin/droidspaces-runner -n "Name" enter user'"
            val shArg = "su -c '${Constants.RUNNER_BINARY_PATH} -n \"$escapedName\" enter $user'"

            TerminalSession(
                /* shell      = */ "/system/bin/sh",
                /* workingDir = */ "/sdcard",
                // args[0] MUST be the shell path - the JNI uses args directly as argv,
                // it does NOT prepend shellPath as argv[0].
                /* args       = */ arrayOf("/system/bin/sh", "-c", shArg),
                /* env        = */ buildEnv().toTypedArray(),
                /* rows       = */ TerminalEmulator.DEFAULT_TERMINAL_TRANSCRIPT_ROWS,
                /* client     = */ sessionClient,
            )
        } else {
            // Bare interactive root shell.
            TerminalSession(
                /* shell      = */ "/system/bin/sh",
                /* workingDir = */ "/sdcard",
                /* args       = */ arrayOf("/system/bin/sh", "-c", "su"),
                /* env        = */ buildEnv().toTypedArray(),
                /* rows       = */ TerminalEmulator.DEFAULT_TERMINAL_TRANSCRIPT_ROWS,
                /* client     = */ sessionClient,
            )
        }
    }

    private fun buildEnv(): List<String> {
        val systemPath = System.getenv("PATH") ?: "/sbin:/system/bin:/system/xbin"
        val env = mutableListOf(
            "TERM=xterm-256color",
            "COLORTERM=truecolor",
            "LANG=C.UTF-8",
            "HOME=/root",
            "PATH=${Constants.INSTALL_PATH}:$systemPath",
            "EXTERNAL_STORAGE=${System.getenv("EXTERNAL_STORAGE") ?: "/sdcard"}",
        )
        listOf(
            "ANDROID_ART_ROOT", "ANDROID_DATA", "ANDROID_I18N_ROOT",
            "ANDROID_ROOT", "ANDROID_RUNTIME_ROOT", "ANDROID_TZDATA_ROOT",
        ).forEach { key ->
            System.getenv(key)?.let { env.add("$key=$it") }
        }
        return env
    }
}
