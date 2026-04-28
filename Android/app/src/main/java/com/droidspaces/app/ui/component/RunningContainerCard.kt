package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.service.TerminalSessionService
import com.droidspaces.app.util.AndroidSystemStatsCollector
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.ContainerSystemdManager
import kotlinx.coroutines.launch

/**
 * Container card for Panel tab - shows container name, icon, quick actions, and stats.
 * Tapping opens ContainerDetailsScreen.
 *
 * @param refreshTrigger Increment this to force a live re-fetch of OS info (e.g. on pull-to-refresh
 *                       or tab re-entry). The card always shows cached data instantly and then
 *                       updates in the background when the trigger changes.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RunningContainerCard(
    container: ContainerInfo,
    onEnter: () -> Unit = {},
    onTerminalClick: () -> Unit = {},
    usage: AndroidSystemStatsCollector.ContainerUsage? = null,
    refreshTrigger: Int = 0,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val cardShape = RoundedCornerShape(20.dp)
    val interactionSource = remember { MutableInteractionSource() }

    // Show cached data instantly (zero delay), then refresh in background whenever
    // refreshTrigger changes (pull-to-refresh, tab re-entry, etc.)
    var osInfo by remember {
        mutableStateOf(ContainerOSInfoManager.getCachedOSInfo(container.name, context))
    }

    LaunchedEffect(container.name, refreshTrigger) {
        osInfo = ContainerOSInfoManager.getOSInfo(
            containerName = container.name,
            useCache = false,
            appContext = context
        )
    }

    Card(
        modifier = modifier
            .fillMaxWidth()
            .clip(cardShape)
            .clickable(
                onClick = onEnter,
                indication = rememberRipple(bounded = true),
                interactionSource = interactionSource
            ),
        shape = cardShape,
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = spring(stiffness = 300f, dampingRatio = 0.8f))
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // --- TOP ROW: Icon, Name, Terminal quick-action ---
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(
                    imageVector = Icons.Default.Storage,
                    contentDescription = "Container",
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

                // Terminal/Restore Pill (Above divider)
                Surface(
                    onClick = onTerminalClick,
                    shape = RoundedCornerShape(10.dp),
                    color = MaterialTheme.colorScheme.primary,
                    contentColor = MaterialTheme.colorScheme.onPrimary,
                    modifier = Modifier.height(32.dp)
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Center
                    ) {
                        val sessionCount by remember {
                            derivedStateOf {
                                TerminalSessionService.globalSessionList.values.count { it.containerName == container.name }
                            }
                        }
                        Icon(
                            imageVector = if (sessionCount > 0) Icons.Default.Terminal else Icons.Default.ChevronRight,
                            contentDescription = null,
                            modifier = Modifier.size(14.dp)
                        )
                        Spacer(Modifier.width(4.dp))
                        Text(
                            text = if (sessionCount > 0) "Restore" else "Terminal",
                            style = MaterialTheme.typography.labelMedium,
                            fontWeight = FontWeight.Black
                        )
                    }
                }
            }

            HorizontalDivider()

            // --- INFO ROWS: Distribution, Uptime, IP ---

            // Distribution (e.g. "Debian GNU/Linux 13 (trixie)")
            osInfo?.prettyName?.let { distro ->
                Text(
                    text = distro,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                )
            }

            // Uptime
            // TODO: Add CPU and memory usage
            Text(
                text = context.getString(R.string.uptime_label, osInfo?.uptime ?: ""),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )

            // IP Address
            Text(
                text = context.getString(R.string.ip_address_label, osInfo?.ipAddress ?: ""),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )

            // --- RESOURCE IMPACT (GIGACHAD METRIC) ---
            usage?.let { stats ->
                if (stats.ramKb > 0) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.surfaceColorAtElevation(2.dp))
                            .padding(8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Memory,
                            contentDescription = null,
                            modifier = Modifier.size(14.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        val ramMb = stats.ramKb / 1024
                        Text(
                            text = String.format("Using %d MB RAM (%.1f%% of system load)", ramMb, stats.relativeRamPercent),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary,
                            fontWeight = FontWeight.Medium
                        )
                    }
                }
            }
        }
    }
}
