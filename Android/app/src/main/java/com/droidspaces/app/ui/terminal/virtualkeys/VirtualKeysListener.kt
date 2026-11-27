package com.droidspaces.app.ui.terminal.virtualkeys

import android.view.KeyEvent
import android.view.View
import android.widget.Button
import com.droidspaces.app.ui.terminal.TerminalScreenState
import com.termux.terminal.TerminalSession

class VirtualKeysListener(val session: TerminalSession) : VirtualKeysView.IVirtualKeysView {

    override fun onVirtualKeyButtonClick(
        view: View?,
        buttonInfo: VirtualKeyButton?,
        button: Button?,
    ) {
        val key = buttonInfo?.key ?: return

        // Arrow/cursor keys require cursor-key-mode awareness.
        // Programs like `less`, `vim`, and `systemctl status` enable DECCKM
        // (\e[?1h, application cursor key mode) and expect \eOA/B/C/D,
        // but we were hardcoding \e[A/B/C/D (normal mode), so they never
        // scrolled. Routing through TerminalView.onKeyDown() lets the
        // TerminalEmulator check its current DECCKM state and send the right
        // escape sequence automatically, matching Termux's own behaviour.
        val dpadCode: Int? = when (key) {
            "UP"    -> KeyEvent.KEYCODE_DPAD_UP
            "DOWN"  -> KeyEvent.KEYCODE_DPAD_DOWN
            "LEFT"  -> KeyEvent.KEYCODE_DPAD_LEFT
            "RIGHT" -> KeyEvent.KEYCODE_DPAD_RIGHT
            else    -> null
        }
        if (dpadCode != null) {
            TerminalScreenState.terminalView.get()?.let { tv ->
                tv.onKeyDown(dpadCode, KeyEvent(KeyEvent.ACTION_DOWN, dpadCode))
                return
            }
        }

        val writeable: String = when (key) {
            "ENTER" -> "\u000D"    // Carriage Return
            "PGUP"  -> "\u001B[5~" // Page Up
            "PGDN"  -> "\u001B[6~" // Page Down
            "TAB"   -> "\u0009"    // Tab
            "HOME"  -> "\u001B[H"  // Home
            "END"   -> "\u001B[F"  // End
            "ESC"   -> "\u001B"    // Escape
            else    -> key
        }
        session.write(writeable)
    }

    override fun performVirtualKeyButtonHapticFeedback(
        view: View?,
        buttonInfo: VirtualKeyButton?,
        button: Button?,
    ): Boolean = false
}
