package com.droidspaces.app.ui.component

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.R
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.ui.util.rememberClearFocus
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Draws a vertical scrollbar indicator for LazyColumn on the right edge.
 */
private fun Modifier.lazyListVerticalScrollbar(
    listState: LazyListState,
    color: Color,
    thickness: Dp = 4.dp
): Modifier = drawWithContent {
    drawContent()

    val totalItems = listState.layoutInfo.totalItemsCount
    if (totalItems == 0) return@drawWithContent

    val visibleItems = listState.layoutInfo.visibleItemsInfo
    if (visibleItems.isEmpty() || visibleItems.size >= totalItems) return@drawWithContent

    val viewportHeight = size.height
    val thumbHeight = (visibleItems.size.toFloat() / totalItems * viewportHeight)
        .coerceAtLeast(24.dp.toPx())
    val scrollRange = viewportHeight - thumbHeight

    val firstVisibleIndex = listState.firstVisibleItemIndex
    val scrollFraction = firstVisibleIndex.toFloat() / (totalItems - visibleItems.size).coerceAtLeast(1)
    val thumbOffset = scrollFraction * scrollRange
    val barWidth = thickness.toPx()

    drawRoundRect(
        color = color,
        topLeft = Offset(size.width - barWidth - 2.dp.toPx(), thumbOffset),
        size = Size(barWidth, thumbHeight),
        cornerRadius = CornerRadius(barWidth / 2f)
    )
}

/**
 * A root-aware file and directory picker dialog that allows browsing from the root (/).
 * Uses libsu (Shell) to bypass Android's scoped storage restrictions for administrative tasks.
 */
@Composable
fun FilePickerDialog(
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
    title: String = "Select Host Path",
    showFiles: Boolean = true
) {
    var currentPath by remember { mutableStateOf("/") }
    var items by remember { mutableStateOf<List<FileItem>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var searchQuery by remember { mutableStateOf("") }

    val context = LocalContext.current

    LaunchedEffect(currentPath) {
        isLoading = true
        items = fetchItems(currentPath, showFiles)
        isLoading = false
    }

    // Smart filtering
    val filteredItems = remember(items, searchQuery) {
        if (searchQuery.isEmpty()) {
            items
        } else if (searchQuery.startsWith("/")) {
            val lastSegment = searchQuery.substringAfterLast("/")
            if (lastSegment.isEmpty()) items else items.filter { it.name.contains(lastSegment, ignoreCase = true) }
        } else {
            items.filter { it.name.contains(searchQuery, ignoreCase = true) }
        }
    }

    LaunchedEffect(searchQuery) {
        if (searchQuery.startsWith("/") && searchQuery.length > 1) {
            val targetDir = if (searchQuery.endsWith("/")) {
                searchQuery.trimEnd('/')
            } else {
                File(searchQuery).parent ?: "/"
            }
            if (targetDir != currentPath && targetDir.isNotEmpty()) {
                val exists = withContext(Dispatchers.IO) {
                    val result = Shell.cmd("[ -d \"$targetDir\" ] && echo yes").exec()
                    result.isSuccess && result.out.firstOrNull() == "yes"
                }
                if (exists) {
                    currentPath = targetDir
                    isLoading = true
                    items = fetchItems(targetDir, showFiles)
                    isLoading = false
                }
            }
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        val clearFocus = rememberClearFocus()

        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .fillMaxHeight(0.75f),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
            tonalElevation = 0.dp
        ) {
            ClearFocusOnClickOutside(
                modifier = Modifier.fillMaxSize()
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(24.dp)
                ) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    val pathScrollState = rememberScrollState()
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 4.dp)
                            .horizontalScroll(pathScrollState),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = currentPath,
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontFamily = JetBrainsMono,
                                fontSize = 12.sp
                            ),
                            color = MaterialTheme.colorScheme.secondary,
                            maxLines = 1,
                            softWrap = false
                        )
                    }

                    Spacer(modifier = Modifier.height(12.dp))

                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        modifier = Modifier.fillMaxWidth(),
                        placeholder = {
                            Text(
                                context.getString(R.string.filter_absolute_path_hint),
                                style = MaterialTheme.typography.bodySmall
                            )
                        },
                        leadingIcon = {
                            Icon(
                                Icons.Default.Search,
                                contentDescription = null,
                                modifier = Modifier.size(20.dp)
                            )
                        },
                        singleLine = true,
                        keyboardOptions = FocusUtils.searchKeyboardOptions,
                        keyboardActions = FocusUtils.clearFocusKeyboardActions(),
                        textStyle = MaterialTheme.typography.bodySmall.copy(
                            fontFamily = JetBrainsMono
                        ),
                        shape = RoundedCornerShape(14.dp),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                            unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                            unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
                            focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
                        )
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    val listState = rememberLazyListState()
                    val scrollbarColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.45f)

                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxWidth()
                            .lazyListVerticalScrollbar(listState, scrollbarColor)
                    ) {
                        if (isLoading) {
                            Box(
                                modifier = Modifier.fillMaxSize(),
                                contentAlignment = Alignment.Center
                            ) {
                                CircularProgressIndicator()
                            }
                        } else {
                            LazyColumn(
                                state = listState,
                                modifier = Modifier.fillMaxSize()
                            ) {
                                if (currentPath != "/") {
                                    item {
                                        FileItemRow(
                                            name = "..",
                                            isDirectory = true,
                                            icon = {
                                                Icon(
                                                    Icons.Default.ArrowUpward,
                                                    contentDescription = null,
                                                    modifier = Modifier.size(24.dp),
                                                    tint = MaterialTheme.colorScheme.tertiary
                                                )
                                            }
                                        ) {
                                            clearFocus()
                                            searchQuery = ""
                                            val parent = File(currentPath).parent ?: "/"
                                            currentPath = parent
                                        }
                                    }
                                }

                                items(filteredItems) { item ->
                                    FileItemRow(name = item.name, isDirectory = item.isDirectory) {
                                        clearFocus()
                                        searchQuery = ""
                                        if (item.isDirectory) {
                                            currentPath = if (currentPath == "/") "/${item.name}" else "$currentPath/${item.name}"
                                        } else {
                                            onConfirm(if (currentPath == "/") "/${item.name}" else "$currentPath/${item.name}")
                                        }
                                    }
                                }

                                if (filteredItems.isEmpty() && !isLoading) {
                                    item {
                                        Text(
                                            text = if (searchQuery.isNotEmpty()) context.getString(R.string.no_matches) else context.getString(R.string.empty_directory),
                                            modifier = Modifier.padding(16.dp),
                                            style = MaterialTheme.typography.bodyMedium,
                                            color = MaterialTheme.colorScheme.outline
                                        )
                                    }
                                }
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Surface(
                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
                            shape = RoundedCornerShape(14.dp),
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                            tonalElevation = 0.dp
                        ) {
                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
                            }
                        }
                        Surface(
                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = {
                                clearFocus()
                                onConfirm(currentPath)
                            }),
                            shape = RoundedCornerShape(14.dp),
                            color = MaterialTheme.colorScheme.primary,
                            tonalElevation = 0.dp
                        ) {
                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                Text(context.getString(R.string.select_folder), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.onPrimary)
                            }
                        }
                    }
                }
            }
        }
    }
}

