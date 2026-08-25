package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R

/**
 * The bordered 36.dp close square that lives in a dialog's header row, next to
 * the title. For dialogs that close from the top instead of a footer row, the
 * terminal log viewer and the About page. [enabled] dims it while a blocking
 * operation runs.
 */
@Composable
fun DialogCloseButton(onClick: () -> Unit, enabled: Boolean = true) {
    val shape = RoundedCornerShape(10.dp)
    Surface(
        modifier = Modifier
            .size(36.dp)
            .clip(shape)
            .clickable(
                enabled = enabled,
                onClick = onClick,
                indication = rememberRipple(bounded = true),
                interactionSource = remember { MutableInteractionSource() }
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (enabled) 0.08f else 0.04f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = if (enabled) 0.3f else 0.15f)),
        tonalElevation = 0.dp
    ) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Icon(
                imageVector = Icons.Default.Close,
                contentDescription = LocalContext.current.getString(R.string.close),
                modifier = Modifier.size(18.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = if (enabled) 1f else 0.38f)
            )
        }
    }
}
