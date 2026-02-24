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
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.Dp
import com.droidspaces.app.util.AnsiColorParser

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
    val transition = rememberInfiniteTransition(label = "shimmer")
    val translateAnim by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1000f,
        animationSpec = infiniteRepeatable(
            tween(durationMillis = 1200, easing = FastOutSlowInEasing),
            RepeatMode.Reverse
        ),
        label = "shimmerTranslate"
    )

    // Smoothly fade shimmer out instead of cutting abruptly.
    // An instant brush change on isProcessing=false was causing a recomposition
    // spike that interrupted the scroll animation at the worst possible moment.
    val shimmerAlpha by animateFloatAsState(
        targetValue = if (enabled) 1f else 0f,
        animationSpec = tween(durationMillis = 800, easing = LinearOutSlowInEasing),
        label = "shimmerAlpha"
    )

    val shades = ShimmerColorShades
    val brush = Brush.linearGradient(
        colors = listOf(
            shades[0].copy(alpha = shades[0].alpha * shimmerAlpha + shades[0].alpha * (1f - shimmerAlpha) * 0.5f),
            shades[1].copy(alpha = shades[1].alpha * shimmerAlpha + shades[0].alpha * (1f - shimmerAlpha) * 0.5f),
            shades[2].copy(alpha = shades[2].alpha * shimmerAlpha + shades[0].alpha * (1f - shimmerAlpha) * 0.5f),
        ),
        start = Offset(10f, 10f),
        end = Offset(translateAnim, translateAnim)
    )

    Surface(
        modifier = modifier.background(brush),
        border = null,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        content(ShimmerScope(brush))
    }
}

/**
 * Terminal console with ANSI color support, smooth rendering, and real-time log streaming.
 *
 * Scroll design notes:
 * - isProcessing is intentionally NOT a LaunchedEffect key. When it flipped false,
 *   the in-flight spring scroll was cancelled mid-animation, freezing the terminal.
 * - logs.size + maxValue are sufficient: the final status lines appended by
 *   ContainerOperationExecutor naturally trigger both keys, so no extra trigger needed.
 * - userScrolledUp is detected via snapshotFlow which is read-only — no scroll mutation
 *   conflicts with the write path in the scroll LaunchedEffect.
 */
@Composable
fun TerminalConsole(
    logs: List<Pair<Int, String>>,
    isProcessing: Boolean = true,
    modifier: Modifier = Modifier,
    maxHeight: Dp? = null
) {
    val verticalScrollState = rememberScrollState()
    val horizontalScrollState = rememberScrollState()

    var userScrolledUp by remember { mutableStateOf(false) }

    // Read-only observer — never mutates scroll state, so zero conflict with animations
    LaunchedEffect(verticalScrollState) {
        snapshotFlow { verticalScrollState.value }
            .collect { value ->
                userScrolledUp = value < verticalScrollState.maxValue - 200
            }
    }

    // isProcessing intentionally excluded — it was cancelling in-flight animations.
    // logs.size handles new lines; maxValue handles layout settling after measurement.
    LaunchedEffect(logs.size, verticalScrollState.maxValue) {
        if (!userScrolledUp) {
            verticalScrollState.animateScrollTo(
                value = verticalScrollState.maxValue,
                animationSpec = spring(
                    dampingRatio = Spring.DampingRatioNoBouncy,
                    stiffness = Spring.StiffnessLow
                )
            )
        }
    }

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
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(brush = brush, shape = RoundedCornerShape(16.dp))
                    .padding(horizontal = 10.dp, vertical = 16.dp)
                    .verticalScroll(verticalScrollState)
                    .horizontalScroll(horizontalScrollState)
            ) {
                Column(
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    logs.forEach { (level, message) ->
                        val annotatedText = remember(message) {
                            val processedMessage = message.replace(
                                Regex("""/data/local/Droidspaces/bin/droidspaces"""),
                                "droidspaces"
                            )

                            val displayMessage = if (processedMessage.isEmpty()) {
                                "\u00A0"
                            } else {
                                processedMessage.replace(Regex("""^( +)""")) { match: kotlin.text.MatchResult ->
                                    match.value.replace(" ", "\u00A0")
                                }
                            }

                            if (displayMessage.contains("\u001B[")) {
                                val defaultColor = when (level) {
                                    Log.ERROR -> errorColor
                                    Log.WARN -> warnColor
                                    else -> defaultTextColor
                                }
                                AnsiColorParser.parseAnsi(displayMessage, defaultColor)
                            } else {
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

                        Text(
                            text = annotatedText,
                            style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                            softWrap = false,
                            modifier = Modifier
                                .wrapContentWidth()
                                .heightIn(min = 16.dp)
                        )
                    }
                }
            }
        }
    }
}
