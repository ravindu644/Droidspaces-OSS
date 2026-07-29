package com.droidspaces.app.ui.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoDelete
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.Cyclone
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.DesktopWindows
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Dns
import androidx.compose.material.icons.filled.GppBad
import androidx.compose.material.icons.filled.GppMaybe
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.NetworkCheck
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Public
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.util.BindMount
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.GatewayErrors
import com.droidspaces.app.util.ValidationUtils

/**
 * The single, shared container-configuration form used by both the Create wizard
 * ([com.droidspaces.app.ui.screen.ContainerConfigScreen]) and the Edit screen
 * ([com.droidspaces.app.ui.screen.EditContainerScreen]).
 *
 * State is fully hoisted: the caller owns a [ContainerConfigState] and receives
 * every edit via [onStateChange]. Transient UI (dialog visibility, NAT octet
 * text) stays local. [gatewayErrors]/[collisionContainer] are computed by the
 * caller (which also needs them to gate its action button) and passed in for
 * display. [leadingContent] renders caller-specific header rows (e.g. the Edit
 * screen's hostname field) at the top of the scrolling column.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerConfigForm(
    state: ContainerConfigState,
    onStateChange: (ContainerConfigState) -> Unit,
    installedContainers: List<ContainerInfo>,
    selfName: String,
    gatewayErrors: GatewayErrors,
    collisionContainer: ContainerInfo?,
    modifier: Modifier = Modifier,
    leadingContent: @Composable ColumnScope.() -> Unit = {},
) {
    val context = LocalContext.current
    val clearFocus = rememberClearFocus()

    var showFilePicker by remember { mutableStateOf(false) }
    var showDestDialog by remember { mutableStateOf(false) }
    var tempSrcPath by remember { mutableStateOf("") }
    var showEnvDialog by remember { mutableStateOf(false) }
    var showPrivilegedDialog by remember { mutableStateOf(false) }
    var showHwAccessDialog by remember { mutableStateOf(false) }

    val modernFieldShape = RoundedCornerShape(16.dp)
    val modernFieldColors = DsTextFieldDefaults.colors()

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
        var roEnabled by remember { mutableStateOf(false) }
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
                color = MaterialTheme.colorScheme.surfaceContainer,
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
                        shape = modernFieldShape,
                        colors = modernFieldColors
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(context.getString(R.string.read_only), style = MaterialTheme.typography.bodyMedium)
                        Switch(checked = roEnabled, onCheckedChange = { roEnabled = it })
                    }
                    DialogFooterRow(
                        dismissLabel = context.getString(R.string.cancel),
                        confirmLabel = context.getString(R.string.ok),
                        onDismiss = { clearFocus(); showDestDialog = false },
                        onConfirm = {
                            clearFocus()
                            if (destPath.isNotBlank()) {
                                onStateChange(state.copy(bindMounts = state.bindMounts + BindMount(tempSrcPath, destPath, roEnabled)))
                                showDestDialog = false
                            }
                        },
                        confirmEnabled = destPath.startsWith("/")
                    )
                }
            }
        }
    }

    if (showPrivilegedDialog) {
        PrivilegedModeDialog(
            initialPrivileged = state.privileged,
            onConfirm = { tags ->
                onStateChange(state.copy(privileged = tags))
                showPrivilegedDialog = false
            },
            onDismiss = { showPrivilegedDialog = false }
        )
    }

    if (showHwAccessDialog) {
        HardwareAccessDialog(
            onConfirm = {
                onStateChange(state.copy(enableHwAccess = true))
                showHwAccessDialog = false
            },
            onDismiss = { showHwAccessDialog = false }
        )
    }

    if (showEnvDialog) {
        EnvironmentVariablesDialog(
            initialContent = state.envFileContent,
            onConfirm = { newContent ->
                onStateChange(state.copy(envFileContent = newContent))
                showEnvDialog = false
            },
            onDismiss = { showEnvDialog = false }
        )
    }

    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp)
            .padding(top = 8.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        leadingContent()

        Text(
            text = context.getString(R.string.cat_networking),
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(top = 8.dp)
        )

        DsDropdown(
            label = context.getString(R.string.network_mode),
            selected = state.netMode,
            options = listOf("nat", "host", "none", "gateway"),
            displayName = { context.getString(when (it) { "nat" -> R.string.network_mode_nat; "none" -> R.string.network_mode_none; "gateway" -> R.string.network_mode_gateway; else -> R.string.network_mode_host }) },
            onSelect = { mode ->
                clearFocus()
                onStateChange(state.copy(netMode = mode, disableIPv6 = if (mode != "host") false else state.disableIPv6))
            },
            leadingIcon = Icons.Default.Public
        )

        GatewaySettingsSection(
            visible = state.netMode == "gateway",
            config = GatewayConfig(state.gatewayContainer, state.gatewayNet, state.gatewayIface, state.gatewayBridge),
            onConfigChange = { c ->
                // Preserve original behavior: clear focus only on gateway-container
                // selection (a dropdown pick), not while typing net/iface/bridge.
                if (c.container != state.gatewayContainer) clearFocus()
                onStateChange(state.copy(gatewayContainer = c.container, gatewayNet = c.net, gatewayIface = c.iface, gatewayBridge = c.bridge))
            },
            selfName = selfName,
            installedContainers = installedContainers,
            errors = gatewayErrors
        )

        if (state.netMode == "nat") {
            Column(
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = context.getString(R.string.nat_settings),
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.primary
                )

                Text(
                    text = context.getString(R.string.static_ip_address),
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(top = 16.dp)
                )
                Text(
                    text = context.getString(R.string.static_ip_description),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.secondary,
                    modifier = Modifier.padding(bottom = 8.dp)
                )

                val octets = remember(state.staticNatIp) {
                    val parts = state.staticNatIp.split(".")
                    if (parts.size == 4) Pair(parts[2], parts[3]) else Pair("", "")
                }
                var octet3 by remember(octets) { mutableStateOf(octets.first) }
                var octet4 by remember(octets) { mutableStateOf(octets.second) }

                val updateIp = { o3: String, o4: String ->
                    onStateChange(
                        state.copy(
                            staticNatIp = if (o3.isBlank() && o4.isBlank()) "" else "${Constants.NAT_IP_PREFIX}.$o3.$o4"
                        )
                    )
                }

                val isOctet3Valid = remember(octet3) {
                    octet3.isEmpty() || (octet3.toIntOrNull()?.let { it in Constants.NAT_OCTET_MIN..Constants.NAT_OCTET_MAX } ?: false)
                }
                val isOctet4Valid = remember(octet4) {
                    octet4.isEmpty() || (octet4.toIntOrNull()?.let { it in Constants.NAT_OCTET_MIN..Constants.NAT_OCTET_MAX } ?: false)
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "${Constants.NAT_IP_PREFIX}.",
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                    OutlinedTextField(
                        value = octet3,
                        onValueChange = {
                            if (it.length <= 3 && it.all { c -> c.isDigit() }) {
                                octet3 = it
                                updateIp(it, octet4)
                            }
                        },
                        label = { Text(context.getString(R.string.octet_label, 3)) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                        shape = modernFieldShape,
                        colors = modernFieldColors,
                        isError = !isOctet3Valid,
                        supportingText = { if (!isOctet3Valid) Text(context.getString(R.string.error_octet_range)) },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
                    )
                    Text(
                        text = ".",
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                    OutlinedTextField(
                        value = octet4,
                        onValueChange = {
                            if (it.length <= 3 && it.all { c -> c.isDigit() }) {
                                octet4 = it
                                updateIp(octet3, it)
                            }
                        },
                        label = { Text(context.getString(R.string.octet_label, 4)) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                        shape = modernFieldShape,
                        colors = modernFieldColors,
                        isError = !isOctet4Valid,
                        supportingText = { if (!isOctet4Valid) Text(context.getString(R.string.error_octet_range)) },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
                    )
                }

                if (collisionContainer != null) {
                    Text(
                        text = context.getString(R.string.error_ip_collision, collisionContainer.name),
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }

                Text(
                    text = context.getString(R.string.upstream_interface_title),
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = context.getString(R.string.upstream_interface_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                UpstreamInterfaceList(
                    upstreamInterfaces = state.upstreamInterfaces,
                    onInterfacesChange = { onStateChange(state.copy(upstreamInterfaces = it)) }
                )

                Text(
                    text = context.getString(R.string.port_forwarding),
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(top = 16.dp)
                )
                PortForwardingList(
                    portForwards = state.portForwards,
                    onPortForwardsChange = { onStateChange(state.copy(portForwards = it)) }
                )
            }
        }

        HorizontalDivider(
            color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
            thickness = 1.dp
        )

        val isDnsError = remember(state.dnsServers) {
            state.dnsServers.isNotEmpty() && !state.dnsServers.all { it.isDigit() || it == '.' || it == ':' || it == ',' }
        }
        OutlinedTextField(
            value = state.dnsServers,
            onValueChange = { onStateChange(state.copy(dnsServers = it)) },
            label = { Text(context.getString(R.string.dns_servers_label)) },
            supportingText = { if (isDnsError) Text(context.getString(R.string.dns_servers_hint)) },
            isError = isDnsError,
            placeholder = { Text(context.getString(R.string.dns_servers_placeholder)) },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            shape = modernFieldShape,
            colors = modernFieldColors,
            leadingIcon = { Icon(Icons.Default.Dns, contentDescription = null) }
        )

        val ipv6IsForced = state.netMode != "host"
        ToggleCard(
            icon = Icons.Default.NetworkCheck,
            title = context.getString(R.string.disable_ipv6),
            description = if (ipv6IsForced) context.getString(R.string.disable_ipv6_nat_forced) else context.getString(R.string.disable_ipv6_description),
            checked = if (ipv6IsForced) true else state.disableIPv6,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(disableIPv6 = it)) },
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
            checked = state.enableAndroidStorage,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableAndroidStorage = it)) }
        )

        ToggleCard(
            icon = Icons.Default.Devices,
            title = context.getString(R.string.hardware_access),
            description = context.getString(R.string.hardware_access_description),
            checked = state.enableHwAccess,
            onCheckedChange = { newValue ->
                clearFocus()
                if (newValue) showHwAccessDialog = true else onStateChange(state.copy(enableHwAccess = false))
            }
        )

        ToggleCard(
            icon = Icons.Default.Memory,
            title = context.getString(R.string.gpu_access),
            description = context.getString(R.string.gpu_access_description),
            checked = if (state.enableHwAccess) true else state.enableGpuMode,
            onCheckedChange = { if (!state.enableHwAccess) { clearFocus(); onStateChange(state.copy(enableGpuMode = it)) } },
            enabled = !state.enableHwAccess
        )

        ToggleCard(
            painter = painterResource(R.drawable.ic_x11),
            title = context.getString(R.string.termux_x11),
            description = context.getString(R.string.termux_x11_description),
            checked = state.enableTermuxX11,
            onCheckedChange = { onStateChange(state.copy(enableTermuxX11 = it)) },
            enabled = true
        )

        ToggleCard(
            icon = Icons.Default.DesktopWindows,
            title = context.getString(R.string.enable_anland),
            description = context.getString(R.string.enable_anland_description),
            checked = state.enableAnland,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableAnland = it)) },
            enabled = true
        )

        ToggleCard(
            icon = Icons.Default.Layers,
            title = context.getString(R.string.enable_virgl),
            description = context.getString(R.string.enable_virgl_description),
            checked = state.enableVirgl,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableVirgl = it)) },
            enabled = true
        )

        ToggleCard(
            icon = Icons.AutoMirrored.Filled.VolumeUp,
            title = context.getString(R.string.enable_pulseaudio),
            description = context.getString(R.string.enable_pulseaudio_description),
            checked = state.enablePulseaudio,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enablePulseaudio = it)) },
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
            checked = state.selinuxPermissive,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(selinuxPermissive = it)) }
        )

        val isSeccompDisabled = state.privileged.contains("noseccomp") || state.privileged.contains("full")
        // /proc/self/setgroups only exists when CONFIG_USER_NS is enabled.
        val usernsSupported = remember { java.io.File("/proc/self/setgroups").exists() }

        LaunchedEffect(isSeccompDisabled, usernsSupported) {
            var s = state
            if (isSeccompDisabled) s = s.copy(blockNestedNs = false)
            if (isSeccompDisabled && usernsSupported) s = s.copy(allowUserns = true)
            if (!usernsSupported) s = s.copy(allowUserns = false)
            if (s != state) onStateChange(s)
        }

        ToggleCard(
            icon = Icons.Default.Groups,
            title = context.getString(R.string.allow_userns),
            description = if (usernsSupported) context.getString(R.string.allow_userns_description) else context.getString(R.string.allow_userns_description_not_supported),
            checked = state.allowUserns,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(allowUserns = it)) },
            enabled = !isSeccompDisabled && usernsSupported
        )

        ToggleCard(
            icon = Icons.Default.AutoDelete,
            title = context.getString(R.string.volatile_mode),
            description = context.getString(R.string.volatile_mode_description),
            checked = state.volatileMode,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(volatileMode = it)) }
        )

        ToggleCard(
            icon = Icons.Default.Cyclone,
            title = context.getString(R.string.force_cgroupv1),
            description = context.getString(R.string.force_cgroupv1_description),
            checked = state.forceCgroupv1,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(forceCgroupv1 = it)) }
        )

        ToggleCard(
            icon = Icons.Default.GppBad,
            title = context.getString(R.string.manual_deadlock_shield),
            description = context.getString(R.string.manual_deadlock_shield_description),
            checked = if (isSeccompDisabled) false else state.blockNestedNs,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(blockNestedNs = it)) },
            enabled = !isSeccompDisabled
        )

        SettingsRowCard(
            title = context.getString(R.string.privileged_mode),
            subtitle = if (state.privileged.isEmpty()) context.getString(R.string.not_configured) else state.privileged,
            description = context.getString(R.string.privileged_mode_description),
            icon = Icons.Default.GppMaybe,
            onClick = { clearFocus(); showPrivilegedDialog = true }
        )

        ToggleCard(
            icon = Icons.Default.PowerSettingsNew,
            title = context.getString(R.string.run_at_boot),
            description = context.getString(R.string.run_at_boot_description),
            checked = state.runAtBoot,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(runAtBoot = it)) }
        )

        Text(
            text = context.getString(R.string.cat_advanced),
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(top = 16.dp)
        )

        val envCount = ValidationUtils.countEnvVars(state.envFileContent)
        val envSubtitle = if (envCount > 0) {
            context.getString(R.string.environment_variables_configured, envCount)
        } else {
            context.getString(R.string.not_configured)
        }
        SettingsRowCard(
            title = context.getString(R.string.environment_variables),
            subtitle = envSubtitle,
            icon = Icons.Default.Code,
            onClick = { clearFocus(); showEnvDialog = true }
        )

        if (state.customInit.isNotEmpty()) {
            Surface(
                color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.15f),
                shape = RoundedCornerShape(16.dp),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.3f)),
                modifier = Modifier.fillMaxWidth()
            ) {
                Row(
                    modifier = Modifier.padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(imageVector = Icons.Default.Warning, contentDescription = null, tint = MaterialTheme.colorScheme.error, modifier = Modifier.size(20.dp))
                    Text(text = context.getString(R.string.custom_init_warning), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
                }
            }
        }

        OutlinedTextField(
            value = state.customInit,
            onValueChange = { newValue -> onStateChange(state.copy(customInit = newValue.filter { !it.isWhitespace() })) },
            label = { Text(context.getString(R.string.custom_init_label)) },
            placeholder = { Text(context.getString(R.string.custom_init_placeholder)) },
            supportingText = {
                if (state.customInit.isNotEmpty() && !state.customInit.startsWith("/")) {
                    Text(context.getString(R.string.custom_init_error_absolute), color = MaterialTheme.colorScheme.error)
                } else {
                    Text(context.getString(R.string.custom_init_hint))
                }
            },
            isError = state.customInit.isNotEmpty() && !state.customInit.startsWith("/"),
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            shape = modernFieldShape,
            colors = modernFieldColors,
            leadingIcon = { Icon(Icons.Default.Terminal, contentDescription = null) }
        )

        AnimatedVisibility(
            visible = state.enableTermuxX11,
            enter = expandVertically(animationSpec = tween(durationMillis = 300)) + fadeIn(animationSpec = tween(durationMillis = 300)),
            exit = shrinkVertically(animationSpec = tween(durationMillis = 300)) + fadeOut(animationSpec = tween(durationMillis = 300))
        ) {
            OutlinedTextField(
                value = state.tx11ExtraFlags,
                onValueChange = { onStateChange(state.copy(tx11ExtraFlags = it)) },
                label = { Text(context.getString(R.string.tx11_extra_flags_label)) },
                placeholder = { Text(context.getString(R.string.tx11_extra_flags_placeholder)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                shape = modernFieldShape,
                colors = modernFieldColors,
                leadingIcon = { Icon(painter = painterResource(R.drawable.ic_x11), contentDescription = null, modifier = Modifier.size(15.dp)) }
            )
        }

        AnimatedVisibility(
            visible = state.enableVirgl,
            enter = expandVertically(animationSpec = tween(durationMillis = 300)) + fadeIn(animationSpec = tween(durationMillis = 300)),
            exit = shrinkVertically(animationSpec = tween(durationMillis = 300)) + fadeOut(animationSpec = tween(durationMillis = 300))
        ) {
            OutlinedTextField(
                value = state.virglExtraFlags,
                onValueChange = { onStateChange(state.copy(virglExtraFlags = it)) },
                label = { Text(context.getString(R.string.virgl_extra_flags_label)) },
                placeholder = { Text(context.getString(R.string.virgl_extra_flags_placeholder)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                shape = modernFieldShape,
                colors = modernFieldColors,
                leadingIcon = { Icon(Icons.Default.Layers, contentDescription = null) }
            )
        }

        Row(
            modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(text = context.getString(R.string.bind_mounts), style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
        }

        state.bindMounts.forEach { mount ->
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
            ) {
                Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(text = context.getString(R.string.host_path, mount.src), style = MaterialTheme.typography.bodyMedium, overflow = TextOverflow.Ellipsis, maxLines = 1)
                        Text(text = context.getString(R.string.container_path, mount.dest), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.secondary, overflow = TextOverflow.Ellipsis, maxLines = 1)
                        if (mount.ro) {
                            Surface(shape = RoundedCornerShape(6.dp), color = MaterialTheme.colorScheme.secondaryContainer, modifier = Modifier.padding(top = 4.dp)) {
                                Text(text = context.getString(R.string.read_only), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSecondaryContainer, modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp))
                            }
                        }
                    }
                    IconButton(onClick = { onStateChange(state.copy(bindMounts = state.bindMounts - mount)) }) {
                        Icon(Icons.Default.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                    }
                }
            }
        }

        val addBindBtnShape = RoundedCornerShape(16.dp)
        Surface(
            modifier = Modifier.fillMaxWidth().clip(addBindBtnShape).clickable(
                onClick = { showFilePicker = true },
                indication = rememberRipple(bounded = true),
                interactionSource = remember { MutableInteractionSource() }
            ),
            shape = addBindBtnShape,
            color = MaterialTheme.colorScheme.surfaceContainerLow,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
            tonalElevation = 0.dp
        ) {
            Row(
                modifier = Modifier.padding(14.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center
            ) {
                Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(modifier = Modifier.width(8.dp))
                Text(text = context.getString(R.string.add_bind_mount), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }

        Spacer(modifier = Modifier.height(16.dp))
    }
}
