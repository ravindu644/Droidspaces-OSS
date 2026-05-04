package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
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
import com.droidspaces.app.util.PortForward

@Composable
fun PortForwardingList(
    portForwards: List<PortForward>,
    onPortForwardsChange: (List<PortForward>) -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var showPortDialog by remember { mutableStateOf(false) }

    Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        // Selected port forwards
        portForwards.forEach { pf ->
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                shape = RoundedCornerShape(20.dp),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val targetText = if (pf.containerPort != null) " → ${pf.containerPort}" else " ${context.getString(R.string.symmetric_label)}"
                    Text(
                        text = "${pf.hostPort}$targetText [${pf.proto.uppercase()}]",
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.bodyLarge
                    )
                    IconButton(onClick = { onPortForwardsChange(portForwards - pf) }) {
                        Icon(Icons.Default.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                    }
                }
            }
        }

        // Add Button
        if (portForwards.size < 32) {
            val addBtnShape = RoundedCornerShape(16.dp)
            Surface(
                modifier = Modifier.fillMaxWidth().clip(addBtnShape).clickable(
                    onClick = { showPortDialog = true }
                ),
                shape = addBtnShape,
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                tonalElevation = 0.dp
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.Center
                ) {
                    Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        context.getString(R.string.add_port_forward),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }

    if (showPortDialog) {
        AddPortForwardDialog(
            existingForwards = portForwards,
            onDismiss = { showPortDialog = false },
            onConfirm = { pf ->
                onPortForwardsChange(portForwards + pf)
                showPortDialog = false
            }
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddPortForwardDialog(
    existingForwards: List<PortForward>,
    onDismiss: () -> Unit,
    onConfirm: (PortForward) -> Unit
) {
    val context = LocalContext.current
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
        val hasOverlap = existingForwards.any { ex ->
            if (ex.proto != proto) return@any false
            val exHost = parseRange(ex.hostPort)
            val exCont = parseRange(ex.containerPort ?: ex.hostPort)
            rangesOverlap(newHost, exHost) || rangesOverlap(newCont, exCont)
        }
        if (hasOverlap) overlapError = context.getString(R.string.error_port_overlap)
    }

    val isFormValid = hostPort.isNotBlank() && hostError == null && containerError == null && widthError == null && overlapError == null

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier.fillMaxWidth(0.92f).wrapContentHeight(),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
            tonalElevation = 0.dp
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = context.getString(R.string.add_port_forward),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )

                Column(
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    modifier = Modifier.verticalScroll(rememberScrollState())
                ) {
                    Text(
                        text = context.getString(R.string.port_forward_examples),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )

                    OutlinedTextField(
                        value = hostPort,
                        onValueChange = { if (it.isEmpty() || it.all { c -> c.isDigit() || c == '-' }) hostPort = it },
                        label = { Text(context.getString(R.string.host_port_hint)) },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        isError = hostError != null || widthError != null || overlapError != null,
                        supportingText = { Text(hostError ?: widthError ?: overlapError ?: "") },
                        shape = RoundedCornerShape(16.dp),
                        colors = OutlinedTextFieldDefaults.colors(
                            unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                            focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                            unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
                            focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
                        )
                    )

                    OutlinedTextField(
                        value = containerPort,
                        onValueChange = { if (it.isEmpty() || it.all { c -> c.isDigit() || c == '-' }) containerPort = it },
                        label = { Text(context.getString(R.string.container_port_hint)) },
                        placeholder = { Text(context.getString(R.string.leave_blank_for_symmetric)) },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        isError = containerError != null || widthError != null || overlapError != null,
                        supportingText = { Text(containerError ?: widthError ?: overlapError ?: context.getString(R.string.optional_symmetric_hint)) },
                        shape = RoundedCornerShape(16.dp),
                        colors = OutlinedTextFieldDefaults.colors(
                            unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                            focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                            unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
                            focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
                        )
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
                            modifier = Modifier.menuAnchor().fillMaxWidth(),
                            shape = RoundedCornerShape(16.dp)
                        )
                        ExposedDropdownMenu(
                            expanded = protoExpanded,
                            onDismissRequest = { protoExpanded = false }
                        ) {
                            DropdownMenuItem(
                                text = { Text(context.getString(R.string.tcp)) }, 
                                onClick = { proto = "tcp"; protoExpanded = false },
                                leadingIcon = if (proto == "tcp") {{ Icon(Icons.Default.Check, null, modifier = Modifier.size(18.dp)) }} else null
                            )
                            DropdownMenuItem(
                                text = { Text(context.getString(R.string.udp)) }, 
                                onClick = { proto = "udp"; protoExpanded = false },
                                leadingIcon = if (proto == "udp") {{ Icon(Icons.Default.Check, null, modifier = Modifier.size(18.dp)) }} else null
                            )
                        }
                    }
                }

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                        }
                    }
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(
                            enabled = isFormValid,
                            onClick = {
                                if (isFormValid) {
                                    onConfirm(PortForward(hostPort.trim(), if (containerPort.isBlank()) null else containerPort.trim(), proto))
                                }
                            }
                        ),
                        shape = RoundedCornerShape(14.dp),
                        color = if (isFormValid) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.add),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = if (isFormValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                            )
                        }
                    }
                }
            }
        }
    }
}
