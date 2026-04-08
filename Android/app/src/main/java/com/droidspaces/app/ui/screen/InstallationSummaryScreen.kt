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

import androidx.compose.ui.res.stringResource

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
                title = { Text(stringResource(R.string.installation_setup_summary)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.back))
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
                    Text(stringResource(R.string.install_container), style = MaterialTheme.typography.labelLarge)
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
                text = stringResource(R.string.review_configuration),
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
                    SummaryItem(stringResource(R.string.tarball_label), tarballName, Icons.Default.Archive)
                    SummaryItem(stringResource(R.string.container_singular), config.name, Icons.Default.Storage)
                    SummaryItem(stringResource(R.string.hostname), config.hostname, Icons.Default.Computer)
                    if (config.useSparseImage && config.sparseImageSizeGB != null) {
                        SummaryItem(stringResource(R.string.storage_configuration), "${stringResource(R.string.sparse_image_configuration)} (${config.sparseImageSizeGB}GB)", Icons.Default.Storage)
                    } else {
                        SummaryItem(stringResource(R.string.storage_configuration), stringResource(R.string.directory_label), Icons.Default.Folder)
                    }
                    SummaryItem(stringResource(R.string.installation_path_label), "${Constants.CONTAINERS_BASE_PATH}/${com.droidspaces.app.util.ContainerManager.sanitizeContainerName(config.name)}", Icons.Default.Folder)

                    HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

                    Text(
                        text = stringResource(R.string.container_options),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )

                    if (config.disableIPv6) SummaryItem(stringResource(R.string.disable_ipv6), stringResource(R.string.enabled_legend), Icons.Default.NetworkCheck)
                    if (config.enableAndroidStorage) SummaryItem(stringResource(R.string.storage_option), stringResource(R.string.enabled_legend), Icons.Default.Storage)
                    if (config.enableHwAccess) SummaryItem(stringResource(R.string.hardware_access), stringResource(R.string.enabled_legend), Icons.Default.Devices)
                    if (config.enableTermuxX11) SummaryItem(stringResource(R.string.x11_option), stringResource(R.string.enabled_legend), painterResource(id = R.drawable.ic_x11))
                    if (config.selinuxPermissive) SummaryItem(stringResource(R.string.selinux_option), stringResource(R.string.enabled_legend), Icons.Default.Security)
                    if (config.volatileMode) SummaryItem(stringResource(R.string.volatile_mode), stringResource(R.string.enabled_legend), Icons.Default.AutoDelete)
                    if (config.runAtBoot) SummaryItem(stringResource(R.string.run_at_boot), stringResource(R.string.enabled_legend), Icons.Default.PowerSettingsNew)
                    if (config.allowUserNs) SummaryItem(stringResource(R.string.allow_user_ns), stringResource(R.string.enabled_legend), Icons.Default.Person)
                    if (config.forceCgroupv1) SummaryItem(stringResource(R.string.force_cgroupv1), stringResource(R.string.enabled_legend), Icons.Default.Layers)
                    if (config.blockNestedNs) SummaryItem(stringResource(R.string.manual_deadlock_shield), stringResource(R.string.enabled_legend), Icons.Default.GppBad)

                    fun countEnvVars(content: String?): Int {
                        if (content.isNullOrBlank()) return 0
                        return content.lines()
                            .map { it.trim() }
                            .count { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
                    }

                    val envCount = countEnvVars(config.envFileContent)
                    if (envCount > 0) {
                        SummaryItem(stringResource(R.string.environment_variables), stringResource(R.string.environment_variables_configured, envCount), Icons.Default.Code)
                    }

                    if (config.bindMounts.isNotEmpty()) {
                        config.bindMounts.forEach { mount ->
                            SummaryItem(stringResource(R.string.bind_mounts), "${mount.src} → ${mount.dest}", Icons.Default.Link)
                        }
                    }

                    if (!config.enableAndroidStorage &&
                        !config.enableHwAccess && !config.selinuxPermissive &&
                        !config.volatileMode && config.bindMounts.isEmpty() &&
                        !config.runAtBoot && !config.disableIPv6 &&
                        !config.allowUserNs && !config.forceCgroupv1 &&
                        !config.blockNestedNs && config.envFileContent.isNullOrBlank()) {
                        Text(
                            text = stringResource(R.string.no_options_enabled),
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

