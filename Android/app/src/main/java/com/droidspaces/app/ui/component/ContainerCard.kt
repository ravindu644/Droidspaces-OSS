package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerStatus
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.ui.platform.LocalContext

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ContainerCard(
    container: ContainerInfo,
    onStart: () -> Unit = {},
    onStop: () -> Unit = {},
    onRestart: () -> Unit = {},
    onEdit: () -> Unit = {},
    onEnter: () -> Unit = {},
    onUninstall: () -> Unit = {},
    onMigrate: () -> Unit = {},
    onResize: () -> Unit = {},
    onExport: () -> Unit = {},
    isOperationRunning: Boolean = false,
    isExpanded: Boolean = false,
    onToggleExpand: () -> Unit = {},
    onShowLogs: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val cardShape = RoundedCornerShape(20.dp)

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .clip(cardShape)
            .combinedClickable(
                onClick = {
                    if (container.isRunning) {
                        onEnter()
                    } else {
                        onToggleExpand()
                    }
                },
                onLongClick = onToggleExpand,
                indication = rememberRipple(bounded = true),
                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
            ),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = AnimationUtils.mediumSpec())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // ... [Lines 151-285 remain the same] ...
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
                        imageVector = Icons.Default.Storage,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp),
                        tint = if (container.isRunning) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                    )
                    Text(
                        text = container.name,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1
                    )
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    IconButton(onClick = onShowLogs, modifier = Modifier.size(32.dp)) {
                        Icon(Icons.Default.Terminal, context.getString(R.string.view_logs), tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(20.dp))
                    }

                    // Premium Pill Status
                    val (statusText, statusColor) = when (container.status) {
                        ContainerStatus.RUNNING -> context.getString(R.string.status_running) to MaterialTheme.colorScheme.primary
                        ContainerStatus.RESTARTING -> context.getString(R.string.status_restarting) to MaterialTheme.colorScheme.tertiary
                        ContainerStatus.STOPPED -> context.getString(R.string.status_stopped) to MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                    }

                    Surface(
                        color = statusColor.copy(alpha = 0.1f),
                        shape = RoundedCornerShape(8.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, statusColor.copy(alpha = 0.2f))
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                        ) {
                            Surface(modifier = Modifier.size(6.dp), shape = androidx.compose.foundation.shape.CircleShape, color = statusColor) {}
                            Text(
                                text = statusText.uppercase(),
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.5.sp,
                                color = statusColor
                            )
                        }
                    }
                }
            }

            if (container.pid != null) {
                Text(context.getString(R.string.pid_label, container.pid), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
            }

            // Info Rows
            val hasHostname = container.hostname.isNotEmpty() && container.hostname != container.name
            val hasSparseImage = container.useSparseImage && container.sparseImageSizeGB != null
            if (hasHostname || hasSparseImage) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    if (hasHostname) {
                        Icon(Icons.Default.Computer, null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                        Text(context.getString(R.string.hostname_label, container.hostname), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                    }
                    if (hasHostname && hasSparseImage) Text(context.getString(R.string.comma), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                    if (hasSparseImage) {
                        Icon(painterResource(id = R.drawable.ic_disk), null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                        Text(context.getString(R.string.gb_size, container.sparseImageSizeGB ?: 0), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                    }
                }
            }

            // Options Row
            val options = mutableListOf<String>()
            if (container.disableIPv6) options.add(context.getString(R.string.ipv6_option))
            if (container.enableAndroidStorage) options.add(context.getString(R.string.storage_option))
            if (container.enableHwAccess) options.add(context.getString(R.string.hw_option))
            if (container.enableTermuxX11) options.add(context.getString(R.string.x11_option))
            if (container.runAtBoot) options.add(context.getString(R.string.run_at_boot))
            if (options.isNotEmpty()) {
                Text(context.getString(R.string.options_label, options.joinToString(", ")), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Unified Control Pill (Anchored)
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
            ) {
                Row(modifier = Modifier.fillMaxWidth().padding(4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    // Start/Stop
                    val isStartEnabled = !container.isRunning && !isOperationRunning
                    val isStopEnabled = container.isRunning && !isOperationRunning
                    
                    Surface(
                        onClick = if (container.isRunning) onStop else onStart,
                        enabled = isStartEnabled || isStopEnabled,
                        modifier = Modifier.weight(1f).height(48.dp),
                        shape = RoundedCornerShape(16.dp),
                        color = if (isStopEnabled) MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f)
                                else if (isStartEnabled) MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f)
                                else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
                    ) {
                        Row(modifier = Modifier.fillMaxSize(), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = if (container.isRunning) Icons.Default.Stop else Icons.Default.PlayArrow,
                                null, modifier = Modifier.size(20.dp),
                                tint = if (container.isRunning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                            )
                            Spacer(Modifier.width(8.dp))
                            Text(
                                if (container.isRunning) context.getString(R.string.stop) else context.getString(R.string.start),
                                style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold,
                                color = if (container.isRunning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                            )
                        }
                    }

                    // Restart (Only show if running)
                    if (container.isRunning) {
                        val isRestartEnabled = !isOperationRunning
                        Surface(
                            onClick = onRestart,
                            enabled = isRestartEnabled,
                            modifier = Modifier.weight(1f).height(48.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = if (isRestartEnabled) MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.4f) else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
                        ) {
                            Row(modifier = Modifier.fillMaxSize(), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                                Icon(Icons.Default.Refresh, null, modifier = Modifier.size(20.dp), tint = MaterialTheme.colorScheme.onSecondaryContainer)
                                Spacer(Modifier.width(8.dp))
                                Text(
                                    context.getString(R.string.restart),
                                    style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                        }
                    }
                }
            }

            // Integrated Expandable Action Drawer (Sealed & Interactive)
            androidx.compose.animation.AnimatedVisibility(
                visible = isExpanded,
                enter = androidx.compose.animation.expandVertically(
                    animationSpec = AnimationUtils.mediumSpec(),
                    expandFrom = Alignment.Top
                ) + androidx.compose.animation.fadeIn(),
                exit = androidx.compose.animation.shrinkVertically(
                    animationSpec = AnimationUtils.mediumSpec(),
                    shrinkTowards = Alignment.Top
                ) + androidx.compose.animation.fadeOut()
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Spacer(Modifier.height(12.dp))
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                    Spacer(Modifier.height(12.dp))

                    ActionItem(
                        icon = Icons.Default.Edit,
                        label = context.getString(R.string.edit_container_configuration),
                        tint = MaterialTheme.colorScheme.primary,
                        onClick = { onToggleExpand(); onEdit() }
                    )

                    if (!container.useSparseImage) {
                        ActionItem(
                            icon = painterResource(id = R.drawable.ic_disk),
                            label = context.getString(R.string.migrate_to_sparse_image),
                            tint = MaterialTheme.colorScheme.secondary,
                            onClick = { onToggleExpand(); onMigrate() }
                        )
                    } else {
                        ActionItem(
                            icon = painterResource(id = R.drawable.ic_disk),
                            label = context.getString(R.string.resize_sparse_image),
                            tint = MaterialTheme.colorScheme.secondary,
                            onClick = { onToggleExpand(); onResize() }
                        )
                    }

                    ActionItem(
                        icon = Icons.Default.FileDownload,
                        label = context.getString(R.string.export_container),
                        tint = MaterialTheme.colorScheme.tertiary,
                        onClick = { onToggleExpand(); onExport() }
                    )

                    ActionItem(
                        icon = Icons.Default.Delete,
                        label = context.getString(R.string.uninstall_container_menu),
                        tint = MaterialTheme.colorScheme.error,
                        isBold = true,
                        onClick = { onToggleExpand(); onUninstall() }
                    )
                }
            }
        }
    }
}

@Composable
private fun ActionItem(
    icon: Any, // ImageVector or Painter
    label: String,
    tint: androidx.compose.ui.graphics.Color,
    isBold: Boolean = false,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().height(52.dp),
        shape = RoundedCornerShape(12.dp),
        color = androidx.compose.ui.graphics.Color.Transparent
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            when (icon) {
                is androidx.compose.ui.graphics.vector.ImageVector -> Icon(icon, null, tint = tint, modifier = Modifier.size(22.dp))
                is androidx.compose.ui.graphics.painter.Painter -> Icon(icon, null, tint = tint, modifier = Modifier.size(22.dp))
            }
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = if (isBold) FontWeight.Bold else FontWeight.Medium,
                color = if (isBold) tint else MaterialTheme.colorScheme.onSurface
            )
        }
    }
}
