package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.DsTopAppBar
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.DialogFooterRow
import com.droidspaces.app.ui.component.DsDialog
import com.droidspaces.app.ui.component.DsMenuTheme
import com.droidspaces.app.ui.component.DsTextFieldDefaults
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.dsMenuBorder
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.ui.util.FullScreenLoading
import com.droidspaces.app.ui.viewmodel.JournaldState
import com.droidspaces.app.ui.viewmodel.JournaldViewModel

private val LINE_COUNT_OPTIONS = listOf(50, 100, 500, 1000)

/**
 * Tail of a unit's journal, `journalctl -u <unit> -n <count>`. Reached from the
 * "View logs" overflow-menu item on [SystemdScreen] or the log icon on [UnitDetailScreen].
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun JournaldScreen(
    containerName: String,
    unitName: String,
    onNavigateBack: () -> Unit,
    viewModel: JournaldViewModel = viewModel()
) {
    val context = LocalContext.current
    val state = viewModel.state

    LaunchedEffect(containerName, unitName, viewModel.lineCount) {
        viewModel.loadLogs(containerName, unitName)
    }

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Scaffold(
            topBar = {
                DsTopAppBar(
                    title = context.getString(R.string.logs_title, unitName),
                    // Title carries a unit name, so it stays one step below titleLarge.
                    titleStyle = MaterialTheme.typography.titleMedium,
                    subtitle = containerName,
                    onBack = onNavigateBack,
                    actions = {
                        LineCountSelector(
                            selected = viewModel.lineCount,
                            onSelected = { viewModel.lineCount = it },
                            enabled = state !is JournaldState.Loading
                        )
                        IconButton(
                            onClick = { viewModel.loadLogs(containerName, unitName) },
                            enabled = state !is JournaldState.Loading
                        ) {
                            Icon(Icons.Default.Refresh, context.getString(R.string.refresh))
                        }
                    }
                )
            },
            containerColor = Color.Transparent
        ) { padding ->
            Box(modifier = Modifier.padding(padding).fillMaxSize()) {
                when (val s = state) {
                    is JournaldState.Loading -> FullScreenLoading(message = context.getString(R.string.fetching_logs))
                    is JournaldState.Error -> ErrorState(
                        title = context.getString(R.string.journal_load_error_title),
                        description = context.getString(R.string.journal_load_error_message),
                        onRetry = { viewModel.loadLogs(containerName, unitName) }
                    )
                    is JournaldState.Ready -> JournaldContent(s.logs)
                }
            }
        }
    }
}

@Composable
private fun JournaldContent(logs: List<String>) {
    Surface(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
        tonalElevation = 0.dp
    ) {
        // Every reload passes through Loading, so this composable is fresh per log
        // set. Start at the tail instead of scrolling there after the first frame.
        val listState = rememberLazyListState(initialFirstVisibleItemIndex = logs.lastIndex.coerceAtLeast(0))

        // Same roles TerminalConsole gives its ERROR and WARN lines, so a red line
        // means the same thing on both surfaces.
        val errorColor = MaterialTheme.colorScheme.error
        val warnColor = MaterialTheme.colorScheme.tertiary
        val textColor = MaterialTheme.colorScheme.onSurface

        SelectionContainer {
            LazyColumn(
                state = listState,
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(16.dp)
            ) {
                items(logs) { line ->
                    Text(
                        text = line,
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono),
                        color = when {
                            line.contains("error", ignoreCase = true) || line.contains("fail", ignoreCase = true) -> errorColor
                            line.contains("warn", ignoreCase = true) -> warnColor
                            else -> textColor
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun LineCountSelector(
    selected: Int,
    onSelected: (Int) -> Unit,
    enabled: Boolean
) {
    val context = LocalContext.current
    var expanded by remember { mutableStateOf(false) }
    var showCustomDialog by remember { mutableStateOf(false) }

    Box {
        // A text-only trigger between the app bar's IconButtons. A bordered Surface
        // here would read heavier than its neighbours.
        TextButton(onClick = { expanded = true }, enabled = enabled) {
            Text(
                text = selected.toString(),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold
            )
        }

        DsMenuTheme {
            DropdownMenu(
                expanded = expanded,
                onDismissRequest = { expanded = false },
                modifier = Modifier.dsMenuBorder()
            ) {
                LINE_COUNT_OPTIONS.forEach { option ->
                    DropdownMenuItem(
                        text = { Text(option.toString()) },
                        onClick = {
                            onSelected(option)
                            expanded = false
                        }
                    )
                }
                DropdownMenuItem(
                    text = { Text(context.getString(R.string.journal_line_count_custom)) },
                    onClick = {
                        expanded = false
                        showCustomDialog = true
                    }
                )
            }
        }
    }

    if (showCustomDialog) {
        var value by remember { mutableStateOf(selected.toString()) }
        val parsed = value.toIntOrNull()
        DsDialog(
            onDismiss = { showCustomDialog = false },
            modifier = Modifier.imePadding(),
            footer = {
                DialogFooterRow(
                    dismissLabel = context.getString(R.string.cancel),
                    confirmLabel = context.getString(R.string.ok),
                    onDismiss = { showCustomDialog = false },
                    onConfirm = {
                        onSelected(parsed!!)
                        showCustomDialog = false
                    },
                    confirmEnabled = parsed != null && parsed > 0
                )
            }
        ) {
            Text(
                context.getString(R.string.journal_line_count_title),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            OutlinedTextField(
                value = value,
                onValueChange = { value = it.filter(Char::isDigit) },
                label = { Text(context.getString(R.string.journal_line_count_label)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                shape = RoundedCornerShape(16.dp),
                colors = DsTextFieldDefaults.surfaceColors(),
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}
