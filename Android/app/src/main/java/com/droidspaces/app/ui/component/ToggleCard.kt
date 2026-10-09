package com.droidspaces.app.ui.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.unit.dp
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.ImageVector

/**
 * Toggle option row: a [SettingsCard] with a trailing [Switch]. Tapping the card
 * toggles the switch. [expandedContent], when given, opens under a divider
 * inside the same card while the switch is on.
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
    painter: Painter? = null,
    expandedContent: (@Composable ColumnScope.() -> Unit)? = null
) {
    val alpha = if (enabled) 1f else 0.38f
    SettingsCard(
        title = title,
        onClick = { if (enabled) onCheckedChange(!checked) },
        modifier = modifier,
        enabled = enabled,
        icon = icon,
        painter = painter,
        below = expandedContent?.let { content ->
            {
                AnimatedVisibility(
                    // A disabled card must not leave live controls on screen
                    visible = checked && enabled,
                    enter = expandVertically(MaterialTheme.motionScheme.slowSpatialSpec()) + fadeIn(MaterialTheme.motionScheme.slowEffectsSpec()),
                    exit = shrinkVertically(MaterialTheme.motionScheme.slowSpatialSpec()) + fadeOut(MaterialTheme.motionScheme.slowEffectsSpec())
                ) {
                    Column(
                        modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                        content()
                    }
                }
            }
        },
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
