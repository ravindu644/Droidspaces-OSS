package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Router
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.GatewayErrors
import com.droidspaces.app.util.ValidationUtils

/**
 * Gateway-mode settings block, shared by the installer and edit-container screens.
 * Shows the required gateway-container dropdown plus a "Configure Gateway" button that
 * opens a dialog for the optional interface / LAN-name / bridge overrides. All
 * validation errors come from [errors]; the caller blocks Save/Next on `errors.isValid`.
 */
@Composable
fun GatewaySettingsSection(
    visible: Boolean,
    gatewayContainer: String,
    onGatewayContainerChange: (String) -> Unit,
    gatewayNet: String,
    onGatewayNetChange: (String) -> Unit,
    gatewayIface: String,
    onGatewayIfaceChange: (String) -> Unit,
    gatewayBridge: String,
    onGatewayBridgeChange: (String) -> Unit,
    selfName: String,
    installedContainers: List<ContainerInfo>,
    errors: GatewayErrors
) {
    val context = LocalContext.current
    var showDialog by remember { mutableStateOf(false) }
    val candidates = remember(installedContainers, selfName) {
        installedContainers.map { it.name }.filter { it != selfName }
    }
    // Error from any of the three advanced (dialog) fields, surfaced under the button.
    val advancedError = errors.iface ?: errors.bridge ?: errors.net

    // Instant show/hide (no expand/shrink) so switching network modes stays smooth
    // instead of fighting the NAT section's animation in the opposite direction.
    if (visible) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = context.getString(R.string.gateway_settings),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary
            )
            Text(
                text = context.getString(R.string.gateway_settings_description),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.secondary
            )

            // Required: which running container is the router.
            val noCandidates = candidates.isEmpty()
            DsDropdown(
                label = context.getString(R.string.gateway_container),
                selected = gatewayContainer,
                options = candidates,
                displayName = { it },
                onSelect = onGatewayContainerChange,
                leadingIcon = Icons.Default.Router,
                isError = errors.container != null,
                supportingText = if (noCandidates)
                    context.getString(R.string.error_no_gateway_candidates)
                else
                    errors.container,
                enabled = !noCandidates
            )

            // "Configure Gateway" entry — same row-card aesthetics as Privileged Mode,
            // showing only title + description.
            SettingsRowCard(
                title = context.getString(R.string.gateway_configure),
                subtitle = "",
                description = context.getString(R.string.gateway_configure_intro),
                icon = Icons.Default.Tune,
                onClick = { showDialog = true }
            )
            if (advancedError != null) {
                Text(
                    text = advancedError,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.padding(start = 4.dp)
                )
            }
        }
    }

    if (showDialog) {
        GatewayConfigureDialog(
            selfName = selfName,
            gatewayContainer = gatewayContainer,
            installed = installedContainers,
            initialNet = gatewayNet,
            initialIface = gatewayIface,
            initialBridge = gatewayBridge,
            onConfirm = { net, iface, bridge ->
                onGatewayNetChange(net)
                onGatewayIfaceChange(iface)
                onGatewayBridgeChange(bridge)
                showDialog = false
            },
            onDismiss = { showDialog = false }
        )
    }
}

@Composable
private fun GatewayConfigureDialog(
    selfName: String,
    gatewayContainer: String,
    installed: List<ContainerInfo>,
    initialNet: String,
    initialIface: String,
    initialBridge: String,
    onConfirm: (net: String, iface: String, bridge: String) -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    var net by remember { mutableStateOf(initialNet) }
    var iface by remember { mutableStateOf(initialIface) }
    var bridge by remember { mutableStateOf(initialBridge) }

    // Live-validate the three fields against every other container so collisions show
    // before the user closes the dialog.
    val errs = ValidationUtils.validateGatewayConfig(
        selfName, gatewayContainer, net, iface, bridge, installed, context
    )
    val advancedValid = errs.iface == null && errs.net == null && errs.bridge == null

    Dialog(
        onDismissRequest = onDismiss,
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
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    context.getString(R.string.gateway_configure_title),
                    fontWeight = FontWeight.Bold,
                    style = MaterialTheme.typography.titleLarge
                )

                // 1 — Interface in Gateway (most important).
                ExplainedField(
                    title = context.getString(R.string.gateway_iface),
                    explanation = context.getString(R.string.gateway_iface_explain),
                    value = iface,
                    onChange = { iface = it },
                    hint = context.getString(R.string.gateway_iface_hint),
                    error = errs.iface
                )
                // 2 — LAN Name.
                ExplainedField(
                    title = context.getString(R.string.gateway_net),
                    explanation = context.getString(R.string.gateway_net_explain),
                    value = net,
                    onChange = { net = it },
                    hint = context.getString(R.string.gateway_net_hint),
                    error = errs.net
                )
                // 3 — Host Bridge.
                ExplainedField(
                    title = context.getString(R.string.gateway_bridge),
                    explanation = context.getString(R.string.gateway_bridge_explain),
                    value = bridge,
                    onChange = { bridge = it },
                    hint = context.getString(R.string.gateway_bridge_hint),
                    error = errs.bridge
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
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
                            enabled = advancedValid,
                            onClick = { onConfirm(net, iface, bridge) }
                        ),
                        shape = RoundedCornerShape(14.dp),
                        color = if (advancedValid) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.ok),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = if (advancedValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ExplainedField(
    title: String,
    explanation: String,
    value: String,
    onChange: (String) -> Unit,
    hint: String,
    error: String?
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold)
        Text(explanation, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        OutlinedTextField(
            value = value,
            // Only interface-name characters are valid; filter the rest as the user types.
            onValueChange = { input ->
                onChange(input.filter { it.isLetterOrDigit() || it == '_' || it == '-' })
            },
            placeholder = { Text(hint) },
            isError = error != null,
            supportingText = error?.let { { Text(it) } },
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
    }
}
