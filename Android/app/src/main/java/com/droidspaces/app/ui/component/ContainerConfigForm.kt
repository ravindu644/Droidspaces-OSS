package com.droidspaces.app.ui.component

import kotlin.math.roundToInt
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.AutoDelete
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Cyclone
import androidx.compose.material.icons.filled.DesktopWindows
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Dns
import androidx.compose.material.icons.filled.FolderOpen
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
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.DsIcons
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.GatewayErrors
import com.droidspaces.app.util.HostCapabilities
import com.droidspaces.app.util.MacvlanErrors
import com.droidspaces.app.util.ResourceLimits
import com.droidspaces.app.util.ValidationUtils

/**
 * The root of the container config screen: one group per section, switches
 * inline, and a row for each setting that needs a page of its own ([onOpenPage]).
 *
 * State is fully hoisted: the caller owns a [ContainerConfigState] and gets
 * every edit through [onStateChange]. [gatewayErrors], [macvlanErrors] and
 * [collisionContainer] are worked out by the caller, which needs them to gate
 * its action button, and only shown here so the network row can say why Save
 * is blocked.
 */
@Composable
fun ContainerConfigForm(
    state: ContainerConfigState,
    onStateChange: (ContainerConfigState) -> Unit,
    onOpenPage: (ConfigPage) -> Unit,
    gatewayErrors: GatewayErrors,
    macvlanErrors: MacvlanErrors,
    collisionContainer: ContainerInfo?,
) {
    val context = LocalContext.current
    val clearFocus = rememberClearFocus()
    var showHwAccessDialog by rememberSaveable { mutableStateOf(false) }

    val caps by HostCapabilities.state.collectAsState()
    // Null until the first check --format lands or the cache loads; nothing is greyed out before then.
    fun ok(key: String) = caps?.has(key) ?: true

    val isSeccompDisabled = privilegedTags(state.privileged).let { "noseccomp" in it || "full" in it }
    val usernsSupported = ok("user_ns")

    // One pass: drop what the kernel cannot do, then the seccomp rule, then a
    // single state write so the Edit screen sees one change, not several.
    LaunchedEffect(caps, isSeccompDisabled, state.netMode) {
        var s = caps?.coerce(state) ?: state
        if (isSeccompDisabled && usernsSupported) s = s.copy(allowSandboxing = true)
        if (s != state) onStateChange(s)
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

    GroupHeader(context.getString(R.string.cat_networking))
    SettingsGroup {
        NavRow(
            icon = Icons.Default.Public,
            title = context.getString(R.string.network_mode),
            value = context.getString(netModeLabel(state.netMode)),
            summary = networkSummary(state),
            error = when (state.netMode) {
                "gateway" -> gatewayErrors.container ?: gatewayErrors.iface ?: gatewayErrors.net ?: gatewayErrors.bridge
                "macvlan" -> macvlanErrors.parent
                "nat" -> collisionContainer?.let { context.getString(R.string.error_ip_collision, it.name) }
                else -> null
            },
            onClick = { onOpenPage(ConfigPage.Network) }
        )
        GroupDivider()
        // NAT without IPv6 NAT is IPv4 only anyway, so the switch is held on.
        val ipv6Forced = state.netMode == "nat" && !ok("ipv6_nat")
        SwitchItem(
            icon = Icons.Default.NetworkCheck,
            title = context.getString(R.string.disable_ipv6),
            // Only host mode shares the host's network stack, so only there can
            // turning IPv6 off break a VPN app running on the host.
            summary = when {
                ipv6Forced -> context.getString(R.string.disable_ipv6_forced)
                state.netMode == "host" -> context.getString(R.string.disable_ipv6_description)
                else -> context.getString(R.string.disable_ipv6_description_isolated)
            },
            checked = state.disableIPv6,
            enabled = !ipv6Forced,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(disableIPv6 = it)) }
        )
        GroupDivider()
        val isDnsError = state.dnsServers.isNotEmpty() &&
            !state.dnsServers.all { it.isDigit() || it == '.' || it == ':' || it == ',' }
        GroupField {
            MonoField(
                value = state.dnsServers,
                onValueChange = { onStateChange(state.copy(dnsServers = it.filter { c -> !c.isWhitespace() })) },
                label = context.getString(R.string.dns_servers_label),
                placeholder = context.getString(R.string.dns_servers_placeholder),
                supporting = context.getString(R.string.dns_servers_hint),
                isError = isDnsError,
                leadingIcon = Icons.Default.Dns,
                keyboardType = KeyboardType.Uri
            )
        }
    }

    GroupHeader(context.getString(R.string.cat_integration))
    SettingsGroup {
        SwitchItem(
            icon = DsIcons.Logo,
            title = context.getString(R.string.android_storage),
            summary = context.getString(R.string.android_storage_description),
            checked = state.enableAndroidStorage,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableAndroidStorage = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.Devices,
            title = context.getString(R.string.hardware_access),
            summary = if (ok("devtmpfs")) context.getString(R.string.hardware_access_description)
                else context.getString(R.string.hardware_access_not_supported),
            checked = state.enableHwAccess,
            enabled = ok("devtmpfs"),
            onCheckedChange = { on ->
                clearFocus()
                if (on) showHwAccessDialog = true else onStateChange(state.copy(enableHwAccess = false))
            }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.DeveloperBoard,
            title = context.getString(R.string.gpu_access),
            summary = context.getString(R.string.gpu_access_description),
            // Hardware access already passes the GPU through.
            checked = state.enableHwAccess || state.enableGpuMode,
            enabled = !state.enableHwAccess,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableGpuMode = it)) }
        )
        GroupDivider()
        SwitchItem(
            painter = painterResource(R.drawable.ic_x11),
            title = context.getString(R.string.termux_x11),
            summary = context.getString(R.string.termux_x11_description),
            checked = state.enableTermuxX11,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableTermuxX11 = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.DesktopWindows,
            title = context.getString(R.string.enable_anland),
            summary = context.getString(R.string.enable_anland_description),
            checked = state.enableAnland,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableAnland = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.Layers,
            title = context.getString(R.string.enable_virgl),
            summary = context.getString(R.string.enable_virgl_description),
            checked = state.enableVirgl,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enableVirgl = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.AutoMirrored.Filled.VolumeUp,
            title = context.getString(R.string.enable_pulseaudio),
            summary = context.getString(R.string.enable_pulseaudio_description),
            checked = state.enablePulseaudio,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(enablePulseaudio = it)) }
        )
    }

    GroupHeader(context.getString(R.string.cat_resource_limits))
    SettingsGroup {
        val totalMemMb = remember { ResourceLimits.totalMemoryMb(context) }
        val cpuCores = remember { ResourceLimits.cpuCores() }
        val mb = 1024L * 1024L
        val memStep = ResourceLimits.MEMORY_STEP_MB

        val memMb = (state.memoryLimit / mb).toInt()
        val shownMemMb = rememberWhileOn(memMb, memMb > 0)
        SwitchItem(
            icon = Icons.Default.Memory,
            title = context.getString(R.string.limit_memory),
            summary = when {
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
            }
        )
        AnimatedVisibility(visible = memMb > 0) {
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
        GroupDivider()

        val cpuLimit = state.cpuQuota.toFloat() / ResourceLimits.CPU_PERIOD_US
        val shownCpu = rememberWhileOn(cpuLimit, cpuLimit > 0)
        SwitchItem(
            icon = Icons.Default.Speed,
            title = context.getString(R.string.limit_cpu),
            summary = when {
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
            }
        )
        AnimatedVisibility(visible = cpuLimit > 0) {
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
        GroupDivider()

        // Its own flag, not pidsLimit > 0: emptying the field while retyping a
        // number must not flip the switch off and fold the field away mid-edit.
        var pidsOn by rememberSaveable { mutableStateOf(state.pidsLimit > 0) }
        LaunchedEffect(state.pidsLimit) { if (state.pidsLimit > 0) pidsOn = true }
        val shownPids = rememberWhileOn(if (state.pidsLimit > 0) state.pidsLimit.toString() else "", pidsOn)
        SwitchItem(
            icon = Icons.Default.Tag,
            title = context.getString(R.string.limit_pids),
            summary = when {
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
            }
        )
        AnimatedVisibility(visible = pidsOn) {
            GroupField(top = 4.dp) {
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
                            Text(context.getString(R.string.limit_pids_error, ResourceLimits.MIN_PIDS))
                        }
                    },
                    isError = !ResourceLimits.isValidPidsLimit(state.pidsLimit),
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    shape = RoundedCornerShape(16.dp),
                    colors = DsTextFieldDefaults.colors(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
                    keyboardActions = FocusUtils.clearFocusKeyboardActions()
                )
            }
        }
    }

    GroupHeader(context.getString(R.string.cat_security))
    SettingsGroup {
        SwitchItem(
            icon = Icons.Default.Security,
            title = context.getString(R.string.selinux_permissive),
            summary = context.getString(R.string.selinux_permissive_description),
            checked = state.selinuxPermissive,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(selinuxPermissive = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.Groups,
            title = context.getString(R.string.allow_userns),
            summary = if (usernsSupported) context.getString(R.string.allow_userns_description)
                else context.getString(R.string.allow_userns_description_not_supported),
            checked = state.allowSandboxing,
            enabled = !isSeccompDisabled && usernsSupported,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(allowSandboxing = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.AutoDelete,
            title = context.getString(R.string.volatile_mode),
            summary = if (ok("overlayfs")) context.getString(R.string.volatile_mode_description)
                else context.getString(R.string.volatile_mode_not_supported),
            checked = state.volatileMode,
            enabled = ok("overlayfs"),
            onCheckedChange = { clearFocus(); onStateChange(state.copy(volatileMode = it)) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.Cyclone,
            title = context.getString(R.string.force_cgroupv1),
            summary = if (ok("cgroup2")) context.getString(R.string.force_cgroupv1_description)
                else context.getString(R.string.force_cgroupv1_not_supported),
            checked = state.forceCgroupv1,
            enabled = ok("cgroup2"),
            onCheckedChange = { clearFocus(); onStateChange(state.copy(forceCgroupv1 = it)) }
        )
        GroupDivider()
        NavRow(
            icon = Icons.Default.GppMaybe,
            title = context.getString(R.string.privileged_mode),
            value = state.privileged.ifEmpty { null },
            summary = if (state.privileged.isEmpty()) context.getString(R.string.not_configured) else null,
            valueFontFamily = JetBrainsMono,
            onClick = { onOpenPage(ConfigPage.Privileged) }
        )
        GroupDivider()
        SwitchItem(
            icon = Icons.Default.PowerSettingsNew,
            title = context.getString(R.string.run_at_boot),
            summary = context.getString(R.string.run_at_boot_description),
            checked = state.runAtBoot,
            onCheckedChange = { clearFocus(); onStateChange(state.copy(runAtBoot = it)) }
        )
    }

    GroupHeader(context.getString(R.string.cat_advanced))
    SettingsGroup {
        val envCount = ValidationUtils.countEnvVars(state.envFileContent)
        NavRow(
            icon = Icons.Default.Code,
            title = context.getString(R.string.environment_variables),
            value = if (envCount > 0) context.getString(R.string.environment_variables_configured, envCount) else null,
            summary = if (envCount > 0) null else context.getString(R.string.not_configured),
            onClick = { onOpenPage(ConfigPage.Env) }
        )
        GroupDivider()
        NavRow(
            icon = Icons.Default.FolderOpen,
            title = context.getString(R.string.bind_mounts),
            value = if (state.bindMounts.isNotEmpty())
                context.resources.getQuantityString(R.plurals.bind_mount_count, state.bindMounts.size, state.bindMounts.size)
                else null,
            summary = if (state.bindMounts.isEmpty()) context.getString(R.string.not_configured) else null,
            onClick = { onOpenPage(ConfigPage.Mounts) }
        )
        GroupDivider()
        GroupField {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                AnimatedVisibility(visible = state.customInit.isNotEmpty()) {
                    InlineWarning(context.getString(R.string.custom_init_warning))
                }
                val initError = state.customInit.isNotEmpty() && !state.customInit.startsWith("/")
                MonoField(
                    value = state.customInit,
                    onValueChange = { v -> onStateChange(state.copy(customInit = v.filter { !it.isWhitespace() })) },
                    label = context.getString(R.string.custom_init_label),
                    placeholder = context.getString(R.string.custom_init_placeholder),
                    supporting = context.getString(if (initError) R.string.custom_init_error_absolute else R.string.custom_init_hint),
                    isError = initError,
                    leadingIcon = Icons.Default.Terminal,
                    keyboardType = KeyboardType.Uri
                )
                AnimatedVisibility(visible = state.enableTermuxX11) {
                    MonoField(
                        value = state.tx11ExtraFlags,
                        onValueChange = { onStateChange(state.copy(tx11ExtraFlags = it)) },
                        label = context.getString(R.string.tx11_extra_flags_label),
                        placeholder = context.getString(R.string.tx11_extra_flags_placeholder)
                    )
                }
                AnimatedVisibility(visible = state.enableVirgl) {
                    MonoField(
                        value = state.virglExtraFlags,
                        onValueChange = { onStateChange(state.copy(virglExtraFlags = it)) },
                        label = context.getString(R.string.virgl_extra_flags_label),
                        placeholder = context.getString(R.string.virgl_extra_flags_placeholder)
                    )
                }
            }
        }
    }
}

/** What the network row shows under the mode, e.g. "172.28.1.12, 2 upstreams, 1 port rule". */
@Composable
private fun networkSummary(state: ContainerConfigState): String? {
    val context = LocalContext.current
    val res = context.resources
    return when (state.netMode) {
        "nat" -> buildList {
            if (state.staticNatIp.isNotEmpty()) add(state.staticNatIp)
            if (state.upstreamInterfaces.isNotEmpty())
                add(res.getQuantityString(R.plurals.upstream_count, state.upstreamInterfaces.size, state.upstreamInterfaces.size))
            if (state.portForwards.isNotEmpty())
                add(res.getQuantityString(R.plurals.port_rule_count, state.portForwards.size, state.portForwards.size))
        }.joinToString(", ").ifEmpty { null }
        "gateway" -> state.gatewayContainer.takeIf { it.isNotEmpty() }?.let { context.getString(R.string.network_summary_gateway, it) }
        "macvlan" -> state.macvlanParent.takeIf { it.isNotEmpty() }?.let { context.getString(R.string.network_summary_macvlan, it) }
        else -> null
    }
}

/** A field inside a [SettingsGroup], padded like the rows around it. */
@Composable
fun GroupField(top: androidx.compose.ui.unit.Dp = 16.dp, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = top, bottom = 16.dp)) { content() }
}

