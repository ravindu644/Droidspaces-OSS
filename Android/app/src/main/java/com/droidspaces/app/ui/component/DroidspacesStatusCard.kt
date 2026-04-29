package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.R

enum class DroidspacesStatus {
    Working,
    UpdateAvailable,
    NotInstalled,
    Unsupported,
    Corrupted,
    ModuleMissing
}

@Composable
fun DroidspacesStatusCard(
    status: DroidspacesStatus,
    version: String? = null,
    isChecking: Boolean = false,
    isRootAvailable: Boolean = true,
    refreshTrigger: Int = 0,
    onClick: () -> Unit = {}
) {
    val context = LocalContext.current
    val isWorking = isRootAvailable && status == DroidspacesStatus.Working

    var droidspacesVersion by remember {
        mutableStateOf(version ?: SystemInfoManager.getCachedDroidspacesVersion(context))
    }
    var backendMode by remember {
        mutableStateOf(if (isWorking) SystemInfoManager.getCachedBackendMode(context) else null)
    }

    LaunchedEffect(status, isRootAvailable, refreshTrigger) {
        if (isWorking) {
            droidspacesVersion = SystemInfoManager.getDroidspacesVersion(context)
            backendMode = SystemInfoManager.getBackendMode(context)
        } else {
            backendMode = null
        }
    }

    val accentColor = when {
        isWorking -> MaterialTheme.colorScheme.primary
        status == DroidspacesStatus.UpdateAvailable -> Color(0xFFFFB300) // Yellow (Amber) for updates
        else -> MaterialTheme.colorScheme.error
    }
    val isError = !isWorking

    val cardShape = RoundedCornerShape(20.dp)
 
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().clip(cardShape),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Premium Header Row (Symmetric) - Fixed Height 32dp, Padding from Column
            Row(
                modifier = Modifier.fillMaxWidth().height(32.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Left side: Status Beacon + Status Label
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Surface(
                        modifier = Modifier.size(10.dp),
                        shape = CircleShape,
                        color = if (isChecking) MaterialTheme.colorScheme.outline else accentColor
                    ) {}
                    Text(
                        text = when {
                            !isRootAvailable -> context.getString(R.string.root_unavailable)
                            isChecking -> context.getString(R.string.backend_checking)
                            isWorking -> context.getString(R.string.backend_installed)
                            status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.backend_update_available)
                            else -> context.getString(R.string.backend_attention_required)
                        }.uppercase(),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.Black,
                        letterSpacing = 0.5.sp,
                        color = accentColor,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }

                // Right side: mode pill
                if (isWorking && backendMode != null) {
                    Surface(
                        color = accentColor.copy(alpha = 0.1f),
                        shape = RoundedCornerShape(8.dp),
                        border = BorderStroke(1.dp, accentColor.copy(alpha = 0.2f))
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                        ) {
                            Surface(modifier = Modifier.size(6.dp), shape = CircleShape, color = accentColor) {}
                            Text(
                                text = backendMode!!.uppercase(),
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.5.sp,
                                color = accentColor
                            )
                        }
                    }
                }
            }

            HorizontalDivider(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f)
            )

            // Wordmark and Version block
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = when {
                        isWorking || status == DroidspacesStatus.UpdateAvailable -> "DROIDSPACES"
                        !isRootAvailable -> context.getString(R.string.root_unavailable)
                        status == DroidspacesStatus.NotInstalled -> context.getString(R.string.backend_not_installed)
                        else -> context.getString(R.string.backend_corrupted)
                    },
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Black,
                    letterSpacing = (-0.5).sp
                )

                if (isWorking) {
                    Text(
                        text = "${context.getString(R.string.version).uppercase()}: ${droidspacesVersion ?: "---"}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 0.5.sp,
                        modifier = Modifier.padding(top = 2.dp)
                    )
                }
            }

            // Error banner
            if (isError) {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(10.dp),
                    color = accentColor.copy(alpha = 0.1f)
                ) {
                    Row(
                        modifier = Modifier.padding(10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = null,
                            tint = accentColor,
                            modifier = Modifier.size(16.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = when {
                                !isRootAvailable -> context.getString(R.string.grant_root_message)
                                status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.update_available_message)
                                else -> context.getString(R.string.tap_to_fix_system)
                            },
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Medium,
                            color = accentColor
                        )
                    }
                }
            }
        }
    }
}
