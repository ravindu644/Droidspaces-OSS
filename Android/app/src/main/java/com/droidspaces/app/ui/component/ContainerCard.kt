package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ReceiptLong
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerStatus
import com.droidspaces.app.util.IconUtils
import com.droidspaces.app.util.ResourceLimits

/**
 * Per-item callbacks for [ContainerCard], grouped so the call site passes one
 * stable object instead of 11 separate lambdas.
 */
data class ContainerCardActions(
    val onStart: () -> Unit = {},
    val onStop: () -> Unit = {},
    val onRestart: () -> Unit = {},
    val onEdit: () -> Unit = {},
    val onEnter: () -> Unit = {},
    val onUninstall: () -> Unit = {},
    val onMigrate: () -> Unit = {},
    val onResize: () -> Unit = {},
    val onExport: () -> Unit = {},
    val onToggleMenu: () -> Unit = {},
    val onShowLogs: () -> Unit = {},
)

/**
 * A container on the Containers tab. Tapping a running card opens its details; there
 * is no Open button, the card is the affordance. A long press, or a tap on a stopped
 * card, opens the manage menu anchored to the card, the same gestures the old in-card
 * drawer answered to. [isMenuOpen] is hoisted so only one card has its menu open.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class, ExperimentalFoundationApi::class, ExperimentalLayoutApi::class)
@Composable
fun ContainerCard(
    container: ContainerInfo,
    actions: ContainerCardActions = ContainerCardActions(),
    isOperationRunning: Boolean = false,
    isMenuOpen: Boolean = false,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scheme = MaterialTheme.colorScheme
    val shape = MaterialTheme.shapes.extraLarge
    val running = container.isRunning

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .clip(shape)
            .combinedClickable(
                onClick = { if (running) actions.onEnter() else actions.onToggleMenu() },
                onLongClick = actions.onToggleMenu
            ),
        shape = shape,
        color = scheme.surfaceContainer,
        // The card the menu belongs to gets an outline so the anchor is obvious
        border = if (isMenuOpen) BorderStroke(2.dp, scheme.primary) else amoledCardBorder()
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = MaterialTheme.motionScheme.defaultSpatialSpec())
                .padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().height(CardHeaderHeight),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Re-read cache whenever iconCacheVersion changes (bumped after prefetch)
                val cacheVersion by ContainerOSInfoManager.iconCacheVersion
                val cachedOsInfo = remember(container.name, cacheVersion) {
                    ContainerOSInfoManager.getCachedOSInfo(container.name, context)
                }
                Surface(
                    modifier = Modifier.size(CardHeaderHeight),
                    shape = MaterialTheme.shapes.medium,
                    color = if (running) scheme.primaryContainer else scheme.surfaceContainerHighest,
                    contentColor = if (running) scheme.onPrimaryContainer else scheme.onSurfaceVariant
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            painter = IconUtils.getDistroIcon(cachedOsInfo?.prettyName ?: cachedOsInfo?.name),
                            contentDescription = null,
                            modifier = Modifier.size(24.dp)
                        )
                    }
                }
                Text(
                    text = container.name,
                    style = MaterialTheme.typography.titleMediumEmphasized,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f)
                )
                // The menu shares the chip's box so it anchors to the chip: it opens just
                // under the header, its right edge on the card's content edge
                Box {
                    // The label keeps reporting the state; the dot spins while an operation
                    // runs, so the chip never changes width mid-operation.
                    StatusChip(
                        label = context.getString(if (running) R.string.status_running else R.string.status_stopped),
                        color = if (running) scheme.primary else scheme.onSurfaceVariant,
                        active = container.status == ContainerStatus.RUNNING,
                        busy = isOperationRunning
                    )
                    ManageMenu(container, actions, expanded = isMenuOpen)
                }
            }

            HorizontalDivider()

            if (container.pid != null) {
                Text(
                    context.getString(R.string.pid_label, container.pid),
                    style = MaterialTheme.typography.bodySmall,
                    color = scheme.onSurfaceVariant
                )
            }

            // Info Rows
            val displayHostname = container.hostname.takeIf { it.isNotEmpty() } ?: container.name
            val hasSparseImage = container.useSparseImage && container.sparseImageSizeGB != null
            val netModeLabel = when (container.netMode) { "nat" -> context.getString(R.string.network_mode_nat_short); "none" -> context.getString(R.string.network_mode_none_short); "gateway" -> context.getString(R.string.network_mode_gateway_short); else -> context.getString(R.string.network_mode_host_short) }
            // Identity first, then what the container is given: disk, memory, CPU, processes.
            val info = buildList {
                add(rememberVectorPainter(Icons.Default.Computer) to displayHostname)
                add(rememberVectorPainter(Icons.Default.Public) to netModeLabel)
                if (hasSparseImage) add(painterResource(id = R.drawable.ic_disk) to context.getString(R.string.gb_size, container.sparseImageSizeGB ?: 0))
                ResourceLimits.memoryLabel(context, container)?.let { add(rememberVectorPainter(Icons.Default.Memory) to it) }
                ResourceLimits.cpuLabel(context, container)?.let { add(rememberVectorPainter(Icons.Default.Speed) to it) }
                ResourceLimits.pidsLabel(context, container)?.let { add(rememberVectorPainter(Icons.Default.Tag) to it) }
            }
            val infoColor = MaterialTheme.colorScheme.onSurfaceVariant
            // FlowRow, not Row: six items do not fit one line on a narrow display, so
            // they wrap as whole icon+value units. The comma rides with the item
            // before it, so a wrapped line never starts with one, and a long hostname
            // ellipsizes inside its own unit instead of pushing the rest off the card.
            FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                info.forEachIndexed { i, (icon, text) ->
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        Icon(icon, null, modifier = Modifier.size(16.dp), tint = infoColor)
                        Text(
                            if (i < info.lastIndex) text + context.getString(R.string.comma) else text,
                            style = MaterialTheme.typography.bodySmall, color = infoColor,
                            maxLines = 1, overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }

            // Options Row, depends only on stable container fields, so build once per container.
            val options = remember(container) {
                buildList {
                    if (container.disableIPv6) add(context.getString(R.string.ipv6_option))
                    if (container.enableAndroidStorage) add(context.getString(R.string.storage_option))
                    if (container.enableHwAccess) add(context.getString(R.string.hw_option))
                    if (container.enableGpuMode || container.enableHwAccess) add(context.getString(R.string.gpu_option))
                    if (container.enableTermuxX11) add(context.getString(R.string.x11_option))
                    if (container.enableVirgl) add(context.getString(R.string.virgl_option))
                    if (container.enablePulseaudio) add(context.getString(R.string.pulseaudio_option))
                    if (container.selinuxPermissive) add(context.getString(R.string.selinux_permissive_option))
                    if (container.allowSandboxing) add(context.getString(R.string.userns_option))
                    if (container.volatileMode) add(context.getString(R.string.volatile_option))
                    if (container.forceCgroupv1) add(context.getString(R.string.cgroup_v1_option))
                    if (container.privileged.isNotEmpty()) add(context.getString(R.string.privileged_option))
                    if (container.customInit.isNotEmpty()) add(context.getString(R.string.custom_init_option))
                    if (container.runAtBoot) add(context.getString(R.string.run_at_boot))
                }
            }
            if (options.isNotEmpty()) {
                // Option chips: the status chip without a dot. Labels, never buttons.
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    options.forEach { StatusChip(label = it, color = null) }
                }
            }

            ActionGroup(running, isOperationRunning, actions)
        }
    }
}

/**
 * The card's connected button group: the state action as the wide filled segment,
 * then Restart (running only) and Logs as 56dp tonal segments. Outer corners are full,
 * inner ones the theme's small, and each segment morphs to its pressed shape.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun ActionGroup(running: Boolean, busy: Boolean, actions: ContainerCardActions) {
    val context = LocalContext.current
    val scheme = MaterialTheme.colorScheme
    val inner = MaterialTheme.shapes.small
    val full = CornerSize(50)
    val leading = ButtonShapes(inner.copy(topStart = full, bottomStart = full), ButtonGroupDefaults.connectedLeadingButtonPressShape)
    val middle = ButtonShapes(inner, ButtonGroupDefaults.connectedMiddleButtonPressShape)
    val trailing = ButtonShapes(inner.copy(topEnd = full, bottomEnd = full), ButtonGroupDefaults.connectedTrailingButtonPressShape)
    val tonal = ButtonDefaults.buttonColors(containerColor = scheme.secondaryContainer, contentColor = scheme.onSecondaryContainer)
    // 48, not the 40 button default: every segment is a touch target in its own right
    val height = Modifier.height(48.dp)

    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Button(
            onClick = if (running) actions.onStop else actions.onStart,
            shapes = leading,
            enabled = !busy,
            colors = if (running) ButtonDefaults.buttonColors(containerColor = scheme.errorContainer, contentColor = scheme.onErrorContainer)
                else ButtonDefaults.buttonColors(),
            modifier = height.weight(1f)
        ) {
            Icon(if (running) Icons.Default.Stop else Icons.Default.PlayArrow, null, Modifier.size(20.dp))
            Spacer(Modifier.width(8.dp))
            Text(context.getString(if (running) R.string.stop else R.string.start), style = MaterialTheme.typography.labelLarge)
        }
        if (running) {
            Button(
                onClick = actions.onRestart,
                shapes = middle,
                enabled = !busy,
                colors = tonal,
                contentPadding = PaddingValues(0.dp),
                modifier = height.width(56.dp)
            ) {
                Icon(Icons.Default.Refresh, context.getString(R.string.restart), Modifier.size(20.dp))
            }
        }
        // Logs stay reachable while an operation runs: that is when they matter
        Button(
            onClick = actions.onShowLogs,
            shapes = trailing,
            colors = tonal,
            contentPadding = PaddingValues(0.dp),
            modifier = height.width(56.dp)
        ) {
            Icon(Icons.AutoMirrored.Filled.ReceiptLong, context.getString(R.string.view_logs), Modifier.size(20.dp))
        }
    }
}

/** The per-container actions that used to live in the card's drawer. */
@Composable
private fun ManageMenu(container: ContainerInfo, actions: ContainerCardActions, expanded: Boolean) {
    val context = LocalContext.current
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = actions.onToggleMenu,
        shape = MaterialTheme.shapes.large,
        containerColor = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        DropdownMenuItem(
            text = { Text(context.getString(R.string.edit_container_configuration)) },
            leadingIcon = { Icon(Icons.Default.Edit, null) },
            onClick = actions.onEdit
        )
        DropdownMenuItem(
            text = {
                Text(context.getString(if (container.useSparseImage) R.string.resize_sparse_image else R.string.migrate_to_sparse_image))
            },
            leadingIcon = { Icon(painterResource(R.drawable.ic_disk), null) },
            onClick = if (container.useSparseImage) actions.onResize else actions.onMigrate
        )
        DropdownMenuItem(
            text = { Text(context.getString(R.string.export_container)) },
            leadingIcon = { Icon(Icons.Default.FileDownload, null) },
            onClick = actions.onExport
        )
        HorizontalDivider()
        DropdownMenuItem(
            text = { Text(context.getString(R.string.uninstall_container_menu)) },
            leadingIcon = { Icon(Icons.Default.Delete, null) },
            onClick = actions.onUninstall,
            colors = MenuDefaults.itemColors(
                textColor = MaterialTheme.colorScheme.error,
                leadingIconColor = MaterialTheme.colorScheme.error
            )
        )
    }
}
