package com.droidspaces.app.ui.component

import android.util.Log
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.Dp
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.util.AnsiColorParser
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

private val ShimmerColorShades
    @Composable get() = listOf(
        androidx.compose.ui.graphics.Color.Transparent,
        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.15f),
        androidx.compose.ui.graphics.Color.Transparent
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
        modifier = modifier
            .clip(RoundedCornerShape(16.dp))
            // Base background first - Using surfaceContainerHighest for premium field-depth
            .background(MaterialTheme.colorScheme.surfaceContainerHighest)
            // Shimmer brush on top
            .background(brush)
            .border(
                width = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f),
                shape = RoundedCornerShape(16.dp)
            ),
        color = Color.Transparent,
        shape = RoundedCornerShape(16.dp),
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        content(ShimmerScope(brush))
    }
}

/**
 * Terminal console with ANSI color support, smooth rendering, and real-time log streaming.
 *
 * Scroll design invariants:
 * - isProcessing IS part of the snapshotFlow tuple. Reading it from closure instead causes
 *   a stale-capture race: the branch (spring vs instant) is decided on an old value, two
 *   scroll coroutines run concurrently, and the spring gets cancelled mid-animation (~500ms
 *   stall before the final log lines appear, reproducible ~1/50 on start/stop sequences).
 * - scrollJob?.cancel() before each launch guarantees at most one scroll coroutine is live.
 *   Rapid log bursts or an isProcessing flip cannot queue up concurrent animateScrollTo calls.
 * - LaunchedEffect(isProcessing) resets state only on start (isProcessing=true). Resetting
 *   on finish produced a spurious snapshotFlow re-emission that was the entry point of the race.
 * - userScrolledUp is detected via a separate read-only snapshotFlow - no scroll mutation
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
    var isAutoScrolling by remember { mutableStateOf(false) }

    // Read-only observer - never mutates scroll state, so zero conflict with animations
    LaunchedEffect(verticalScrollState) {
        snapshotFlow { verticalScrollState.value }
            .collect { value ->
                // Only track user scrolling if we aren't currently auto-scrolling.
                // This prevents the animation itself from triggering "userScrolledUp"
                // due to measurement delays or extreme log bursts.
                if (!isAutoScrolling) {
                    userScrolledUp = value < verticalScrollState.maxValue - 200
                }
            }
    }

    // Auto-scroll logic: ensures the terminal stays at the bottom during processing.
    //
    // Why isProcessing is inside the snapshotFlow tuple and not read from closure:
    //   snapshotFlow only re-emits when its observed state changes. If isProcessing
    //   were read from closure (as it was post-4a3f6a5), the collector would branch on
    //   a stale value - e.g. starting a spring animation when isProcessing was true,
    //   then having isProcessing flip false mid-spring, causing a second emission (via
    //   the LaunchedEffect(isProcessing) reset below) that fires scrollTo() concurrently
    //   with the still-running animateScrollTo(). The two coroutines fight over scroll
    //   state, the spring gets cancelled mid-animation, and the terminal stalls ~500ms
    //   before printing the final 2 lines. 1-in-50 because it's a narrow timing window.
    //
    // Why scrollJob?.cancel() before each launch:
    //   Guarantees at most one scroll coroutine is live at any moment. Without this,
    //   rapid log bursts or an isProcessing flip can queue up multiple animateScrollTo
    //   calls that run concurrently, producing the same stutter even with the correct
    //   isProcessing value.
    LaunchedEffect(Unit) {
        var scrollJob: Job? = null
        snapshotFlow {
            Pair(
                Triple(logs.size, verticalScrollState.maxValue, userScrolledUp),
                isProcessing
            )
        }.collect { (triple, processing) ->
            val (_, maxValue, scrolledUp) = triple
            if (!scrolledUp && verticalScrollState.value < maxValue) {
                scrollJob?.cancel()
                scrollJob = launch {
                    isAutoScrolling = true
                    try {
                        if (processing) {
                            verticalScrollState.animateScrollTo(
                                value = maxValue,
                                animationSpec = spring(
                                    dampingRatio = Spring.DampingRatioNoBouncy,
                                    stiffness = Spring.StiffnessLow
                                )
                            )
                        } else {
                            // Instant snap after processing ends: layout is stable by now
                            // and no spring is in flight (scrollJob?.cancel() above killed it).
                            verticalScrollState.scrollTo(maxValue)
                        }
                    } finally {
                        isAutoScrolling = false
                    }
                }
            }
        }
    }

    // Reset scroll state only when a NEW operation starts, never on finish.
    // Resetting on finish (isProcessing = false) was the trigger that produced
    // the spurious snapshotFlow re-emission that started the race in 4a3f6a5.
    LaunchedEffect(isProcessing) {
        if (isProcessing) {
            userScrolledUp = false
            isAutoScrolling = false
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
            MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 16.dp)
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
                            style = MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono),
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
