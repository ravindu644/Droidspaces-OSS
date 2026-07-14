package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.ThemePalette

/**
 * A horizontally scrollable row of [ColorPaletteSwatch] items,
 * mimicking Android's "Wallpaper & style" accent color picker.
 *
 * Shows a section header with a palette icon, followed by the swatch row.
 */
@Composable
fun AccentColorPicker(
    selectedPalette: ThemePalette,
    isDarkTheme: Boolean,
    onPaletteSelected: (ThemePalette) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.fillMaxWidth()) {
        // Section header
        ListItem(
            leadingContent = {
                Icon(
                    imageVector = Icons.Default.Palette,
                    contentDescription = null
                )
            },
            headlineContent = {
                Text(
                    text = stringResource(R.string.accent_color),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            },
            supportingContent = {
                Text(
                    text = selectedPalette.displayName,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        )

        // Horizontally scrollable palette swatches
        LazyRow(
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 8.dp),
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            items(
                items = ThemePalette.entries.toList(),
                key = { it.name }
            ) { palette ->
                ColorPaletteSwatch(
                    palette = palette,
                    selected = palette == selectedPalette,
                    isDarkTheme = isDarkTheme,
                    onClick = { onPaletteSelected(palette) }
                )
            }
        }
    }
}
