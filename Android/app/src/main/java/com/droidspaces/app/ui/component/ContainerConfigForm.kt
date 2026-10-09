package com.droidspaces.app.ui.component

import kotlin.math.roundToInt
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
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Dns
import androidx.compose.material.icons.filled.GppMaybe
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.NetworkCheck
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Public
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material.icons.filled.Tag
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
import androidx.compose.runtime.produceState
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
import com.droidspaces.app.ui.component.DsDialog
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.util.BindMount
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.GatewayErrors
import com.droidspaces.app.util.ResourceLimits
import com.droidspaces.app.util.ValidationUtils
import com.droidspaces.app.util.HostCapabilities
import androidx.compose.runtime.collectAsState

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
            },
            // Bind mounting the host root hands the container the whole host filesystem.
            allowRoot = false
        )
    }

    if (showDestDialog) {
        var destPath by remember { mutableStateOf("") }
        var roEnabled by remember { mutableStateOf(false) }
        DsDialog(
            onDismiss = { showDestDialog = false },
            modifier = Modifier.imePadding(),
            footer = {
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
        ) {
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

        val caps by HostCapabilities.state.collectAsState()
        // Null until the first check --format lands or the cache loads; nothing is greyed out before then.
        fun ok(key: String) = caps?.has(key) ?: true

        SectionHeader(
            text = context.getString(R.string.cat_networking),
            modifier = Modifier.padding(top = 16.dp)
        )

        DsDropdown(
            label = context.getString(R.string.network_mode),
            selected = state.netMode,
            options = caps?.supportedNetModes() ?: HostCapabilities.ALL_NET_MODES,
            displayName = { context.getString(when (it) { "nat" -> R.string.network_mode_nat; "none" -> R.string.network_mode_none; "gateway" -> R.string.network_mode_gateway; else -> R.string.network_mode_host }) },
            onSelect = { mode ->
                clearFocus()
                onStateChange(state.copy(netMode = mode))
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
                SectionHeader(text = context.getString(R.string.nat_settings))

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

        // NAT without IPv6 NAT is IPv4 only anyway, so the switch is held on.
        val ipv6Forced = state.netMode == "nat" && !ok("ipv6_nat")
        ToggleCard(
            icon = Icons.Default.NetworkCheck,
            title = context.getString(R.string.disable_ipv6),
            // Only host mode shares the host's network stack, so only there can
            // turning IPv6 off break a VPN app running on the host.
            description = when {
                ipv6Forced -> context.getString(R.string.disable_ipv6_forced)
                state.netMode == "host" -> context.getString(R.string.disable_ipv6_description)
                else -> context.getString(R.string.disable_ipv6_description_isolated)
            },
            checked = state.disableIPv6,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(disableIPv6 = it)) },
            enabled = !ipv6Forced
        )

        SectionHeader(
            text = context.getString(R.string.cat_integration),
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
            description = if (ok("devtmpfs")) context.getString(R.string.hardware_access_description)
                else context.getString(R.string.hardware_access_not_supported),
            checked = state.enableHwAccess,
            onCheckedChange = { newValue ->
                clearFocus()
                if (newValue) showHwAccessDialog = true else onStateChange(state.copy(enableHwAccess = false))
            },
            enabled = ok("devtmpfs")
        )

        ToggleCard(
            icon = Icons.Default.DeveloperBoard,
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

        SectionHeader(
            text = context.getString(R.string.cat_resource_limits),
            modifier = Modifier.padding(top = 16.dp)
        )

        val totalMemMb = remember { ResourceLimits.totalMemoryMb(context) }
        val cpuCores = remember { ResourceLimits.cpuCores() }
        val mb = 1024L * 1024L
        val memStep = ResourceLimits.MEMORY_STEP_MB

        val memMb = (state.memoryLimit / mb).toInt()
        val shownMemMb = rememberWhileOn(memMb, memMb > 0)
        ToggleCard(
            icon = Icons.Default.Memory,
            title = context.getString(R.string.limit_memory),
            description = when {
                !ok("memory_limit") -> context.getString(R.string.limit_not_supported, context.getString(R.string.limit_memory_requirement))
                memMb > 0 -> context.getString(R.string.limit_memory_on, ResourceLimits.formatMemory(context, totalMemMb))
                else -> context.getString(R.string.limit_memory_off, ResourceLimits.formatMemory(context, totalMemMb))
            },
            checked = memMb > 0,
            enabled = ok("memory_limit"),
            onCheckedChange = { on ->
                clearFocus()
                // Half the device is a sane place to start dragging from
                val half = (totalMemMb / 2 / memStep * memStep).coerceAtLeast(memStep)
                onStateChange(state.copy(memoryLimit = if (on) half * mb else 0))
            },
            expandedContent = {
                LimitSlider(
                    value = shownMemMb.coerceIn(memStep, totalMemMb).toFloat(),
                    valueRange = memStep.toFloat()..totalMemMb.toFloat(),
                    minLabel = ResourceLimits.formatMemory(context, memStep),
                    valueLabel = ResourceLimits.formatMemory(context, shownMemMb),
                    maxLabel = ResourceLimits.formatMemory(context, totalMemMb),
                    onValueChange = {
                        val snapped = ((it / memStep).roundToInt() * memStep).coerceIn(memStep, totalMemMb)
                        onStateChange(state.copy(memoryLimit = snapped * mb))
                    }
                )
            }
        )

        val cpuLimit = state.cpuQuota.toFloat() / ResourceLimits.CPU_PERIOD_US
        val shownCpu = rememberWhileOn(cpuLimit, cpuLimit > 0)
        ToggleCard(
            icon = Icons.Default.Speed,
            title = context.getString(R.string.limit_cpu),
            description = when {
                !ok("cpu_limit") -> context.getString(R.string.limit_not_supported, context.getString(R.string.limit_cpu_requirement))
                cpuLimit > 0 -> context.getString(R.string.limit_cpu_on, ResourceLimits.formatCores(context, cpuCores.toFloat()))
                else -> context.getString(R.string.limit_cpu_off, ResourceLimits.formatCores(context, cpuCores.toFloat()))
            },
            checked = cpuLimit > 0,
            enabled = ok("cpu_limit"),
            onCheckedChange = { on ->
                clearFocus()
                // Half the device, as for memory. Always a multiple of half a core.
                val half = (cpuCores / 2f).coerceAtLeast(0.5f)
                onStateChange(state.copy(cpuQuota = if (on) (half * ResourceLimits.CPU_PERIOD_US).toLong() else 0))
            },
            expandedContent = {
                LimitSlider(
                    value = shownCpu.coerceIn(0.5f, cpuCores.toFloat()),
                    valueRange = 0.5f..cpuCores.toFloat(),
                    minLabel = ResourceLimits.formatCores(context, 0.5f),
                    valueLabel = ResourceLimits.formatCores(context, shownCpu),
                    maxLabel = ResourceLimits.formatCores(context, cpuCores.toFloat()),
                    onValueChange = {
                        // Half-core steps
                        val snapped = ((it * 2).roundToInt() / 2f).coerceIn(0.5f, cpuCores.toFloat())
                        onStateChange(state.copy(cpuQuota = (snapped * ResourceLimits.CPU_PERIOD_US).toLong()))
                    }
                )
            }
        )

        // Its own flag, not pidsLimit > 0: emptying the field while retyping a
        // number must not flip the switch off and fold the field away mid-edit.
        var pidsOn by remember { mutableStateOf(state.pidsLimit > 0) }
        LaunchedEffect(state.pidsLimit) { if (state.pidsLimit > 0) pidsOn = true }
        val shownPids = rememberWhileOn(if (state.pidsLimit > 0) state.pidsLimit.toString() else "", pidsOn)
        ToggleCard(
            icon = Icons.Default.Tag,
            title = context.getString(R.string.limit_pids),
            description = when {
                !ok("pids_limit") -> context.getString(R.string.limit_not_supported, context.getString(R.string.limit_pids_requirement))
                pidsOn -> context.getString(R.string.limit_pids_on)
                else -> context.getString(R.string.limit_pids_off)
            },
            checked = pidsOn,
            enabled = ok("pids_limit"),
            onCheckedChange = { on ->
                clearFocus()
                pidsOn = on
                onStateChange(state.copy(pidsLimit = if (on) ResourceLimits.DEFAULT_PIDS else 0))
            },
            expandedContent = {
                OutlinedTextField(
                    value = shownPids,
                    onValueChange = { text ->
                        val n = text.filter { it.isDigit() }.take(7).toLongOrNull() ?: 0
                        onStateChange(state.copy(pidsLimit = n.coerceAtMost(ResourceLimits.MAX_PIDS)))
                    },
                    label = { Text(context.getString(R.string.limit_pids_label)) },
                    supportingText = {
                        if (ResourceLimits.isValidPidsLimit(state.pidsLimit)) {
                            Text(context.getString(R.string.limit_pids_hint))
                        } else {
                            Text(context.getString(R.string.limit_pids_error, ResourceLimits.MIN_PIDS), color = MaterialTheme.colorScheme.error)
                        }
                    },
                    isError = !ResourceLimits.isValidPidsLimit(state.pidsLimit),
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    shape = modernFieldShape,
                    colors = modernFieldColors,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
                )
            }
        )

        SectionHeader(
            text = context.getString(R.string.cat_security),
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
        val usernsSupported = ok("user_ns")

        // One pass: apply capability adjustments, then the seccomp rule, then a
        // single state write so the Edit screen sees one change, not several.
        LaunchedEffect(caps, isSeccompDisabled, state.netMode) {
            var s = caps?.coerce(state) ?: state
            if (isSeccompDisabled && usernsSupported) s = s.copy(allowSandboxing = true)
            if (s != state) onStateChange(s)
        }

        ToggleCard(
            icon = Icons.Default.Groups,
            title = context.getString(R.string.allow_userns),
            description = if (usernsSupported) context.getString(R.string.allow_userns_description) else context.getString(R.string.allow_userns_description_not_supported),
            checked = state.allowSandboxing,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(allowSandboxing = it)) },
            enabled = !isSeccompDisabled && usernsSupported
        )

        ToggleCard(
            icon = Icons.Default.AutoDelete,
            title = context.getString(R.string.volatile_mode),
            description = if (ok("overlayfs")) context.getString(R.string.volatile_mode_description)
                else context.getString(R.string.volatile_mode_not_supported),
            checked = state.volatileMode,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(volatileMode = it)) },
            enabled = ok("overlayfs")
        )

        ToggleCard(
            icon = Icons.Default.Cyclone,
            title = context.getString(R.string.force_cgroupv1),
            description = if (ok("cgroup2")) context.getString(R.string.force_cgroupv1_description)
                else context.getString(R.string.force_cgroupv1_not_supported),
            checked = state.forceCgroupv1,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(forceCgroupv1 = it)) },
            enabled = ok("cgroup2")
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

        SectionHeader(
            text = context.getString(R.string.cat_advanced),
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
                            Surface(shape = RoundedCornerShape(8.dp), color = MaterialTheme.colorScheme.secondaryContainer, modifier = Modifier.padding(top = 4.dp)) {
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

/**
 * The value a limit's controls should show. Switching a limit off zeroes it at
 * once, while its body is still animating closed, and a slider drawing that
 * zero would jump to its minimum on the way out. So hold the last value it had
 * while it was on.
 */
@Composable
private fun <T> rememberWhileOn(value: T, on: Boolean): T {
    val last = remember { mutableListOf(value) }
    if (on) last[0] = value
    return last[0]
}

/** Min, current value and max over a [DsSlider], as the body of a limit [ToggleCard]. */
@Composable
private fun LimitSlider(
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    minLabel: String,
    valueLabel: String,
    maxLabel: String,
    onValueChange: (Float) -> Unit
) {
    val quiet = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(minLabel, style = MaterialTheme.typography.bodySmall, color = quiet)
            Text(
                valueLabel,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.primary
            )
            Text(maxLabel, style = MaterialTheme.typography.bodySmall, color = quiet)
        }
        DsSlider(value = value, onValueChange = onValueChange, valueRange = valueRange)
    }
}
