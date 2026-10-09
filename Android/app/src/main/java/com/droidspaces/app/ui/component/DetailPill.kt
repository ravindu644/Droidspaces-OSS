package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The detail pill from DESIGN.md: Open, Manage and Copy login on container details,
 * and Copy on the requirements screen. It trails its row. The sites had already
 * converged on these numbers by hand, which is why the pill is a plain filled Button
 * at its default 40.dp height rather than one of the Expressive button sizes.
 *
 * Pass 18.dp as [iconSize] for the copy glyph; chevrons and the rest are 16.
 */
@Composable
fun DetailPill(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector = Icons.Default.ChevronRight,
    iconSize: Dp = 16.dp,
) {
    Button(
        onClick = onClick,
        modifier = modifier.widthIn(min = 140.dp),
        shape = MaterialTheme.shapes.large
    ) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(iconSize))
        Spacer(Modifier.width(6.dp))
        Text(text, style = MaterialTheme.typography.labelLarge)
    }
}