data class FileItem(val name: String, val isDirectory: Boolean)

@Composable
private fun FileItemRow(
    name: String,
    isDirectory: Boolean,
    icon: @Composable (() -> Unit)? = null,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 12.dp, horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (icon != null) {
            icon()
        } else {
            Icon(
                imageVector = if (isDirectory) Icons.Default.Folder else Icons.Default.Description,
                contentDescription = null,
                modifier = Modifier.size(24.dp),
                tint = if (isDirectory) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary
            )
        }
        Spacer(modifier = Modifier.width(16.dp))
        val nameScrollState = rememberScrollState()
        Box(
            modifier = Modifier
                .weight(1f)
                .horizontalScroll(nameScrollState)
        ) {
            Text(
                text = name,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                softWrap = false
            )
        }
    }
}

private suspend fun fetchItems(path: String, showFiles: Boolean): List<FileItem> = withContext(Dispatchers.IO) {
    val result = Shell.cmd("ls -F \"$path\" 2>/dev/null").exec()
    if (!result.isSuccess) return@withContext emptyList()

    result.out.mapNotNull { line ->
        if (line.isEmpty()) return@mapNotNull null
        val rawName = line.trim()
        val isDirectory = rawName.endsWith("/")
        val cleanName = if (isDirectory) {
            rawName.dropLast(1)
        } else {
            val last = rawName.last()
            if (last == '*' || last == '@' || last == '=' || last == '|' || last == '>') {
                rawName.dropLast(1)
            } else {
                rawName
            }
        }
        if (cleanName.isEmpty()) return@mapNotNull null
        if (!showFiles && !isDirectory) return@mapNotNull null
        FileItem(cleanName, isDirectory)
    }.sortedWith(compareBy({ !it.isDirectory }, { it.name }))
}
