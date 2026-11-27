package com.droidspaces.app.ui.component

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import kotlinx.coroutines.launch
import kotlinx.coroutines.CoroutineScope
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.remember
import androidx.compose.material3.SheetState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.util.rememberClearFocus

/**
 * The sheet an "add" action opens: a title, a short form and a footer, over the
 * page it adds to, so the list being added to stays in view.
 *
 * Laid out like [DsDialog] and for the same reason: [footer] comes after the
 * weighted body, so it is measured first at its natural height and the body
 * scrolls in whatever is left. The column takes the keyboard's height as
 * padding, which on a 320px-tall screen leaves the body a sliver, but the
 * footer and the focused field (verticalScroll brings it into view) stay
 * reachable. The sheet draws under the system bars, so the column pads for the
 * navigation bar itself.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DsBottomSheet(
    onDismiss: () -> Unit,
    title: String,
    footer: @Composable SheetCloser.() -> Unit,
    titleAction: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    StandardMotion {
        val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        val scope = rememberCoroutineScope()
        val closer = remember(sheetState) { SheetCloser(scope, sheetState) }
        val side = if (isCompactWidth()) 16.dp else 24.dp
        ModalBottomSheet(
            onDismissRequest = onDismiss,
            sheetState = sheetState,
            // A sheet tall enough to reach the top would draw its title under the
            // status bar, so the space it may grow into stops below it.
            modifier = Modifier.windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top)),
            shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
            containerColor = MaterialTheme.colorScheme.surfaceContainer,
            tonalElevation = 0.dp,
            contentWindowInsets = { WindowInsets(0) },
            dragHandle = { BottomSheetDefaults.DragHandle() }
        ) {
            // Taken here, inside the sheet: the sheet is its own window, and a focus
            // manager taken outside it clears focus in the screen behind instead.
            val clearFocus = rememberClearFocus()
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    // ClearFocusOnClickOutside would fill the height and make every sheet full screen
                    .pointerInput(Unit) { detectTapGestures { clearFocus() } }
                    .imePadding()
                    .navigationBarsPadding()
                    .padding(start = side, end = side, bottom = 16.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        title,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.weight(1f)
                    )
                    titleAction?.invoke()
                }
                Column(
                    modifier = Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    content = content
                )
                closer.footer()
            }
        }
    }
}

/**
 * Handed to a sheet's footer. A sheet its caller simply stops showing is torn
 * down mid-frame and vanishes, so the footer's buttons [close] it: the sheet
 * slides away first and [close]'s action runs once it is gone. A swipe or a tap
 * on the scrim already animates out before onDismiss.
 *
 * Only the first [close] counts. A second tap during the slide-out would cancel
 * the first hide, and a cancelled hide completes too, so the action ran at once
 * and then again when the second hide finished: one tap on Add, two mounts.
 */
@OptIn(ExperimentalMaterial3Api::class)
class SheetCloser internal constructor(private val scope: CoroutineScope, private val state: SheetState) {
    private var closing = false

    fun close(then: () -> Unit) {
        if (closing) return
        closing = true
        scope.launch { state.hide() }.invokeOnCompletion { then() }
    }
}

/**
 * The app theme is Expressive for the installer's shape morphs, and a sheet
 * inherits its spring, so it bounces as it arrives. Sheets slide in on the
 * standard motion instead, the way menus do (see DsMenuTheme).
 */
@Composable
fun StandardMotion(content: @Composable () -> Unit) {
    MaterialTheme(motionScheme = MotionScheme.standard(), content = content)
}
