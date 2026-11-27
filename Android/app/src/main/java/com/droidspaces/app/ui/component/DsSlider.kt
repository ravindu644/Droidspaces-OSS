package com.droidspaces.app.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp

/**
 * The app's slider: a bar thumb on a tall rounded track. The stock 1.2.x
 * Material slider still draws the old dot-on-thin-line style, so the thumb and
 * track are drawn here once instead of at every call site.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DsSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float>,
    modifier: Modifier = Modifier,
    onValueChangeFinished: (() -> Unit)? = null
) {
    Slider(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier,
        onValueChangeFinished = onValueChangeFinished,
        valueRange = valueRange,
        thumb = {
            Box(
                modifier = Modifier
                    .size(width = 5.dp, height = 28.dp)
                    .background(MaterialTheme.colorScheme.primary, CircleShape)
            )
        },
        track = { state ->
            val span = valueRange.endInclusive - valueRange.start
            val fraction = if (span > 0f) ((state.value - valueRange.start) / span).coerceIn(0f, 1f) else 0f
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(10.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surfaceContainerHighest)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth(fraction)
                        .fillMaxHeight()
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.primary)
                )
            }
        }
    )
}
