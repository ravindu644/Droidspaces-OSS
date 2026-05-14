package com.droidspaces.app.ui.screen

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.component.ToggleCard
import com.droidspaces.app.ui.component.DsDropdown
import androidx.compose.material.icons.filled.Public
import com.droidspaces.app.ui.component.UpstreamInterfaceList
import com.droidspaces.app.ui.component.PortForwardingList
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside

import androidx.compose.ui.text.style.TextOverflow
import com.droidspaces.app.util.BindMount
import com.droidspaces.app.util.PortForward
import com.droidspaces.app.util.ContainerManager
import kotlinx.coroutines.launch
import com.droidspaces.app.ui.component.FilePickerDialog
import com.droidspaces.app.ui.component.SettingsRowCard
import com.droidspaces.app.ui.component.EnvironmentVariablesDialog
import com.droidspaces.app.ui.component.PrivilegedModeDialog
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.LinearEasing
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.foundation.shape.RoundedCornerShape
import kotlinx.coroutines.delay
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ContainerConfigScreen(
    initialNetMode: String = "host",
    initialDisableIPv6: Boolean = false,
    initialEnableAndroidStorage: Boolean = false,
    initialEnableHwAccess: Boolean = false,
    initialEnableGpuMode: Boolean = false,
    initialEnableTermuxX11: Boolean = false,
    initialSelinuxPermissive: Boolean = false,
    initialVolatileMode: Boolean = false,
    initialBindMounts: List<BindMount> = emptyList(),
    initialDnsServers: String = "",
    initialRunAtBoot: Boolean = false,
    initialForceCgroupv1: Boolean = false,
    initialBlockNestedNs: Boolean = false,
    initialPrivileged: String = "",
    initialEnvFileContent: String = "",
    initialUpstreamInterfaces: List<String> = emptyList(),
    initialPortForwards: List<PortForward> = emptyList(),
    onNext: (
        netMode: String,
        disableIPv6: Boolean,
        enableAndroidStorage: Boolean,
        enableHwAccess: Boolean,
        enableGpuMode: Boolean,
        enableTermuxX11: Boolean,
        selinuxPermissive: Boolean,
        volatileMode: Boolean,
        bindMounts: List<BindMount>,
        dnsServers: String,
        runAtBoot: Boolean,
        forceCgroupv1: Boolean,
        blockNestedNs: Boolean,
        privileged: String,
        envFileContent: String?,
        upstreamInterfaces: List<String>,
        portForwards: List<PortForward>
    ) -> Unit,
    onBack: () -> Unit
) {
    var netMode by remember { mutableStateOf(initialNetMode) }
    var disableIPv6 by remember { mutableStateOf(initialDisableIPv6) }
    var enableAndroidStorage by remember { mutableStateOf(initialEnableAndroidStorage) }
    var enableHwAccess by remember { mutableStateOf(initialEnableHwAccess) }
    var enableGpuMode by remember { mutableStateOf(initialEnableGpuMode) }
    var enableTermuxX11 by remember { mutableStateOf(initialEnableTermuxX11) }
    var selinuxPermissive by remember { mutableStateOf(initialSelinuxPermissive) }
    var volatileMode by remember { mutableStateOf(initialVolatileMode) }
    var bindMounts by remember { mutableStateOf(initialBindMounts) }
    var dnsServers by remember { mutableStateOf(initialDnsServers) }
    var runAtBoot by remember { mutableStateOf(initialRunAtBoot) }
    var forceCgroupv1 by remember { mutableStateOf(initialForceCgroupv1) }
    var blockNestedNs by remember { mutableStateOf(initialBlockNestedNs) }
    var envFileContent by remember { mutableStateOf(initialEnvFileContent) }
    var upstreamInterfaces by remember { mutableStateOf(initialUpstreamInterfaces) }
    var portForwards by remember { mutableStateOf(initialPortForwards) }
    var privileged by remember { mutableStateOf(initialPrivileged) }
    val context = LocalContext.current

    // Internal UI States
    var showFilePicker by remember { mutableStateOf(false) }
    var showDestDialog by remember { mutableStateOf(false) }
    var tempSrcPath by remember { mutableStateOf("") }

    if (showFilePicker) {
        FilePickerDialog(
            onDismiss = { showFilePicker = false },
            onConfirm = { path ->
                tempSrcPath = path
                showFilePicker = false
                showDestDialog = true
            }
        )
    }

    if (showDestDialog) {
        var destPath by remember { mutableStateOf("") }
        val clearFocus = com.droidspaces.app.ui.util.rememberClearFocus()
        Dialog(
            onDismissRequest = { showDestDialog = false },
            properties = DialogProperties(usePlatformDefaultWidth = false)
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 24.dp)
                    .imePadding(),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surfaceVariant,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                tonalElevation = 0.dp
            ) {
                Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    Text(context.getString(R.string.enter_container_path), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                    OutlinedTextField(
                        value = destPath,
                        onValueChange = { destPath = it },
                        label = { Text(context.getString(R.string.container_path_placeholder)) },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(16.dp),
                        colors = OutlinedTextFieldDefaults.colors(
                            unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                            focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLow,
                            focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                        )
                    )
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Surface(
                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = { clearFocus(); showDestDialog = false }),
                            shape = RoundedCornerShape(14.dp),
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                            tonalElevation = 0.dp
                        ) {
                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                            }
                        }
                        Surface(
                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(
                                enabled = destPath.startsWith("/"),
                                onClick = {
                                    clearFocus()
                                    if (destPath.isNotBlank()) {
                                        bindMounts = bindMounts + BindMount(tempSrcPath, destPath)
                                        showDestDialog = false
                                    }
                                }
                            ),
                            shape = RoundedCornerShape(14.dp),
                            color = if (destPath.startsWith("/")) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                            tonalElevation = 0.dp
                        ) {
                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                Text(
                                    context.getString(R.string.ok),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = if (destPath.startsWith("/")) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    var showEnvDialog by remember { mutableStateOf(false) }
    var showPrivilegedDialog by remember { mutableStateOf(false) }

    if (showPrivilegedDialog) {
        PrivilegedModeDialog(
            initialPrivileged = privileged,
            onConfirm = { tags ->
                privileged = tags
                showPrivilegedDialog = false
            },
            onDismiss = { showPrivilegedDialog = false }
        )
    }

    if (showEnvDialog) {
        EnvironmentVariablesDialog(
            initialContent = envFileContent,
            onConfirm = { newContent ->
                envFileContent = newContent
                showEnvDialog = false
            },
            onDismiss = { showEnvDialog = false }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.configuration_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            val btnShape = RoundedCornerShape(20.dp)
            val isUpstreamValid = netMode != "nat" || upstreamInterfaces.isNotEmpty()
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceContainer,
                tonalElevation = 0.dp
            ) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    HorizontalDivider(
                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
                        thickness = 1.dp
                    )
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(24.dp)
                            .navigationBarsPadding()
                            .clip(btnShape)
                            .clickable(
                                enabled = isUpstreamValid,
                                onClick = {
                                    onNext(netMode, disableIPv6, enableAndroidStorage, enableHwAccess, enableGpuMode, enableTermuxX11, selinuxPermissive, volatileMode, bindMounts, dnsServers, runAtBoot, forceCgroupv1, blockNestedNs, privileged, if (envFileContent.isBlank()) null else envFileContent, upstreamInterfaces, portForwards)
                                },
                                indication = androidx.compose.material.ripple.rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = btnShape,
                        color = if (isUpstreamValid) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp),
                                    tint = if (isUpstreamValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                                Text(
                                    context.getString(R.string.next_storage),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = if (isUpstreamValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                            }
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        ClearFocusOnClickOutside(
            modifier = Modifier
                .padding(innerPadding)
                .consumeWindowInsets(innerPadding)
                .imePadding()
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 24.dp)
                    .padding(top = 24.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
            Text(
                text = context.getString(R.string.container_options),
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = context.getString(R.string.cat_networking),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier.padding(top = 8.dp)
            )

            DsDropdown(
                label = context.getString(R.string.network_mode),
                selected = netMode,
                options = listOf("host", "nat", "none"),
                displayName = { context.getString(when (it) { "nat" -> R.string.network_mode_nat; "none" -> R.string.network_mode_none; else -> R.string.network_mode_host }) },
                onSelect = { mode -> netMode = mode; if (mode != "host") disableIPv6 = false },
                leadingIcon = Icons.Default.Public
            )

            androidx.compose.animation.AnimatedVisibility(
                visible = netMode == "nat",
                enter = androidx.compose.animation.expandVertically(
                    animationSpec = tween(durationMillis = 300, easing = androidx.compose.animation.core.FastOutSlowInEasing),
                    expandFrom = Alignment.Top
                ) + androidx.compose.animation.fadeIn(animationSpec = tween(durationMillis = 300)),
                exit = androidx.compose.animation.shrinkVertically(
                    animationSpec = tween(durationMillis = 300, easing = androidx.compose.animation.core.FastOutSlowInEasing),
                    shrinkTowards = Alignment.Top
                ) + androidx.compose.animation.fadeOut(animationSpec = tween(durationMillis = 300))
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Text(
                        text = context.getString(R.string.nat_settings),
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.primary
                    )

                    // Upstream Interfaces
                    val isUpstreamValid = upstreamInterfaces.isNotEmpty()
                    Text(
                        text = context.getString(R.string.upstream_interfaces_mandatory),
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Bold,
                        color = if (!isUpstreamValid) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
                    )

                    if (!isUpstreamValid) {
                        Text(
                            text = context.getString(R.string.upstream_interfaces_required_error),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error
                        )
                    }

                    UpstreamInterfaceList(
                        upstreamInterfaces = upstreamInterfaces,
                        onInterfacesChange = { upstreamInterfaces = it }
                    )

                    // Port Forwards
                    Text(
                        text = context.getString(R.string.port_forwarding),
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 16.dp)
                    )

                    PortForwardingList(
                        portForwards = portForwards,
                        onPortForwardsChange = { portForwards = it }
                    )
                }
            }

            // DNS Servers input
            val isDnsError = remember(dnsServers) {
                dnsServers.isNotEmpty() && !dnsServers.all { it.isDigit() || it == '.' || it == ':' || it == ',' }
            }

            OutlinedTextField(
                value = dnsServers,
                onValueChange = { dnsServers = it },
                label = { Text(context.getString(R.string.dns_servers_label)) },
                supportingText = {
                    if (isDnsError) {
                        Text(context.getString(R.string.dns_servers_hint))
                    }
                },
                isError = isDnsError,
                placeholder = { Text(context.getString(R.string.dns_servers_placeholder)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                shape = RoundedCornerShape(16.dp),
                colors = OutlinedTextFieldDefaults.colors(
                    unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                    focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerLow,
                    focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                ),
                leadingIcon = {
                    Icon(Icons.Default.Dns, contentDescription = null)
                }
            )

            // In NAT/NONE mode, IPv6 is always disabled (forced). In host mode the user can opt in.
            val ipv6IsForced = netMode != "host"
            ToggleCard(
                icon = Icons.Default.NetworkCheck,
                title = context.getString(R.string.disable_ipv6),
                description = if (ipv6IsForced)
                    context.getString(R.string.disable_ipv6_nat_forced)
                else
                    context.getString(R.string.disable_ipv6_description),
                checked = if (ipv6IsForced) true else disableIPv6,
                onCheckedChange = { disableIPv6 = it },
                enabled = !ipv6IsForced
            )

            Text(
                text = context.getString(R.string.cat_integration),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier.padding(top = 16.dp)
            )

            ToggleCard(
                icon = Icons.Default.Storage,
                title = context.getString(R.string.android_storage),
                description = context.getString(R.string.android_storage_description),
                checked = enableAndroidStorage,
                onCheckedChange = { enableAndroidStorage = it }
            )

            ToggleCard(
                icon = Icons.Default.Devices,
                title = context.getString(R.string.hardware_access),
                description = context.getString(R.string.hardware_access_description),
                checked = enableHwAccess,
                onCheckedChange = { enableHwAccess = it }
            )

            ToggleCard(
                icon = Icons.Default.Memory,
                title = context.getString(R.string.gpu_access),
                description = context.getString(R.string.gpu_access_description),
                checked = if (enableHwAccess) true else enableGpuMode,
                onCheckedChange = { if (!enableHwAccess) enableGpuMode = it },
                enabled = !enableHwAccess
            )

            ToggleCard(
                painter = androidx.compose.ui.res.painterResource(R.drawable.ic_x11),
                title = context.getString(R.string.termux_x11),
                description = context.getString(R.string.termux_x11_description),
                checked = enableTermuxX11,
                onCheckedChange = { enableTermuxX11 = it },
                enabled = true
            )

            Text(
                text = context.getString(R.string.cat_security),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier.padding(top = 16.dp)
            )

            ToggleCard(
                icon = Icons.Default.Security,
                title = context.getString(R.string.selinux_permissive),
                description = context.getString(R.string.selinux_permissive_description),
                checked = selinuxPermissive,
                onCheckedChange = { selinuxPermissive = it }
            )

            ToggleCard(
                icon = Icons.Default.AutoDelete,
                title = context.getString(R.string.volatile_mode),
                description = context.getString(R.string.volatile_mode_description),
                checked = volatileMode,
                onCheckedChange = { volatileMode = it }
            )

            ToggleCard(
                icon = Icons.Default.Cyclone,
                title = context.getString(R.string.force_cgroupv1),
                description = context.getString(R.string.force_cgroupv1_description),
                checked = forceCgroupv1,
                onCheckedChange = { forceCgroupv1 = it }
            )

            val isSeccompDisabled = privileged.contains("noseccomp") || privileged.contains("full")
            LaunchedEffect(isSeccompDisabled) {
                if (isSeccompDisabled) blockNestedNs = false
            }

            ToggleCard(
                icon = Icons.Default.GppBad,
                title = context.getString(R.string.manual_deadlock_shield),
                description = context.getString(R.string.manual_deadlock_shield_description),
                checked = if (isSeccompDisabled) false else blockNestedNs,
                onCheckedChange = { blockNestedNs = it },
                enabled = !isSeccompDisabled
            )

            SettingsRowCard(
                title = context.getString(R.string.privileged_mode),
                subtitle = if (privileged.isEmpty()) context.getString(R.string.not_configured) else privileged,
                description = context.getString(R.string.privileged_mode_description),
                icon = Icons.Default.GppMaybe,
                onClick = {
                    showPrivilegedDialog = true
                }
            )

            Text(
                text = context.getString(R.string.cat_advanced),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier.padding(top = 16.dp)
            )

            // Environment Variables Row
            fun countEnvVars(content: String): Int {
                return content.lines()
                    .map { it.trim() }
                    .count { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
            }

            val envCount = countEnvVars(envFileContent)
            val envSubtitle = if (envCount > 0) {
                context.getString(R.string.environment_variables_configured, envCount)
            } else {
                context.getString(R.string.not_configured)
            }

            SettingsRowCard(
                title = context.getString(R.string.environment_variables),
                subtitle = envSubtitle,
                icon = Icons.Default.Code,
                onClick = {
                    showEnvDialog = true
                }
            )


            // Bind Mounts Section
            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = context.getString(R.string.bind_mounts),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
            }

            bindMounts.forEach { mount ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                    )
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = context.getString(R.string.host_path, mount.src),
                                style = MaterialTheme.typography.bodyMedium,
                                overflow = TextOverflow.Ellipsis,
                                maxLines = 1
                            )
                            Text(
                                text = context.getString(R.string.container_path, mount.dest),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.secondary,
                                overflow = TextOverflow.Ellipsis,
                                maxLines = 1
                            )
                        }
                        IconButton(onClick = {
                            bindMounts = bindMounts - mount
                        }) {
                            Icon(Icons.Default.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                        }
                    }
                }
            }

            OutlinedButton(
                onClick = { showFilePicker = true },
                modifier = Modifier.fillMaxWidth(),
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
            ) {
                Icon(Icons.Default.Add, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text(context.getString(R.string.add_bind_mount))
            }
            ToggleCard(
                icon = Icons.Default.PowerSettingsNew,
                title = context.getString(R.string.run_at_boot),
                description = context.getString(R.string.run_at_boot_description),
                checked = runAtBoot,
                onCheckedChange = { runAtBoot = it }
            )

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}
}
