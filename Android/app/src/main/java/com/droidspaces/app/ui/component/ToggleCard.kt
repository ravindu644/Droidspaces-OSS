package com.droidspaces.app.ui.component

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.ImageVector

/**
 * Toggle option row: a [SettingsCard] with a trailing [Switch]. Tapping the card
 * toggles the switch.
 */
@Composable
fun ToggleCard(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null,
    painter: Painter? = null
) {
    val alpha = if (enabled) 1f else 0.5f
    SettingsCard(
        title = title,
        onClick = { if (enabled) onCheckedChange(!checked) },
        modifier = modifier,
        enabled = enabled,
        icon = icon,
        painter = painter,
        subtitleContent = {
            Text(
                text = description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f * alpha)
            )
        },
        trailing = {
            Switch(
                checked = checked,
                onCheckedChange = null,
                enabled = enabled,
                colors = SwitchDefaults.colors(
                    checkedThumbColor = MaterialTheme.colorScheme.onPrimary,
                    checkedTrackColor = MaterialTheme.colorScheme.primary,
                    uncheckedThumbColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.8f),
                    uncheckedTrackColor = MaterialTheme.colorScheme.surfaceContainerHigh,
                    uncheckedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f),
                )
            )
        }
    )
}
