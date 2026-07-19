package com.droidspaces.app.ui.component

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp

/**
 * Shared "primary call-to-action" bottom bar: a full-width [Surface] with a top
 * divider and a rounded primary action button (icon + label). Replaces the same
 * Surface -> Column -> HorizontalDivider -> clickable Surface -> Box -> Row block
 * that was copy-pasted across the wizard/detail screens (DT-4).
 *
 * The varying bits are parameters so each screen keeps its exact look:
 * [enabled] toggles the disabled colors, [secondaryAction] renders an extra row
 * (e.g. RootCheck's Skip), and [containerColor]/[disabledContainerColor] let a
 * screen keep the button a constant color regardless of [enabled].
 */
@Composable
fun PrimaryActionBottomBar(
    label: String,
    icon: ImageVector,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    containerColor: Color = MaterialTheme.colorScheme.primary,
    contentColor: Color = MaterialTheme.colorScheme.onPrimary,
    disabledContainerColor: Color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
    disabledContentColor: Color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f),
    barColor: Color = MaterialTheme.colorScheme.surfaceContainer,
    dividerAlpha: Float = 0.25f,
    horizontalPadding: Dp = 24.dp,
    labelFontSize: TextUnit = TextUnit.Unspecified,
    secondaryAction: (@Composable ColumnScope.() -> Unit)? = null,
) {
    val btnShape = RoundedCornerShape(20.dp)
    val bg = if (enabled) containerColor else disabledContainerColor
    val fg = if (enabled) contentColor else disabledContentColor
    Surface(
        modifier = modifier.fillMaxWidth(),
        color = barColor,
        tonalElevation = 0.dp
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            HorizontalDivider(
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = dividerAlpha),
                thickness = 1.dp
            )
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontalPadding)
                    .navigationBarsPadding(),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(btnShape)
                        .clickable(enabled = enabled, onClick = onClick),
                    shape = btnShape,
                    color = bg,
                    tonalElevation = 0.dp
                ) {
                    Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp), tint = fg)
                            Text(
                                text = label,
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                fontSize = labelFontSize,
                                color = fg
                            )
                        }
                    }
                }
                secondaryAction?.invoke(this)
            }
        }
    }
}
