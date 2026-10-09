package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import android.os.Build
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.PreferencesManager
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun SystemInfoCard(
    refreshTrigger: Int = 0  // Increment this to trigger a refresh
) {
    val context = LocalContext.current
    // Start with the last known value so a fresh launch never shows a loading
    // row; the background read corrects it silently if it changed.
    var selinuxStatus by remember {
        mutableStateOf(
            SystemInfoManager.cachedSelinuxStatus
                ?: PreferencesManager.getInstance(context).cachedSelinuxStatus
                ?: context.getString(R.string.loading)
        )
    }

    // Use cached system info - instant access, no computation
    val kernelVersion = SystemInfoManager.kernelVersion
    val architecture = SystemInfoManager.architecture
    val androidVersion = SystemInfoManager.androidVersion

    // Live read on first compose and on every pull-to-refresh.
    LaunchedEffect(refreshTrigger) {
        selinuxStatus = SystemInfoManager.refreshSELinuxStatus()
    }

    val rows = listOf(
        Triple(Icons.Default.DeveloperBoard, R.string.kernel, kernelVersion),
        Triple(Icons.Default.Security, R.string.selinux_status, selinuxStatus),
        Triple(Icons.Default.Android, R.string.android_version, androidVersion),
        Triple(Icons.Default.BuildCircle, R.string.architecture, architecture),
        Triple(Icons.Default.Memory, R.string.supported_abis, Build.SUPPORTED_ABIS.joinToString(", "))
    )

    // Five facts about one device, so one segmented group rather than a card
    SegmentedGroup(count = rows.size) { index, shape ->
        val (icon, title, value) = rows[index]
        Surface(shape = shape, color = MaterialTheme.colorScheme.surfaceContainer) {
            ListItem(
                headlineContent = {
                    Text(context.getString(title), style = MaterialTheme.typography.bodyLargeEmphasized)
                },
                supportingContent = { Text(value) },
                leadingContent = { Icon(icon, contentDescription = null) },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent)
            )
        }
    }
}
