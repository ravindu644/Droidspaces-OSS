package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.ContainerConfigHost
import com.droidspaces.app.ui.component.DsTextFieldDefaults
import com.droidspaces.app.ui.component.GroupField
import com.droidspaces.app.ui.component.GroupHeader
import com.droidspaces.app.ui.component.SaveActionBottomBar
import com.droidspaces.app.ui.component.SettingsGroup
import com.droidspaces.app.ui.component.screenGutter
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.HostCapabilities
import com.droidspaces.app.util.ResourceLimits
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.util.ValidationUtils
import com.droidspaces.app.util.toConfigState
import com.droidspaces.app.util.withConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.ui.component.InlineWarning
import androidx.compose.material3.Icon

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

    val macvlanErrors = ValidationUtils.validateMacvlanConfig(
        selfName = container.name,
        netMode = state.netMode,
        parent = state.macvlanParent,
        mode = state.macvlanMode,
        installed = containerViewModel.containerList,
        context = context
    )

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

    // A config saved by an older app build may hold a value this kernel cannot
    // honour. Collect every correction, then write the file once, so the Save
    // pill stays idle for a change the user did not make.
    val caps by HostCapabilities.state.collectAsState()
    LaunchedEffect(caps) {
        val c = caps ?: return@LaunchedEffect
        state = c.coerce(state)
        val healed = c.coerce(savedState)
        if (healed == savedState) return@LaunchedEffect
        withContext(Dispatchers.IO) {
            ContainerManager.updateContainerConfig(context, container.name, container.withConfig(healed))
        }.fold(
            onSuccess = { savedState = healed; containerViewModel.refresh() },
            onFailure = { errorMessage = it.message ?: context.getString(R.string.failed_to_update_config) }
        )
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

    val isReadyToSave = !isSaving && !isSaved && hasChanges && hostnameError == null &&
        (state.netMode != "gateway" || gatewayErrors.isValid) &&
        macvlanErrors.isValid && collisionContainer == null &&
        ResourceLimits.isValidPidsLimit(state.pidsLimit)

    ContainerConfigHost(
        title = context.getString(R.string.edit_container_title, container.name),
        onBack = onBack,
        bottomBar = {
            SaveActionBottomBar(
                isSaved = isSaved,
                isSaving = isSaving,
                canSave = isReadyToSave,
                onSave = { clearFocus(); saveChanges() },
                saveLabel = context.getString(R.string.save_changes),
                savingLabel = context.getString(R.string.saving),
                savedLabel = context.getString(R.string.saved)
            )
        },
        state = state,
        onStateChange = { state = it },
        installedContainers = containerViewModel.containerList,
        selfName = container.name,
        gatewayErrors = gatewayErrors,
        macvlanErrors = macvlanErrors,
        collisionContainer = collisionContainer,
        leadingContent = {
            val gutter = Modifier.padding(start = screenGutter(), end = screenGutter(), top = 8.dp)
            errorMessage?.let { InlineWarning(it, gutter) }

            if (container.isRunning) {
                InlineWarning(context.getString(R.string.changes_take_effect_after_restart), gutter, title = context.getString(R.string.container_is_running))
            }

            GroupHeader(context.getString(R.string.config_general))
            SettingsGroup {
                GroupField {
                    OutlinedTextField(
                        value = hostname,
                        onValueChange = { hostname = it },
                        label = { Text(context.getString(R.string.hostname)) },
                        placeholder = { Text(ValidationUtils.sanitizeHostname(container.name), maxLines = 1, overflow = TextOverflow.Ellipsis) },
                        isError = hostnameError != null,
                        supportingText = { Text(hostnameError ?: context.getString(R.string.hostname_hint)) },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(16.dp),
                        colors = DsTextFieldDefaults.colors(),
                        leadingIcon = { Icon(Icons.Default.Computer, contentDescription = null) },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Done, autoCorrectEnabled = false),
                        keyboardActions = FocusUtils.clearFocusKeyboardActions()
                    )
                }
            }
        }
    )
}
