package com.droidspaces.app.ui.component

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import kotlinx.coroutines.flow.first
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.component.DsDialog
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.FullScreenLoading
import com.droidspaces.app.service.AssetDownloadState
import com.droidspaces.app.service.RootfsDownloadService
import com.droidspaces.app.ui.viewmodel.RepoUiState
import com.droidspaces.app.ui.viewmodel.RootfsRepoViewModel
import com.droidspaces.app.util.IconUtils
import com.droidspaces.app.util.ResourceLimits
import com.droidspaces.app.util.RootfsAsset
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.foundation.text.TextAutoSize

/** The sheet arrives on the standard motion, like every sheet; see [StandardMotion]. */
@Composable
fun RootfsRepoSheet(onDismiss: () -> Unit, onInstall: (Uri) -> Unit) {
    StandardMotion { RootfsRepoSheetContent(onDismiss, onInstall) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RootfsRepoSheetContent(
    onDismiss: () -> Unit,
    onInstall: (Uri) -> Unit
) {
    val vm: RootfsRepoViewModel = viewModel()
    val context = LocalContext.current

    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    var showRepoManager by remember { mutableStateOf(false) }

    val appSettings = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", context.packageName, null))
    // API 26-28 has no MediaStore.Downloads, writing public Downloads needs the runtime permission
    val requestStorage = rememberPermissionRequest(
        permission = Manifest.permission.WRITE_EXTERNAL_STORAGE,
        title = context.getString(R.string.repo_storage_permission_title),
        rationale = context.getString(R.string.repo_storage_permission_rationale),
        settingsIntent = appSettings
    )
    // Downloads run without it, but then progress, Cancel and tap-to-install are all invisible
    val requestNotifications = rememberPermissionRequest(
        permission = Manifest.permission.POST_NOTIFICATIONS,
        title = context.getString(R.string.notification_permission_title),
        rationale = context.getString(R.string.repo_notification_permission_rationale),
        settingsIntent = Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
            .putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)
    )
    // Once per sheet, so a user who said no is not nagged on every Download tap
    var askedNotifications by remember { mutableStateOf(false) }
    val download: (RootfsAsset) -> Unit = { asset ->
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && !askedNotifications) {
            askedNotifications = true
            requestNotifications {}
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            requestStorage { granted ->
                if (granted) vm.startDownload(asset)
                else vm.failDownload(asset, context.getString(R.string.repo_dl_error_storage_permission))
            }
        } else {
            vm.startDownload(asset)
        }
    }

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
        contentWindowInsets = { WindowInsets(0) },
        dragHandle = { BottomSheetDefaults.DragHandle() }
    ) {
        ClearFocusOnClickOutside(modifier = Modifier.fillMaxWidth()) {
        // fillMaxSize, not fillMaxWidth: the sheet skips its partially-expanded
        // state, so it is full height. Without it the column wraps its content and
        // every state below sits stacked at the top of a tall, mostly empty sheet.
        Column(modifier = Modifier.fillMaxSize().imePadding()) {
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
                    modifier = Modifier.size(20.dp)
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
                DsSearchBar(searchQuery, { searchQuery = it }, context.getString(R.string.search) + "...", Modifier.padding(horizontal = 20.dp, vertical = 8.dp))
            }

            // Content: list stays in composition during refresh so sheet height is stable.
            // Takes the height the header and the bottom spacer leave behind, so the
            // loading and error states centre in the real empty area.
            Box(modifier = Modifier.weight(1f)) {
            when {
                displayAssets.isNotEmpty() -> {
                    Box {
                        if (filteredAssets.isEmpty()) {
                            Box(
                                modifier = Modifier.fillMaxSize(),
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
                                onDownload     = download,
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

                showError -> EmptyState(
                    icon = Icons.Default.CloudOff,
                    title = context.getString(R.string.repo_failed_to_load),
                    description = (state as RepoUiState.Error).message,
                    onRetry = { vm.load() }
                )

                else -> FullScreenLoading(message = context.getString(R.string.fetching_distros))
            }
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
        verticalArrangement = Arrangement.spacedBy(16.dp)
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

private data class AssetAction(val fill: Color, val accent: Color, val icon: ImageVector, val label: String, val onClick: () -> Unit)

/** A 16dp icon and a label in one tint, for the size, arch and date chips. */
@Composable
private fun IconChip(icon: ImageVector, text: String, tint: Color) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(16.dp), tint = tint)
        Text(text, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Medium, color = tint)
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
                    Text(
                        text = asset.name,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        softWrap = false,
                        // A long distro name shrinks to fit rather than wrapping under the badge.
                        autoSize = TextAutoSize.StepBased(minFontSize = 10.sp, maxFontSize = 16.sp)
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
                    modifier = Modifier.size(16.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
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
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
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
                    IconChip(Icons.Default.Archive, ResourceLimits.formatBytes(asset.sizeBytes), MaterialTheme.colorScheme.primary)
                    if (asset.architecture.isNotEmpty()) IconChip(Icons.Default.Memory, asset.architecture, MaterialTheme.colorScheme.tertiary)
                    if (asset.buildDate.isNotEmpty()) IconChip(Icons.Default.CalendarToday, formatBuildDate(asset.buildDate), MaterialTheme.colorScheme.secondary)
                }
            }

            // Progress/Indicator layer
            if (state is AssetDownloadState.Downloading) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    // The service reports whole percents, so the bar would step 1% at a time
                    val progress by animateFloatAsState(
                        targetValue = state.percent / 100f,
                        animationSpec = WavyProgressIndicatorDefaults.ProgressAnimationSpec,
                        label = "downloadProgress"
                    )
                    LinearWavyProgressIndicator(
                        progress = { progress },
                        modifier = Modifier.fillMaxWidth(),
                        color = MaterialTheme.colorScheme.tertiary, // Match state pill
                        trackColor = MaterialTheme.colorScheme.tertiary.copy(alpha = 0.15f),
                        // The default flattens the wave below 10% and above 95%; a bar that
                        // only starts waving part way through a download looks broken.
                        amplitude = { 1f }
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

            // The one action the state allows
            val (btnColor, accentColor, btnIcon, btnText, onClickAction) = when (state) {
                is AssetDownloadState.Idle -> AssetAction(
                    MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f), MaterialTheme.colorScheme.primary,
                    Icons.Default.CloudDownload, context.getString(R.string.repo_download), onDownload
                )
                is AssetDownloadState.Downloading -> AssetAction(
                    MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f), MaterialTheme.colorScheme.error,
                    Icons.Default.Close, context.getString(R.string.repo_cancel), onCancel
                )
                is AssetDownloadState.Done -> AssetAction(
                    MaterialTheme.colorScheme.primary, MaterialTheme.colorScheme.onPrimary,
                    Icons.Default.InstallMobile, context.getString(R.string.repo_install)
                ) {
                    RootfsDownloadService.dismissDone(context, asset.downloadUrl)
                    onInstall(state.uri)
                }
                is AssetDownloadState.Failed -> AssetAction(
                    MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.4f), MaterialTheme.colorScheme.onSecondaryContainer,
                    Icons.Default.Refresh, context.getString(R.string.repo_retry), onRetry
                )
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
        shape = RoundedCornerShape(20.dp),
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

    val fieldShape  = RoundedCornerShape(16.dp)
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

    DsDialog(
        onDismiss = onDismiss,
        scrollableContent = false,
        footer = {
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
            )
        }
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
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
                // fill = false so the dialog wraps a short repo list instead of
                // pinning itself at the shell's full height bound
                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f, fill = false),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    itemsIndexed(repos, key = { _, item -> item.second }) { _, (repoName, repoUrl) ->
                        Surface(
                            shape = RoundedCornerShape(16.dp),
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

            // Inline add-repo form, always visible, no animation toggle
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
                shape = RoundedCornerShape(16.dp),
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
        }
    }
    }
