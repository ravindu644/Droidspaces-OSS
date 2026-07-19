package com.droidspaces.app.ui.component

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight

/**
 * Non-toggle settings row: a clickable [SettingsCard] with an optional
 * [description] line and an accent [subtitle] line.
 */
@Composable
fun SettingsRowCard(
    title: String,
    subtitle: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null,
    painter: Painter? = null,
    description: String? = null
) {
    val alpha = if (enabled) 1f else 0.5f
    SettingsCard(
        title = title,
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        icon = icon,
        painter = painter,
        subtitleContent = {
            if (description != null) {
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f * alpha)
                )
            }
            if (subtitle.isNotBlank()) {
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.9f * alpha),
                    fontWeight = FontWeight.Bold
                )
            }
        }
    )
}
