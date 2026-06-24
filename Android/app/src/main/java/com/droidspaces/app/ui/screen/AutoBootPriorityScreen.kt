package com.droidspaces.app.ui.screen

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.DragHandle
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.IconUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import sh.calvin.reorderable.ReorderableItem
import sh.calvin.reorderable.rememberReorderableLazyListState

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun AutoBootPriorityScreen(
    containerViewModel: ContainerViewModel,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    // Snapshot of run-at-boot containers, ordered by saved priority (unset goes
    // last, then alphabetical). Built once so user drags are not clobbered.
    val initialOrder = remember {
        containerViewModel.containerList
            .filter { it.runAtBoot }
            .sortedWith(
                compareBy(
                    { if (it.runAtBootPriority > 0) it.runAtBootPriority else Int.MAX_VALUE },
                    { it.name.lowercase() }
                )
            )
    }
    var items by remember { mutableStateOf(initialOrder) }
    // Baseline order persisted so far; updated after a successful save so the
    // button greys out again once there is nothing new to save.
    var savedNames by remember { mutableStateOf(initialOrder.map { it.name }) }
    val hasChanges by remember { derivedStateOf { items.map { it.name } != savedNames } }
    var isSaving by remember { mutableStateOf(false) }
    var isSaved by remember { mutableStateOf(false) }

    // Drop the "Saved" state as soon as the user reorders again.
    LaunchedEffect(hasChanges) {
        if (hasChanges && isSaved) isSaved = false
    }

    val lazyListState = rememberLazyListState()
    val reorderableState = rememberReorderableLazyListState(lazyListState) { from, to ->
        items = items.toMutableList().apply { add(to.index, removeAt(from.index)) }
    }

    fun saveChanges() {
        scope.launch {
            isSaving = true
            isSaved = false
            withContext(Dispatchers.IO) {
                // Assign 1..N by current order -- collision-free by construction.
                items.forEachIndexed { index, info ->
                    ContainerManager.updateContainerConfig(
                        context,
                        info.name,
                        info.copy(runAtBootPriority = index + 1)
                    )
                }
            }
            containerViewModel.refresh()
            savedNames = items.map { it.name }
            isSaving = false
            isSaved = true
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.auto_boot_priority)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = context.getString(R.string.back)
                        )
                    }
                }
            )
        },
        bottomBar = {
            if (items.isNotEmpty()) {
                val btnShape = RoundedCornerShape(20.dp)
                val isReadyToSave = !isSaving && !isSaved && hasChanges
                val targetBtnColor = when {
                    isSaved -> MaterialTheme.colorScheme.primaryContainer
                    isSaving || isReadyToSave -> MaterialTheme.colorScheme.primary
                    else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
                }
                val animatedBtnColor by animateColorAsState(
                    targetValue = targetBtnColor,
                    animationSpec = tween(durationMillis = 250),
                    label = "btn_color"
                )
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    color = MaterialTheme.colorScheme.surfaceContainer,
                    tonalElevation = 0.dp
                ) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        HorizontalDivider(
                            color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
                            thickness = 1.dp
                        )
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(24.dp)
                                .navigationBarsPadding()
                                .clip(btnShape)
                                .clickable(
                                    enabled = isReadyToSave,
                                    onClick = { saveChanges() },
                                    indication = rememberRipple(bounded = true),
                                    interactionSource = remember { MutableInteractionSource() }
                                ),
                            shape = btnShape,
                            color = animatedBtnColor,
                            tonalElevation = 0.dp
                        ) {
                            Box(modifier = Modifier.padding(vertical = 16.dp).fillMaxWidth(), contentAlignment = Alignment.Center) {
                                when {
                                    isSaved -> {
                                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                            Icon(
                                                imageVector = Icons.Default.Check,
                                                contentDescription = null,
                                                modifier = Modifier.size(20.dp),
                                                tint = MaterialTheme.colorScheme.onPrimaryContainer
                                            )
                                            Text(
                                                text = context.getString(R.string.saved),
                                                style = MaterialTheme.typography.labelLarge,
                                                fontWeight = FontWeight.SemiBold,
                                                color = MaterialTheme.colorScheme.onPrimaryContainer
                                            )
                                        }
                                    }
                                    isSaving -> {
                                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                            LoadingIndicator(
                                                modifier = Modifier.size(20.dp),
                                                color = MaterialTheme.colorScheme.onPrimary
                                            )
                                            Text(
                                                text = context.getString(R.string.saving),
                                                style = MaterialTheme.typography.labelLarge,
                                                fontWeight = FontWeight.SemiBold,
                                                color = MaterialTheme.colorScheme.onPrimary
                                            )
                                        }
                                    }
                                    else -> {
                                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                            Icon(
                                                imageVector = Icons.Default.Save,
                                                contentDescription = null,
                                                modifier = Modifier.size(20.dp),
                                                tint = if (isReadyToSave) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                            )
                                            Text(
                                                text = context.getString(R.string.save_changes),
                                                style = MaterialTheme.typography.labelLarge,
                                                fontWeight = FontWeight.SemiBold,
                                                color = if (isReadyToSave) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        if (items.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .padding(horizontal = 32.dp),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(
                        Icons.Default.PowerSettingsNew,
                        contentDescription = null,
                        modifier = Modifier.size(48.dp),
                        tint = MaterialTheme.colorScheme.outline
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        context.getString(R.string.auto_boot_priority_empty),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontWeight = FontWeight.Medium
                    )
                }
            }
        } else {
            // Hint lives OUTSIDE the LazyColumn so the reorderable item indices map
            // 1:1 to `items` (a header item would offset from.index/to.index).
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .consumeWindowInsets(innerPadding)
                    .padding(horizontal = 16.dp)
            ) {
                Text(
                    text = context.getString(R.string.auto_boot_priority_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.secondary,
                    modifier = Modifier.padding(horizontal = 4.dp, vertical = 12.dp)
                )

                LazyColumn(
                    state = lazyListState,
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(bottom = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(items, key = { it.name }) { container ->
                    ReorderableItem(reorderableState, key = container.name) { isDragging ->
                        BootPriorityRow(
                            index = items.indexOf(container) + 1,
                            container = container,
                            isDragging = isDragging,
                            dragHandle = {
                                IconButton(
                                    modifier = Modifier.draggableHandle(),
                                    onClick = {}
                                ) {
                                    Icon(
                                        Icons.Default.DragHandle,
                                        contentDescription = context.getString(R.string.reorder_handle),
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        )
                    }
                    }
                }
            }
        }
    }
}

@Composable
private fun BootPriorityRow(
    index: Int,
    container: ContainerInfo,
    isDragging: Boolean,
    dragHandle: @Composable () -> Unit
) {
    val elevation = if (isDragging) 8.dp else 0.dp
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .shadow(elevation, RoundedCornerShape(16.dp), clip = false),
        shape = RoundedCornerShape(16.dp),
        color = if (isDragging)
            MaterialTheme.colorScheme.surfaceVariant
        else
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Priority position badge
            Box(
                modifier = Modifier
                    .size(28.dp)
                    .clip(RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center
            ) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    shape = RoundedCornerShape(8.dp),
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                    tonalElevation = 0.dp
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Text(
                            text = index.toString(),
                            style = MaterialTheme.typography.labelMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }

            // Distro icon, sourced + tinted exactly like the containers tab.
            val context = LocalContext.current
            val cacheVersion by ContainerOSInfoManager.iconCacheVersion
            val cachedOsInfo = remember(container.name, cacheVersion) {
                ContainerOSInfoManager.getCachedOSInfo(container.name, context)
            }
            Icon(
                painter = IconUtils.getDistroIcon(cachedOsInfo?.prettyName ?: cachedOsInfo?.name),
                contentDescription = null,
                modifier = Modifier.size(24.dp),
                tint = if (container.isRunning)
                    MaterialTheme.colorScheme.primary
                else
                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
            )

            Text(
                text = container.name,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f)
            )

            dragHandle()
        }
    }
}
