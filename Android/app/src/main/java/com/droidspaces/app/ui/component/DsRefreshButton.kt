package com.droidspaces.app.ui.component

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

/**
 * A refresh icon button that spins while [onRefresh] runs. Most refreshes finish in
 * a few milliseconds, so without a full turn the tap looks like it did nothing. It
 * always turns at least once and only stops at the top of a turn, never mid-spin.
 */
@Composable
fun DsRefreshButton(
    contentDescription: String,
    onRefresh: suspend () -> Unit,
    modifier: Modifier = Modifier,
    iconSize: Dp = 24.dp,
    tint: Color = LocalContentColor.current,
) {
    val scope = rememberCoroutineScope()
    val angle = remember { Animatable(0f) }
    var busy by remember { mutableStateOf(false) }
    IconButton(
        onClick = {
            busy = true
            scope.launch {
                var done = false
                val spin = launch {
                    do {
                        angle.animateTo(360f, tween(600, easing = LinearEasing))
                        angle.snapTo(0f)
                    } while (!done)
                }
                try {
                    onRefresh()
                } finally {
                    done = true
                    spin.join()
                    busy = false
                }
            }
        },
        enabled = !busy,
        modifier = modifier,
    ) {
        Icon(
            Icons.Default.Refresh,
            contentDescription = contentDescription,
            tint = tint,
            modifier = Modifier.size(iconSize).graphicsLayer { rotationZ = angle.value },
        )
    }
}