/** An error-tinted note inside a group, for settings that can stop a container booting. */
@Composable
fun InlineWarning(text: String, modifier: Modifier = Modifier) {
    Surface(
        color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.2f),
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.3f)),
        modifier = modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(Icons.Default.Warning, contentDescription = null, tint = MaterialTheme.colorScheme.error, modifier = Modifier.size(16.dp))
            Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error, modifier = Modifier.weight(1f))
        }
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

/**
 * Min, current value and max over a [DsSlider], under a limit's switch. The
 * three labels share the row by weight so they cannot collide on a narrow screen.
 */
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
    // Starts under the switch's title (16 edge + 24 icon + 16 gap), not under its icon.
    Column(Modifier.fillMaxWidth().padding(start = 56.dp, end = 16.dp, bottom = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(minLabel, style = MaterialTheme.typography.bodySmall, color = quiet, maxLines = 1, modifier = Modifier.weight(1f))
            Text(
                valueLabel,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.primary,
                maxLines = 1
            )
            Text(
                maxLabel,
                style = MaterialTheme.typography.bodySmall,
                color = quiet,
                maxLines = 1,
                textAlign = androidx.compose.ui.text.style.TextAlign.End,
                modifier = Modifier.weight(1f)
            )
        }
        DsSlider(value = value, onValueChange = onValueChange, valueRange = valueRange)
    }
}
