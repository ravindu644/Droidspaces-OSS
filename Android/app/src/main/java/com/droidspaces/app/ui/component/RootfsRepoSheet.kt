package com.droidspaces.app.ui.component

import android.net.Uri
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.TrendingUp
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import kotlinx.coroutines.flow.first
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.ui.viewmodel.AssetDownloadState
import com.droidspaces.app.ui.viewmodel.RepoUiState
import com.droidspaces.app.ui.viewmodel.RootfsRepoViewModel
import com.droidspaces.app.util.IconUtils
import com.droidspaces.app.util.RootfsAsset

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RootfsRepoSheet(
    onDismiss: () -> Unit,
    onInstall: (Uri) -> Unit
) {
    val vm: RootfsRepoViewModel = viewModel()
    val context = LocalContext.current

    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    LaunchedEffect(Unit) {
        snapshotFlow { sheetState.currentValue }
            .first { it == SheetValue.Expanded }
        vm.load()
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        tonalElevation = 0.dp,
        windowInsets = WindowInsets(0),
        dragHandle = { BottomSheetDefaults.DragHandle() }
    ) {
        ClearFocusOnClickOutside(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.fillMaxWidth().imePadding()) {
            var searchQuery by remember { mutableStateOf("") }

            // Derive stable display state - avoids AnimatedContent recomposition that collapses the sheet
            val state = vm.uiState
            val isLoading = state is RepoUiState.Loading || state is RepoUiState.Idle
            val displayAssets = when (state) {
                is RepoUiState.Success -> state.assets
                is RepoUiState.Loading -> state.previousAssets
                else -> emptyList()
            }
            val showError = state is RepoUiState.Error

            val filteredAssets = remember(displayAssets, searchQuery) {
                if (searchQuery.isBlank()) displayAssets
                else displayAssets.filter {
                    val friendly = getFriendlyName(it.name)
                    friendly.contains(searchQuery, ignoreCase = true) || it.name.contains(searchQuery, ignoreCase = true)
                }
            }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp)
                    .padding(bottom = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = Icons.Default.CloudDownload,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(22.dp)
                )
                Spacer(Modifier.width(10.dp))
                Text(
                    text = context.getString(R.string.repo_title),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )
                IconButton(onClick = { vm.load() }, enabled = !isLoading) {
                    if (isLoading) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            strokeWidth = 2.dp
                        )
                    } else {
                        Icon(
                            Icons.Default.Refresh,
                            contentDescription = context.getString(R.string.repo_refresh)
                        )
                    }
                }
            }

            HorizontalDivider(
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
                thickness = 1.dp
            )

            if (displayAssets.isNotEmpty()) {
                RepoSearchBar(query = searchQuery, onQueryChange = { searchQuery = it })
            }

            // Content: list stays in composition during refresh so sheet height is stable
            when {
                displayAssets.isNotEmpty() -> {
                    Box {
                        if (filteredAssets.isEmpty()) {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(240.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Column(
                                    horizontalAlignment = Alignment.CenterHorizontally,
                                    verticalArrangement = Arrangement.spacedBy(16.dp)
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.SearchOff,
                                        contentDescription = null,
                                        modifier = Modifier.size(64.dp),
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.3f)
                                    )
                                    Text(
                                        text = context.getString(R.string.no_services_found),
                                        style = MaterialTheme.typography.bodyLarge,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                                    )
                                }
                            }
                        } else {
                            RepoListContent(
                                assets         = filteredAssets,
                                downloadStates = vm.downloadStates,
                                onDownload     = { vm.startDownload(it) },
                                onCancel       = { vm.cancelDownload(it) },
                                onInstall      = { uri -> onDismiss(); onInstall(uri) },
                                onRetry        = { vm.resetAsset(it.name) }
                            )
                        }
                        if (isLoading) {
                            LinearProgressIndicator(
                                modifier = Modifier.fillMaxWidth().align(Alignment.TopCenter)
                            )
                        }
                    }
                }

                showError -> RepoErrorContent(
                    message = (state as RepoUiState.Error).message,
                    onRetry = { vm.load() }
                )

                else -> RepoLoadingContent()
            }

            Spacer(Modifier.navigationBarsPadding())
        }
        } // ClearFocusOnClickOutside
    }
}

@Composable
private fun RepoLoadingContent() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(240.dp),
        contentAlignment = Alignment.Center
    ) {
        CircularProgressIndicator()
    }
}

@Composable
private fun RepoErrorContent(message: String, onRetry: () -> Unit) {
    val context = LocalContext.current
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Icon(
            imageVector = Icons.Default.CloudOff,
            contentDescription = null,
            modifier = Modifier.size(48.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
        )
        Text(
            text = context.getString(R.string.repo_failed_to_load),
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            text = message,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
            textAlign = TextAlign.Center
        )
        Button(
            onClick = onRetry,
            shape = RoundedCornerShape(12.dp)
        ) {
            Icon(Icons.Default.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(6.dp))
            Text(context.getString(R.string.repo_retry))
        }
    }
}

