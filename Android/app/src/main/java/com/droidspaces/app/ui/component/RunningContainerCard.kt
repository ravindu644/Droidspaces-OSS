package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager

/**
 * Container card for Panel tab - shows container name, icon, quick actions, and stats.
 * Tapping opens ContainerDetailsScreen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RunningContainerCard(
    container: ContainerInfo,
    onEnter: () -> Unit = {},
    onTerminalClick: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val cardShape = RoundedCornerShape(20.dp)
    val interactionSource = remember { MutableInteractionSource() }

    // Pre-load cached OS info for instant display - zero delay
    // Uses persistent cache that survives app restarts
    var osInfo by remember {
        mutableStateOf(ContainerOSInfoManager.getCachedOSInfo(container.name, context))
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
            // --- TOP ROW: Icon, Name, and Quick Actions ---
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                // Temporary placeholder until we have a distro icon
                Icon(
                    imageVector = Icons.Default.Computer,
                    contentDescription = "Distro Icon",
                    modifier = Modifier.size(24.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )

                Spacer(modifier = Modifier.width(12.dp))

                // Container Name
                Text(
                    text = container.name,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )

                // Quick Actions
                IconButton(
                    onClick = onTerminalClick,
                    modifier = Modifier.size(32.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Terminal,
                        contentDescription = "Open Terminal",
                        modifier = Modifier.size(20.dp)
                    )
                }
            }

            // --- BOTTOM ROWs: Status, Uptime, IP ---
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                // Green Status Dot
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .clip(CircleShape)
                        .background(Color(0xFF00FF00))
                )
                
                Spacer(modifier = Modifier.width(8.dp))

                // Status Text
                Text(
                    text = context.getString(R.string.running),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                // Uptime Text
                // TODO: Add CPU and memory usage
                Text(
                    text = context.getString(R.string.uptime_label, osInfo?.uptime ?: ""),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                // IP Address Text
                Text(
                    text = context.getString(R.string.ip_address_label, osInfo?.ipAddress ?: ""),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}
