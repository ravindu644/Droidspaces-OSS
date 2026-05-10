package com.droidspaces.app.ui.component

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Circular progress indicator showing percentage - similar to Server Box's PercentCircle.
 * Premium animation with smooth transitions.
 */
@Composable
fun PercentCircle(
    percent: Double,
    modifier: Modifier = Modifier,
    size: Dp = 57.dp,
    strokeWidth: Dp = 4.dp,
    textStyle: TextStyle = TextStyle(
        fontSize = 12.7.sp,
        fontWeight = FontWeight.Medium
    ),
    progressColor: Color = MaterialTheme.colorScheme.primary,
    backgroundColor: Color = MaterialTheme.colorScheme.surfaceContainerLow
) {
    // Clamp percent between 0.01 and 99.9 for visual consistency
    val clampedPercent = when {
        percent <= 0.01 -> 0.01
        percent >= 99.9 -> 99.9
        else -> percent
    }

    // Animate the progress smoothly
    val animatedProgress by animateFloatAsState(
        targetValue = clampedPercent.toFloat(),
        animationSpec = tween(durationMillis = 777),
        label = "percentCircle"
    )

    Box(
        modifier = modifier.size(size),
        contentAlignment = Alignment.Center
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val strokeWidthPx = strokeWidth.toPx()
            val radius = (size.toPx() - strokeWidthPx) / 2f

            // Draw background circle
            drawCircle(
                color = backgroundColor,
                radius = radius,
                style = Stroke(width = strokeWidthPx, cap = StrokeCap.Round)
            )

            // Draw progress arc
            val sweepAngle = (animatedProgress / 100f) * 360f
            drawArc(
                color = progressColor,
                startAngle = -90f, // Start from top
                sweepAngle = sweepAngle,
                useCenter = false,
                style = Stroke(width = strokeWidthPx, cap = StrokeCap.Round)
            )
        }

        // Percentage text
        androidx.compose.material3.Text(
            text = "${clampedPercent.toInt()}%",
            style = textStyle,
            color = MaterialTheme.colorScheme.onSurface
        )
    }
}