@Composable
private fun RepoListContent(
    assets: List<RootfsAsset>,
    downloadStates: Map<String, AssetDownloadState>,
    onDownload: (RootfsAsset) -> Unit,
    onCancel: (RootfsAsset) -> Unit,
    onInstall: (Uri) -> Unit,
    onRetry: (RootfsAsset) -> Unit
) {
    LazyColumn(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        items(assets, key = { it.name }) { asset ->
            RootfsAssetCard(
                asset      = asset,
                state      = downloadStates[asset.name] ?: AssetDownloadState.Idle,
                onDownload = { onDownload(asset) },
                onCancel   = { onCancel(asset) },
                onInstall  = onInstall,
                onRetry    = { onRetry(asset) }
            )
        }
    }
}

@Composable
private fun RootfsAssetCard(
    asset: RootfsAsset,
    state: AssetDownloadState,
    onDownload: () -> Unit,
    onCancel: () -> Unit,
    onInstall: (Uri) -> Unit,
    onRetry: () -> Unit
) {
    val context = LocalContext.current
    val cardShape = RoundedCornerShape(20.dp)

    Surface(
        modifier = Modifier.fillMaxWidth().clip(cardShape),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(
            1.dp,
            when (state) {
                is AssetDownloadState.Done   -> MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                is AssetDownloadState.Failed -> MaterialTheme.colorScheme.error.copy(alpha = 0.4f)
                else                         -> MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)
            }
        ),
        tonalElevation = 0.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Top Row: Distro Icon, Name, and Status Badge
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(
                        painter = IconUtils.getDistroIcon(asset.name),
                        contentDescription = null,
                        modifier = Modifier.size(24.dp),
                        tint = if (state is AssetDownloadState.Done) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                    )
                    var nameFontSize by remember(asset.name) { mutableStateOf(16.sp) }
                    Text(
                        text = getFriendlyName(asset.name),
                        style = MaterialTheme.typography.titleMedium.copy(fontSize = nameFontSize),
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        softWrap = false,
                        overflow = TextOverflow.Visible,
                        onTextLayout = { if (it.hasVisualOverflow) nameFontSize = (nameFontSize.value - 1).sp }
                    )
                }

                // Premium State Pill Badges
                val (displayLabel, statusColor) = when (state) {
                    is AssetDownloadState.Done        -> context.getString(R.string.repo_status_ready) to MaterialTheme.colorScheme.primary
                    is AssetDownloadState.Downloading -> context.getString(R.string.repo_status_downloading) to MaterialTheme.colorScheme.tertiary
                    is AssetDownloadState.Failed      -> context.getString(R.string.repo_status_failed) to MaterialTheme.colorScheme.error
                    else                              -> "" to MaterialTheme.colorScheme.primary
                }

                if (displayLabel.isNotEmpty()) {
                    Surface(
                        color = statusColor.copy(alpha = 0.1f),
                        shape = RoundedCornerShape(8.dp),
                        border = BorderStroke(1.dp, statusColor.copy(alpha = 0.2f))
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                        ) {
                            Surface(modifier = Modifier.size(6.dp), shape = CircleShape, color = statusColor) {}
                            Text(
                                text = displayLabel,
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.5.sp,
                                color = statusColor
                            )
                        }
                    }
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Resource Bar (CPU/RAM Style details block)
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(10.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Archive,
                            contentDescription = null,
                            modifier = Modifier.size(13.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = formatSize(asset.sizeBytes),
                            style = MaterialTheme.typography.labelSmall,
                            fontWeight = FontWeight.Medium,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }

                    val arch = getArchitecture(asset.name)
                    if (arch != "unknown") {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Memory,
                                contentDescription = null,
                                modifier = Modifier.size(13.dp),
                                tint = MaterialTheme.colorScheme.tertiary
                            )
                            Text(
                                text = arch,
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Medium,
                                color = MaterialTheme.colorScheme.tertiary
                            )
                        }
                    }

                    if (asset.downloadCount > 0) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.TrendingUp,
                                contentDescription = null,
                                modifier = Modifier.size(13.dp),
                                tint = MaterialTheme.colorScheme.secondary
                            )
                            Text(
                                text = context.getString(R.string.repo_downloads_count, formatCount(asset.downloadCount)),
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Medium,
                                color = MaterialTheme.colorScheme.secondary
                            )
                        }
                    }
                }
            }

            // Progress/Indicator layer
            if (state is AssetDownloadState.Downloading) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    LinearProgressIndicator(
                        progress = { state.percent / 100f },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(4.dp)
                            .clip(RoundedCornerShape(2.dp)),
                        color = MaterialTheme.colorScheme.tertiary, // Match state pill
                        trackColor = MaterialTheme.colorScheme.tertiary.copy(alpha = 0.15f)
                    )
                    Text(
                        text = "${state.percent}%",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                    )
                }
            }

            if (state is AssetDownloadState.Failed) {
                Text(
                    text = state.reason,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
            }

            // Action Control Pill (Unified Action Button)
            val btnColor: androidx.compose.ui.graphics.Color
            val accentColor: androidx.compose.ui.graphics.Color
            val btnIcon: androidx.compose.ui.graphics.vector.ImageVector
            val btnText: String
            val onClickAction: () -> Unit

            when (state) {
                is AssetDownloadState.Idle -> {
                    btnColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f)
                    accentColor = MaterialTheme.colorScheme.primary
                    btnIcon = Icons.Default.CloudDownload
                    btnText = context.getString(R.string.repo_download)
                    onClickAction = onDownload
                }
                is AssetDownloadState.Downloading -> {
                    btnColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f)
                    accentColor = MaterialTheme.colorScheme.error
                    btnIcon = Icons.Default.Close
                    btnText = context.getString(R.string.repo_cancel)
                    onClickAction = onCancel
                }
                is AssetDownloadState.Done -> {
                    btnColor = MaterialTheme.colorScheme.primary
                    accentColor = MaterialTheme.colorScheme.onPrimary
                    btnIcon = Icons.Default.InstallMobile
                    btnText = context.getString(R.string.repo_install)
                    onClickAction = { onInstall(state.uri) }
                }
                is AssetDownloadState.Failed -> {
                    btnColor = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.4f)
                    accentColor = MaterialTheme.colorScheme.onSecondaryContainer
                    btnIcon = Icons.Default.Refresh
                    btnText = context.getString(R.string.repo_retry)
                    onClickAction = onRetry
                }
            }

            Surface(
                onClick = onClickAction,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(48.dp),
                shape = RoundedCornerShape(16.dp),
                color = btnColor,
                border = if (state !is AssetDownloadState.Done) BorderStroke(1.dp, accentColor.copy(alpha = 0.2f)) else null
            ) {
                Row(
                    modifier = Modifier.fillMaxSize(),
                    horizontalArrangement = Arrangement.Center,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = btnIcon,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp),
                        tint = accentColor
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text = btnText,
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.Bold,
                        color = accentColor
                    )
                }
            }
        }
    }
}

