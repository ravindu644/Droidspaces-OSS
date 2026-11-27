package com.droidspaces.app.ui.terminal

import com.termux.terminal.TerminalEmulator
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient

/**
 * A stateless, application-scoped [TerminalSessionClient].
 *
 * A running [TerminalSession] keeps a reference to its client. The UI's client is a
 * [TerminalBackEnd], which strongly holds the Activity and its TerminalView, so a
 * backgrounded session living in [com.droidspaces.app.service.TerminalSessionService]
 * would otherwise pin the destroyed Activity/view tree in memory.
 *
 * When the terminal screen is disposed, each session's client is swapped to this
 * no-op so nothing UI-scoped is retained; the screen re-attaches its own
 * [TerminalBackEnd] via `updateTerminalSessionClient` on re-entry.
 */
object NoOpTerminalSessionClient : TerminalSessionClient {
    override fun onTextChanged(changedSession: TerminalSession) {}
    override fun onTitleChanged(changedSession: TerminalSession) {}
    override fun onSessionFinished(finishedSession: TerminalSession) {}
    override fun onCopyTextToClipboard(session: TerminalSession, text: String) {}
    override fun onPasteTextFromClipboard(session: TerminalSession) {}
    override fun onBell(session: TerminalSession) {}
    override fun onColorsChanged(session: TerminalSession) {}
    override fun onTerminalCursorStateChange(state: Boolean) {}
    override fun getTerminalCursorStyle(): Int = TerminalEmulator.DEFAULT_TERMINAL_CURSOR_STYLE

    override fun logError(tag: String?, message: String?) {}
    override fun logWarn(tag: String?, message: String?) {}
    override fun logInfo(tag: String?, message: String?) {}
    override fun logDebug(tag: String?, message: String?) {}
    override fun logVerbose(tag: String?, message: String?) {}
    override fun logStackTraceWithMessage(tag: String?, message: String?, e: Exception?) {}
    override fun logStackTrace(tag: String?, e: Exception?) {}
}
