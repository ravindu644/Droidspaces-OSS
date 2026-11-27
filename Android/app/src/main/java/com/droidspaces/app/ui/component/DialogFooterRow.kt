package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/**
 * The Cancel and Confirm row every dialog ends with: two equal-weight buttons,
 * a quiet tonal dismiss and a filled confirm.
 *
 * Both buttons are weight(1f) at a fixed 48.dp, so they are always the same
 * width and the same height and neither grows when the other's label is long.
 * On a narrow window they stack instead, see [isCompactWidth].
 * Labels stay on one line and ellipsize: a button label that wraps is too long
 * to be a button label, shorten the string instead.
 *
 * Set [destructive] for a delete or a wipe. It fills the confirm with the error
 * colour and takes the label colour with it, which is why it is a flag rather
 * than a colour a caller passes in.
 */
@Composable
fun DialogFooterRow(
    dismissLabel: String,
    confirmLabel: String,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
    modifier: Modifier = Modifier,
    confirmEnabled: Boolean = true,
    destructive: Boolean = false,
) {
    val shape = RoundedCornerShape(16.dp)
    val confirmFill = when {
        !confirmEnabled -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
        destructive -> MaterialTheme.colorScheme.error
        else -> MaterialTheme.colorScheme.primary
    }
    val confirmLabelColor = when {
        !confirmEnabled -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
        destructive -> MaterialTheme.colorScheme.onError
        else -> MaterialTheme.colorScheme.onPrimary
    }

    val dismiss = @Composable { m: Modifier ->
        Surface(
            modifier = m
                .height(48.dp)
                .clip(shape)
                .clickable(onClick = onDismiss),
            shape = shape,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
            tonalElevation = 0.dp
        ) {
            FooterLabel(dismissLabel, MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
    val confirm = @Composable { m: Modifier ->
        Surface(
            modifier = m
                .height(48.dp)
                .clip(shape)
                .clickable(enabled = confirmEnabled, onClick = onConfirm),
            shape = shape,
            color = confirmFill,
            tonalElevation = 0.dp
        ) {
            FooterLabel(confirmLabel, confirmLabelColor)
        }
    }

    // Two buttons side by side leave about 60dp each on a 240px flip phone, too
    // little for "Cancel", so narrow windows stack them, confirm on top.
    if (isCompactWidth()) {
        Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            confirm(Modifier.fillMaxWidth())
            dismiss(Modifier.fillMaxWidth())
        }
    } else {
        Row(
            modifier = modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            dismiss(Modifier.weight(1f))
            confirm(Modifier.weight(1f))
        }
    }
}

/**
 * A dialog whose only action is "close" uses this: the dismiss button on its
 * own, full width. [DialogFooterRow] always draws a pair.
 */
@Composable
fun DialogDismissButton(
    label: String,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shape = RoundedCornerShape(16.dp)
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .height(48.dp)
            .clip(shape)
            .clickable(onClick = onDismiss),
        shape = shape,
        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
        tonalElevation = 0.dp
    ) {
        FooterLabel(label, MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun FooterLabel(text: String, color: androidx.compose.ui.graphics.Color) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.SemiBold,
            color = color,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}
