package com.droidspaces.app.ui.component

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

/**
 * The label above a group of rows or cards: labelLarge SemiBold in primary, title
 * case. Four screens had grown their own version of this, so this is the one the
 * design language documents.
 *
 * Spacing stays at the call site through [modifier] until each screen converts to
 * the DESIGN.md rhythm (20 above, 8 below, 4 inset); baking it in now would double
 * the spacers those screens already carry.
 */
@Composable
fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = modifier
    )
}
