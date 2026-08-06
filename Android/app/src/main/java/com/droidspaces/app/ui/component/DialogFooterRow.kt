package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/**
 * Shared "Cancel / Confirm" dialog footer: two equal-weight rounded [Surface]
 * buttons. Replaces the identical two-Surface footer that was copy-pasted across
 * the app's dialogs (DT-5). The confirm button dims and disables via
 * [confirmEnabled]; [cancelBorderAlpha] and [textFontWeight] keep the small
 * per-dialog cosmetic differences.
 */
@Composable
fun DialogFooterRow(
    dismissLabel: String,
    confirmLabel: String,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
    modifier: Modifier = Modifier.padding(horizontal = 4.dp),
    confirmEnabled: Boolean = true,
    cancelBorderAlpha: Float = 0.4f,
    textFontWeight: FontWeight = FontWeight.SemiBold,
    confirmColor: Color = MaterialTheme.colorScheme.primary,
    confirmContentColor: Color = MaterialTheme.colorScheme.onPrimary,
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        shape = RoundedCornerShape(20.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            // Dismiss/Cancel Button
            Surface(
                onClick = onDismiss,
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = cancelBorderAlpha))
            ) {
                Box(modifier = Modifier.padding(vertical = 12.dp), contentAlignment = Alignment.Center) {
                    Text(
                        text = dismissLabel,
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = textFontWeight
                    )
                }
            }

            // Confirm/OK Button
            Surface(
                onClick = onConfirm,
                enabled = confirmEnabled,
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(16.dp),
                color = if (confirmEnabled) confirmColor else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
            ) {
                Box(modifier = Modifier.padding(vertical = 12.dp), contentAlignment = Alignment.Center) {
                    Text(
                        text = confirmLabel,
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = textFontWeight,
                        color = if (confirmEnabled) confirmContentColor else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                    )
                }
            }
        }
    }
}
