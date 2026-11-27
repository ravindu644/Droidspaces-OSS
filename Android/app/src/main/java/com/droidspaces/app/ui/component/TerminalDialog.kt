package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
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
import androidx.compose.ui.platform.LocalConfiguration
import com.droidspaces.app.R
import com.droidspaces.app.util.AnsiColorParser
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector

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
    val configuration = LocalConfiguration.current
    val screenHeight = configuration.screenHeightDp.dp
    val dialogShape = RoundedCornerShape(24.dp)

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
                .height(screenHeight * 0.75f)
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

                    DialogCloseButton(onClick = onDismiss, enabled = !isBlocking)
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
                        TerminalDialogButton(
                            icon = Icons.Default.Delete,
                            label = context.getString(R.string.clear_logs),
                            enabled = canClear,
                            fill = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                            border = MaterialTheme.colorScheme.error.copy(alpha = 0.4f),
                            content = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f),
                            onClick = onClear
                        )
                    }

                    val canCopy = logs.isNotEmpty() && !isBlocking
                    TerminalDialogButton(
                        icon = Icons.Default.ContentCopy,
                        label = context.getString(R.string.copy_logs),
                        enabled = canCopy,
                        fill = MaterialTheme.colorScheme.primary.copy(alpha = 0.08f),
                        border = MaterialTheme.colorScheme.primary.copy(alpha = 0.3f),
                        content = MaterialTheme.colorScheme.primary
                    ) {
                        val logText = logs.joinToString("\n") { AnsiColorParser.stripAnsi(it.second) }
                        val clipboard = context.getSystemService(ClipboardManager::class.java)
                        clipboard.setPrimaryClip(ClipData.newPlainText(context.getString(R.string.terminal_logs), logText))
                        Toast.makeText(context, R.string.logs_copied, Toast.LENGTH_SHORT).show()
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

/**
 * One of the two 38dp buttons over the console. Enabled, it wears the colours the
 * caller gives it; disabled, both buttons share the same faint grey.
 */
@Composable
private fun RowScope.TerminalDialogButton(
    icon: ImageVector,
    label: String,
    enabled: Boolean,
    fill: Color,
    border: Color,
    content: Color,
    onClick: () -> Unit,
) {
    val shape = RoundedCornerShape(16.dp)
    val contentColor = if (enabled) content else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
    Surface(
        modifier = Modifier
            .height(38.dp)
            .weight(1f)
            .clip(shape)
            .clickable(enabled = enabled, onClick = onClick, indication = ripple(bounded = true), interactionSource = remember { MutableInteractionSource() }),
        shape = shape,
        color = if (enabled) fill else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.03f),
        border = BorderStroke(1.dp, if (enabled) border else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f)),
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.fillMaxSize().padding(horizontal = 14.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(16.dp), tint = contentColor)
            Spacer(modifier = Modifier.width(8.dp))
            Text(label, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.SemiBold, color = contentColor)
        }
    }
}
