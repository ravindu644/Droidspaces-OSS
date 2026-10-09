package com.droidspaces.app.ui.component

import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ReceiptLong
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.util.AnsiColorParser

/**
 * The log viewer: a bottom sheet with a receipt glyph and [title], the console, and the
 * actions within thumb reach at the bottom. Container operations, the bug report and
 * the requirements check all stream into it.
 *
 * While [isBlocking] (an operation still writing to the log) nothing dismisses it: not
 * a swipe, not back, not a tap on the scrim, not the close button. The user watches it
 * finish, as the old dialog made them.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LogsSheet(
    title: String,
    logs: List<Pair<Int, String>>,
    onDismiss: () -> Unit,
    onClear: (() -> Unit)? = null,
    isBlocking: Boolean = false
) {
    val context = LocalContext.current
    val blocking by rememberUpdatedState(isBlocking)
    val sheetState = rememberModalBottomSheetState(
        skipPartiallyExpanded = true,
        confirmValueChange = { it != SheetValue.Hidden || !blocking }
    )
    val canAct = logs.isNotEmpty() && !isBlocking

    ModalBottomSheet(
        onDismissRequest = { if (!blocking) onDismiss() },
        sheetState = sheetState,
        sheetGesturesEnabled = !isBlocking,
        // The sheet component's own 28; the theme's extraLarge is 24 for cards
        shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
        containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
        properties = ModalBottomSheetProperties(shouldDismissOnBackPress = !isBlocking)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.9f)
                .padding(start = 16.dp, end = 16.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.AutoMirrored.Filled.ReceiptLong,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.padding(horizontal = 12.dp)
                )
                Text(
                    text = title,
                    style = MaterialTheme.typography.headlineSmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f)
                )
                IconButton(onClick = onDismiss, enabled = !isBlocking) {
                    Icon(Icons.Default.Close, contentDescription = context.getString(R.string.close))
                }
            }

            TerminalConsole(
                logs = logs,
                isProcessing = isBlocking,
                modifier = Modifier.fillMaxWidth().weight(1f)
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        val logText = logs.joinToString("\n") { AnsiColorParser.stripAnsi(it.second) }
                        context.getSystemService(ClipboardManager::class.java)
                            .setPrimaryClip(ClipData.newPlainText(context.getString(R.string.terminal_logs), logText))
                        Toast.makeText(context, R.string.logs_copied, Toast.LENGTH_SHORT).show()
                    },
                    enabled = canAct,
                    modifier = Modifier.weight(1f).height(48.dp)
                ) {
                    Icon(Icons.Default.ContentCopy, null, Modifier.size(20.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(context.getString(R.string.copy_logs))
                }
                if (onClear != null) {
                    OutlinedButton(onClick = onClear, enabled = canAct, modifier = Modifier.height(48.dp)) {
                        Icon(Icons.Default.Delete, null, Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(context.getString(R.string.clear_logs))
                    }
                }
            }
        }
    }
}
