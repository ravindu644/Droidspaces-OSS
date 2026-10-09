package com.droidspaces.app.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.contentColorFor
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/**
 * The status chip from DESIGN.md: a dot and a label, a label and never a button.
 *
 * [active] is the running or healthy state and fills the chip with primaryContainer;
 * everything else sits quietly on surfaceContainerHighest. A chip on a surface that is
 * already primaryContainer (the Home status card) passes [containerColor] instead, or
 * it would vanish into the card. [color] tints only the dot,
 * so a caller with its own hue (the init system traffic light) keeps it without the
 * chip turning into a colour swatch.
 *
 * [busy] spins the dot in place while an operation runs. The ring occupies the same
 * 6.dp the dot does, so the chip keeps its width and nothing beside it shifts.
 */
@Composable
fun StatusChip(
    label: String,
    color: Color,
    modifier: Modifier = Modifier,
    active: Boolean = false,
    busy: Boolean = false,
    containerColor: Color = if (active) MaterialTheme.colorScheme.primaryContainer
        else MaterialTheme.colorScheme.surfaceContainerHighest,
) {
    val quiet = containerColor == MaterialTheme.colorScheme.surfaceContainerHighest
    Surface(
        modifier = modifier.height(32.dp),
        color = containerColor,
        // contentColorFor would give onSurface; a quiet chip reads one step down
        contentColor = if (quiet) MaterialTheme.colorScheme.onSurfaceVariant else contentColorFor(containerColor),
        shape = MaterialTheme.shapes.small
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            modifier = Modifier.padding(horizontal = 16.dp)
        ) {
            if (busy) {
                // Not the shared LoadingIndicator: its smallest size is 16.dp and the
                // morphing shape is unreadable this small. A plain ring still reads as
                // motion at dot size.
                CircularProgressIndicator(
                    modifier = Modifier.size(6.dp),
                    color = color,
                    strokeWidth = 1.5.dp
                )
            } else {
                Box(Modifier.size(6.dp).background(color, CircleShape))
            }
            Text(text = label, style = MaterialTheme.typography.labelMedium)
        }
    }
}
