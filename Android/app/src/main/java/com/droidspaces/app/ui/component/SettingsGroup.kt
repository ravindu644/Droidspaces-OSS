package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.theme.isDark

/**
 * One section's rows on one surface, the way the settings screen draws them:
 * rows sit flat inside and [GroupDivider] separates them. Forms put a
 * [SectionHeader] above each group instead of giving every row its own card.
 */
@Composable
fun SettingsGroup(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = modifier.fillMaxWidth().padding(horizontal = screenGutter()),
        shape = RoundedCornerShape(24.dp),
        color = if (MaterialTheme.colorScheme.isDark) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
        tonalElevation = 0.dp
    ) {
        Column(content = content)
    }
}

@Composable
fun GroupDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(horizontal = 16.dp),
        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f)
    )
}

/**
 * The [SectionHeader] that sits above a [SettingsGroup], inset to line up with
 * the text inside the group rather than with its edge. [sub] is for the
 * sections inside a page, which keep the bodyLarge Bold title the form gave
 * them before it was grouped.
 */
@Composable
fun GroupHeader(text: String, modifier: Modifier = Modifier, trailing: String? = null, sub: Boolean = false) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(start = screenGutter() + 16.dp, end = screenGutter() + 16.dp, top = 24.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (sub) {
            Text(text, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
        } else {
            SectionHeader(text = text, modifier = Modifier.weight(1f))
        }
        if (trailing != null) {
            Text(
                trailing,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
        }
    }
}

/** Helper text under a [GroupHeader], above its group. */
@Composable
fun GroupIntro(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
        modifier = modifier.padding(start = screenGutter() + 16.dp, end = screenGutter() + 16.dp, bottom = 8.dp)
    )
}

/**
 * A row inside a [SettingsGroup] that opens a page. [value] is the setting's
 * current state in the accent colour, [summary] a quieter line under it.
 * [error] replaces the summary with the reason the page's settings cannot be
 * saved yet. A row that opens a dialog rather than a page passes
 * [chevron] = false, so the mark keeps meaning "a page follows".
 */
@Composable
fun NavRow(
    title: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    value: String? = null,
    summary: String? = null,
    valueFontFamily: FontFamily? = null,
    enabled: Boolean = true,
    error: String? = null,
    chevron: Boolean = true,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            // SwitchItem's metrics, so a group mixing the two keeps one rhythm.
            .heightIn(min = if (value != null || summary != null || error != null) 72.dp else 56.dp)
            .padding(horizontal = 16.dp, vertical = 12.dp)
            .alpha(if (enabled) 1f else 0.38f),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (icon != null) {
            // LocalContentColor, as SwitchItem's icon, so a group mixing the two keeps one shade.
            Icon(icon, contentDescription = null)
            Spacer(Modifier.width(16.dp))
        }
        Column(Modifier.weight(1f)) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
            if (value != null) {
                Text(
                    value,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                    fontFamily = valueFontFamily,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
            }
            if (error != null || summary != null) {
                Text(
                    error ?: summary!!,
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (error != null) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        if (chevron) {
            Spacer(Modifier.width(8.dp))
            Icon(
                Icons.Default.ChevronRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
            )
        }
    }
}

/**
 * Side margin for screen content. Droidspaces runs on flip phones as narrow as
 * 240px, where 16dp a side plus a group's own padding leaves too little room
 * for a field label, so narrow windows get half.
 */
@Composable
fun screenGutter(): Dp = if (isCompactWidth()) 8.dp else 16.dp

/** True below the width where two fields or two buttons still fit side by side. */
@Composable
fun isCompactWidth(): Boolean = LocalConfiguration.current.screenWidthDp < 320

/**
 * True on a window too short for full-size bars, a 320px-tall flip phone or a
 * phone in landscape with the keyboard up, where a 64dp top bar and a 100dp
 * action bar would leave the content a sliver.
 */
@Composable
fun isShortHeight(): Boolean = LocalConfiguration.current.screenHeightDp < 480
