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
import com.droidspaces.app.ui.component.FilePickerDialog
import com.droidspaces.app.util.Constants

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerConfigScreen(
    initialEnableIPv6: Boolean = false,
    initialEnableAndroidStorage: Boolean = false,
    initialEnableHwAccess: Boolean = false,
    initialSelinuxPermissive: Boolean = false,
    initialVolatileMode: Boolean = false,
    initialBindMounts: List<BindMount> = emptyList(),
    initialDnsServers: String = "",
    initialRunAtBoot: Boolean = false,
    initialDisableSeccompFilter: Boolean = false,
    onNext: (
        enableIPv6: Boolean,
        enableAndroidStorage: Boolean,
        enableHwAccess: Boolean,
        selinuxPermissive: Boolean,
        volatileMode: Boolean,
        bindMounts: List<BindMount>,
        dnsServers: String,
        runAtBoot: Boolean,
        disableSeccompFilter: Boolean
    ) -> Unit,
    onBack: () -> Unit
) {
    var enableIPv6 by remember { mutableStateOf(initialEnableIPv6) }
    var enableAndroidStorage by remember { mutableStateOf(initialEnableAndroidStorage) }
    var enableHwAccess by remember { mutableStateOf(initialEnableHwAccess) }
    var selinuxPermissive by remember { mutableStateOf(initialSelinuxPermissive) }
    var volatileMode by remember { mutableStateOf(initialVolatileMode) }
    var bindMounts by remember { mutableStateOf(initialBindMounts) }
    var dnsServers by remember { mutableStateOf(initialDnsServers) }
    var runAtBoot by remember { mutableStateOf(initialRunAtBoot) }
    var disableSeccompFilter by remember { mutableStateOf(initialDisableSeccompFilter) }
    val context = LocalContext.current

    // Internal UI States
    var showFilePicker by remember { mutableStateOf(false) }
    var showDestDialog by remember { mutableStateOf(false) }
    var tempSrcPath by remember { mutableStateOf("") }
    var showSeccompWarning by remember { mutableStateOf(false) }

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

    if (showSeccompWarning) {
        AlertDialog(
            onDismissRequest = { 
                showSeccompWarning = false
                disableSeccompFilter = false
            },
            title = { Text(context.getString(R.string.seccomp_bypass_warning_title)) },
            text = { Text(context.getString(R.string.seccomp_bypass_warning_message)) },
            confirmButton = {
                Button(
                    onClick = {
                        disableSeccompFilter = true
                        showSeccompWarning = false
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text(context.getString(R.string.ok))
                }
            },
            dismissButton = {
                TextButton(onClick = { 
                    showSeccompWarning = false
                    disableSeccompFilter = false
                }) {
                    Text(context.getString(R.string.cancel))
                }
            }
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
            Surface(
                tonalElevation = 2.dp,
                shadowElevation = 8.dp
            ) {
                Button(
                    onClick = {
                        onNext(enableIPv6, enableAndroidStorage, enableHwAccess, selinuxPermissive, volatileMode, bindMounts, dnsServers, runAtBoot, disableSeccompFilter)
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp)
                        .navigationBarsPadding()
                        .height(56.dp)
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

            ToggleCard(
                icon = Icons.Default.NetworkCheck,
                title = context.getString(R.string.enable_ipv6),
                description = context.getString(R.string.enable_ipv6_description),
                checked = enableIPv6,
                onCheckedChange = { enableIPv6 = it }
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
                icon = Icons.Default.ShieldMoon,
                title = context.getString(R.string.disable_seccomp_filter),
                description = context.getString(R.string.disable_seccomp_filter_description),
                checked = disableSeccompFilter,
                onCheckedChange = { checked ->
                    if (checked) {
                        showSeccompWarning = true
                    } else {
                        disableSeccompFilter = false
                    }
                }
            )

            ToggleCard(
                icon = Icons.Default.PowerSettingsNew,
                title = context.getString(R.string.run_at_boot),
                description = context.getString(R.string.run_at_boot_description),
                checked = runAtBoot,
                onCheckedChange = { runAtBoot = it }
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

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

