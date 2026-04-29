package com.droidspaces.app.ui.terminal

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.inputmethod.InputMethodManager
import com.droidspaces.app.ui.terminal.virtualkeys.SpecialButton
import com.termux.terminal.TerminalEmulator
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient
import com.termux.view.TerminalView
import com.termux.view.TerminalViewClient

class TerminalBackEnd(
    val terminal: TerminalView,
    val activity: android.app.Activity,
    initialFontSizePx: Int,
    private val onSessionFinished: () -> Unit = {},
    private val onFontSizeChanged: (Int) -> Unit = {},
) : TerminalViewClient, TerminalSessionClient {

    private var currentFontSizePx: Float = initialFontSizePx.toFloat()

    override fun onTextChanged(changedSession: TerminalSession) { terminal.onScreenUpdated() }
    override fun onTitleChanged(changedSession: TerminalSession) {}

    override fun onSessionFinished(finishedSession: TerminalSession) {
        // Don't auto-close - let the terminal show the exit message so the user
        // can see what went wrong. Closed by Enter key (onKeyDown) or Back button.
        terminal.onScreenUpdated()
    }

    override fun onCopyTextToClipboard(session: TerminalSession, text: String) {
        val cb = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cb.setPrimaryClip(ClipData.newPlainText(activity.getString(com.droidspaces.app.R.string.terminal_label), text))
    }

    override fun onPasteTextFromClipboard(session: TerminalSession) {
        val cb = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val text = cb.primaryClip?.getItemAt(0)?.text?.toString() ?: return
        if (text.isNotBlank() && terminal.mEmulator != null) terminal.mEmulator.paste(text)
    }

    override fun onBell(session: TerminalSession) {}
    override fun onColorsChanged(session: TerminalSession) {}
    override fun onTerminalCursorStateChange(state: Boolean) {}
    override fun getTerminalCursorStyle(): Int = TerminalEmulator.DEFAULT_TERMINAL_CURSOR_STYLE

    override fun logError(tag: String?, message: String?) { Log.e(tag.orEmpty(), message.orEmpty()) }
    override fun logWarn(tag: String?, message: String?) { Log.w(tag.orEmpty(), message.orEmpty()) }
    override fun logInfo(tag: String?, message: String?) { Log.i(tag.orEmpty(), message.orEmpty()) }
    override fun logDebug(tag: String?, message: String?) { Log.d(tag.orEmpty(), message.orEmpty()) }
    override fun logVerbose(tag: String?, message: String?) { Log.v(tag.orEmpty(), message.orEmpty()) }
    override fun logStackTraceWithMessage(tag: String?, message: String?, e: Exception?) {
        Log.e(tag.orEmpty(), message.orEmpty()); e?.printStackTrace()
    }
    override fun logStackTrace(tag: String?, e: Exception?) { e?.printStackTrace() }

    override fun onScale(scale: Float): Float {
        // `scale` is the accumulated pinch factor in TerminalView (per-frame deltas
        // multiplied together). Returning 1.0f resets that accumulator so the next
        // step needs a fresh pinch - prevents runaway zooming.
        when {
            scale > 1.08f -> {
                currentFontSizePx = (currentFontSizePx + 1f).coerceAtMost(72f)
                terminal.setTextSize(currentFontSizePx.toInt())
                onFontSizeChanged(currentFontSizePx.toInt())
                return 1.0f
            }
            scale < 0.92f -> {
                currentFontSizePx = (currentFontSizePx - 1f).coerceAtLeast(6f)
                terminal.setTextSize(currentFontSizePx.toInt())
                onFontSizeChanged(currentFontSizePx.toInt())
                return 1.0f
            }
        }
        return scale
    }

    override fun onSingleTapUp(e: MotionEvent) {
        val hasHardwareKeyboard = Resources.getSystem().configuration.keyboard != Configuration.KEYBOARD_NOKEYS
        if (!hasHardwareKeyboard) showSoftInput()
    }

    override fun shouldBackButtonBeMappedToEscape(): Boolean = false
    override fun shouldEnforceCharBasedInput(): Boolean = true
    override fun shouldUseCtrlSpaceWorkaround(): Boolean = true
    override fun isTerminalViewSelected(): Boolean = true
    override fun copyModeChanged(copyMode: Boolean) {}

    override fun onKeyDown(keyCode: Int, e: KeyEvent, session: TerminalSession): Boolean {
        if (keyCode == KeyEvent.KEYCODE_ENTER && !session.isRunning) {
            onSessionFinished.invoke()
            return true
        }
        return false
    }

    override fun onKeyUp(keyCode: Int, e: KeyEvent): Boolean = false
    override fun onLongPress(event: MotionEvent): Boolean = false

    override fun readControlKey(): Boolean =
        TerminalScreenState.virtualKeysView.get()?.readSpecialButton(SpecialButton.CTRL, true) == true

    override fun readAltKey(): Boolean =
        TerminalScreenState.virtualKeysView.get()?.readSpecialButton(SpecialButton.ALT, true) == true

    override fun readShiftKey(): Boolean =
        TerminalScreenState.virtualKeysView.get()?.readSpecialButton(SpecialButton.SHIFT, true) == true

    override fun readFnKey(): Boolean =
        TerminalScreenState.virtualKeysView.get()?.readSpecialButton(SpecialButton.FN, true) == true

    override fun onCodePoint(codePoint: Int, ctrlDown: Boolean, session: TerminalSession): Boolean = false

    override fun onEmulatorSet() {
        if (terminal.mEmulator != null) terminal.setTerminalCursorBlinkerState(true, true)
    }

    private fun showSoftInput() {
        terminal.requestFocus()
        val imm = activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imm.showSoftInput(terminal, InputMethodManager.SHOW_IMPLICIT)
    }
}
