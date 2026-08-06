package com.droidspaces.app.ui.component

import android.net.Uri

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import kotlinx.coroutines.flow.first
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
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

    var showRepoManager by remember { mutableStateOf(false) }

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
                    it.name.contains(searchQuery, ignoreCase = true) ||
                    it.description.contains(searchQuery, ignoreCase = true) ||
                    it.author.contains(searchQuery, ignoreCase = true) ||
                    it.sourceRepoName.contains(searchQuery, ignoreCase = true)
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
                // Manage / add repos
                IconButton(onClick = { showRepoManager = true }) {
                    Icon(Icons.Default.Tune, contentDescription = context.getString(R.string.repo_manage_custom))
                }
                // Refresh
                IconButton(onClick = { vm.load() }, enabled = !isLoading) {
                    Icon(
                        Icons.Default.Refresh,
                        contentDescription = context.getString(R.string.repo_refresh)
                    )
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
                                        text = context.getString(R.string.repo_not_found_in_repo, searchQuery),
                                        style = MaterialTheme.typography.bodyLarge,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                                    )
                                }
                            }
                        } else {
                            RepoListContent(
                                assets         = filteredAssets,
                                isFiltered     = searchQuery.isNotBlank(),
                                downloadStates = vm.downloadStates,
                                onDownload     = { vm.startDownload(it) },
                                onCancel       = { vm.cancelDownload(it) },
                                onInstall      = { uri -> onInstall(uri) },
                                onRetry        = { vm.resetAsset(it.downloadUrl) }
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

    if (showRepoManager) {
        RepoManagerDialog(
            initialRepos = vm.getCustomRepos(),
            onDismiss    = { showRepoManager = false },
            onSave       = { toAdd, toRemove ->
                toRemove.forEach { vm.removeCustomRepo(it) }
                toAdd.forEach { (name, url) -> vm.addCustomRepo(name, url) }
                showRepoManager = false
            }
        )
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
        LoadingIndicator(size = LoadingSize.Medium)
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
    isFiltered: Boolean,
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
        items(assets, key = { it.downloadUrl }) { asset ->
            RootfsAssetCard(
                asset      = asset,
                state      = downloadStates[asset.downloadUrl] ?: AssetDownloadState.Idle,
                onDownload = { onDownload(asset) },
                onCancel   = { onCancel(asset) },
                onInstall  = onInstall,
                onRetry    = { onRetry(asset) }
            )
        }
        // Footer: banner only when not filtering
        if (!isFiltered) {
            item {
                Spacer(Modifier.height(8.dp))
                RepoSourceBanner()
                Spacer(Modifier.height(12.dp))
            }
        } else {
            item { Spacer(Modifier.height(12.dp)) }
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
                        text = asset.name,
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
                    StatusPill(label = displayLabel, color = statusColor)
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Author row
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.Person,
                    contentDescription = null,
                    modifier = Modifier.size(13.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f)
                )
                Text(
                    text = asset.author,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                    fontWeight = FontWeight.Medium
                )
            }

            if (asset.description.isNotEmpty()) {
                Text(
                    text = asset.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.75f),
                    modifier = Modifier.padding(horizontal = 2.dp)
                )
            }

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

                    val arch = asset.architecture
                    if (arch.isNotEmpty()) {
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

                    if (asset.buildDate.isNotEmpty()) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.CalendarToday,
                                contentDescription = null,
                                modifier = Modifier.size(13.dp),
                                tint = MaterialTheme.colorScheme.secondary
                            )
                            Text(
                                text = formatBuildDate(asset.buildDate),
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
                            .height(4.dp),
                        color = MaterialTheme.colorScheme.tertiary, // Match state pill
                        trackColor = MaterialTheme.colorScheme.tertiary.copy(alpha = 0.15f),
                        strokeCap = StrokeCap.Round
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

@Composable
private fun RepoSourceBanner() {
    val context = LocalContext.current
    val url = context.getString(R.string.repo_banner_url)

    Surface(
        onClick = {
            context.startActivity(
                android.content.Intent(android.content.Intent.ACTION_VIEW, Uri.parse(url))
            )
        },
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp),
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(
                imageVector = Icons.Default.Info,
                contentDescription = null,
                modifier = Modifier.size(16.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
            )
            Text(
                text = context.getString(R.string.repo_banner_text),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                modifier = Modifier.weight(1f)
            )
        }
    }
}

private fun formatSize(bytes: Long): String = when {
    bytes >= 1_073_741_824L -> "%.1f GB".format(bytes / 1_073_741_824.0)
    bytes >= 1_048_576L     -> "%.1f MB".format(bytes / 1_048_576.0)
    bytes >= 1_024L         -> "%.0f KB".format(bytes / 1_024.0)
    else                    -> "$bytes B"
}

/** Formats \"20260525\" -> \"2026-05-25\" for display. Returns raw string if not 8 digits. */
private fun formatBuildDate(raw: String): String {
    if (raw.length == 8 && raw.all { it.isDigit() }) {
        return "${raw.substring(0, 4)}-${raw.substring(4, 6)}-${raw.substring(6, 8)}"
    }
    return raw
}

@Composable
private fun RepoManagerDialog(
    initialRepos: List<Pair<String, String>>,
    onDismiss: () -> Unit,
    onSave: (toAdd: List<Pair<String, String>>, toRemove: List<String>) -> Unit
) {
    val context = LocalContext.current

    // Local mutable state so deletes reflect immediately without waiting for VM
    var repos by remember { mutableStateOf(initialRepos) }
    val originalUrls = remember { initialRepos.map { it.second }.toSet() }

    var newName   by remember { mutableStateOf("") }
    var newUrl    by remember { mutableStateOf("") }
    var nameError by remember { mutableStateOf("") }
    var urlError  by remember { mutableStateOf("") }

    val fieldShape  = RoundedCornerShape(14.dp)
    val fieldColors = DsTextFieldDefaults.surfaceColors()

    fun tryAdd() {
        val n = newName.trim(); val u = newUrl.trim()
        nameError = if (n.isEmpty()) context.getString(R.string.repo_custom_name_empty) else ""
        urlError = when {
            u.isEmpty()               -> context.getString(R.string.repo_custom_url_empty)
            !u.startsWith("https://") -> context.getString(R.string.repo_custom_url_invalid)
            repos.any { it.second == u } -> context.getString(R.string.repo_custom_url_invalid)
            else -> ""
        }
        if (nameError.isEmpty() && urlError.isEmpty()) {
            repos = repos + (n to u)
            newName = ""; newUrl = ""
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .imePadding(),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
            tonalElevation = 0.dp
        ) {
            Column(modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp)
            ) {
                // Header
                Text(
                    text = context.getString(R.string.repo_manage_custom),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = context.getString(R.string.repo_manager_subtitle),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 2.dp)
                )

                Spacer(Modifier.height(16.dp))
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                Spacer(Modifier.height(8.dp))

                // Existing repo list
                if (repos.isNotEmpty()) {
                    LazyColumn(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(max = 200.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        itemsIndexed(repos, key = { _, item -> item.second }) { _, (repoName, repoUrl) ->
                            Surface(
                                shape = RoundedCornerShape(14.dp),
                                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f))
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(start = 14.dp, end = 4.dp, top = 10.dp, bottom = 10.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                            text = repoName,
                                            style = MaterialTheme.typography.bodyMedium,
                                            fontWeight = FontWeight.SemiBold,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                        Text(
                                            text = repoUrl,
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                    IconButton(
                                        onClick = { repos = repos.filter { it.second != repoUrl } },
                                        modifier = Modifier.size(36.dp)
                                    ) {
                                        Icon(
                                            Icons.Default.Delete,
                                            contentDescription = context.getString(R.string.repo_custom_remove),
                                            modifier = Modifier.size(18.dp),
                                            tint = MaterialTheme.colorScheme.error.copy(alpha = 0.7f)
                                        )
                                    }
                                }
                            }
                        }
                    }
                    Spacer(Modifier.height(12.dp))
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                    Spacer(Modifier.height(12.dp))
                }

                // Inline add-repo form — always visible, no animation toggle
                Text(
                    text = context.getString(R.string.repo_add_custom),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(8.dp))

                OutlinedTextField(
                    value = newName,
                    onValueChange = { newName = it; nameError = "" },
                    label = { Text(context.getString(R.string.repo_custom_name_hint)) },
                    isError = nameError.isNotEmpty(),
                    supportingText = if (nameError.isNotEmpty()) { { Text(nameError) } } else null,
                    shape = fieldShape,
                    colors = fieldColors,
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )

                OutlinedTextField(
                    value = newUrl,
                    onValueChange = { newUrl = it; urlError = "" },
                    label = { Text(context.getString(R.string.repo_custom_url_hint)) },
                    isError = urlError.isNotEmpty(),
                    supportingText = if (urlError.isNotEmpty()) { { Text(urlError) } } else null,
                    shape = fieldShape,
                    colors = fieldColors,
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )

                Spacer(Modifier.height(8.dp))

                Surface(
                    onClick = { tryAdd() },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp),
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.secondaryContainer
                ) {
                    Row(
                        modifier = Modifier.fillMaxSize(),
                        horizontalArrangement = Arrangement.Center,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            Icons.Default.Add,
                            contentDescription = context.getString(R.string.repo_custom_add),
                            tint = MaterialTheme.colorScheme.onSecondaryContainer,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = context.getString(R.string.repo_custom_add),
                            style = MaterialTheme.typography.labelLarge,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    }
                }

                Spacer(Modifier.height(16.dp))

                // Single footer row: Close / Save
                DialogFooterRow(
                    dismissLabel = context.getString(R.string.cancel),
                    confirmLabel = context.getString(R.string.ok),
                    onDismiss = onDismiss,
                    onConfirm = {
                        val currentUrls = repos.map { it.second }.toSet()
                        val toRemove = originalUrls.filter { it !in currentUrls }
                        val toAdd = repos.filter { it.second !in originalUrls }
                        onSave(toAdd, toRemove)
                    },
                    cancelBorderAlpha = 0.35f
                )
            }
        }
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


