package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.ui.util.rememberClearFocus

/**
 * The shell every dialog sits in, and the layout inside it.
 *
 * The shell exists because the width had drifted five ways. It owns the layout
 * because the height was worse: a Column measures its unweighted children in
 * order against the space that is left, so a dialog whose actions sat below a
 * scrolling list handed that list the whole budget and measured its own buttons
 * with the remainder. On a tall screen that was 48.dp and looked fine. Rotate to
 * landscape and the same dialog squeezed its buttons to a sliver, or lost them.
 *
 * So [footer] is not part of [content]. It is unweighted and comes second in the
 * source, which means it is measured FIRST, at its natural height, always. The
 * body takes what is left and scrolls. That holds at any window size, in any
 * rotation, with the keyboard up.
 *
 * Callers therefore do not set a width, their own padding, or their own scroll.
 * Pass [scrollableContent] = false when the body is a LazyColumn, which cannot
 * live inside a scrolling parent: give the list `Modifier.weight(1f)` instead,
 * the body is already bounded. [modifier] is for what is genuinely per-dialog,
 * a `fillMaxHeight` fraction or a `heightIn` cap. The shell handles the keyboard
 * itself, so no `imePadding()`. Never pass `wrapContentHeight()`, the shell
 * already wraps. [borderColor] outlines
 * destructive dialogs in error.
 */
@Composable
fun DsDialog(
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    borderColor: Color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f),
    scrollableContent: Boolean = true,
    footer: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Dialog(
        onDismissRequest = onDismiss,
        // With the default decorFitsSystemWindows the dialog window pans itself to keep the
        // focused field above the keyboard and Compose never sees the IME inset, so the
        // footer slid under the keyboard and a caller's imePadding() was always zero.
        // Taking the insets here lets the shell shrink above the keyboard instead, and the
        // footer, which is measured first, stays on screen. Only the keyboard's inset: the
        // bars' insets drop to zero while the window closes, and padding for them made the
        // dialog jump on every dismiss. The height bound below already keeps it off both bars.
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        // The activity handles configChanges itself, so a dialog that is open
        // through a rotation keeps its stale window constraints and a bound
        // taken from them dangles off the shorter screen. LocalConfiguration is
        // live across rotation, the same source TerminalDialog sizes from, so
        // the shell bounds itself with it: screen height minus the 24.dp gutter
        // on each side. Caller caps land after it and shrink it further.
        val screenHeight = LocalConfiguration.current.screenHeightDp.dp
        // A 240px flip phone has no room for 96dp of side margins.
        val gutter = if (isCompactWidth()) 8.dp else 24.dp
        val inner = if (isCompactWidth()) 16.dp else 24.dp
        val clearFocus = rememberClearFocus()
        // Compose trims the IME inset by the gap between our view and the window edge.
        // A view that wraps the Surface changes that gap every time the padding below
        // resizes it, so the inset chased its own tail and the dialog bounced down, up,
        // then back to the middle as the keyboard opened. Filling the window keeps the gap
        // at zero. The cost is that every tap now lands inside our view, so the dialog's
        // own outside-tap dismissal never fires and the scrim dismisses here instead.
        Box(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(WindowInsets.ime)
                .pointerInput(Unit) { detectTapGestures { onDismiss() } },
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = gutter, vertical = gutter)
                    .heightIn(max = screenHeight - gutter * 2)
                    .then(modifier)
                    // A dialog is its own window, so the ClearFocusOnClickOutside that wraps the
                    // screen never sees these taps and a focused field stayed outlined. A gesture
                    // detector rather than clickable keeps TalkBack from announcing the dialog as
                    // a button.
                    .pointerInput(Unit) { detectTapGestures { clearFocus() } },
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surfaceContainer,
                border = BorderStroke(1.dp, borderColor),
                tonalElevation = 0.dp
            ) {
                Column(
                    modifier = Modifier.padding(inner),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Column(
                        modifier = Modifier
                            .weight(1f, fill = false)
                            .then(
                                if (scrollableContent) {
                                    Modifier.verticalScroll(rememberScrollState())
                                } else {
                                    Modifier
                                }
                            ),
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                        content = content
                    )
                    footer?.invoke()
                }
            }
        }
    }
}
