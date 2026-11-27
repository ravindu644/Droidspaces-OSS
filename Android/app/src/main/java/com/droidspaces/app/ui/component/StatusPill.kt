package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Small "status pill": a colored dot + bold label in a tinted rounded chip.
 * Previously copy-pasted verbatim in ContainerCard, DroidspacesStatusCard and
 * RootfsRepoSheet. The caller supplies the already-formatted [label] (some sites
 * uppercase it, some don't) and the accent [color].
 *
 * [busy] spins the dot in place while an operation runs. It deliberately occupies
 * the same 6.dp the dot does, so the pill is exactly as wide either way and
 * nothing to the left of it shifts when the state changes.
 */
@Composable
fun StatusPill(
    label: String,
    color: Color,
    modifier: Modifier = Modifier,
    busy: Boolean = false,
) {
    Surface(
        modifier = modifier,
        color = color.copy(alpha = 0.1f),
        shape = RoundedCornerShape(8.dp),
        border = BorderStroke(1.dp, color.copy(alpha = 0.2f))
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
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
                Surface(modifier = Modifier.size(6.dp), shape = CircleShape, color = color) {}
            }
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Black,
                letterSpacing = 0.5.sp,
                color = color
            )
        }
    }
}
