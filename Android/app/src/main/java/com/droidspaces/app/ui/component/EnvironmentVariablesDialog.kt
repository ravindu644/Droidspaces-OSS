package com.droidspaces.app.ui.component

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.R

/**
 * Draws a vertical scrollbar indicator on the right edge.
 */
private fun Modifier.verticalScrollbar(
    scrollState: ScrollState,
    color: Color,
    thickness: Dp = 4.dp
): Modifier = drawWithContent {
    drawContent()
    val viewportHeight = size.height
    val totalContentHeight = scrollState.maxValue + viewportHeight
    if (totalContentHeight <= viewportHeight || scrollState.maxValue == 0) return@drawWithContent

    val thumbHeight = (viewportHeight / totalContentHeight * viewportHeight).coerceAtLeast(24.dp.toPx())
    val scrollRange = viewportHeight - thumbHeight
    val thumbOffset = scrollState.value.toFloat() / scrollState.maxValue * scrollRange
    val barWidth = thickness.toPx()

    drawRoundRect(
        color = color,
        topLeft = Offset(size.width - barWidth - 2.dp.toPx(), thumbOffset),
        size = Size(barWidth, thumbHeight),
        cornerRadius = CornerRadius(barWidth / 2f)
    )
}

/**
 * Draws a horizontal scrollbar indicator on the bottom edge.
 */
private fun Modifier.horizontalScrollbar(
    scrollState: ScrollState,
    color: Color,
    thickness: Dp = 4.dp
): Modifier = drawWithContent {
    drawContent()
    val viewportWidth = size.width
    val totalContentWidth = scrollState.maxValue + viewportWidth
    if (totalContentWidth <= viewportWidth || scrollState.maxValue == 0) return@drawWithContent

    val thumbWidth = (viewportWidth / totalContentWidth * viewportWidth).coerceAtLeast(24.dp.toPx())
    val scrollRange = viewportWidth - thumbWidth
    val thumbOffset = scrollState.value.toFloat() / scrollState.maxValue * scrollRange
    val barHeight = thickness.toPx()

    drawRoundRect(
        color = color,
        topLeft = Offset(thumbOffset, size.height - barHeight - 2.dp.toPx()),
        size = Size(thumbWidth, barHeight),
        cornerRadius = CornerRadius(barHeight / 2f)
    )
}

/**
 * A responsive, full-screen-style dialog for editing environment variables.
 *
 * Features:
 * - Fills ~92% width and ~75% height, adapting to phones, tablets, and TVs
 * - Bidirectional scrolling with visible scrollbar indicators
 * - No text wrapping — each KEY=VALUE stays on one line
 * - Fixed dialog height — never grows past its container on Enter press
 * - Monospace font with placeholder hint when empty
 */
@Composable
fun EnvironmentVariablesDialog(
    initialContent: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
    confirmLabel: String? = null
) {
    val context = LocalContext.current
    var content by remember { mutableStateOf(initialContent) }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .fillMaxHeight(0.75f),
            shape = RoundedCornerShape(28.dp),
            tonalElevation = 6.dp,
            color = MaterialTheme.colorScheme.surface
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp)
            ) {
                // Title
                Text(
                    text = context.getString(R.string.environment_variables),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface
                )

                Spacer(modifier = Modifier.height(16.dp))

                // Editor area — fixed height via weight, bidirectional scroll with visible scrollbars
                val verticalScrollState = rememberScrollState()
                val horizontalScrollState = rememberScrollState()
                val textColor = MaterialTheme.colorScheme.onSurface
                val hintColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                val editorBgColor = MaterialTheme.colorScheme.surfaceContainerLow
                val scrollbarColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.45f)

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth()
                        .background(
                            color = editorBgColor,
                            shape = RoundedCornerShape(12.dp)
                        )
                        .verticalScrollbar(verticalScrollState, scrollbarColor)
                        .horizontalScrollbar(horizontalScrollState, scrollbarColor)
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(16.dp)
                            .verticalScroll(verticalScrollState)
                            .horizontalScroll(horizontalScrollState)
                    ) {
                        BasicTextField(
                            value = content,
                            onValueChange = { content = it },
                            modifier = Modifier
                                .fillMaxWidth()
                                .defaultMinSize(minHeight = 200.dp, minWidth = 600.dp),
                            textStyle = TextStyle(
                                fontFamily = FontFamily.Monospace,
                                fontSize = 14.sp,
                                color = textColor,
                                lineHeight = 22.sp
                            ),
                            cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                            maxLines = Int.MAX_VALUE,
                            decorationBox = { innerTextField ->
                                Box {
                                    if (content.isEmpty()) {
                                        Text(
                                            text = context.getString(R.string.environment_variables_hint),
                                            style = TextStyle(
                                                fontFamily = FontFamily.Monospace,
                                                fontSize = 14.sp,
                                                color = hintColor,
                                                lineHeight = 22.sp
                                            )
                                        )
                                    }
                                    innerTextField()
                                }
                            }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Action buttons
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(onClick = onDismiss) {
                        Text(context.getString(R.string.cancel))
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    TextButton(onClick = { onConfirm(content) }) {
                        Text(confirmLabel ?: context.getString(R.string.ok))
                    }
                }
            }
        }
    }
}

