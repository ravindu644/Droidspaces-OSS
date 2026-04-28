package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import android.os.Build
import com.droidspaces.app.util.SystemInfoManager
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@Composable
fun SystemInfoCard(
    refreshTrigger: Int = 0  // Increment this to trigger a refresh
) {
    val context = LocalContext.current
    // Start with cached value if available (instant display)
    var selinuxStatus by remember {
        mutableStateOf(SystemInfoManager.cachedSelinuxStatus ?: context.getString(R.string.loading))
    }

    // Use cached system info - instant access, no computation
    val kernelVersion = SystemInfoManager.kernelVersion
    val architecture = SystemInfoManager.architecture
    val androidVersion = SystemInfoManager.androidVersion

    // Load SELinux status in background (non-blocking)
    // Re-runs when refreshTrigger changes (on pull-to-refresh)
    LaunchedEffect(refreshTrigger) {
        selinuxStatus = if (refreshTrigger > 0) {
            // Force refresh - bypass cache
            SystemInfoManager.refreshSELinuxStatus()
        } else {
            // Initial load - use cache if available
            SystemInfoManager.getSELinuxStatus()
        }
    }

    Surface(
        color = MaterialTheme.colorScheme.surfaceContainer,
        shape = RoundedCornerShape(20.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp)
    ) {
        // MMRL-style: items stacked directly with internal padding
        Column(
            modifier = Modifier.fillMaxWidth()
        ) {
            // 1. Kernel
            SystemInfoItem(
                icon = Icons.Default.DeveloperBoard,
                title = context.getString(R.string.kernel),
                description = kernelVersion
            )

            // 2. SELinux Status
            SystemInfoItem(
                icon = Icons.Default.Security,
                title = context.getString(R.string.selinux_status),
                description = selinuxStatus
            )

            // 3. Android Version
            SystemInfoItem(
                icon = Icons.Default.Android,
                title = context.getString(R.string.android_version),
                description = androidVersion
            )

            // 4. Architecture
            SystemInfoItem(
                icon = Icons.Default.BuildCircle,
                title = context.getString(R.string.architecture),
                description = architecture
            )

            // 5. Supported ABIs
            SystemInfoItem(
                icon = Icons.Default.Memory,
                title = context.getString(R.string.supported_abis),
                description = Build.SUPPORTED_ABIS.joinToString(", ")
            )
        }
    }
}

@Composable
private fun SystemInfoItem(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    description: String
) {
    // MMRL-style padding: vertical = 16.dp, horizontal = 25.dp
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 16.dp, horizontal = 25.dp)
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(24.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.width(16.dp))
        Column(
            // MMRL-style: 4.dp spacing between title and description
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
            Text(
                text = description,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
        }
    }
}
