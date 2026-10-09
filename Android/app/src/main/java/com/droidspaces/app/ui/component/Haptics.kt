package com.droidspaces.app.ui.component

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.remember
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback

/**
 * The haptics table in DESIGN.md, one method per moment, so a call site says what
 * happened and the moment-to-type mapping lives in one place instead of at every site.
 */
@Stable
class Haptics(private val feedback: HapticFeedback) {
    /** Start or stop completed, install completed. */
    fun confirm() = feedback.performHapticFeedback(HapticFeedbackType.Confirm)

    /** An operation failed. */
    fun reject() = feedback.performHapticFeedback(HapticFeedbackType.Reject)

    /** A switch or toggle button flipped to [on]. */
    fun toggle(on: Boolean) = feedback.performHapticFeedback(
        if (on) HapticFeedbackType.ToggleOn else HapticFeedbackType.ToggleOff
    )

    /** A slider stepped. */
    fun tick() = feedback.performHapticFeedback(HapticFeedbackType.SegmentTick)

    /** The manage menu opened by long press. */
    fun longPress() = feedback.performHapticFeedback(HapticFeedbackType.LongPress)

    /** Pull to refresh passed its threshold. */
    fun threshold() = feedback.performHapticFeedback(HapticFeedbackType.GestureThresholdActivate)
}

@Composable
fun rememberHaptics(): Haptics {
    val feedback = LocalHapticFeedback.current
    return remember(feedback) { Haptics(feedback) }
}
