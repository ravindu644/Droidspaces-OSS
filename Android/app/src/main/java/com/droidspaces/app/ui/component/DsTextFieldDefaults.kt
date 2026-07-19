package com.droidspaces.app.ui.component

import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.TextFieldColors
import androidx.compose.runtime.Composable

/**
 * Shared OutlinedTextField color sets. These exact color blocks were previously
 * copy-pasted across a dozen call sites. Two visual variants exist in the app —
 * both are preserved verbatim so nothing changes on screen:
 *  - [colors]        container tint = surfaceContainerLow / surfaceVariant (forms, screens, dropdowns)
 *  - [surfaceColors] container tint = translucent surface (dialogs)
 */
object DsTextFieldDefaults {
    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun colors(): TextFieldColors = OutlinedTextFieldDefaults.colors(
        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLow,
        focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    )

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun surfaceColors(): TextFieldColors = OutlinedTextFieldDefaults.colors(
        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
        unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
        focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
    )
}
