package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties

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
 * `imePadding()`, a `fillMaxHeight` fraction, or a `heightIn` cap. Never pass
 * `wrapContentHeight()`, the shell already wraps. [borderColor] outlines
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
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        // The activity handles configChanges itself, so a dialog that is open
        // through a rotation keeps its stale window constraints and a bound
        // taken from them dangles off the shorter screen. LocalConfiguration is
        // live across rotation, so the shell bounds itself with it: screen
        // height minus the 24.dp gutter on each side. Caller caps land after it
        // and shrink it further.
        val screenHeight = LocalConfiguration.current.screenHeightDp.dp
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 24.dp)
                .heightIn(max = screenHeight - 48.dp)
                .then(modifier),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, borderColor),
            tonalElevation = 0.dp
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
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
