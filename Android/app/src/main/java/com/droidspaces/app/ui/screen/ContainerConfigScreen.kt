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
import com.droidspaces.app.ui.component.ToggleCard
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

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

    var availableUpstreams by remember { mutableStateOf<List<String>>(emptyList()) }
    LaunchedEffect(Unit) {
        availableUpstreams = ContainerManager.listUpstreamInterfaces()
    }

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
        AlertDialog(
            onDismissRequest = { showDestDialog = false },
            title = { Text(context.getString(R.string.enter_container_path)) },
            text = {
                OutlinedTextField(
                    value = destPath,
                    onValueChange = { destPath = it },
                    label = { Text(context.getString(R.string.container_path_placeholder)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        if (destPath.isNotBlank()) {
                            bindMounts = bindMounts + BindMount(tempSrcPath, destPath)
                            showDestDialog = false
                        }
                    },
                    enabled = destPath.startsWith("/")
                ) {
                    Text(context.getString(R.string.ok))
                }
            },
            dismissButton = {
                TextButton(onClick = { showDestDialog = false }) {
                    Text(context.getString(R.string.cancel))
                }
            }
        )
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
            val isUpstreamValid = netMode != "nat" || upstreamInterfaces.isNotEmpty()
            Surface(
                tonalElevation = 0.dp,
                shadowElevation = 0.dp,
                color = MaterialTheme.colorScheme.surface
            ) {
                Button(
                    onClick = {
                        onNext(netMode, disableIPv6, enableAndroidStorage, enableHwAccess, enableGpuMode, enableTermuxX11, selinuxPermissive, volatileMode, bindMounts, dnsServers, runAtBoot, forceCgroupv1, blockNestedNs, privileged, if (envFileContent.isBlank()) null else envFileContent, upstreamInterfaces, portForwards)
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp)
                        .navigationBarsPadding()
                        .height(56.dp),
                    shape = RoundedCornerShape(20.dp),
                    enabled = isUpstreamValid
                ) {
                    Text(context.getString(R.string.next_storage), style = MaterialTheme.typography.labelLarge)
                }
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp)
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

            var expanded by remember { mutableStateOf(false) }
            val modes = listOf("host", "nat", "none")
            val modeNames = mapOf(
                "host" to context.getString(R.string.network_mode_host),
                "nat" to context.getString(R.string.network_mode_nat),
                "none" to context.getString(R.string.network_mode_none)
            )

            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = { expanded = !expanded }
            ) {
                OutlinedTextField(
                    value = modeNames[netMode] ?: netMode,
                    onValueChange = {},
                    readOnly = true,
                    label = { Text(context.getString(R.string.network_mode)) },
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                    leadingIcon = { Icon(Icons.Default.Public, contentDescription = null) },
                    modifier = Modifier.menuAnchor().fillMaxWidth()
                )
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    modes.forEach { mode ->
                        DropdownMenuItem(
                            text = { Text(modeNames[mode] ?: mode) },
                            onClick = {
                                netMode = mode
                                // IPv6 is always disabled in NAT/NONE, clear any saved value
                                if (mode != "host") {
                                    disableIPv6 = false
                                }
                                expanded = false
                            }
                        )
                    }
                }
            }

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

                    // Existing selected interfaces
                    upstreamInterfaces.forEach { iface ->
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                            shape = RoundedCornerShape(20.dp),
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(text = iface, modifier = Modifier.weight(1f))
                                IconButton(onClick = { upstreamInterfaces = upstreamInterfaces - iface }) {
                                    Icon(Icons.Default.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                                }
                            }
                        }
                    }

                    // Add Interface dialog logic
                    var showUpstreamDialog by remember { mutableStateOf(false) }
                    if (showUpstreamDialog) {
                        var customIface by remember { mutableStateOf("") }
                        var isManuallyRefreshing by remember { mutableStateOf(false) }
                        val scope = rememberCoroutineScope()
                        val rotation by animateFloatAsState(
                            targetValue = if (isManuallyRefreshing) 360f else 0f,
                            animationSpec = if (isManuallyRefreshing) {
                                tween(durationMillis = 600, easing = LinearEasing)
                            } else {
                                tween(durationMillis = 0, easing = LinearEasing)
                            },
                            label = "refresh_rotation"
                        )

                        Dialog(
                            onDismissRequest = { showUpstreamDialog = false },
                            properties = DialogProperties(usePlatformDefaultWidth = false)
                        ) {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth(0.92f)
                                    .wrapContentHeight(),
                                shape = RoundedCornerShape(28.dp),
                                color = MaterialTheme.colorScheme.surface
                            ) {
                                Column(
                                    modifier = Modifier.padding(24.dp),
                                    verticalArrangement = Arrangement.spacedBy(16.dp)
                                ) {
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceBetween,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Text(
                                            text = context.getString(R.string.add_upstream_interface),
                                            style = MaterialTheme.typography.headlineSmall,
                                            fontWeight = FontWeight.Bold,
                                            color = MaterialTheme.colorScheme.onSurface
                                        )
                                        IconButton(
                                            onClick = {
                                                if (!isManuallyRefreshing) {
                                                    isManuallyRefreshing = true
                                                    scope.launch {
                                                        val startTime = System.currentTimeMillis()
                                                        val newUpstreams = ContainerManager.listUpstreamInterfaces()
                                                        availableUpstreams = newUpstreams
                                                        val elapsed = System.currentTimeMillis() - startTime
                                                        val minRotationTime = 600L
                                                        if (elapsed < minRotationTime) {
                                                            delay(minRotationTime - elapsed)
                                                        }
                                                        isManuallyRefreshing = false
                                                    }
                                                }
                                            },
                                            enabled = !isManuallyRefreshing,
                                            modifier = Modifier.size(40.dp)
                                        ) {
                                            Icon(
                                                Icons.Default.Refresh,
                                                contentDescription = "Refresh Interfaces",
                                                modifier = Modifier
                                                    .size(20.dp)
                                                    .graphicsLayer { rotationZ = rotation },
                                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }

                                    if (availableUpstreams.isNotEmpty()) {
                                        Text(context.getString(R.string.available_system_interfaces), style = MaterialTheme.typography.labelMedium)

                                        Box(
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .weight(1f, fill = false)
                                                .heightIn(max = 240.dp)
                                        ) {
                                            FlowRow(
                                                modifier = Modifier
                                                    .fillMaxWidth()
                                                    .verticalScroll(rememberScrollState()),
                                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                                verticalArrangement = Arrangement.spacedBy(8.dp)
                                            ) {
                                                availableUpstreams.forEach { iface ->
                                                    OutlinedButton(
                                                        onClick = {
                                                            if (upstreamInterfaces.size < 8 && !upstreamInterfaces.contains(iface)) {
                                                                upstreamInterfaces = upstreamInterfaces + iface
                                                                showUpstreamDialog = false
                                                            }
                                                        },
                                                        enabled = !upstreamInterfaces.contains(iface),
                                                        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp)
                                                    ) {
                                                        Text(iface)
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Spacer(modifier = Modifier.height(8.dp))
                                    Text(context.getString(R.string.enter_manually), style = MaterialTheme.typography.labelMedium)
                                    OutlinedTextField(
                                        value = customIface,
                                        onValueChange = { customIface = it },
                                        label = { Text(context.getString(R.string.interface_name_hint)) },
                                        singleLine = true,
                                        modifier = Modifier.fillMaxWidth()
                                    )

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.End,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        TextButton(onClick = { showUpstreamDialog = false }) {
                                            Text(context.getString(R.string.cancel))
                                        }
                                        Spacer(modifier = Modifier.width(8.dp))
                                        Button(
                                            onClick = {
                                                if (customIface.isNotBlank() && upstreamInterfaces.size < 8 && !upstreamInterfaces.contains(customIface.trim())) {
                                                    upstreamInterfaces = upstreamInterfaces + customIface.trim()
                                                    showUpstreamDialog = false
                                                }
                                            },
                                            enabled = customIface.isNotBlank() && upstreamInterfaces.size < 8
                                        ) {
                                            Text(context.getString(R.string.add))
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (upstreamInterfaces.size < 8) {
                        OutlinedButton(
                            onClick = { showUpstreamDialog = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Icon(Icons.Default.Add, contentDescription = null)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(context.getString(R.string.add_upstream_interface))
                        }
                    }

                    // Port Forwards
                    Text(
                        text = context.getString(R.string.port_forwarding),
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 16.dp)
                    )

                    portForwards.forEach { pf ->
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                            shape = RoundedCornerShape(20.dp),
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                val targetText = if (pf.containerPort != null) " → ${pf.containerPort}" else " ${context.getString(R.string.symmetric_label)}"
                                Text(
                                    text = "${pf.hostPort}$targetText [${pf.proto.uppercase()}]",
                                    modifier = Modifier.weight(1f)
                                )
                                IconButton(onClick = { portForwards = portForwards - pf }) {
                                    Icon(Icons.Default.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                                }
                            }
                        }
                    }

                    var showPortDialog by remember { mutableStateOf(false) }
                    if (showPortDialog) {
                        var hostPort by remember { mutableStateOf("") }
                        var containerPort by remember { mutableStateOf("") }
                        var protoExpanded by remember { mutableStateOf(false) }
                        var proto by remember { mutableStateOf("tcp") }

                        fun validatePortSpec(spec: String): String? {
                            if (spec.isBlank()) return null
                            if (spec.contains("-")) {
                                val parts = spec.split("-")
                                if (parts.size != 2) return context.getString(R.string.error_invalid_range_format)
                                val start = parts[0].toIntOrNull()
                                val end = parts[1].toIntOrNull()
                                if (start == null || end == null) return context.getString(R.string.error_ports_must_be_numbers)
                                if (start !in 1..65535 || end !in 1..65535) return context.getString(R.string.error_port_out_of_range)
                                if (start >= end) return context.getString(R.string.error_start_must_be_less_than_end)
                                return null
                            }
                            val p = spec.toIntOrNull() ?: return context.getString(R.string.error_port_must_be_number)
                            if (p !in 1..65535) return context.getString(R.string.error_port_out_of_range)
                            return null
                        }

                        fun getWidth(spec: String): Int {
                            if (spec.contains("-")) {
                                val parts = spec.split("-")
                                return (parts[1].toIntOrNull() ?: 0) - (parts[0].toIntOrNull() ?: 0)
                            }
                            return 0
                        }

                        val hostError = validatePortSpec(hostPort)
                        val containerError = validatePortSpec(containerPort)

                        var widthError: String? = null
                        if (hostError == null && containerError == null && hostPort.isNotBlank() && containerPort.isNotBlank()) {
                            if (getWidth(hostPort) != getWidth(containerPort)) {
                                widthError = context.getString(R.string.error_port_width_mismatch)
                            }
                        }

                        // Overlap detection - computed reactively like widthError
                        fun parseRange(spec: String): Pair<Int, Int> {
                            if (spec.contains("-")) {
                                val parts = spec.split("-")
                                return (parts[0].toIntOrNull() ?: 0) to (parts[1].toIntOrNull() ?: 0)
                            }
                            val p = spec.toIntOrNull() ?: 0
                            return p to p
                        }
                        fun rangesOverlap(a: Pair<Int, Int>, b: Pair<Int, Int>): Boolean =
                            a.first <= b.second && b.first <= a.second

                        var overlapError: String? = null
                        if (hostError == null && containerError == null && widthError == null && hostPort.isNotBlank()) {
                            val newHost = parseRange(hostPort.trim())
                            val newCont = parseRange((if (containerPort.isBlank()) hostPort else containerPort).trim())
                            val hasOverlap = portForwards.any { ex ->
                                if (ex.proto != proto) return@any false
                                val exHost = parseRange(ex.hostPort)
                                val exCont = parseRange(ex.containerPort ?: ex.hostPort)
                                rangesOverlap(newHost, exHost) || rangesOverlap(newCont, exCont)
                            }
                            if (hasOverlap) overlapError = context.getString(R.string.error_port_overlap)
                        }

                        val isFormValid = hostPort.isNotBlank() && hostError == null && containerError == null && widthError == null && overlapError == null

                        Dialog(
                            onDismissRequest = { showPortDialog = false },
                            properties = DialogProperties(usePlatformDefaultWidth = false)
                        ) {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth(0.95f)
                                    .wrapContentHeight(),
                                shape = RoundedCornerShape(28.dp),
                                color = MaterialTheme.colorScheme.surface
                            ) {
                                Column(
                                    modifier = Modifier.padding(24.dp),
                                    verticalArrangement = Arrangement.spacedBy(16.dp)
                                ) {
                                    Text(
                                        text = context.getString(R.string.add_port_forward),
                                        style = MaterialTheme.typography.headlineSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.onSurface
                                    )

                                    Box(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .weight(1f, fill = false)
                                    ) {
                                        Column(
                                            verticalArrangement = Arrangement.spacedBy(10.dp),
                                            modifier = Modifier.verticalScroll(rememberScrollState())
                                        ) {
                                            Text(
                                                text = context.getString(R.string.port_forward_examples),
                                                style = MaterialTheme.typography.bodySmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )

                                            OutlinedTextField(
                                                value = hostPort,
                                                onValueChange = { if (it.isEmpty() || it.all { c -> c.isDigit() || c == '-' }) hostPort = it },
                                                label = { Text(context.getString(R.string.host_port_hint)) },
                                                singleLine = true,
                                                modifier = Modifier.fillMaxWidth(),
                                                isError = hostError != null || widthError != null || overlapError != null,
                                                supportingText = { Text(hostError ?: widthError ?: overlapError ?: "") }
                                            )

                                            OutlinedTextField(
                                                value = containerPort,
                                                onValueChange = { if (it.isEmpty() || it.all { c -> c.isDigit() || c == '-' }) containerPort = it },
                                                label = { Text(context.getString(R.string.container_port_hint)) },
                                                placeholder = { Text(context.getString(R.string.leave_blank_for_symmetric)) },
                                                singleLine = true,
                                                modifier = Modifier.fillMaxWidth(),
                                                isError = containerError != null || widthError != null || overlapError != null,
                                                supportingText = { Text(containerError ?: widthError ?: overlapError ?: context.getString(R.string.optional_symmetric_hint)) }
                                            )

                                            ExposedDropdownMenuBox(
                                                expanded = protoExpanded,
                                                onExpandedChange = { protoExpanded = !protoExpanded }
                                            ) {
                                                OutlinedTextField(
                                                    value = proto.uppercase(),
                                                    onValueChange = {},
                                                    readOnly = true,
                                                    label = { Text(context.getString(R.string.protocol)) },
                                                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = protoExpanded) },
                                                    modifier = Modifier.menuAnchor().fillMaxWidth()
                                                )
                                                ExposedDropdownMenu(
                                                    expanded = protoExpanded,
                                                    onDismissRequest = { protoExpanded = false }
                                                ) {
                                                    DropdownMenuItem(text = { Text("TCP") }, onClick = { proto = "tcp"; protoExpanded = false })
                                                    DropdownMenuItem(text = { Text("UDP") }, onClick = { proto = "udp"; protoExpanded = false })
                                                }
                                            }
                                        }
                                    }

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.End,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        TextButton(onClick = { showPortDialog = false }) {
                                            Text(context.getString(R.string.cancel))
                                        }
                                        Spacer(modifier = Modifier.width(8.dp))
                                        Button(
                                            onClick = {
                                                if (isFormValid) {
                                                    val pf = PortForward(
                                                        hostPort.trim(),
                                                        if (containerPort.isBlank()) null else containerPort.trim(),
                                                        proto
                                                    )
                                                    portForwards = portForwards + pf
                                                    showPortDialog = false
                                                }
                                            },
                                            enabled = isFormValid && portForwards.size < 32
                                        ) {
                                            Text(context.getString(R.string.add))
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (portForwards.size < 32) OutlinedButton(
                        onClick = { showPortDialog = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Default.Add, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(context.getString(R.string.add_port_forward))
                    }
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
