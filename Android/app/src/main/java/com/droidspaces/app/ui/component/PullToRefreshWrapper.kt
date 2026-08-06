package com.droidspaces.app.ui.component

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.layout.*
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.pulltorefresh.PullToRefreshContainer
import androidx.compose.material3.pulltorefresh.rememberPullToRefreshState
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.util.LoadingIndicator
import kotlinx.coroutines.delay

/**
 * Optimized Pull-to-Refresh Wrapper with smooth animations.
 *
 * Key optimizations:
 * - Minimum spin time ensures refresh indicator is visible (better UX)
 * - Hardware-accelerated indicator with graphicsLayer
 * - Material You theming integration
 * - No redundant state management (removed unused triggerRefresh)
 * - Smooth spring animation on release — indicator slides to resting
 *   position instead of teleporting (fixes the jump-on-release bug)
 *
 * Performance characteristics:
 * - 0 allocations in hot path
 * - Hardware-accelerated rendering
 * - Stable nested scroll connection
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PullToRefreshWrapper(
    onRefresh: suspend () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    val pullToRefreshState = rememberPullToRefreshState()

    // Handle pull-to-refresh with minimum visible spin time
    LaunchedEffect(pullToRefreshState.isRefreshing) {
        if (pullToRefreshState.isRefreshing) {
            val startTime = System.currentTimeMillis()

            // Execute the refresh callback
            onRefresh()

            // Ensure minimum spin time for better UX (indicator actually visible)
            val elapsed = System.currentTimeMillis() - startTime
            val minSpinTime = 600L // Reduced from 800ms for snappier feel
            if (elapsed < minSpinTime) {
                delay(minSpinTime - elapsed)
            }

            pullToRefreshState.endRefresh()
        }
    }

    // Smooth spring animation for the indicator's vertical position.
    //
    // M3's PullToRefreshContainer internally uses an Animatable for verticalOffset,
    // but in some BOM versions the Animatable snaps (instead of animating) when
    // isRefreshing flips to true — causing the visible "jump". We work around this
    // by reading state.verticalOffset and re-applying it through animateFloatAsState
    // with a spring so the transition from any pull distance to the resting position
    // is always a smooth slide.
    val animatedOffset by animateFloatAsState(
        targetValue = pullToRefreshState.verticalOffset,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "pullToRefreshOffset"
    )

    Box(
        modifier = modifier
            .fillMaxSize()
            .nestedScroll(pullToRefreshState.nestedScrollConnection)
    ) {
        content()

        // Hardware-accelerated refresh indicator with smooth release animation.
        // We override the vertical position with our spring-animated offset so that
        // releasing the pull always produces a smooth slide rather than a teleport.
        PullToRefreshContainer(
            state = pullToRefreshState,
            modifier = Modifier
                .align(Alignment.TopCenter)
                .graphicsLayer {
                    // Replace the container's own offset with our smooth animated value.
                    // The container positions itself at y=0 (top), so we shift it down
                    // by the animated offset to match where the finger dragged to, then
                    // let the spring bring it back to the resting position smoothly.
                    translationY = animatedOffset - pullToRefreshState.verticalOffset
                    shadowElevation = 0f
                },
            containerColor = MaterialTheme.colorScheme.primaryContainer,
            contentColor = MaterialTheme.colorScheme.primary,
            indicator = { state ->
                val progress = state.progress
                val isRefreshing = state.isRefreshing

                Box(
                    modifier = Modifier.size(40.dp),
                    contentAlignment = Alignment.Center
                ) {
                    if (isRefreshing) {
                        LoadingIndicator(
                            modifier = Modifier.size(24.dp),
                            color = MaterialTheme.colorScheme.primary
                        )
                    } else {
                        LoadingIndicator(
                            progress = { progress },
                            modifier = Modifier
                                .size(24.dp)
                                .drawWithContent {
                                    if (progress > 1f) {
                                        // Rotate the entire shape-morphing path as the pull continues past 1.0
                                        rotate(-(progress - 1) * 180) {
                                            this@drawWithContent.drawContent()
                                        }
                                    } else {
                                        drawContent()
                                    }
                                },
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }
        )
    }
}
