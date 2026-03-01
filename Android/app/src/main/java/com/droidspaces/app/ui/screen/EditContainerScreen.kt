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
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.R

import com.droidspaces.app.ui.component.FilePickerDialog
import com.droidspaces.app.util.BindMount
import com.droidspaces.app.util.Constants
import androidx.compose.ui.text.style.TextOverflow
import com.droidspaces.app.ui.component.SettingsRowCard
import com.droidspaces.app.ui.component.EnvironmentVariablesDialog

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
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
    var enableIPv6 by remember { mutableStateOf(container.enableIPv6) }
    var enableAndroidStorage by remember { mutableStateOf(container.enableAndroidStorage) }
    var enableHwAccess by remember { mutableStateOf(container.enableHwAccess) }
    var enableTermuxX11 by remember { mutableStateOf(container.enableTermuxX11) }
    var selinuxPermissive by remember { mutableStateOf(container.selinuxPermissive) }
    var volatileMode by remember { mutableStateOf(container.volatileMode) }
    var bindMounts by remember { mutableStateOf(container.bindMounts) }
    var dnsServers by remember { mutableStateOf(container.dnsServers) }
    var runAtBoot by remember { mutableStateOf(container.runAtBoot) }
    var envFileContent by remember { mutableStateOf(container.envFileContent ?: "") }

    // Track the "saved" baseline values - updated after each successful save
    var savedHostname by remember { mutableStateOf(container.hostname) }
    var savedEnableIPv6 by remember { mutableStateOf(container.enableIPv6) }
    var savedEnableAndroidStorage by remember { mutableStateOf(container.enableAndroidStorage) }
    var savedEnableHwAccess by remember { mutableStateOf(container.enableHwAccess) }
    var savedEnableTermuxX11 by remember { mutableStateOf(container.enableTermuxX11) }
    var savedSelinuxPermissive by remember { mutableStateOf(container.selinuxPermissive) }
    var savedVolatileMode by remember { mutableStateOf(container.volatileMode) }
    var savedBindMounts by remember { mutableStateOf(container.bindMounts) }
    var savedDnsServers by remember { mutableStateOf(container.dnsServers) }
    var savedRunAtBoot by remember { mutableStateOf(container.runAtBoot) }
    var savedEnvFileContent by remember { mutableStateOf(container.envFileContent ?: "") }

    // Navigation and internal UI states
    var showFilePicker by remember { mutableStateOf(false) }
    var showDestDialog by remember { mutableStateOf(false) }
    var tempSrcPath by remember { mutableStateOf("") }

    // Loading and error states
    var isSaving by remember { mutableStateOf(false) }
    var isSaved by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    // Track if any field has changed from SAVED values (not original)
    val hasChanges by remember {
        derivedStateOf {
            hostname != savedHostname ||
            enableIPv6 != savedEnableIPv6 ||
            enableAndroidStorage != savedEnableAndroidStorage ||
            enableHwAccess != savedEnableHwAccess ||
            enableTermuxX11 != savedEnableTermuxX11 ||
            selinuxPermissive != savedSelinuxPermissive ||
            volatileMode != savedVolatileMode ||
            bindMounts != savedBindMounts ||
            dnsServers != savedDnsServers ||
            runAtBoot != savedRunAtBoot ||
            envFileContent != savedEnvFileContent
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
                    enableIPv6 = enableIPv6,
                    enableAndroidStorage = enableAndroidStorage,
                    enableHwAccess = enableHwAccess,
                    enableTermuxX11 = if (enableHwAccess) true else enableTermuxX11,
                    selinuxPermissive = selinuxPermissive,
                    volatileMode = volatileMode,
                    bindMounts = bindMounts,
                    dnsServers = dnsServers,
                    runAtBoot = runAtBoot,
                    envFileContent = if (envFileContent.isBlank()) null else envFileContent
                )

                // Update config file
                val result = withContext(Dispatchers.IO) {
                    ContainerManager.updateContainerConfig(context, container.name, updatedConfig)
                }

                result.fold(
                    onSuccess = {
                        // Success - update saved baseline values to current values
                        savedHostname = hostname
                        savedEnableIPv6 = enableIPv6
                        savedEnableAndroidStorage = enableAndroidStorage
                        savedEnableHwAccess = enableHwAccess
                        savedEnableTermuxX11 = enableTermuxX11
                        savedSelinuxPermissive = selinuxPermissive
                        savedVolatileMode = volatileMode
                        savedBindMounts = bindMounts
                        savedDnsServers = dnsServers
                        savedRunAtBoot = runAtBoot
                        savedEnvFileContent = envFileContent

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
            Surface(
                tonalElevation = 2.dp,
                shadowElevation = 8.dp
            ) {
                Button(
                    onClick = {
                        clearFocus()
                        saveChanges()
                    },
                    enabled = !isSaving && !isSaved && hasChanges,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp)
                        .navigationBarsPadding()
                        .height(56.dp)
                ) {
                    when {
                        isSaved -> {
                            Icon(
                                imageVector = Icons.Default.Check,
                                contentDescription = context.getString(R.string.saved),
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.saved),
                                style = MaterialTheme.typography.labelLarge
                            )
                        }
                        isSaving -> {
                            LoadingIndicator(
                                size = LoadingSize.Small,
                                color = MaterialTheme.colorScheme.onPrimary
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.saving),
                                style = MaterialTheme.typography.labelLarge
                            )
                        }
                        else -> {
                            Text(
                                text = context.getString(R.string.save_changes),
                                style = MaterialTheme.typography.labelLarge
                            )
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
                    .padding(top = 8.dp, bottom = 24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
            // Warning if container is running
            if (container.isRunning) {
                val cardShape = RoundedCornerShape(20.dp)
                val interactionSource = remember { MutableInteractionSource() }

                ElevatedCard(
                    colors = CardDefaults.elevatedCardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer
                    ),
                    shape = cardShape,
                    elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 100.dp)
                        .clip(cardShape)
                        .combinedClickable(
                            interactionSource = interactionSource,
                            indication = rememberRipple(bounded = true),
                            onClick = { clearFocus() }
                        )
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(28.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Warning,
                            contentDescription = null,
                            modifier = Modifier.size(38.dp),
                            tint = MaterialTheme.colorScheme.onErrorContainer
                        )
                        Column(
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = context.getString(R.string.container_is_running),
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                            Text(
                                text = context.getString(R.string.changes_take_effect_after_restart),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                        }
                    }
                }
            }

            // Hostname input
            OutlinedTextField(
                value = hostname,
                onValueChange = { hostname = it },
                label = { Text(context.getString(R.string.hostname_label_edit)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                leadingIcon = {
                    Icon(Icons.Default.Computer, contentDescription = null)
                }
            )

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

            // Configuration toggles
            ToggleCard(
                icon = Icons.Default.NetworkCheck,
                title = context.getString(R.string.enable_ipv6),
                description = context.getString(R.string.enable_ipv6_description),
                checked = enableIPv6,
                onCheckedChange = {
                    clearFocus()
                    enableIPv6 = it
                }
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
                painter = androidx.compose.ui.res.painterResource(R.drawable.ic_x11),
                title = context.getString(R.string.termux_x11),
                description = context.getString(R.string.termux_x11_description),
                checked = enableHwAccess || enableTermuxX11,
                onCheckedChange = {
                    clearFocus()
                    enableTermuxX11 = it
                },
                enabled = !enableHwAccess
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
                icon = Icons.Default.PowerSettingsNew,
                title = context.getString(R.string.run_at_boot),
                description = context.getString(R.string.run_at_boot_description),
                checked = runAtBoot,
                onCheckedChange = {
                    clearFocus()
                    runAtBoot = it
                }
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
                if (bindMounts.size >= Constants.MAX_BIND_MOUNTS) {
                    Text(
                        text = context.getString(R.string.max_bind_mounts_reached, Constants.MAX_BIND_MOUNTS),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error
                    )
                }
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
                enabled = bindMounts.size < Constants.MAX_BIND_MOUNTS
            ) {
                Icon(Icons.Default.Add, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text(context.getString(R.string.add_bind_mount))
            }

            // Error message
            errorMessage?.let { error ->
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                        contentColor = MaterialTheme.colorScheme.onErrorContainer
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
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
