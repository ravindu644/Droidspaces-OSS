package com.droidspaces.app.ui.component

import android.util.Log
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.Dp
import com.droidspaces.app.util.AnsiColorParser

// LSPatch-style shimmer animation - optimized for GPU rendering
private val ShimmerColorShades
    @Composable get() = listOf(
        MaterialTheme.colorScheme.secondaryContainer.copy(0.9f),
        MaterialTheme.colorScheme.secondaryContainer.copy(0.2f),
        MaterialTheme.colorScheme.secondaryContainer.copy(0.9f)
    )

class ShimmerScope(val brush: Brush)

@Composable
fun ShimmerAnimation(
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    content: @Composable ShimmerScope.() -> Unit
) {
    val transition = rememberInfiniteTransition()
    val translateAnim by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1000f,
        animationSpec = infiniteRepeatable(
            tween(durationMillis = 1200, easing = FastOutSlowInEasing),
            RepeatMode.Reverse
        )
    )

    val brush = Brush.linearGradient(
        colors = if (enabled) ShimmerColorShades else List(3) { ShimmerColorShades[0] },
        start = Offset(10f, 10f),
        end = Offset(translateAnim, translateAnim)
    )

    // Use Surface wrapper like LSPatch for better graphics acceleration
    Surface(
        modifier = modifier.background(brush),
        border = null,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        content(ShimmerScope(brush))
    }
}

// Extension function to animate scroll to bottom
suspend fun androidx.compose.foundation.ScrollState.animateScrollToBottom() {
    animateScrollTo(maxValue)
}

/**
 * Ultra-optimized terminal console with ANSI color support, smooth rendering,
 * and real-time log streaming. No word wrap with full horizontal scrolling.
 *
 * Features:
 * - ANSI color code parsing and rendering
 * - Smart auto-scroll (only when user is at bottom)
 * - Invisible scrollbars for both vertical and horizontal scrolling
 * - No word wrapping - lines extend horizontally as needed
 * - Better spacing between log lines
 * - Rounded corners without black border
 * - 90% opacity logs with monospace font
 */
@Composable
fun TerminalConsole(
    logs: List<Pair<Int, String>>,
    isProcessing: Boolean = true,
    modifier: Modifier = Modifier,
    maxHeight: Dp? = null
) {
    // Invisible scrollbars for both directions
    val verticalScrollState = rememberScrollState()
    val horizontalScrollState = rememberScrollState()

    // Track if user was at bottom - only auto-scroll if they were following logs
    var wasAtBottom by remember { mutableStateOf(true) }
    var lastLogCount by remember { mutableStateOf(logs.size) }

    // Auto-scroll when new logs arrive (if user was at bottom)
    LaunchedEffect(logs.size) {
        if (logs.size > lastLogCount && wasAtBottom) {
            // Scroll to bottom when new logs arrive
            verticalScrollState.animateScrollTo(verticalScrollState.maxValue)
        }
        lastLogCount = logs.size
    }

    // Track scroll position to determine if user is at bottom
    LaunchedEffect(verticalScrollState.value) {
        wasAtBottom = verticalScrollState.value >= verticalScrollState.maxValue - 50 // Small threshold
    }

    // Pre-compute colors for performance
    val defaultTextColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.9f)
    val errorColor = MaterialTheme.colorScheme.error.copy(alpha = 0.9f)
    val warnColor = MaterialTheme.colorScheme.tertiary.copy(alpha = 0.9f)

    ShimmerAnimation(
        modifier = if (maxHeight != null) {
            modifier.heightIn(max = maxHeight)
        } else {
            modifier
        },
        enabled = isProcessing
    ) {
        androidx.compose.material3.ProvideTextStyle(
            MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace)
        ) {
            // Box with both vertical and horizontal scrolling (invisible scrollbars)
            Box(
                modifier = Modifier
                    .fillMaxWidth() // Full width for maximum console space
                    .background(brush = brush, shape = RoundedCornerShape(16.dp)) // Rounded corners without black border
                    .padding(horizontal = 10.dp, vertical = 16.dp) // Minimal horizontal padding for high DPI screens
                    .verticalScroll(verticalScrollState) // Invisible vertical scrollbar
                    .horizontalScroll(horizontalScrollState) // Invisible horizontal scrollbar
            ) {
                // Column for vertical arrangement of log lines
                Column(
                verticalArrangement = Arrangement.spacedBy(4.dp) // Better spacing between logs
            ) {
                    logs.forEach { (level, message) ->
                    // Process message: strip full path, parse ANSI
                    val annotatedText = remember(message) {
                        // Strip full path from droidspaces binary
                        val processedMessage = message.replace(
                            Regex("""/data/local/Droidspaces/bin/droidspaces"""),
                            "droidspaces"
                        )

                        // Handle empty lines - use non-breaking space to ensure they're visible
                        val displayMessage = if (processedMessage.isEmpty()) {
                            "\u00A0" // Non-breaking space to make empty lines visible
                        } else {
                            // Preserve leading spaces by replacing them with non-breaking spaces
                            // This ensures indentation from backend (like "  Rootfs OS:") is preserved
                            processedMessage.replace(Regex("""^( +)""")) { match: kotlin.text.MatchResult ->
                                match.value.replace(" ", "\u00A0")
                            }
                        }

                        if (displayMessage.contains("\u001B[")) {
                            // Has ANSI codes - parse them
                            val defaultColor = when (level) {
                                Log.ERROR -> errorColor
                                Log.WARN -> warnColor
                                else -> defaultTextColor
                            }
                            AnsiColorParser.parseAnsi(displayMessage, defaultColor)
                        } else {
                            // No ANSI codes - simple text with color
                            androidx.compose.ui.text.AnnotatedString(
                                text = displayMessage,
                                spanStyle = androidx.compose.ui.text.SpanStyle(
                        color = when (level) {
                                        Log.ERROR -> errorColor
                                        Log.WARN -> warnColor
                                        else -> defaultTextColor
                                    }
                                )
                            )
                        }
                    }

                        // No word wrap - text can extend horizontally with horizontal scrolling
                    Text(
                        text = annotatedText,
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                            softWrap = false, // Word wrap disabled - allows horizontal scrolling
                            modifier = Modifier
                                .wrapContentWidth() // Allow text to be as wide as needed
                                .heightIn(min = 16.dp) // Ensure minimum height for empty lines (matches line spacing)
                        )
        }
                }
            }
        }
    }
}
