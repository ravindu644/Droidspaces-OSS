package com.droidspaces.app.ui.util

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Standardized loading indicator sizes.
 */
enum class LoadingSize(val size: Dp) {
    Small(16.dp),
    Medium(24.dp),
    Large(48.dp),
    /** Inside the 240dp setup hero. The indicator only fills about 80% of its box, so
     *  this is what it takes to carry the same weight as the 96dp glyph it stands in for. */
    Hero(144.dp)
}

/** The M3 Expressive loading indicator at one of the app's standard sizes. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun LoadingIndicator(
    size: LoadingSize,
    modifier: Modifier = Modifier,
    color: Color? = null
) {
    androidx.compose.material3.LoadingIndicator(
        modifier = modifier.size(size.size),
        color = color ?: MaterialTheme.colorScheme.primary
    )
}

/**
 * Full-screen loading indicator with optional message.
 */
@Composable
fun FullScreenLoading(
    message: String? = null,
    modifier: Modifier = Modifier,
    size: LoadingSize = LoadingSize.Large,
) {
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            LoadingIndicator(size = size)
            message?.let {
                Text(
                    text = it,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    // Fills the column so the text's centre line is the spinner's
                    // centre line; without it the column shrinks to the text and a
                    // wrapped message drifts out of line with the spinner above it.
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}
