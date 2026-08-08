package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.DsTextFieldDefaults

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.ContainerConfigForm
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.ValidationUtils
import com.droidspaces.app.util.toConfigState
import com.droidspaces.app.util.withConfig
import com.droidspaces.app.util.isDirectNetworkValid
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EditContainerScreen(
    container: ContainerInfo,
    containerViewModel: ContainerViewModel,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val clearFocus = rememberClearFocus()

    // Editable config + baseline for change detection.
    var state by remember { mutableStateOf(container.toConfigState()) }
    var savedState by remember { mutableStateOf(container.toConfigState()) }

    // Hostname is edited here (the create wizard collects it on a separate screen).
    var hostname by remember { mutableStateOf(container.hostname) }
    var savedHostname by remember { mutableStateOf(container.hostname) }
    var hostnameError by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(hostname) {
        hostnameError = ValidationUtils.validateHostname(
            hostname.ifEmpty { ValidationUtils.sanitizeHostname(container.name) },
            context
        ).errorMessage
    }

    val gatewayErrors = ValidationUtils.validateGatewayConfig(
        selfName = container.name,
        gatewayContainer = state.gatewayContainer,
        net = state.gatewayNet,
        iface = state.gatewayIface,
        bridge = state.gatewayBridge,
        installed = containerViewModel.containerList,
        context = context
    )

    val collisionContainer = remember(state.netMode, state.staticNatIp, containerViewModel.containerList) {
        if (state.netMode != "nat" || state.staticNatIp.isEmpty()) null
        else containerViewModel.containerList.find { it.name != container.name && it.staticNatIp == state.staticNatIp }
    }

    val hasChanges by remember { derivedStateOf { state != savedState || hostname != savedHostname } }

    var isSaving by remember { mutableStateOf(false) }
    var isSaved by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(hasChanges) {
        if (hasChanges && isSaved) isSaved = false
    }

    fun saveChanges() {
        scope.launch {
            isSaving = true
            isSaved = false
            errorMessage = null
            try {
                val finalHostname = hostname.ifEmpty { ValidationUtils.sanitizeHostname(container.name) }
                val updatedConfig = container.withConfig(state).copy(hostname = finalHostname)
                val result = withContext(Dispatchers.IO) {
                    ContainerManager.updateContainerConfig(context, container.name, updatedConfig)
                }
                result.fold(
                    onSuccess = {
                        hostname = finalHostname
                        savedHostname = finalHostname
                        savedState = state
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

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.edit_container_title, container.name)) },
                navigationIcon = {
                    IconButton(onClick = { clearFocus(); onBack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            val btnShape = RoundedCornerShape(20.dp)
            val isReadyToSave = !isSaving && !isSaved && hasChanges && hostnameError == null &&
                (state.netMode != "gateway" || gatewayErrors.isValid) &&
                state.isDirectNetworkValid() && collisionContainer == null
            val targetBtnColor = when {
                isSaved -> MaterialTheme.colorScheme.primaryContainer
                isSaving || isReadyToSave -> MaterialTheme.colorScheme.primary
                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
            }
            val animatedBtnColor by animateColorAsState(
                targetValue = targetBtnColor,
                animationSpec = tween(durationMillis = 250),
                label = "btn_color"
            )
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
                                enabled = isReadyToSave,
                                onClick = { clearFocus(); saveChanges() },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { MutableInteractionSource() }
                            ),
                        shape = btnShape,
                        color = animatedBtnColor,
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(vertical = 16.dp).fillMaxWidth(), contentAlignment = Alignment.Center) {
                            when {
                                isSaved -> {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        Icon(imageVector = Icons.Default.Check, contentDescription = null, modifier = Modifier.size(20.dp), tint = MaterialTheme.colorScheme.onPrimaryContainer)
                                        Text(text = context.getString(R.string.saved), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onPrimaryContainer)
                                    }
                                }
                                isSaving -> {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        LoadingIndicator(modifier = Modifier.size(20.dp), color = MaterialTheme.colorScheme.onPrimary)
                                        Text(text = context.getString(R.string.saving), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onPrimary)
                                    }
                                }
                                else -> {
                                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        Icon(imageVector = Icons.Default.Save, contentDescription = null, modifier = Modifier.size(20.dp), tint = if (isReadyToSave) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f))
                                        Text(text = context.getString(R.string.save_changes), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, color = if (isReadyToSave) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f))
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
            modifier = Modifier
                .padding(innerPadding)
                .consumeWindowInsets(innerPadding)
                .imePadding()
        ) {
            ContainerConfigForm(
                state = state,
                onStateChange = { state = it },
                installedContainers = containerViewModel.containerList,
                selfName = container.name,
                gatewayErrors = gatewayErrors,
                collisionContainer = collisionContainer,
                modifier = Modifier.fillMaxSize(),
                leadingContent = {
                    errorMessage?.let { error ->
                        Surface(
                            color = MaterialTheme.colorScheme.surfaceContainerHigh,
                            shape = RoundedCornerShape(20.dp),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                            modifier = Modifier.fillMaxWidth().clickable { clearFocus() }
                        ) {
                            Text(
                                text = error,
                                modifier = Modifier.padding(16.dp),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                        }
                    }

                    if (container.isRunning) {
                        Surface(
                            color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.2f),
                            shape = RoundedCornerShape(20.dp),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.3f)),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(modifier = Modifier.padding(20.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                                Icon(imageVector = Icons.Default.Warning, contentDescription = null, modifier = Modifier.size(24.dp), tint = MaterialTheme.colorScheme.error)
                                Column {
                                    Text(text = context.getString(R.string.container_is_running), style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.error)
                                    Text(text = context.getString(R.string.changes_take_effect_after_restart), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                                }
                            }
                        }
                    }

                    val hostnameFieldShape = RoundedCornerShape(16.dp)
                    val hostnameFieldColors = DsTextFieldDefaults.colors()
                    OutlinedTextField(
                        value = hostname,
                        onValueChange = { hostname = it },
                        label = { Text(context.getString(R.string.hostname)) },
                        placeholder = { Text(ValidationUtils.sanitizeHostname(container.name)) },
                        isError = hostnameError != null,
                        supportingText = hostnameError?.let { { Text(it) } } ?: { Text(context.getString(R.string.hostname_hint)) },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = hostnameFieldShape,
                        colors = hostnameFieldColors,
                        leadingIcon = { Icon(Icons.Default.Computer, contentDescription = null) }
                    )
                }
            )
        }
    }
}
