package com.droidspaces.app.ui.screen

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.runtime.*
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.FocusUtils
import androidx.compose.foundation.clickable
import com.droidspaces.app.ui.component.ToggleCard
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.Constants
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.R

import com.droidspaces.app.ui.component.FilePickerDialog
import com.droidspaces.app.util.BindMount
import androidx.compose.ui.text.style.TextOverflow
import com.droidspaces.app.ui.component.SettingsRowCard
import com.droidspaces.app.ui.component.EnvironmentVariablesDialog
import com.droidspaces.app.util.PortForward
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
import kotlinx.coroutines.delay
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class, ExperimentalLayoutApi::class)
@Composable
fun EditContainerScreen(
    container: ContainerInfo,
    containerViewModel: ContainerViewModel,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val clearFocus = rememberClearFocus()

    // State for editable fields
    var hostname by remember { mutableStateOf(container.hostname) }
    var netMode by remember { mutableStateOf(container.netMode) }
    var disableIPv6 by remember { mutableStateOf(container.disableIPv6) }
    var enableAndroidStorage by remember { mutableStateOf(container.enableAndroidStorage) }
    var enableHwAccess by remember { mutableStateOf(container.enableHwAccess) }
    var enableGpuMode by remember { mutableStateOf(container.enableGpuMode) }
    var enableTermuxX11 by remember { mutableStateOf(container.enableTermuxX11) }
    var selinuxPermissive by remember { mutableStateOf(container.selinuxPermissive) }
    var volatileMode by remember { mutableStateOf(container.volatileMode) }
    var bindMounts by remember { mutableStateOf(container.bindMounts) }
    var dnsServers by remember { mutableStateOf(container.dnsServers) }
    var runAtBoot by remember { mutableStateOf(container.runAtBoot) }
    var envFileContent by remember { mutableStateOf(container.envFileContent ?: "") }
    var upstreamInterfaces by remember { mutableStateOf(container.upstreamInterfaces) }
    var portForwards by remember { mutableStateOf(container.portForwards) }
    var forceCgroupv1 by remember { mutableStateOf(container.forceCgroupv1) }
    var blockNestedNs by remember { mutableStateOf(container.blockNestedNs) }
    var staticNatIp by remember { mutableStateOf(container.staticNatIp) }
    var privileged by remember { mutableStateOf(container.privileged) }

    // Track the "saved" baseline values - updated after each successful save
    var savedHostname by remember { mutableStateOf(container.hostname) }
    var savedNetMode by remember { mutableStateOf(container.netMode) }
    var savedDisableIPv6 by remember { mutableStateOf(container.disableIPv6) }
    var savedEnableAndroidStorage by remember { mutableStateOf(container.enableAndroidStorage) }
    var savedEnableHwAccess by remember { mutableStateOf(container.enableHwAccess) }
    var savedEnableGpuMode by remember { mutableStateOf(container.enableGpuMode) }
    var savedEnableTermuxX11 by remember { mutableStateOf(container.enableTermuxX11) }
    var savedSelinuxPermissive by remember { mutableStateOf(container.selinuxPermissive) }
    var savedVolatileMode by remember { mutableStateOf(container.volatileMode) }
    var savedBindMounts by remember { mutableStateOf(container.bindMounts) }
    var savedDnsServers by remember { mutableStateOf(container.dnsServers) }
    var savedRunAtBoot by remember { mutableStateOf(container.runAtBoot) }
    var savedEnvFileContent by remember { mutableStateOf(container.envFileContent ?: "") }
    var savedUpstreamInterfaces by remember { mutableStateOf(container.upstreamInterfaces) }
    var savedPortForwards by remember { mutableStateOf(container.portForwards) }
    var savedForceCgroupv1 by remember { mutableStateOf(container.forceCgroupv1) }
    var savedBlockNestedNs by remember { mutableStateOf(container.blockNestedNs) }
    var savedStaticNatIp by remember { mutableStateOf(container.staticNatIp) }
    var savedPrivileged by remember { mutableStateOf(container.privileged) }

    // Navigation and internal UI states
    var showFilePicker by remember { mutableStateOf(false) }
    var showDestDialog by remember { mutableStateOf(false) }
    var tempSrcPath by remember { mutableStateOf("") }

    // Loading and error states
    var isSaving by remember { mutableStateOf(false) }
    var isSaved by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    var availableUpstreams by remember { mutableStateOf<List<String>>(emptyList()) }
    LaunchedEffect(Unit) {
        availableUpstreams = ContainerManager.listUpstreamInterfaces()
    }

    // Track if any field has changed from SAVED values (not original)
    val hasChanges by remember {
        derivedStateOf {
            hostname != savedHostname ||
            netMode != savedNetMode ||
            disableIPv6 != savedDisableIPv6 ||
            enableAndroidStorage != savedEnableAndroidStorage ||
            enableHwAccess != savedEnableHwAccess ||
            enableGpuMode != savedEnableGpuMode ||
            enableTermuxX11 != savedEnableTermuxX11 ||
            selinuxPermissive != savedSelinuxPermissive ||
            volatileMode != savedVolatileMode ||
            bindMounts != savedBindMounts ||
            dnsServers != savedDnsServers ||
            runAtBoot != savedRunAtBoot ||
            envFileContent != savedEnvFileContent ||
            upstreamInterfaces != savedUpstreamInterfaces ||
            portForwards != savedPortForwards ||
            forceCgroupv1 != savedForceCgroupv1 ||
            blockNestedNs != savedBlockNestedNs ||
            staticNatIp != savedStaticNatIp ||
            privileged != savedPrivileged
        }
    }

    // Reset saved state when user makes changes
    LaunchedEffect(hasChanges) {
        if (hasChanges && isSaved) {
            isSaved = false
        }
    }

    fun saveChanges() {
        scope.launch {
            isSaving = true
            isSaved = false
            errorMessage = null

            try {
                // Create updated ContainerInfo with new values
                val updatedConfig = container.copy(
                    hostname = hostname,
                    netMode = netMode,
                    disableIPv6 = disableIPv6,
                    enableAndroidStorage = enableAndroidStorage,
                    enableHwAccess = enableHwAccess,
                    enableGpuMode = enableGpuMode,
                    enableTermuxX11 = enableTermuxX11,
                    selinuxPermissive = selinuxPermissive,
                    volatileMode = volatileMode,
                    bindMounts = bindMounts,
                    dnsServers = dnsServers,
                    runAtBoot = runAtBoot,
                    envFileContent = if (envFileContent.isBlank()) null else envFileContent,
                    upstreamInterfaces = upstreamInterfaces,
                    portForwards = portForwards,
                    forceCgroupv1 = forceCgroupv1,
                    blockNestedNs = blockNestedNs,
                    staticNatIp = staticNatIp,
                    privileged = privileged
                )

                // Update config file
                val result = withContext(Dispatchers.IO) {
                    ContainerManager.updateContainerConfig(context, container.name, updatedConfig)
                }

                result.fold(
                    onSuccess = {
                        // Success - update saved baseline values to current values
                        savedHostname = hostname
                        savedNetMode = netMode
                        savedDisableIPv6 = disableIPv6
                        savedEnableAndroidStorage = enableAndroidStorage
                        savedEnableHwAccess = enableHwAccess
                        savedEnableGpuMode = enableGpuMode
                        savedEnableTermuxX11 = enableTermuxX11
                        savedSelinuxPermissive = selinuxPermissive
                        savedVolatileMode = volatileMode
                        savedBindMounts = bindMounts
                        savedDnsServers = dnsServers
                        savedRunAtBoot = runAtBoot
                        savedEnvFileContent = envFileContent
                        savedUpstreamInterfaces = upstreamInterfaces
                        savedPortForwards = portForwards
                        savedForceCgroupv1 = forceCgroupv1
                        savedBlockNestedNs = blockNestedNs
                        savedStaticNatIp = staticNatIp
                        savedPrivileged = privileged

                        // Refresh container list and SELinux status using ViewModel
                        containerViewModel.refresh()
                        SystemInfoManager.refreshSELinuxStatus()

                        isSaving = false
                        isSaved = true
                    },
                    onFailure = { e ->
                        errorMessage = e.message ?: context.getString(R.string.failed_to_update_config)
                        isSaving = false
                        isSaved = false
                    }
                )
            } catch (e: Exception) {
                errorMessage = e.message ?: context.getString(R.string.failed_to_update_config)
                isSaving = false
                isSaved = false
            }
        }
    }

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
        Dialog(
            onDismissRequest = { showDestDialog = false },
            properties = DialogProperties(usePlatformDefaultWidth = false)
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 24.dp),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surfaceContainer,
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
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
                            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                            focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                        )
                    )
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Surface(
                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = { showDestDialog = false }),
                            shape = RoundedCornerShape(14.dp),
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
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
            onDismiss = { showEnvDialog = false },
            confirmLabel = context.getString(R.string.save_changes)
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(context.getString(R.string.edit_container_title, container.name))
                },
                navigationIcon = {
                    IconButton(onClick = {
                        clearFocus()
                        onBack()
                    }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            val btnShape = RoundedCornerShape(20.dp)
            val isReadyToSave = !isSaving && !isSaved && hasChanges && (netMode != "nat" || upstreamInterfaces.isNotEmpty())
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.98f),
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
                                enabled = isReadyToSave,
                                onClick = {
                                    clearFocus()
                                    saveChanges()
                                },
                                indication = androidx.compose.material.ripple.rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = btnShape,
                        color = when {
                            isSaved -> MaterialTheme.colorScheme.primaryContainer
                            isReadyToSave -> MaterialTheme.colorScheme.primary
                            else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
                        },
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                            when {
                                isSaved -> {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        Icon(
                                            imageVector = Icons.Default.Check,
                                            contentDescription = null,
                                            modifier = Modifier.size(20.dp),
                                            tint = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                        Text(
                                            text = context.getString(R.string.saved),
                                            style = MaterialTheme.typography.labelLarge,
                                            fontWeight = FontWeight.SemiBold,
                                            color = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                    }
                                }
                                isSaving -> {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        LoadingIndicator(
                                            size = LoadingSize.Small,
                                            color = MaterialTheme.colorScheme.onPrimary
                                        )
                                        Text(
                                            text = context.getString(R.string.saving),
                                            style = MaterialTheme.typography.labelLarge,
                                            fontWeight = FontWeight.SemiBold,
                                            color = MaterialTheme.colorScheme.onPrimary
                                        )
                                    }
                                }
                                else -> {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        Icon(
                                            imageVector = Icons.Default.Save,
                                            contentDescription = null,
                                            modifier = Modifier.size(20.dp),
                                            tint = if (isReadyToSave) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                        )
                                        Text(
                                            text = context.getString(R.string.save_changes),
                                            style = MaterialTheme.typography.labelLarge,
                                            fontWeight = FontWeight.SemiBold,
                                            color = if (isReadyToSave) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        ClearFocusOnClickOutside(
            modifier = Modifier.padding(innerPadding)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 24.dp)
                    .padding(top = 8.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
            // Warning if container is running
            if (container.isRunning) {
                Surface(
                    color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.2f),
                    shape = RoundedCornerShape(20.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.3f)),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Row(
                        modifier = Modifier.padding(20.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Warning,
                            contentDescription = null,
                            modifier = Modifier.size(24.dp),
                            tint = MaterialTheme.colorScheme.error
                        )
                        Column {
                            Text(
                                text = context.getString(R.string.container_is_running),
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.error
                            )
                            Text(
                                text = context.getString(R.string.changes_take_effect_after_restart),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            )
                        }
                    }
                }
            }

            // Hostname input
            val modernFieldShape = RoundedCornerShape(16.dp)
            val modernFieldColors = OutlinedTextFieldDefaults.colors(
                unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
            )
            OutlinedTextField(
                value = hostname,
                onValueChange = { hostname = it },
                label = { Text(context.getString(R.string.hostname)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                shape = modernFieldShape,
                colors = modernFieldColors,
                leadingIcon = {
                    Icon(Icons.Default.Computer, contentDescription = null)
                }
            )

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
                    shape = modernFieldShape,
                    colors = modernFieldColors,
                    modifier = Modifier.menuAnchor().fillMaxWidth()
                )
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    modes.forEach { mode ->
                        DropdownMenuItem(
                            text = { Text(modeNames[mode] ?: mode, fontWeight = FontWeight.Medium) },
                            onClick = {
                                clearFocus()
                                netMode = mode
                                if (mode != "host") disableIPv6 = false
                                expanded = false
                            },
                            leadingIcon = if (mode == netMode) {{
                                Icon(Icons.Default.Check, contentDescription = null, modifier = Modifier.size(18.dp),
                                    tint = MaterialTheme.colorScheme.primary)
                            }} else null
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

                    // Static IP Address Configuration
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

                    val octets = remember(staticNatIp) {
                        val parts = staticNatIp.split(".")
                        if (parts.size == 4) {
                            Pair(parts[2], parts[3])
                        } else {
                            Pair("", "")
                        }
                    }

                    var octet3 by remember(octets) { mutableStateOf(octets.first) }
                    var octet4 by remember(octets) { mutableStateOf(octets.second) }

                    val updateIp = { o3: String, o4: String ->
                        staticNatIp = if (o3.isBlank() && o4.isBlank()) {
                            ""
                        } else {
                            "${Constants.NAT_IP_PREFIX}.$o3.$o4"
                        }
                    }

                    val isOctet3Valid = remember(octet3) {
                        octet3.isEmpty() || (octet3.toIntOrNull()?.let { it in Constants.NAT_OCTET_MIN..Constants.NAT_OCTET_MAX } ?: false)
                    }
                    val isOctet4Valid = remember(octet4) {
                        octet4.isEmpty() || (octet4.toIntOrNull()?.let { it in Constants.NAT_OCTET_MIN..Constants.NAT_OCTET_MAX } ?: false)
                    }

                    val collisionContainer = remember(staticNatIp) {
                        if (staticNatIp.isEmpty()) null
                        else containerViewModel.containerList.find { it.name != container.name && it.staticNatIp == staticNatIp }
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
                            shape = RoundedCornerShape(20.dp),
                            color = MaterialTheme.colorScheme.surfaceContainerHigh,
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(text = iface, modifier = Modifier.weight(1f))
                                IconButton(onClick = { clearFocus(); upstreamInterfaces = upstreamInterfaces - iface }) {
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
                                shape = RoundedCornerShape(24.dp),
                                color = MaterialTheme.colorScheme.surfaceContainer,
                                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                                tonalElevation = 0.dp
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
                                                clearFocus()
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
                                                            clearFocus()
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
                                        shape = RoundedCornerShape(16.dp),
                                        colors = OutlinedTextFieldDefaults.colors(
                                            unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                                            focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                                            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                                            focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                                        ),
                                        modifier = Modifier.fillMaxWidth()
                                    )

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                    ) {
                                        Surface(
                                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = { clearFocus(); showUpstreamDialog = false }),
                                            shape = RoundedCornerShape(14.dp),
                                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                                            tonalElevation = 0.dp
                                        ) {
                                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                                Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                                            }
                                        }
                                        Surface(
                                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(
                                                enabled = customIface.isNotBlank() && upstreamInterfaces.size < 8,
                                                onClick = {
                                                    clearFocus()
                                                    if (customIface.isNotBlank() && upstreamInterfaces.size < 8 && !upstreamInterfaces.contains(customIface.trim())) {
                                                        upstreamInterfaces = upstreamInterfaces + customIface.trim()
                                                        showUpstreamDialog = false
                                                    }
                                                }
                                            ),
                                            shape = RoundedCornerShape(14.dp),
                                            color = if (customIface.isNotBlank() && upstreamInterfaces.size < 8) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                                            tonalElevation = 0.dp
                                        ) {
                                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                                Text(
                                                    context.getString(R.string.add),
                                                    style = MaterialTheme.typography.labelLarge,
                                                    fontWeight = FontWeight.SemiBold,
                                                    color = if (customIface.isNotBlank() && upstreamInterfaces.size < 8) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                                )
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (upstreamInterfaces.size < 8) {
                        val addBtnShape = RoundedCornerShape(16.dp)
                        Surface(
                            modifier = Modifier.fillMaxWidth().clip(addBtnShape).clickable(
                                onClick = { clearFocus(); showUpstreamDialog = true },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { MutableInteractionSource() }
                            ),
                            shape = addBtnShape,
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                            tonalElevation = 0.dp
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.Center
                            ) {
                                Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp),
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = context.getString(R.string.add_upstream_interface),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
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
                            shape = RoundedCornerShape(20.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
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
                                IconButton(onClick = { clearFocus(); portForwards = portForwards - pf }) {
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
                                    .fillMaxWidth(0.92f)
                                    .wrapContentHeight(),
                                shape = RoundedCornerShape(24.dp),
                                color = MaterialTheme.colorScheme.surfaceContainer,
                                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                                tonalElevation = 0.dp
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
                                                shape = RoundedCornerShape(16.dp),
                                                colors = OutlinedTextFieldDefaults.colors(
                                                    unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                                                    focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                                                    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                                                    focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                                                ),
                                                isError = hostError != null || widthError != null || overlapError != null,
                                                supportingText = { Text(hostError ?: widthError ?: overlapError ?: "") },
                                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Text)
                                            )

                                            OutlinedTextField(
                                                value = containerPort,
                                                onValueChange = { if (it.isEmpty() || it.all { c -> c.isDigit() || c == '-' }) containerPort = it },
                                                label = { Text(context.getString(R.string.container_port_hint)) },
                                                placeholder = { Text(context.getString(R.string.leave_blank_for_symmetric)) },
                                                singleLine = true,
                                                modifier = Modifier.fillMaxWidth(),
                                                shape = RoundedCornerShape(16.dp),
                                                colors = OutlinedTextFieldDefaults.colors(
                                                    unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                                                    focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                                                    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                                                    focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                                                ),
                                                isError = containerError != null || widthError != null || overlapError != null,
                                                supportingText = { Text(containerError ?: widthError ?: overlapError ?: context.getString(R.string.optional_symmetric_hint)) },
                                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Text)
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
                                                    shape = RoundedCornerShape(16.dp),
                                                    colors = OutlinedTextFieldDefaults.colors(
                                                        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                                                        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                                                        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                                                        focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                                                    ),
                                                    modifier = Modifier.menuAnchor().fillMaxWidth()
                                                )
                                                ExposedDropdownMenu(
                                                    expanded = protoExpanded,
                                                    onDismissRequest = { protoExpanded = false }
                                                ) {
                                                    DropdownMenuItem(text = { Text(context.getString(R.string.tcp), fontWeight = FontWeight.Medium) }, onClick = { clearFocus(); proto = "tcp"; protoExpanded = false },
                                                        leadingIcon = if (proto == "tcp") {{ Icon(Icons.Default.Check, null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.primary) }} else null)
                                                    DropdownMenuItem(text = { Text(context.getString(R.string.udp), fontWeight = FontWeight.Medium) }, onClick = { clearFocus(); proto = "udp"; protoExpanded = false },
                                                        leadingIcon = if (proto == "udp") {{ Icon(Icons.Default.Check, null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.primary) }} else null)
                                                }
                                            }
                                        }
                                    }

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                    ) {
                                        Surface(
                                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = { clearFocus(); showPortDialog = false }),
                                            shape = RoundedCornerShape(14.dp),
                                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                                            tonalElevation = 0.dp
                                        ) {
                                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                                Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                                            }
                                        }
                                        Surface(
                                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(
                                                enabled = isFormValid && portForwards.size < 32,
                                                onClick = {
                                                    clearFocus()
                                                    if (isFormValid) {
                                                        val pf = PortForward(
                                                            hostPort.trim(),
                                                            if (containerPort.isBlank()) null else containerPort.trim(),
                                                            proto
                                                        )
                                                        portForwards = portForwards + pf
                                                        showPortDialog = false
                                                    }
                                                }
                                            ),
                                            shape = RoundedCornerShape(14.dp),
                                            color = if (isFormValid && portForwards.size < 32) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                                            tonalElevation = 0.dp
                                        ) {
                                            Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                                                Text(
                                                    context.getString(R.string.add),
                                                    style = MaterialTheme.typography.labelLarge,
                                                    fontWeight = FontWeight.SemiBold,
                                                    color = if (isFormValid && portForwards.size < 32) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                                )
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (portForwards.size < 32) {
                        val addPortBtnShape = RoundedCornerShape(16.dp)
                        Surface(
                            modifier = Modifier.fillMaxWidth().clip(addPortBtnShape).clickable(
                                onClick = { clearFocus(); showPortDialog = true },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { MutableInteractionSource() }
                            ),
                            shape = addPortBtnShape,
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                            tonalElevation = 0.dp
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.Center
                            ) {
                                Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp),
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = context.getString(R.string.add_port_forward),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
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
                    if (isDnsError) Text(context.getString(R.string.dns_servers_hint))
                },
                isError = isDnsError,
                placeholder = { Text(context.getString(R.string.dns_servers_placeholder)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                shape = modernFieldShape,
                colors = modernFieldColors,
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
                onCheckedChange = {
                    clearFocus()
                    disableIPv6 = it
                },
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
                onCheckedChange = {
                    clearFocus()
                    enableAndroidStorage = it
                }
            )

            ToggleCard(
                icon = Icons.Default.Devices,
                title = context.getString(R.string.hardware_access),
                description = context.getString(R.string.hardware_access_description),
                checked = enableHwAccess,
                onCheckedChange = {
                    clearFocus()
                    enableHwAccess = it
                }
            )

            ToggleCard(
                icon = Icons.Default.Memory,
                title = context.getString(R.string.gpu_access),
                description = context.getString(R.string.gpu_access_description),
                checked = if (enableHwAccess) true else enableGpuMode,
                onCheckedChange = {
                    if (!enableHwAccess) {
                        clearFocus()
                        enableGpuMode = it
                    }
                },
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
                onCheckedChange = {
                    clearFocus()
                    selinuxPermissive = it
                }
            )

            ToggleCard(
                icon = Icons.Default.AutoDelete,
                title = context.getString(R.string.volatile_mode),
                description = context.getString(R.string.volatile_mode_description),
                checked = volatileMode,
                onCheckedChange = {
                    clearFocus()
                    volatileMode = it
                }
            )

            ToggleCard(
                icon = Icons.Default.Cyclone,
                title = context.getString(R.string.force_cgroupv1),
                description = context.getString(R.string.force_cgroupv1_description),
                checked = forceCgroupv1,
                onCheckedChange = {
                    clearFocus()
                    forceCgroupv1 = it
                }
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
                onCheckedChange = {
                    clearFocus()
                    blockNestedNs = it
                },
                enabled = !isSeccompDisabled
            )

            SettingsRowCard(
                title = context.getString(R.string.privileged_mode),
                subtitle = if (privileged.isEmpty()) context.getString(R.string.not_configured) else privileged,
                description = context.getString(R.string.privileged_mode_description),
                icon = Icons.Default.GppMaybe,
                onClick = {
                    clearFocus()
                    showPrivilegedDialog = true
                }
            )

            ToggleCard(
                icon = Icons.Default.PowerSettingsNew,
                title = context.getString(R.string.run_at_boot),
                description = context.getString(R.string.run_at_boot_description),
                checked = runAtBoot,
                onCheckedChange = {
                    clearFocus()
                    runAtBoot = it
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
                    clearFocus()
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
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(20.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                    border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
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

            val addBindBtnShape = RoundedCornerShape(16.dp)
            Surface(
                modifier = Modifier.fillMaxWidth().clip(addBindBtnShape).clickable(
                    onClick = { showFilePicker = true },
                    indication = rememberRipple(bounded = true),
                    interactionSource = remember { MutableInteractionSource() }
                ),
                shape = addBindBtnShape,
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                tonalElevation = 0.dp
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.Center
                ) {
                    Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = context.getString(R.string.add_bind_mount),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            // Error message
            errorMessage?.let { error ->
                Surface(
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                    shape = RoundedCornerShape(20.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 16.dp)
                        .clickable { clearFocus() }
                ) {
                    Text(
                        text = error,
                        modifier = Modifier.padding(16.dp),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onErrorContainer
                    )
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
            }
        }
    }
}
