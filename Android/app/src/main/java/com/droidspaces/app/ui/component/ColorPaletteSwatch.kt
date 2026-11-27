package com.droidspaces.app.ui.component

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.Spring
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.size
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.theme.ThemePalette

/**
 * A single circular color-palette swatch for the accent color picker.
 *
 * Visually splits the circle into 3 geometric sections:
 *  - Top half → primary color
 *  - Bottom-left quarter → secondary color
 *  - Bottom-right quarter → tertiary color
 *
 * Selected state shows:
 *  - Animated checkmark icon in center
 *  - Outer ring border matching primary
 */
@Composable
fun ColorPaletteSwatch(
    palette: ThemePalette,
    selected: Boolean,
    isDarkTheme: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    swatchSize: Float = 52f
) {
    val (primary, secondary, tertiary) = palette.swatch
        ?: if (isDarkTheme) listOf(palette.primaryDark, palette.secondaryDark, palette.tertiaryDark)
        else listOf(palette.primaryLight, palette.secondaryLight, palette.tertiaryLight)

    // Animate selection ring and checkmark with spring physics
    val selectionProgress by animateFloatAsState(
        targetValue = if (selected) 1f else 0f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "selectionAnimation"
    )

    val interactionSource = remember { MutableInteractionSource() }

    Canvas(
        modifier = modifier
            .size(swatchSize.dp)
            .semantics { contentDescription = palette.displayName }
            .clickable(
                interactionSource = interactionSource,
                indication = ripple(bounded = false, radius = (swatchSize / 2 + 4).dp),
                onClick = onClick
            )
    ) {
        val canvasSize = size.minDimension
        val center = Offset(size.width / 2f, size.height / 2f)
        val radius = canvasSize / 2f

        // Outer ring border (animated)
        val ringPadding = 4.dp.toPx()
        val outerRadius = radius + ringPadding
        if (selectionProgress > 0f) {
            drawCircle(
                color = primary.copy(alpha = selectionProgress * 0.8f),
                radius = outerRadius,
                center = center,
                style = Stroke(width = 3.dp.toPx() * selectionProgress)
            )
        }

        // Draw the 3 color sections using arcs
        val arcRect = Size(canvasSize, canvasSize)
        val arcTopLeft = Offset(
            (size.width - canvasSize) / 2f,
            (size.height - canvasSize) / 2f
        )

        // Top half: primary (180° from -180° to 0°)
        drawArc(
            color = primary,
            startAngle = -180f,
            sweepAngle = 180f,
            useCenter = true,
            topLeft = arcTopLeft,
            size = arcRect
        )

        // Bottom-left quarter: secondary (90° from 0° to 90°, which is left side in canvas coords)
        drawArc(
            color = secondary,
            startAngle = 0f,
            sweepAngle = 90f,
            useCenter = true,
            topLeft = arcTopLeft,
            size = arcRect
        )

        // Bottom-right quarter: tertiary (90° from 90° to 180°)
        drawArc(
            color = tertiary,
            startAngle = 90f,
            sweepAngle = 90f,
            useCenter = true,
            topLeft = arcTopLeft,
            size = arcRect
        )

        // Checkmark (animated)
        if (selectionProgress > 0.01f) {
            drawCheckmark(center, radius * 0.35f, selectionProgress)
        }
    }
}

/**
 * Draws a checkmark (✓) centered at [center] with the given [checkSize].
 * Alpha and scale are controlled by [progress] (0 → 1).
 */
private fun DrawScope.drawCheckmark(
    center: Offset,
    checkSize: Float,
    progress: Float
) {
    val strokeWidth = 2.5.dp.toPx()
    val alpha = progress.coerceIn(0f, 1f)
    val scale = 0.5f + 0.5f * progress

    val scaledSize = checkSize * scale

    // Checkmark points (relative to center)
    val startX = center.x - scaledSize * 0.5f
    val startY = center.y + scaledSize * 0.05f
    val midX = center.x - scaledSize * 0.1f
    val midY = center.y + scaledSize * 0.45f
    val endX = center.x + scaledSize * 0.6f
    val endY = center.y - scaledSize * 0.35f

    // Background circle for contrast
    drawCircle(
        color = Color.Black.copy(alpha = 0.35f * alpha),
        radius = checkSize * 1.1f * scale,
        center = center
    )

    // Checkmark stroke
    val checkColor = Color.White.copy(alpha = alpha)
    drawLine(
        color = checkColor,
        start = Offset(startX, startY),
        end = Offset(midX, midY),
        strokeWidth = strokeWidth,
        cap = StrokeCap.Round
    )
    drawLine(
        color = checkColor,
        start = Offset(midX, midY),
        end = Offset(endX, endY),
        strokeWidth = strokeWidth,
        cap = StrokeCap.Round
    )
}
