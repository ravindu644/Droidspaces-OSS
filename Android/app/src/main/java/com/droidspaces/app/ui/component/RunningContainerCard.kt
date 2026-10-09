package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.service.TerminalSessionService
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.IconUtils

/**
 * A running container on the Panel tab, the live glance: who it is, one stats box, and
 * the way into a shell. The card itself opens the container's details.
 * Receives fully-populated OSInfo from SystemStatsViewModel (single polling source).
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun RunningContainerCard(
    container: ContainerInfo,
    onEnter: () -> Unit = {},
    onTerminalClick: () -> Unit = {},
    osInfo: ContainerOSInfoManager.OSInfo? = null,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scheme = MaterialTheme.colorScheme

    Surface(
        onClick = onEnter,
        modifier = modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = scheme.surfaceContainer,
        border = amoledCardBorder()
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
                Surface(
                    modifier = Modifier.size(CardHeaderHeight),
                    shape = MaterialTheme.shapes.medium,
                    color = scheme.primaryContainer,
                    contentColor = scheme.onPrimaryContainer
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            painter = IconUtils.getDistroIcon(osInfo?.prettyName ?: container.name),
                            contentDescription = null,
                            modifier = Modifier.size(24.dp)
                        )
                    }
                }
                Column(Modifier.weight(1f)) {
                    Text(
                        container.name,
                        style = MaterialTheme.typography.titleMediumEmphasized,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    osInfo?.prettyName?.let {
                        Text(it, style = MaterialTheme.typography.bodySmall, color = scheme.onSurfaceVariant, maxLines = 1)
                    }
                }
                Icon(Icons.Default.ChevronRight, contentDescription = null, tint = scheme.onSurfaceVariant)
            }

            HorizontalDivider()

            ContainerStatsBox(osInfo)

            val sessionCount by remember(container.name) {
                derivedStateOf {
                    TerminalSessionService.globalSessionList.values.count { it.containerName == container.name }
                }
            }
            Button(onClick = onTerminalClick, modifier = Modifier.fillMaxWidth().height(48.dp)) {
                Icon(Icons.Default.Terminal, contentDescription = null, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(8.dp))
                Text(context.getString(if (sessionCount > 0) R.string.restore else R.string.terminal))
            }
        }
    }
}

/**
 * The live stats box Panel and container details share: label and value rows on
 * surfaceContainerHigh, CPU and memory each with a plain bar. Plain on purpose, a wavy
 * bar reads as noise on a number that idles at zero (DESIGN.md, Decided exceptions).
 * A null field is "no data yet" and its row waits; a container idling under 1 MB still
 * gets its memory row.
 */
@Composable
fun ContainerStatsBox(osInfo: ContainerOSInfoManager.OSInfo?, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = amoledCardBorder()
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            osInfo?.uptime?.let { StatRow(context.getString(R.string.uptime), it) }
            osInfo?.ipAddress?.takeIf { it.isNotBlank() }?.let { StatRow(context.getString(R.string.ip_address), it) }
            osInfo?.cpuUsage?.takeIf { it >= 0.0 }?.let {
                StatRow(context.getString(R.string.cpu), context.getString(R.string.cpu_percent_label, it), (it / 100).toFloat())
            }
            osInfo?.ramLabel(context)?.let {
                StatRow(context.getString(R.string.property_memory), it, ((osInfo.ramPercent ?: 0.0) / 100).toFloat())
            }
        }
    }
}

/** A label and value row of a stats box, with an optional plain bar under it. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun StatRow(label: String, value: String, fraction: Float? = null) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Text(label, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(
                value,
                style = MaterialTheme.typography.bodyMediumEmphasized,
                textAlign = TextAlign.End,
                modifier = Modifier.weight(1f)
            )
        }
        if (fraction != null) {
            val animated by animateFloatAsState(
                fraction.coerceIn(0f, 1f),
                animationSpec = MaterialTheme.motionScheme.defaultEffectsSpec(),
                label = "stat_bar"
            )
            LinearProgressIndicator(progress = { animated }, modifier = Modifier.fillMaxWidth())
        }
    }
}