private fun formatSize(bytes: Long): String = when {
    bytes >= 1_073_741_824L -> "%.1f GB".format(bytes / 1_073_741_824.0)
    bytes >= 1_048_576L     -> "%.1f MB".format(bytes / 1_048_576.0)
    bytes >= 1_024L         -> "%.0f KB".format(bytes / 1_024.0)
    else                    -> "$bytes B"
}

private fun formatCount(count: Int): String =
    if (count >= 1_000) "%.1fk".format(count / 1_000.0) else count.toString()

private fun getFriendlyName(fileName: String): String {
    val prefix = fileName.substringBefore("-Droidspaces-rootfs-")
        .substringBefore("-droidspaces-rootfs-")
    return prefix
        .replace("-base", " Base")
        .replace("-Minimal-Systemd", " Minimal Systemd")
        .replace("-Minimal", " Minimal")
        .replace("-Systemd", " Systemd")
        .replace("-latest", " Latest")
        .replace("-v", " v")
        .replace("-and-up", " and up")
        .replace("-", " ")
        .trim()
}

private fun getArchitecture(fileName: String): String {
    return when {
        fileName.contains("aarch64", true) -> "aarch64"
        fileName.contains("x86_64", true)  -> "x86_64"
        fileName.contains("armhf", true)   -> "armhf"
        fileName.contains("x86", true) || fileName.contains("i386", true) -> "x86"
        fileName.contains("amd64", true)   -> "x86_64"
        else -> "unknown"
    }
}

@Composable
private fun RepoSearchBar(query: String, onQueryChange: (String) -> Unit) {
    val context = LocalContext.current
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    val borderColor by animateColorAsState(
        targetValue = if (isFocused) MaterialTheme.colorScheme.primary
                      else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
        label = "searchBorder"
    )

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 8.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, borderColor)
    ) {
        TextField(
            value = query,
            onValueChange = onQueryChange,
            modifier = Modifier.fillMaxWidth(),
            interactionSource = interactionSource,
            placeholder = {
                Text(
                    text = context.getString(R.string.search) + "...",
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                )
            },
            leadingIcon = {
                Icon(
                    imageVector = Icons.Default.Search,
                    contentDescription = null,
                    tint = if (isFocused) MaterialTheme.colorScheme.primary
                           else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                )
            },
            trailingIcon = {
                if (query.isNotEmpty()) {
                    IconButton(onClick = { onQueryChange("") }) {
                        Icon(
                            imageVector = Icons.Default.Clear,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            },
            colors = TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent,
                unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent,
                cursorColor = MaterialTheme.colorScheme.primary
            ),
            singleLine = true,
            textStyle = MaterialTheme.typography.bodyLarge,
            keyboardOptions = FocusUtils.searchKeyboardOptions,
            keyboardActions = FocusUtils.clearFocusKeyboardActions()
        )
    }
}


