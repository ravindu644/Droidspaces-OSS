package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import com.droidspaces.app.R
import com.droidspaces.app.util.AnsiColorParser

/**
 * Terminal log viewer dialog with modern, shadowless design language.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TerminalDialog(
    title: String,
    logs: List<Pair<Int, String>>,
    onDismiss: () -> Unit,
    onClear: (() -> Unit)? = null,
    isBlocking: Boolean = false
) {
    val context = LocalContext.current
    val dialogShape = RoundedCornerShape(28.dp)
    val buttonShape = RoundedCornerShape(14.dp)

    Dialog(
        onDismissRequest = if (isBlocking) { {} } else { onDismiss },
        properties = DialogProperties(
            dismissOnBackPress = !isBlocking,
            dismissOnClickOutside = !isBlocking,
            usePlatformDefaultWidth = false
        )
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.75f)
                .padding(horizontal = 16.dp),
            shape = dialogShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
            tonalElevation = 0.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(20.dp)
            ) {
                // Header: Title + Close button
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier
                            .weight(1f, fill = false)
                            .padding(end = 12.dp),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )

                    Surface(
                        modifier = Modifier
                            .size(36.dp)
                            .clip(RoundedCornerShape(10.dp))
                            .clickable(
                                enabled = !isBlocking,
                                onClick = onDismiss,
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = RoundedCornerShape(10.dp),
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (!isBlocking) 0.08f else 0.04f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = if (!isBlocking) 0.3f else 0.15f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = context.getString(R.string.close),
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(
                                    alpha = if (!isBlocking) 1f else 0.38f
                                )
                            )
                        }
                    }
                }

                // Action buttons row
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    if (onClear != null) {
                        val canClear = logs.isNotEmpty() && !isBlocking
                        Surface(
                            modifier = Modifier
                                .height(38.dp)
                                .weight(1f)
                                .clip(buttonShape)
                                .clickable(
                                    enabled = canClear,
                                    onClick = onClear,
                                    indication = rememberRipple(bounded = true),
                                    interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                                ),
                            shape = buttonShape,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (canClear) 0.06f else 0.03f),
                            border = BorderStroke(1.dp, if (canClear) MaterialTheme.colorScheme.error.copy(alpha = 0.4f) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f)),
                            tonalElevation = 0.dp
                        ) {
                            Row(
                                modifier = Modifier.fillMaxSize().padding(horizontal = 14.dp),
                                horizontalArrangement = Arrangement.Center,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Delete,
                                    contentDescription = context.getString(R.string.clear_logs),
                                    modifier = Modifier.size(16.dp),
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = if (canClear) 0.8f else 0.38f)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = context.getString(R.string.clear_logs),
                                    style = MaterialTheme.typography.labelMedium,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = if (canClear) 0.8f else 0.38f)
                                )
                            }
                        }
                    }

                    val effectivelyCanCopy = logs.isNotEmpty() && !isBlocking
                    Surface(
                        modifier = Modifier
                            .height(38.dp)
                            .weight(1f)
                            .clip(buttonShape)
                            .clickable(
                                enabled = effectivelyCanCopy,
                                onClick = {
                                    val logText = logs.joinToString("\n") { AnsiColorParser.stripAnsi(it.second) }
                                    val clipboard = context.getSystemService(ClipboardManager::class.java)
                                    val clip = ClipData.newPlainText(context.getString(R.string.terminal_logs), logText)
                                    clipboard.setPrimaryClip(clip)
                                    Toast.makeText(context, R.string.logs_copied, Toast.LENGTH_SHORT).show()
                                },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = buttonShape,
                        color = if (effectivelyCanCopy) MaterialTheme.colorScheme.primary.copy(alpha = 0.08f) else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.03f),
                        border = BorderStroke(1.dp, if (effectivelyCanCopy) MaterialTheme.colorScheme.primary.copy(alpha = 0.3f) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f)),
                        tonalElevation = 0.dp
                    ) {
                        Row(
                            modifier = Modifier.fillMaxSize().padding(horizontal = 14.dp),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = Icons.Default.ContentCopy,
                                contentDescription = context.getString(R.string.copy_logs),
                                modifier = Modifier.size(16.dp),
                                tint = if (effectivelyCanCopy) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.copy_logs),
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.SemiBold,
                                color = if (effectivelyCanCopy) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                            )
                        }
                    }
                }

                // Terminal Console
                TerminalConsole(
                    logs = logs,
                    isProcessing = isBlocking,
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                )
            }
        }
    }
}
