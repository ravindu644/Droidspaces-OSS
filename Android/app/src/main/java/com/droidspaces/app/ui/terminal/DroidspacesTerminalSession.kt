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

    // Container names allow letters, digits, spaces, '_', '.', '-' (mirrors
    // ValidationUtils.validateContainerName), none are shell metacharacters, so
    // a validated name is safe inside the "..." payload. A username discovered
    // from the container's /etc/passwd is NOT validated elsewhere, so it is
    // checked here too.
    private val SAFE_NAME = Regex("^[a-zA-Z0-9_\\s.-]+$")
    private val SAFE_USER = Regex("^[A-Za-z0-9_.@:+-]+$")

    fun create(
        sessionClient: TerminalSessionClient,
        containerName: String?,
        containerUser: String? = null,
    ): TerminalSession {
        val safeName = containerName?.takeIf { SAFE_NAME.matches(it) }
        // No container, or a name carrying shell metacharacters: a bare root shell
        // rather than an injectable payload. A username with metacharacters falls
        // back to root the same way. safeName cannot hold " ' $ or `, the escape is
        // belt-and-suspenders.
        val shArg = if (safeName == null) "su" else {
            val user = containerUser?.takeIf { SAFE_USER.matches(it) } ?: "root"
            "su -c 'droidspaces -n \"${safeName.replace("\"", "\\\"")}\" enter $user'"
        }
        return TerminalSession(
            /* shell      = */ "/system/bin/sh",
            /* workingDir = */ "/sdcard",
            // args[0] MUST be the shell path - the JNI uses args directly as argv,
            // it does NOT prepend shellPath as argv[0].
            /* args       = */ arrayOf("/system/bin/sh", "-c", shArg),
            /* env        = */ buildEnv().toTypedArray(),
            /* rows       = */ TerminalEmulator.DEFAULT_TERMINAL_TRANSCRIPT_ROWS,
            /* client     = */ sessionClient,
        )
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
