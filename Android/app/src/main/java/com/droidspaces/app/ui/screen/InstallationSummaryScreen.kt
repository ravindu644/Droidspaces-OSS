package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.res.painterResource
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.Constants

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InstallationSummaryScreen(
    config: ContainerInfo,
    tarballName: String,
    onInstall: () -> Unit,
    onBack: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Installation Summary") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        },
        bottomBar = {
            Surface(
                tonalElevation = 2.dp,
                shadowElevation = 8.dp
            ) {
                Button(
                    onClick = onInstall,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp)
                        .navigationBarsPadding()
                        .height(56.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Icon(Icons.Default.InstallMobile, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Install Container", style = MaterialTheme.typography.labelLarge)
                }
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp, vertical = 24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text(
                text = "Review Configuration",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(8.dp))

            // Summary Card
            Card(
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    SummaryItem("Tarball", tarballName, Icons.Default.Archive)
                    SummaryItem("Container Name", config.name, Icons.Default.Storage)
                    SummaryItem("Hostname", config.hostname, Icons.Default.Computer)
                    if (config.useSparseImage && config.sparseImageSizeGB != null) {
                        SummaryItem("Storage Type", "Sparse Image (${config.sparseImageSizeGB}GB)", Icons.Default.Storage)
                    } else {
                        SummaryItem("Storage Type", "Directory", Icons.Default.Folder)
                    }
                    SummaryItem("Installation Path", "${Constants.CONTAINERS_BASE_PATH}/${com.droidspaces.app.util.ContainerManager.sanitizeContainerName(config.name)}", Icons.Default.Folder)

                    HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

                    Text(
                        text = "Options",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )

                    if (config.enableIPv6) SummaryItem("IPv6", "Enabled", Icons.Default.NetworkCheck)
                    if (config.enableAndroidStorage) SummaryItem("Android Storage", "Enabled", Icons.Default.Storage)
                    if (config.enableHwAccess) SummaryItem("Hardware Access", "Enabled", Icons.Default.Devices)
                    if (config.enableTermuxX11) SummaryItem("Termux X11", "Enabled", painterResource(id = R.drawable.ic_x11))
                    if (config.selinuxPermissive) SummaryItem("SELinux", "Permissive", Icons.Default.Security)
                    if (config.volatileMode) SummaryItem("Volatile Mode", "Enabled", Icons.Default.AutoDelete)
                    if (config.runAtBoot) SummaryItem("Run at Boot", "Enabled", Icons.Default.PowerSettingsNew)

                    fun countEnvVars(content: String?): Int {
                        if (content.isNullOrBlank()) return 0
                        return content.lines()
                            .map { it.trim() }
                            .count { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
                    }

                    val envCount = countEnvVars(config.envFileContent)
                    if (envCount > 0) {
                        SummaryItem("Environment Variables", "$envCount configured", Icons.Default.Code)
                    }

                    if (config.bindMounts.isNotEmpty()) {
                        config.bindMounts.forEach { mount ->
                            SummaryItem("Bind Mount", "${mount.src} → ${mount.dest}", Icons.Default.Link)
                        }
                    }

                    if (!config.enableIPv6 && !config.enableAndroidStorage &&
                        !config.enableHwAccess && !config.selinuxPermissive && 
                        !config.volatileMode && config.bindMounts.isEmpty() && 
                        !config.runAtBoot && config.envFileContent.isNullOrBlank()) {
                        Text(
                            text = "No additional options enabled",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

@Composable
private fun SummaryItem(
    label: String,
    value: String,
    icon: Any // Can be ImageVector or Painter
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        when (icon) {
            is androidx.compose.ui.graphics.vector.ImageVector -> {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }
            is androidx.compose.ui.graphics.painter.Painter -> {
                Icon(
                    painter = icon,
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
            Text(
                text = value,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium
            )
        }
    }
}

