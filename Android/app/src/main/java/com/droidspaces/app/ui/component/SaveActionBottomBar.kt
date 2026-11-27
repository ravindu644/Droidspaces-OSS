package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.size
import androidx.compose.ui.unit.dp
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Save
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.LoadingIndicator

/**
 * The save bar used by the edit and auto boot screens: one button that reads
 * "Save changes", then a spinner and "Saving", then a check mark and "Saved".
 * Both screens had grown their own copy of all three states.
 *
 * [canSave] gates the click. The saved and saving states are not clickable
 * either, so the caller only has to say which state it is in.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun SaveActionBottomBar(
    isSaved: Boolean,
    isSaving: Boolean,
    canSave: Boolean,
    onSave: () -> Unit,
    saveLabel: String,
    savingLabel: String,
    savedLabel: String,
    modifier: Modifier = Modifier,
) {
    val containerColor = when {
        isSaved -> MaterialTheme.colorScheme.primaryContainer
        isSaving || canSave -> MaterialTheme.colorScheme.primary
        else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
    }
    PrimaryActionBottomBar(
        onClick = onSave,
        containerColor = containerColor,
        modifier = modifier,
        enabled = canSave
    ) {
        when {
            isSaved -> {
                val fg = MaterialTheme.colorScheme.onPrimaryContainer
                Icon(Icons.Default.Check, contentDescription = null, modifier = Modifier.size(20.dp), tint = fg)
                Text(savedLabel, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = fg)
            }
            isSaving -> {
                val fg = MaterialTheme.colorScheme.onPrimary
                LoadingIndicator(modifier = Modifier.size(20.dp), color = fg)
                Text(savingLabel, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = fg)
            }
            else -> {
                val fg = if (canSave) {
                    MaterialTheme.colorScheme.onPrimary
                } else {
                    MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                }
                Icon(Icons.Default.Save, contentDescription = null, modifier = Modifier.size(20.dp), tint = fg)
                Text(saveLabel, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = fg)
            }
        }
    }
}
