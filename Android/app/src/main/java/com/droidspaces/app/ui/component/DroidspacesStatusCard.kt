package com.droidspaces.app.ui.component

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

enum class DroidspacesStatus {
    Working,
    UpdateAvailable,
    NotInstalled,
    Unsupported,
    Corrupted,
    ModuleMissing
}

@OptIn(ExperimentalFoundationApi::class)
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

    // Sync state for instant UI (killing the 1cm shift glitch)
    val cachedDroidVer = remember(context) { SystemInfoManager.getCachedDroidspacesVersion(context) }
    val cachedMode = remember(context) { SystemInfoManager.getCachedBackendMode(context) }
    val cachedRoot = remember(context) { SystemInfoManager.getCachedRootProviderVersion(context) }

    var rootProviderVersion by remember { mutableStateOf(cachedRoot) }
    var droidspacesVersion by remember { mutableStateOf(version ?: cachedDroidVer) }
    var backendMode by remember { mutableStateOf(cachedMode) }

    LaunchedEffect(status, refreshTrigger) {
        if (status == DroidspacesStatus.Working || status == DroidspacesStatus.UpdateAvailable) {
            rootProviderVersion = SystemInfoManager.getRootProviderVersion(context)
            droidspacesVersion = SystemInfoManager.getDroidspacesVersion(context)
            backendMode = SystemInfoManager.getBackendMode(context)
        }
    }

    val isError = !isRootAvailable || status != DroidspacesStatus.Working
    val accentColor = when {
        !isRootAvailable -> MaterialTheme.colorScheme.error
        status == DroidspacesStatus.Working -> MaterialTheme.colorScheme.primary
        else -> MaterialTheme.colorScheme.error
    }

    val cardShape = RoundedCornerShape(24.dp)
    
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f))
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Header Row
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    // Status Beacon
                    Surface(
                        modifier = Modifier.size(12.dp),
                        shape = androidx.compose.foundation.shape.CircleShape,
                        color = if (isChecking) MaterialTheme.colorScheme.outline else accentColor
                    ) {}
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = when {
                            !isRootAvailable -> context.getString(R.string.root_unavailable)
                            isChecking -> context.getString(R.string.backend_checking)
                            status == DroidspacesStatus.Working -> context.getString(R.string.backend_installed)
                            else -> context.getString(R.string.backend_attention_required)
                        }.uppercase(),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.Black,
                        letterSpacing = 1.sp,
                        color = if (isError) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                    )
                }
                
                if (backendMode != null) {
                    Surface(
                        color = accentColor.copy(alpha = 0.1f),
                        shape = RoundedCornerShape(8.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, accentColor.copy(alpha = 0.2f))
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                        ) {
                            Surface(
                                modifier = Modifier.size(6.dp),
                                shape = androidx.compose.foundation.shape.CircleShape,
                                color = accentColor
                            ) {}
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

            // Main Info
            Column {
                Text(
                    text = if (isError && isRootAvailable) {
                        when (status) {
                            DroidspacesStatus.UpdateAvailable -> context.getString(R.string.backend_update_available)
                            DroidspacesStatus.NotInstalled -> context.getString(R.string.backend_not_installed)
                            else -> context.getString(R.string.backend_corrupted)
                        }
                    } else if (!isRootAvailable) {
                        context.getString(R.string.root_unavailable)
                    } else {
                        "DROIDSPACES"
                    },
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.ExtraBold
                )
                
                if (status == DroidspacesStatus.Working && isRootAvailable) {
                    Text(
                        text = context.getString(R.string.version_label, droidspacesVersion ?: "---"),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }
            }

            // Footer / Action Bar
            if (isError) {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(12.dp),
                    color = accentColor.copy(alpha = 0.1f)
                ) {
                    Row(
                        modifier = Modifier.padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = null,
                            tint = accentColor,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = if (!isRootAvailable) context.getString(R.string.grant_root_message) else context.getString(R.string.tap_to_fix_system),
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Medium,
                            color = accentColor
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center
                ) {
                    InfoDetail(
                        label = context.getString(R.string.root_provider_label, "").trim().replace(":", ""),
                        value = rootProviderVersion ?: "---"
                    )
                }
            }
        }
    }
}

@Composable
private fun InfoDetail(label: String, value: String) {
    Column {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f),
            fontWeight = FontWeight.Bold,
            letterSpacing = 0.5.sp
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Bold
        )
    }
}

