package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.service.TerminalSessionService
import com.droidspaces.app.util.AnimationUtils
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.IconUtils

/**
 * Container card for the Panel tab. Shows container name, OS info, resource usage, and quick actions.
 * Receives fully-populated OSInfo from SystemStatsViewModel (single polling source).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun RunningContainerCard(
    container: ContainerInfo,
    onEnter: () -> Unit = {},
    onTerminalClick: () -> Unit = {},
    anlandEnabled: Boolean = false,
    onLaunchAnland: () -> Unit = {},
    osInfo: ContainerOSInfoManager.OSInfo? = null,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current

    val cardShape = RoundedCornerShape(20.dp)

    Surface(
        onClick = onEnter,
        modifier = modifier.fillMaxWidth().clip(cardShape),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = AnimationUtils.mediumSpec())
                .padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Top row: icon, name, terminal pill
            Row(
                modifier = Modifier.fillMaxWidth().height(CardHeaderHeight),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    painter = IconUtils.getDistroIcon(osInfo?.prettyName ?: container.name),
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.width(12.dp))
                Text(
                    text = container.name,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f)
                )

                // Small "anland" pill next to the terminal button, shown only when
                // this container has the anland display daemon enabled and a live
                // socket recorded.
                if (anlandEnabled) {
                    Surface(
                        onClick = onLaunchAnland,
                        shape = RoundedCornerShape(12.dp),
                        color = MaterialTheme.colorScheme.secondaryContainer,
                        contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                        modifier = Modifier.height(30.dp).clip(RoundedCornerShape(12.dp)),
                        tonalElevation = 0.dp
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 10.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.DesktopWindows,
                                contentDescription = null,
                                modifier = Modifier.size(14.dp)
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                text = "anland",
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }
                    Spacer(Modifier.width(8.dp))
                }

                val sessionCount by remember(container.name) {
                    derivedStateOf {
                        TerminalSessionService.globalSessionList.values.count {
                            it.containerName == container.name
                        }
                    }
                }
                Surface(
                    onClick = onTerminalClick,
                    shape = RoundedCornerShape(12.dp), // Sharper, refined radius
                    color = MaterialTheme.colorScheme.primary,
                    contentColor = MaterialTheme.colorScheme.onPrimary,
                    modifier = Modifier.height(30.dp).clip(RoundedCornerShape(12.dp)),
                    tonalElevation = 0.dp
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 10.dp), // Tighter padding
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Center
                    ) {
                        Icon(
                            imageVector = if (sessionCount > 0) Icons.Default.Terminal else Icons.Default.ChevronRight,
                            contentDescription = null,
                            modifier = Modifier.size(14.dp) // Slightly smaller icon
                        )
                        Spacer(Modifier.width(6.dp))
                        Text(
                            text = if (sessionCount > 0) context.getString(R.string.restore) else context.getString(R.string.terminal),
                            style = MaterialTheme.typography.labelMedium, // More compact typography
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // OS info rows
            osInfo?.prettyName?.let { distro ->
                Text(
                    text = distro,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                )
            }
            Text(
                text = context.getString(R.string.uptime_label, context.getString(R.string.uptime), osInfo?.uptime ?: ""),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
            Text(
                text = context.getString(R.string.ip_address_label, context.getString(R.string.ip_address), osInfo?.ipAddress ?: ""),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )


            // Resource usage row, only shown when we have real data. Null is the
            // only "no data" signal: a container idling under 1MB reads as 0 and
            // still deserves its chip.
            val ramUsageMb = osInfo?.ramUsageMb
            val cpuPercent = osInfo?.cpuUsage ?: -1.0
            if (ramUsageMb != null || cpuPercent >= 0.0) {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(10.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
                ) {
                    // FlowRow, not Row: on a narrow display (watch, small phone, or a
                    // locale with longer labels) CPU and RAM do not fit side by side.
                    // A Row would hand RAM whatever sliver is left and shred its text;
                    // this drops it onto a second line instead.
                    FlowRow(
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                        horizontalArrangement = Arrangement.spacedBy(16.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        if (cpuPercent >= 0.0) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Speed,
                                    contentDescription = null,
                                    modifier = Modifier.size(16.dp),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                                Text(
                                    text = "${context.getString(R.string.cpu)} ${context.getString(R.string.cpu_percent_label, cpuPercent)}",
                                    style = MaterialTheme.typography.labelSmall,
                                    fontWeight = FontWeight.Medium,
                                    color = MaterialTheme.colorScheme.primary
                                )
                            }
                        }
                        if (ramUsageMb != null) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Memory,
                                    contentDescription = null,
                                    modifier = Modifier.size(16.dp),
                                    tint = MaterialTheme.colorScheme.secondary
                                )
                                Text(
                                    text = "${context.getString(R.string.ram)} ${context.getString(R.string.ram_percent_label, ramUsageMb.toInt(), osInfo.ramPercent ?: 0.0)}",
                                    style = MaterialTheme.typography.labelSmall,
                                    fontWeight = FontWeight.Medium,
                                    color = MaterialTheme.colorScheme.secondary
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
