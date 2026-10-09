package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.shape.CornerBasedShape
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Shape

/**
 * Rows that belong together, drawn as one object with visible joints: a 2.dp gap of
 * background between rows, extraLarge (24) on the group's outer corners and
 * extraSmall (6) on the inner ones. Each [row] gets its shape and paints its own
 * surfaceContainer fill, usually a ListItem or SegmentedListItem.
 *
 * Not ListItemDefaults.segmentedShapes: that takes the outer corners from the theme's
 * large slot (16), and DESIGN.md puts groups on the same 24 as cards.
 *
 * On AMOLED the gap and the rows are the same black and the joints vanish, so every
 * row after the first draws a 1px outlineVariant line along its top. The check is the
 * colour itself, so no AMOLED flag has to be threaded down here.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun SegmentedGroup(
    count: Int,
    modifier: Modifier = Modifier,
    row: @Composable (index: Int, shape: Shape) -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val outer = MaterialTheme.shapes.extraLarge
    val inner = MaterialTheme.shapes.extraSmall
    val joinless = scheme.surfaceContainer == scheme.background
    Column(modifier, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
        for (i in 0 until count) {
            val separator = joinless && i > 0
            Box(
                Modifier.drawWithContent {
                    drawContent()
                    if (separator) drawLine(scheme.outlineVariant, Offset.Zero, Offset(size.width, 0f), 1f)
                }
            ) {
                row(i, segmentShape(i, count, outer, inner))
            }
        }
    }
}

private fun segmentShape(index: Int, count: Int, outer: CornerBasedShape, inner: CornerBasedShape): Shape {
    val top = if (index == 0) outer else inner
    val bottom = if (index == count - 1) outer else inner
    return inner.copy(
        topStart = top.topStart,
        topEnd = top.topEnd,
        bottomStart = bottom.bottomStart,
        bottomEnd = bottom.bottomEnd
    )
}
