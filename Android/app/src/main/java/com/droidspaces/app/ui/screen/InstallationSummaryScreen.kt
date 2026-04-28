package com.droidspaces.app.ui.screen

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
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
    val btnShape = RoundedCornerShape(20.dp)

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
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                tonalElevation = 0.dp
            ) {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp)
                        .navigationBarsPadding()
                        .clip(btnShape)
                        .clickable(
                            onClick = onInstall,
                            indication = rememberRipple(bounded = true),
                            interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                        ),
                    shape = btnShape,
                    color = MaterialTheme.colorScheme.primary,
                    tonalElevation = 0.dp
                ) {
                    Box(
                        modifier = Modifier.padding(vertical = 16.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(
                                Icons.Default.InstallMobile,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.onPrimary
                            )
                            Text(
                                stringResource(R.string.install_container),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onPrimary
                            )
                        }
                    }
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

            Spacer(modifier = Modifier.height(4.dp))

            // Summary Card
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                tonalElevation = 0.dp
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

                    HorizontalDivider(
                        modifier = Modifier.padding(vertical = 8.dp),
                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)
                    )

                    Text(
                        text = stringResource(R.string.options),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.padding(bottom = 4.dp)
                    )

                    if (config.disableIPv6) SummaryItem(stringResource(R.string.disable_ipv6), stringResource(R.string.enabled_legend), Icons.Default.NetworkCheck)
                    if (config.enableAndroidStorage) SummaryItem(stringResource(R.string.android_storage), stringResource(R.string.enabled_legend), Icons.Default.Storage)
                    if (config.enableHwAccess) SummaryItem(stringResource(R.string.hardware_access), stringResource(R.string.enabled_legend), Icons.Default.Devices)
                    if (!config.enableHwAccess && config.enableGpuMode) SummaryItem(stringResource(R.string.gpu_access), stringResource(R.string.enabled_legend), Icons.Default.Memory)
                    if (config.enableTermuxX11) SummaryItem(stringResource(R.string.termux_x11), stringResource(R.string.enabled_legend), painterResource(id = R.drawable.ic_x11))
                    if (config.selinuxPermissive) SummaryItem(stringResource(R.string.selinux_permissive), stringResource(R.string.enabled_legend), Icons.Default.Security)
                    if (config.volatileMode) SummaryItem(stringResource(R.string.volatile_mode), stringResource(R.string.enabled_legend), Icons.Default.AutoDelete)
                    if (config.runAtBoot) SummaryItem(stringResource(R.string.run_at_boot), stringResource(R.string.enabled_legend), Icons.Default.PowerSettingsNew)
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
                        !config.enableHwAccess && !config.enableGpuMode && !config.selinuxPermissive &&
                        !config.volatileMode && config.bindMounts.isEmpty() &&
                        !config.runAtBoot && !config.disableIPv6 &&
                        !config.forceCgroupv1 && !config.blockNestedNs &&
                        config.envFileContent.isNullOrBlank()) {
                        Text(
                            text = stringResource(R.string.no_options_enabled),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))
        }
    }
}

@Composable
private fun SummaryItem(
    label: String,
    value: String,
    icon: Any
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        when (icon) {
            is androidx.compose.ui.graphics.vector.ImageVector -> {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f)
                )
            }
            is androidx.compose.ui.graphics.painter.Painter -> {
                Icon(
                    painter = icon,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f)
                )
            }
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
            )
            Text(
                text = value,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium
            )
        }
    }
}
